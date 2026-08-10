#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>
#include <wincred.h>
#include <process.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <curl/curl.h>

#include <deque>
#include <new>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "headers\resource.h"
#include "headers\pdw.h"
#include "headers\initapp.h"
#include "headers\notification.h"
#include "notification_core.h"
#include "curl_runtime.h"
#include "smtp.h"

using namespace std;

namespace
{
	const long APPRISE_CONNECT_TIMEOUT_MS = 4000L;
	const long APPRISE_TOTAL_TIMEOUT_MS = 12000L;
	const int APPRISE_MAX_ATTEMPTS = 3;
	const size_t APPRISE_MAX_QUEUE = 100;
	const size_t APPRISE_SECRET_LEN = 2048;
	const size_t APPRISE_STATUS_LEN = 511;
	const size_t APPRISE_PUSH_BODY_LEN = 480;
	const UINT_PTR APPRISE_DIALOG_TIMER = 4;
	const char APPRISE_API_COMPATIBILITY[] = "Apprise API v1.5.1 / Apprise v1.12.0";

	struct AppriseConfig
	{
		string endpoint;
		string destinations;
		string username;
		string password;
	};

	struct CredentialSnapshot
	{
		bool exists;
		string username;
		string secret;
		CredentialSnapshot() : exists(false) {}
	};

	struct AppriseTask
	{
		NotificationEvent event;
		bool test;
		AppriseConfig testConfig;
		AppriseTask() : test(false) {}
	};

	struct DeliveryOutcome
	{
		CURLcode curlCode;
		long httpStatus;
		curl_off_t uploadedBytes;
		DeliveryOutcome() : curlCode(CURLE_FAILED_INIT), httpStatus(0), uploadedBytes(0) {}
	};

	CRITICAL_SECTION g_notificationLock;
	bool g_notificationInitialized = false;
	bool g_curlInitialized = false;
	volatile LONG g_shuttingDown = 0;
	volatile LONG g_appriseEnabled = 0;
	volatile LONG g_eventCounter = 0;
	HANDLE g_workEvent = NULL;
	HANDLE g_stopEvent = NULL;
	HANDLE g_workerThread = NULL;
	deque<AppriseTask> g_tasks;
	set<string> g_pendingIdentifiers;
	char g_status[APPRISE_STATUS_LEN+1] = "Apprise notifications have not been initialized.";

	void CopyText(char *destination, size_t destinationSize, const char *source)
	{
		if (destination == NULL || destinationSize == 0) return;
		if (source == NULL) source = "";
		strncpy(destination, source, destinationSize-1);
		destination[destinationSize-1] = '\0';
	}

	void WipeString(string &value)
	{
		if (!value.empty()) SecureZeroMemory(&value[0], value.size());
		value.clear();
	}

	void WipeConfig(AppriseConfig &config)
	{
		WipeString(config.endpoint);
		WipeString(config.destinations);
		WipeString(config.username);
		WipeString(config.password);
	}

	void WipeSnapshot(CredentialSnapshot &snapshot)
	{
		WipeString(snapshot.username);
		WipeString(snapshot.secret);
		snapshot.exists = false;
	}

	string TrimSystemMessage(const string &message)
	{
		size_t end = message.size();
		while (end > 0 && (message[end-1] == '\r' || message[end-1] == '\n' || message[end-1] == ' ')) end--;
		return message.substr(0, end);
	}

	string WindowsErrorText(DWORD errorCode)
	{
		LPSTR systemMessage = NULL;
		DWORD chars = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, errorCode, 0, (LPSTR) &systemMessage, 0, NULL);
		if (chars && systemMessage)
		{
			string result = TrimSystemMessage(systemMessage);
			LocalFree(systemMessage);
			return result;
		}

		char fallback[64];
		snprintf(fallback, sizeof(fallback), "Windows error %lu", (unsigned long) errorCode);
		fallback[sizeof(fallback)-1] = '\0';
		return fallback;
	}

	void SetStatus(const string &status)
	{
		if (!g_notificationInitialized) return;
		EnterCriticalSection(&g_notificationLock);
		CopyText(g_status, sizeof(g_status), status.c_str());
		LeaveCriticalSection(&g_notificationLock);
	}

	void AppendSanitizedLog(const NotificationEvent &event, const char *result)
	{
		char logPath[MAX_PATH];
		snprintf(logPath, sizeof(logPath), "%s\\Apprise.log", szPath);
		logPath[sizeof(logPath)-1] = '\0';
		FILE *logFile = fopen(logPath, "a");
		if (logFile == NULL) return;

		SYSTEMTIME now;
		GetLocalTime(&now);
		fprintf(logFile, "%04u-%02u-%02u %02u:%02u:%02u event=%s result=%s\n",
			(unsigned int) now.wYear, (unsigned int) now.wMonth, (unsigned int) now.wDay,
			(unsigned int) now.wHour, (unsigned int) now.wMinute, (unsigned int) now.wSecond,
			event.identifier.c_str(), result == NULL ? "unknown" : result);
		fclose(logFile);
	}

	string CredentialTarget(const char *part)
	{
		return string("PDW Apprise ") + part + ": " + szIniPathName;
	}

	bool ReadCredential(const char *part, CredentialSnapshot &snapshot, string &error)
	{
		WipeSnapshot(snapshot);
		PCREDENTIALA credential = NULL;
		string target = CredentialTarget(part);
		if (!CredReadA(target.c_str(), CRED_TYPE_GENERIC, 0, &credential))
		{
			DWORD errorCode = GetLastError();
			if (errorCode == ERROR_NOT_FOUND) return true;
			error = "Windows could not read the saved Apprise settings: " + WindowsErrorText(errorCode);
			return false;
		}

		snapshot.exists = true;
		if (credential->UserName != NULL) snapshot.username = credential->UserName;
		if (credential->CredentialBlob != NULL && credential->CredentialBlobSize > 0)
			snapshot.secret.assign((const char *) credential->CredentialBlob,
				credential->CredentialBlobSize);
		CredFree(credential);
		return true;
	}

	bool DeleteCredential(const char *part, string &error)
	{
		string target = CredentialTarget(part);
		if (CredDeleteA(target.c_str(), CRED_TYPE_GENERIC, 0)) return true;
		DWORD errorCode = GetLastError();
		if (errorCode == ERROR_NOT_FOUND) return true;
		error = "Windows could not remove the saved Apprise settings: " + WindowsErrorText(errorCode);
		return false;
	}

	bool WriteCredential(const char *part, const CredentialSnapshot &snapshot, string &error)
	{
		if (!snapshot.exists) return DeleteCredential(part, error);
		if (snapshot.secret.empty() || snapshot.secret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE)
		{
			error = "An Apprise setting is empty or too long for Windows Credential Manager.";
			return false;
		}

		string target = CredentialTarget(part);
		CREDENTIALA credential;
		ZeroMemory(&credential, sizeof(credential));
		credential.Type = CRED_TYPE_GENERIC;
		credential.TargetName = const_cast<LPSTR>(target.c_str());
		credential.CredentialBlobSize = (DWORD) snapshot.secret.size();
		credential.CredentialBlob = (LPBYTE) snapshot.secret.data();
		credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
		credential.UserName = const_cast<LPSTR>(snapshot.username.empty() ? "PDW" : snapshot.username.c_str());

		if (CredWriteA(&credential, 0)) return true;
		error = "Windows could not save the Apprise settings: " + WindowsErrorText(GetLastError());
		return false;
	}

	bool ReadSecureConfig(AppriseConfig &config, bool requireComplete, string &error)
	{
		CredentialSnapshot endpoint;
		CredentialSnapshot destinations;
		CredentialSnapshot authentication;
		bool ok = ReadCredential("Endpoint", endpoint, error) &&
			ReadCredential("Destinations", destinations, error) &&
			ReadCredential("Authentication", authentication, error);

		if (ok)
		{
			config.endpoint = endpoint.secret;
			config.destinations = destinations.secret;
			config.username = authentication.username;
			config.password = authentication.secret;
			if (requireComplete && (!endpoint.exists || !authentication.exists ||
				config.endpoint.empty() || config.username.empty() || config.password.empty()))
			{
				error = "Complete the Apprise API URL and authenticated API account first.";
				ok = false;
			}
		}

		WipeSnapshot(endpoint);
		WipeSnapshot(destinations);
		WipeSnapshot(authentication);
		return ok;
	}

	bool SaveSecureConfig(const AppriseConfig &config, string &error)
	{
		CredentialSnapshot oldEndpoint;
		CredentialSnapshot oldDestinations;
		CredentialSnapshot oldAuthentication;
		if (!ReadCredential("Endpoint", oldEndpoint, error) ||
			!ReadCredential("Destinations", oldDestinations, error) ||
			!ReadCredential("Authentication", oldAuthentication, error))
		{
			WipeSnapshot(oldEndpoint);
			WipeSnapshot(oldDestinations);
			WipeSnapshot(oldAuthentication);
			return false;
		}

		CredentialSnapshot endpoint;
		endpoint.exists = true;
		endpoint.username = "PDW";
		endpoint.secret = config.endpoint;
		CredentialSnapshot destinations;
		destinations.exists = !config.destinations.empty();
		destinations.username = "PDW";
		destinations.secret = config.destinations;
		CredentialSnapshot authentication;
		authentication.exists = true;
		authentication.username = config.username;
		authentication.secret = config.password;

		bool savedEndpoint = WriteCredential("Endpoint", endpoint, error);
		bool savedDestinations = savedEndpoint && WriteCredential("Destinations", destinations, error);
		bool savedAuthentication = savedDestinations && WriteCredential("Authentication", authentication, error);
		bool saved = savedEndpoint && savedDestinations && savedAuthentication;

		if (!saved)
		{
			string ignored;
			WriteCredential("Endpoint", oldEndpoint, ignored);
			WriteCredential("Destinations", oldDestinations, ignored);
			WriteCredential("Authentication", oldAuthentication, ignored);
		}

		WipeSnapshot(endpoint);
		WipeSnapshot(destinations);
		WipeSnapshot(authentication);
		WipeSnapshot(oldEndpoint);
		WipeSnapshot(oldDestinations);
		WipeSnapshot(oldAuthentication);
		return saved;
	}

	bool DeleteSecureConfig(string &error)
	{
		string firstError;
		string currentError;
		bool ok = true;
		const char *parts[] = { "Endpoint", "Destinations", "Authentication" };
		for (size_t index = 0; index < sizeof(parts)/sizeof(parts[0]); index++)
		{
			currentError.clear();
			if (!DeleteCredential(parts[index], currentError))
			{
				if (firstError.empty()) firstError = currentError;
				ok = false;
			}
		}
		if (!ok) error = firstError;
		return ok;
	}

	bool ContainsHeaderCharacters(const string &value)
	{
		return value.find('\r') != string::npos || value.find('\n') != string::npos;
	}

	bool ValidateConfig(const AppriseConfig &config, bool &statefulEndpoint, string &error)
	{
		statefulEndpoint = false;
		if (config.endpoint.empty())
		{
			error = "Enter the full HTTPS Apprise /notify URL.";
			return false;
		}
		if (config.username.empty() || config.password.empty())
		{
			error = "Enter the API username and password required by the HTTPS reverse proxy.";
			return false;
		}
		if (config.endpoint.size() > APPRISE_SECRET_LEN ||
			config.destinations.size() > APPRISE_SECRET_LEN ||
			config.username.size() > APPRISE_SECRET_LEN ||
			config.password.size() > APPRISE_SECRET_LEN)
		{
			error = "An Apprise setting is longer than PDW can store safely.";
			return false;
		}
		if (ContainsHeaderCharacters(config.username) || ContainsHeaderCharacters(config.password))
		{
			error = "The API username and password cannot contain line breaks.";
			return false;
		}
		if (ContainsHeaderCharacters(config.endpoint) || ContainsHeaderCharacters(config.destinations))
		{
			error = "Apprise URLs cannot contain line breaks; separate destinations with spaces or commas.";
			return false;
		}

		CURLU *parsed = curl_url();
		if (parsed == NULL)
		{
			error = "PDW could not initialize HTTPS URL validation.";
			return false;
		}

		CURLUcode parseResult = curl_url_set(parsed, CURLUPART_URL, config.endpoint.c_str(), 0);
		char *scheme = NULL;
		char *host = NULL;
		char *path = NULL;
		char *user = NULL;
		char *password = NULL;
		char *fragment = NULL;
		if (parseResult == CURLUE_OK) curl_url_get(parsed, CURLUPART_SCHEME, &scheme, 0);
		if (parseResult == CURLUE_OK) curl_url_get(parsed, CURLUPART_HOST, &host, 0);
		if (parseResult == CURLUE_OK) curl_url_get(parsed, CURLUPART_PATH, &path, 0);
		curl_url_get(parsed, CURLUPART_USER, &user, 0);
		curl_url_get(parsed, CURLUPART_PASSWORD, &password, 0);
		curl_url_get(parsed, CURLUPART_FRAGMENT, &fragment, 0);

		string endpointPath = path == NULL ? "" : path;
		if (parseResult != CURLUE_OK || scheme == NULL || _stricmp(scheme, "https") != 0)
			error = "The Apprise API URL must use HTTPS.";
		else if (host == NULL || host[0] == '\0')
			error = "The Apprise API URL must include a host name.";
		else if (user != NULL || password != NULL)
			error = "Do not put API credentials in the URL; use the username and password fields.";
		else if (fragment != NULL)
			error = "The Apprise API URL cannot contain a # fragment.";
		else
		{
			size_t notify = endpointPath.find("/notify");
			if (notify == string::npos ||
				(notify + 7 < endpointPath.size() && endpointPath[notify+7] != '/'))
				error = "Enter an Apprise API endpoint ending in /notify or /notify/{key}.";
			else
			{
				size_t keyStart = notify + 7;
				while (keyStart < endpointPath.size() && endpointPath[keyStart] == '/') keyStart++;
				statefulEndpoint = keyStart < endpointPath.size();
				if (!statefulEndpoint && config.destinations.empty())
					error = "Enter at least one Apprise destination URL for a stateless /notify endpoint.";
				else if (statefulEndpoint && !config.destinations.empty())
					error = "Leave Destination URL(s) blank for /notify/{key}; configure those destinations on the Apprise server.";
			}
		}

		if (scheme != NULL) curl_free(scheme);
		if (host != NULL) curl_free(host);
		if (path != NULL) curl_free(path);
		if (user != NULL) curl_free(user);
		if (password != NULL) curl_free(password);
		if (fragment != NULL) curl_free(fragment);
		curl_url_cleanup(parsed);

		if (!error.empty()) return false;
		if (!config.destinations.empty() && config.destinations.find("://") == string::npos)
		{
			error = "Destination entries must be valid Apprise service URLs.";
			return false;
		}
		return true;
	}

	string Windows1252ToUtf8(const char *source)
	{
		if (source == NULL || source[0] == '\0') return string();
		int wideLength = MultiByteToWideChar(1252, 0, source, -1, NULL, 0);
		if (wideLength <= 0) return string(source);
		vector<wchar_t> wide((size_t) wideLength);
		if (!MultiByteToWideChar(1252, 0, source, -1, &wide[0], wideLength)) return string(source);
		int utf8Length = WideCharToMultiByte(CP_UTF8, 0, &wide[0], -1, NULL, 0, NULL, NULL);
		if (utf8Length <= 0) return string(source);
		vector<char> utf8((size_t) utf8Length);
		if (!WideCharToMultiByte(CP_UTF8, 0, &wide[0], -1, &utf8[0], utf8Length, NULL, NULL))
			return string(source);
		return string(&utf8[0]);
	}

	string EventTimestamp(void)
	{
		SYSTEMTIME now;
		GetSystemTime(&now);
		char timestamp[40];
		snprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
			(unsigned int) now.wYear, (unsigned int) now.wMonth, (unsigned int) now.wDay,
			(unsigned int) now.wHour, (unsigned int) now.wMinute, (unsigned int) now.wSecond,
			(unsigned int) now.wMilliseconds);
		timestamp[sizeof(timestamp)-1] = '\0';
		return timestamp;
	}

	string EventIdentifier(void)
	{
		FILETIME now;
		GetSystemTimeAsFileTime(&now);
		ULARGE_INTEGER ticks;
		ticks.LowPart = now.dwLowDateTime;
		ticks.HighPart = now.dwHighDateTime;
		LONG sequence = InterlockedIncrement(&g_eventCounter);
		char identifier[96];
		snprintf(identifier, sizeof(identifier), "decoded.filtered-%08lx%08lx-%lu-%ld",
			(unsigned long) ticks.HighPart, (unsigned long) ticks.LowPart,
			(unsigned long) GetCurrentProcessId(), (long) sequence);
		identifier[sizeof(identifier)-1] = '\0';
		return identifier;
	}

	size_t DiscardResponse(char *buffer, size_t size, size_t count, void *userData)
	{
		(void) buffer;
		(void) userData;
		return size * count;
	}

	int TransferProgress(void *clientData, curl_off_t downloadTotal, curl_off_t downloaded,
		curl_off_t uploadTotal, curl_off_t uploaded)
	{
		(void) clientData;
		(void) downloadTotal;
		(void) downloaded;
		(void) uploadTotal;
		(void) uploaded;
		return InterlockedCompareExchange(&g_shuttingDown, 0, 0) ? 1 : 0;
	}

	bool CurlFailureIsTransient(CURLcode code, curl_off_t uploadedBytes)
	{
		if (uploadedBytes != 0) return false;
		return code == CURLE_COULDNT_RESOLVE_PROXY ||
			code == CURLE_COULDNT_RESOLVE_HOST ||
			code == CURLE_COULDNT_CONNECT ||
			code == CURLE_OPERATION_TIMEDOUT;
	}

	DeliveryOutcome Deliver(const NotificationEvent &event, const AppriseConfig &config,
		bool statefulEndpoint)
	{
		DeliveryOutcome outcome;
		CURL *curl = curl_easy_init();
		if (curl == NULL) return outcome;

		string payload = NotificationBuildApprisePayload(event,
			statefulEndpoint ? string() : config.destinations);
		string identifierHeader = "X-PDW-Notification-ID: " + event.identifier;
		string timestampHeader = "X-PDW-Event-Timestamp: " + event.timestamp;
		string userAgent = string("PDW/") + (pdw_version ? pdw_version : "unknown") +
			" Apprise-Client/1.0";
		struct curl_slist *headers = NULL;
		headers = curl_slist_append(headers, "Content-Type: application/json");
		headers = curl_slist_append(headers, "Accept: application/json");
		headers = curl_slist_append(headers, identifierHeader.c_str());
		headers = curl_slist_append(headers, timestampHeader.c_str());

		CURLcode setup = CURLE_OK;
#define PDW_APPRISE_SETOPT(option, value) \
		do { if (setup == CURLE_OK) setup = curl_easy_setopt(curl, option, value); } while (0)
		PDW_APPRISE_SETOPT(CURLOPT_URL, config.endpoint.c_str());
		PDW_APPRISE_SETOPT(CURLOPT_PROTOCOLS_STR, "https");
		PDW_APPRISE_SETOPT(CURLOPT_POST, 1L);
		PDW_APPRISE_SETOPT(CURLOPT_POSTFIELDS, payload.data());
		PDW_APPRISE_SETOPT(CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t) payload.size());
		PDW_APPRISE_SETOPT(CURLOPT_HTTPHEADER, headers);
		PDW_APPRISE_SETOPT(CURLOPT_HTTPAUTH, (long) CURLAUTH_BASIC);
		PDW_APPRISE_SETOPT(CURLOPT_USERNAME, config.username.c_str());
		PDW_APPRISE_SETOPT(CURLOPT_PASSWORD, config.password.c_str());
		PDW_APPRISE_SETOPT(CURLOPT_CONNECTTIMEOUT_MS, APPRISE_CONNECT_TIMEOUT_MS);
		PDW_APPRISE_SETOPT(CURLOPT_TIMEOUT_MS, APPRISE_TOTAL_TIMEOUT_MS);
		PDW_APPRISE_SETOPT(CURLOPT_NOSIGNAL, 1L);
		PDW_APPRISE_SETOPT(CURLOPT_SSL_VERIFYPEER, 1L);
		PDW_APPRISE_SETOPT(CURLOPT_SSL_VERIFYHOST, 2L);
		PDW_APPRISE_SETOPT(CURLOPT_FOLLOWLOCATION, 0L);
		PDW_APPRISE_SETOPT(CURLOPT_WRITEFUNCTION, DiscardResponse);
		PDW_APPRISE_SETOPT(CURLOPT_XFERINFOFUNCTION, TransferProgress);
		PDW_APPRISE_SETOPT(CURLOPT_NOPROGRESS, 0L);
		PDW_APPRISE_SETOPT(CURLOPT_USERAGENT, userAgent.c_str());
#undef PDW_APPRISE_SETOPT

		outcome.curlCode = setup == CURLE_OK ? curl_easy_perform(curl) : setup;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &outcome.httpStatus);
		curl_easy_getinfo(curl, CURLINFO_SIZE_UPLOAD_T, &outcome.uploadedBytes);
		curl_slist_free_all(headers);
		curl_easy_cleanup(curl);
		WipeString(payload);
		return outcome;
	}

	bool WaitForRetry(DWORD milliseconds)
	{
		if (g_stopEvent == NULL) return false;
		return WaitForSingleObject(g_stopEvent, milliseconds) == WAIT_TIMEOUT;
	}

	void FinishTask(const AppriseTask &task)
	{
		EnterCriticalSection(&g_notificationLock);
		g_pendingIdentifiers.erase(task.event.identifier);
		LeaveCriticalSection(&g_notificationLock);
	}

	void ProcessTask(AppriseTask &task)
	{
		if (!task.test && !InterlockedCompareExchange(&g_appriseEnabled, 0, 0))
		{
			FinishTask(task);
			return;
		}

		AppriseConfig config;
		string error;
		bool stateful = false;
		if (task.test)
			config = task.testConfig;
		else if (!ReadSecureConfig(config, true, error))
		{
			SetStatus(error);
			AppendSanitizedLog(task.event, "configuration-unavailable");
			FinishTask(task);
			WipeConfig(config);
			return;
		}

		if (!ValidateConfig(config, stateful, error))
		{
			SetStatus(error);
			AppendSanitizedLog(task.event, "configuration-invalid");
			FinishTask(task);
			WipeConfig(config);
			return;
		}

		SetStatus(task.test ? "Sending the Apprise test notification..." :
			"Sending a filtered-message notification...");
		DeliveryOutcome outcome;
		for (int attempt = 1; attempt <= APPRISE_MAX_ATTEMPTS; attempt++)
		{
			outcome = Deliver(task.event, config, stateful);
			if (outcome.curlCode == CURLE_OK && outcome.httpStatus == 200) break;

			bool retry = false;
			if (outcome.curlCode == CURLE_OK)
				retry = NotificationHttpStatusIsTransient(outcome.httpStatus);
			else
				retry = CurlFailureIsTransient(outcome.curlCode, outcome.uploadedBytes);
			if (!retry || attempt == APPRISE_MAX_ATTEMPTS) break;
			SetStatus("Apprise is temporarily unavailable; retrying shortly...");
			if (!WaitForRetry((DWORD) (350U << (attempt-1)))) break;
		}

		if (outcome.curlCode == CURLE_OK)
		{
			string status = NotificationSanitizedHttpStatus(outcome.httpStatus);
			SetStatus(status);
			if (outcome.httpStatus == 200)
				AppendSanitizedLog(task.event, task.test ? "test-delivered" : "delivered");
			else
				AppendSanitizedLog(task.event, "http-failure");
		}
		else if (outcome.curlCode == CURLE_PEER_FAILED_VERIFICATION ||
			outcome.curlCode == CURLE_SSL_CACERT || outcome.curlCode == CURLE_SSL_CONNECT_ERROR)
		{
			SetStatus("Apprise HTTPS certificate validation failed.");
			AppendSanitizedLog(task.event, "tls-failure");
		}
		else if (outcome.curlCode == CURLE_ABORTED_BY_CALLBACK &&
			InterlockedCompareExchange(&g_shuttingDown, 0, 0))
		{
			AppendSanitizedLog(task.event, "shutdown-cancelled");
		}
		else
		{
			SetStatus("Apprise could not be reached within the delivery timeout.");
			AppendSanitizedLog(task.event, "network-failure");
		}

		FinishTask(task);
		WipeConfig(config);
		WipeConfig(task.testConfig);
	}

	unsigned int __stdcall NotificationWorker(void *context)
	{
		(void) context;
		HANDLE events[2] = { g_stopEvent, g_workEvent };
		for (;;)
		{
			DWORD waitResult = WaitForMultipleObjects(2, events, FALSE, INFINITE);
			if (waitResult == WAIT_OBJECT_0) break;
			if (waitResult != WAIT_OBJECT_0+1) break;

			for (;;)
			{
				AppriseTask task;
				bool haveTask = false;
				EnterCriticalSection(&g_notificationLock);
				if (!g_tasks.empty())
				{
					task = std::move(g_tasks.front());
					g_tasks.pop_front();
					haveTask = true;
				}
				LeaveCriticalSection(&g_notificationLock);
				if (!haveTask || InterlockedCompareExchange(&g_shuttingDown, 0, 0)) break;
				ProcessTask(task);
			}
		}
		return 0;
	}

	bool QueueTask(AppriseTask &task)
	{
		if (!g_notificationInitialized || g_workerThread == NULL ||
			InterlockedCompareExchange(&g_shuttingDown, 0, 0)) return false;

		bool queued = false;
		EnterCriticalSection(&g_notificationLock);
		if (g_tasks.size() < APPRISE_MAX_QUEUE &&
			g_pendingIdentifiers.insert(task.event.identifier).second)
		{
			g_tasks.push_back(std::move(task));
			queued = true;
		}
		LeaveCriticalSection(&g_notificationLock);

		if (queued) SetEvent(g_workEvent);
		else SetStatus("The Apprise queue is full; the newest notification was not queued.");
		return queued;
	}

	NotificationEvent BuildTestEvent(void)
	{
		NotificationEvent event;
		event.identifier = string("test.") + EventIdentifier();
		event.title = "PDW Apprise test";
		event.message = "PDW can deliver filtered-message notifications through Apprise.";
		event.severity = NOTIFICATION_SEVERITY_SUCCESS;
		event.timestamp = EventTimestamp();
		NotificationMetadata source;
		source.name = "source";
		source.value = "PDW";
		event.metadata.push_back(source);
		return event;
	}

	bool QueueTest(const AppriseConfig &config)
	{
		AppriseTask task;
		task.event = BuildTestEvent();
		task.test = true;
		task.testConfig = config;
		bool queued = QueueTask(task);
		if (queued) SetStatus("The Apprise test notification is queued.");
		else WipeConfig(task.testConfig);
		return queued;
	}

	bool ReadControl(HWND hDlg, int controlId, string &value)
	{
		vector<char> buffer(APPRISE_SECRET_LEN+1);
		int chars = GetDlgItemTextA(hDlg, controlId, &buffer[0], (int) buffer.size());
		value.assign(&buffer[0], chars > 0 ? (size_t) chars : 0);
		SecureZeroMemory(&buffer[0], buffer.size());
		return chars > 0;
	}

	void ClearSecretControls(HWND hDlg)
	{
		SetDlgItemTextA(hDlg, IDC_APPRISE_ENDPOINT, "");
		SetDlgItemTextA(hDlg, IDC_APPRISE_DESTINATIONS, "");
		SetDlgItemTextA(hDlg, IDC_APPRISE_USERNAME, "");
		SetDlgItemTextA(hDlg, IDC_APPRISE_PASSWORD, "");
	}

	void ReadDialogConfig(HWND hDlg, AppriseConfig &config)
	{
		ReadControl(hDlg, IDC_APPRISE_ENDPOINT, config.endpoint);
		ReadControl(hDlg, IDC_APPRISE_DESTINATIONS, config.destinations);
		ReadControl(hDlg, IDC_APPRISE_USERNAME, config.username);
		ReadControl(hDlg, IDC_APPRISE_PASSWORD, config.password);
	}

	void ShowDialogStatus(HWND hDlg)
	{
		char status[APPRISE_STATUS_LEN+1];
		NotificationGetStatusText(status, sizeof(status));
		SetDlgItemTextA(hDlg, IDC_APPRISE_STATUS, status);
	}
}

void NotificationManagerInitialize(void)
{
	if (g_notificationInitialized) return;
	InitializeCriticalSection(&g_notificationLock);
	g_notificationInitialized = true;
	InterlockedExchange(&g_shuttingDown, 0);

	g_workEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	g_stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (g_workEvent == NULL || g_stopEvent == NULL)
	{
		SetStatus("PDW could not initialize the Apprise worker events.");
		return;
	}

	g_curlInitialized = CurlRuntimeAcquire();
	if (!g_curlInitialized)
	{
		SetStatus("PDW could not initialize the HTTPS notification client.");
		return;
	}

	uintptr_t worker = _beginthreadex(NULL, 0, NotificationWorker, NULL, 0, NULL);
	if (worker == 0)
	{
		SetStatus("PDW could not start the Apprise background worker.");
		return;
	}
	g_workerThread = (HANDLE) worker;
	NotificationSettingsChanged();
}

void NotificationManagerShutdown(void)
{
	if (!g_notificationInitialized) return;
	InterlockedExchange(&g_shuttingDown, 1);
	InterlockedExchange(&g_appriseEnabled, 0);
	if (g_stopEvent != NULL) SetEvent(g_stopEvent);
	if (g_workerThread != NULL)
	{
		DWORD workerResult = WaitForSingleObject(g_workerThread, 20000);
		// If an operating-system network call ignored the bounded curl timeout,
		// leave process-owned state intact rather than freeing it under the worker.
		if (workerResult != WAIT_OBJECT_0) return;
		CloseHandle(g_workerThread);
		g_workerThread = NULL;
	}

	EnterCriticalSection(&g_notificationLock);
	for (deque<AppriseTask>::iterator task = g_tasks.begin(); task != g_tasks.end(); ++task)
	{
		WipeString(task->event.message);
		WipeConfig(task->testConfig);
	}
	g_tasks.clear();
	g_pendingIdentifiers.clear();
	LeaveCriticalSection(&g_notificationLock);

	if (g_workEvent != NULL) CloseHandle(g_workEvent);
	if (g_stopEvent != NULL) CloseHandle(g_stopEvent);
	g_workEvent = NULL;
	g_stopEvent = NULL;
	if (g_curlInitialized) CurlRuntimeRelease();
	g_curlInitialized = false;
	g_notificationInitialized = false;
	DeleteCriticalSection(&g_notificationLock);
}

void NotificationSettingsChanged(void)
{
	if (!g_notificationInitialized) return;
	if (!Profile.appriseEnabled)
	{
		InterlockedExchange(&g_appriseEnabled, 0);
		SetStatus("Apprise is disabled.");
		return;
	}
	AppriseConfig config;
	string error;
	bool stateful = false;
	if (ReadSecureConfig(config, true, error) && ValidateConfig(config, stateful, error))
	{
		InterlockedExchange(&g_appriseEnabled, 1);
		SetStatus("Apprise is enabled and ready for filtered messages.");
	}
	else
	{
		InterlockedExchange(&g_appriseEnabled, 0);
		SetStatus(error);
	}
	WipeConfig(config);
}

void NotificationGetStatusText(char *buffer, size_t bufferSize)
{
	if (buffer == NULL || bufferSize == 0) return;
	if (!g_notificationInitialized)
	{
		CopyText(buffer, bufferSize, "Apprise notifications have not been initialized.");
		return;
	}
	EnterCriticalSection(&g_notificationLock);
	CopyText(buffer, bufferSize, g_status);
	LeaveCriticalSection(&g_notificationLock);
}

void NotificationPublishDecodedMessage(const DecodedMessageNotificationContext &context)
{
	NotificationRouteDecision routes = NotificationDecideRoutes(Profile.SMTP != 0,
		InterlockedCompareExchange(&g_appriseEnabled, 0, 0) != 0, context.filtered);

	if (routes.email)
	{
		SendMail(0, context.filterMatched, context.monitorOnly, context.selectedForEmail,
			const_cast<char *>(context.address), const_cast<char *>(context.time),
			const_cast<char *>(context.date), const_cast<char *>(context.mode),
			const_cast<char *>(context.messageType), const_cast<char *>(context.bitrate),
			const_cast<char *>(context.message), const_cast<char *>(context.filterLabel));
	}

	if (!routes.push) return;
	NotificationEvent event;
	event.identifier = EventIdentifier();
	event.title = "PDW filtered message";
	event.message = NotificationBuildFilteredBody(Windows1252ToUtf8(context.filterLabel),
		Windows1252ToUtf8(context.address), Windows1252ToUtf8(context.mode),
		Windows1252ToUtf8(context.messageType), Windows1252ToUtf8(context.message),
		Profile.appriseIncludeMessageText != 0, APPRISE_PUSH_BODY_LEN);
	event.severity = NOTIFICATION_SEVERITY_INFORMATION;
	event.timestamp = EventTimestamp();
	NotificationMetadata source;
	source.name = "source";
	source.value = "PDW";
	event.metadata.push_back(source);
	NotificationMetadata eventType;
	eventType.name = "event";
	eventType.value = "decoded.filtered";
	event.metadata.push_back(eventType);

	AppriseTask task;
	task.event = std::move(event);
	QueueTask(task);
}

BOOL FAR PASCAL AppriseDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	(void) lParam;
	switch (uMsg)
	{
		case WM_INITDIALOG:
		{
			if (!CenterWindow(hDlg)) return FALSE;
			SendDlgItemMessageA(hDlg, IDC_APPRISE_ENDPOINT, EM_SETLIMITTEXT, APPRISE_SECRET_LEN, 0);
			SendDlgItemMessageA(hDlg, IDC_APPRISE_DESTINATIONS, EM_SETLIMITTEXT, APPRISE_SECRET_LEN, 0);
			SendDlgItemMessageA(hDlg, IDC_APPRISE_USERNAME, EM_SETLIMITTEXT, APPRISE_SECRET_LEN, 0);
			SendDlgItemMessageA(hDlg, IDC_APPRISE_PASSWORD, EM_SETLIMITTEXT, APPRISE_SECRET_LEN, 0);
			CheckDlgButton(hDlg, IDC_APPRISE_ENABLED,
				Profile.appriseEnabled ? BST_CHECKED : BST_UNCHECKED);
			CheckDlgButton(hDlg, IDC_APPRISE_INCLUDE_MESSAGE,
				Profile.appriseIncludeMessageText ? BST_CHECKED : BST_UNCHECKED);
			SetDlgItemTextA(hDlg, IDC_APPRISE_VERSION, APPRISE_API_COMPATIBILITY);

			AppriseConfig config;
			string error;
			if (ReadSecureConfig(config, false, error))
			{
				SetDlgItemTextA(hDlg, IDC_APPRISE_ENDPOINT, config.endpoint.c_str());
				SetDlgItemTextA(hDlg, IDC_APPRISE_DESTINATIONS, config.destinations.c_str());
				SetDlgItemTextA(hDlg, IDC_APPRISE_USERNAME, config.username.c_str());
				SetDlgItemTextA(hDlg, IDC_APPRISE_PASSWORD, config.password.c_str());
			}
			else SetDlgItemTextA(hDlg, IDC_APPRISE_STATUS, error.c_str());
			WipeConfig(config);
			ShowDialogStatus(hDlg);
			SetTimer(hDlg, APPRISE_DIALOG_TIMER, 500, NULL);
			return TRUE;
		}

		case WM_TIMER:
			if (wParam == APPRISE_DIALOG_TIMER) ShowDialogStatus(hDlg);
		return TRUE;

		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
				case IDC_APPRISE_TEST:
				{
					AppriseConfig config;
					ReadDialogConfig(hDlg, config);
					string error;
					bool stateful = false;
					if (!ValidateConfig(config, stateful, error))
						MessageBoxA(hDlg, error.c_str(), "Apprise settings", MB_OK | MB_ICONWARNING);
					else if (!QueueTest(config))
						MessageBoxA(hDlg, "PDW could not queue the Apprise test notification.",
							"Apprise settings", MB_OK | MB_ICONWARNING);
					WipeConfig(config);
					return TRUE;
				}

				case IDC_APPRISE_CLEAR:
					if (MessageBoxA(hDlg,
						"Remove the saved Apprise endpoint, destinations, and API credentials from Windows Credential Manager?",
						"Clear Apprise settings", MB_YESNO | MB_ICONWARNING) == IDYES)
					{
						string error;
						if (!DeleteSecureConfig(error))
							MessageBoxA(hDlg, error.c_str(), "Apprise settings", MB_OK | MB_ICONERROR);
						else
						{
							ClearSecretControls(hDlg);
							CheckDlgButton(hDlg, IDC_APPRISE_ENABLED, BST_UNCHECKED);
							Profile.appriseEnabled = 0;
							WriteSettings();
							NotificationSettingsChanged();
						}
					}
				return TRUE;

				case IDOK:
				{
					AppriseConfig config;
					ReadDialogConfig(hDlg, config);
					bool enable = IsDlgButtonChecked(hDlg, IDC_APPRISE_ENABLED) == BST_CHECKED;
					bool anyConfig = !config.endpoint.empty() || !config.destinations.empty() ||
						!config.username.empty() || !config.password.empty();
					string error;
					bool stateful = false;
					bool validConfig = false;
					if (anyConfig || enable) validConfig = ValidateConfig(config, stateful, error);
					if (enable && !validConfig)
					{
						MessageBoxA(hDlg, error.c_str(), "Apprise settings", MB_OK | MB_ICONWARNING);
						WipeConfig(config);
						return TRUE;
					}
					if (validConfig && !SaveSecureConfig(config, error))
					{
						MessageBoxA(hDlg, error.c_str(), "Apprise settings", MB_OK | MB_ICONERROR);
						WipeConfig(config);
						return TRUE;
					}

					Profile.appriseEnabled = enable ? 1 : 0;
					Profile.appriseIncludeMessageText =
						IsDlgButtonChecked(hDlg, IDC_APPRISE_INCLUDE_MESSAGE) == BST_CHECKED ? 1 : 0;
					WriteSettings();
					NotificationSettingsChanged();
					WipeConfig(config);
					KillTimer(hDlg, APPRISE_DIALOG_TIMER);
					ClearSecretControls(hDlg);
					EndDialog(hDlg, TRUE);
					return TRUE;
				}

				case IDCANCEL:
					KillTimer(hDlg, APPRISE_DIALOG_TIMER);
					ClearSecretControls(hDlg);
					EndDialog(hDlg, FALSE);
				return TRUE;
			}
		break;

		case WM_DESTROY:
			KillTimer(hDlg, APPRISE_DIALOG_TIMER);
			ClearSecretControls(hDlg);
		return TRUE;
	}
	return FALSE;
}
