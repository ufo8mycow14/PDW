#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>
#include <wincred.h>
#include <process.h>
#include <shellapi.h>
#include <shlobj.h>

#include <curl/curl.h>
#include <openssl/hmac.h>

#include <algorithm>
#include <deque>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "headers\resource.h"
#include "headers\pdw.h"
#include "headers\initapp.h"
#include "headers\notification.h"
#include "headers\publishing.h"
#include "publishing_core.h"

namespace
{
	const std::size_t MAX_QUEUE = 500;
	const std::size_t MAX_FEED_EVENTS = 200;
	const int MAX_ATTEMPTS = 5;
	const UINT_PTR DIALOG_TIMER = 7;

	struct PublishTask
	{
		pdw::publishing::PublishEvent event;
		std::string payload;
		std::string spoolPath;
		int attempts;
		bool staticDone;
		bool webhookDone;
		bool test;
		std::string testUrl;
		std::string testBearer;
		std::string testHmac;
		PublishTask() : attempts(0), staticDone(false), webhookDone(false), test(false) {}
	};

	void WipeTaskSecrets(PublishTask& task)
	{
		if (!task.testBearer.empty()) SecureZeroMemory(&task.testBearer[0], task.testBearer.size());
		if (!task.testHmac.empty()) SecureZeroMemory(&task.testHmac[0], task.testHmac.size());
		task.testBearer.clear();
		task.testHmac.clear();
	}

	struct Config
	{
		bool enabled;
		bool acknowledged;
		bool paused;
		bool filteredOnly;
		bool staticEnabled;
		bool webhookEnabled;
		bool maskAddress;
		bool includeMessage;
		unsigned int minimumInterval;
		std::string outputPath;
		std::string webhookUrl;
		std::string sourceAlias;
	};

	CRITICAL_SECTION g_lock;
	bool g_initialized = false;
	volatile LONG g_stopping = 0;
	HANDLE g_workEvent = NULL;
	HANDLE g_stopEvent = NULL;
	HANDLE g_thread = NULL;
	std::deque<PublishTask> g_queue;
	std::vector<pdw::publishing::PublishEvent> g_feed;
	char g_status[512] = "Publishing has not been initialized.";
	volatile LONG g_counter = 0;

	void CopyText(char* destination, std::size_t size, const char* source)
	{
		if (!destination || !size) return;
		strncpy(destination, source ? source : "", size - 1);
		destination[size - 1] = '\0';
	}

	void SetStatus(const std::string& status)
	{
		if (!g_initialized) return;
		EnterCriticalSection(&g_lock);
		CopyText(g_status, sizeof(g_status), status.c_str());
		LeaveCriticalSection(&g_lock);
	}

	std::string JoinPath(const std::string& folder, const std::string& file)
	{
		if (folder.empty()) return file;
		const char last = folder[folder.size() - 1];
		return folder + ((last == '\\' || last == '/') ? "" : "\\") + file;
	}

	std::string QueueFolder() { return JoinPath(szPath, "PublishQueue"); }
	std::string DeadLetterFolder() { return JoinPath(QueueFolder(), "DeadLetter"); }

	void EnsureFolder(const std::string& path)
	{
		if (CreateDirectoryA(path.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS) return;
	}

	std::string ResolveOutputPath(const std::string& path)
	{
		if (path.empty()) return JoinPath(szPath, "Published");
		if ((path.size() >= 2 && path[1] == ':') ||
			(path.size() >= 2 && path[0] == '\\' && path[1] == '\\')) return path;
		return JoinPath(szPath, path);
	}

	Config SnapshotConfig()
	{
		Config config;
		config.enabled = Profile.publishingEnabled != 0;
		config.acknowledged = Profile.publishingPermissionAcknowledged != 0;
		config.paused = Profile.publishingPaused != 0;
		config.filteredOnly = Profile.publishingFilteredOnly != 0;
		config.staticEnabled = Profile.publishingStaticEnabled != 0;
		config.webhookEnabled = Profile.publishingWebhookEnabled != 0;
		config.maskAddress = Profile.publishingMaskAddress != 0;
		config.includeMessage = Profile.publishingIncludeMessage != 0;
		config.minimumInterval = Profile.publishingMinimumIntervalSeconds;
		config.outputPath = ResolveOutputPath(Profile.publishingOutputPath);
		config.webhookUrl = Profile.publishingWebhookUrl;
		config.sourceAlias = Profile.publishingSourceAlias;
		return config;
	}

	std::string Utf8(const char* source)
	{
		if (!source || !source[0]) return std::string();
		const int wideLength = MultiByteToWideChar(1252, 0, source, -1, NULL, 0);
		if (wideLength <= 1) return std::string(source);
		std::vector<wchar_t> wide(static_cast<std::size_t>(wideLength));
		MultiByteToWideChar(1252, 0, source, -1, &wide[0], wideLength);
		const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, &wide[0], -1, NULL, 0, NULL, NULL);
		if (utf8Length <= 1) return std::string();
		std::vector<char> result(static_cast<std::size_t>(utf8Length));
		WideCharToMultiByte(CP_UTF8, 0, &wide[0], -1, &result[0], utf8Length, NULL, NULL);
		return std::string(&result[0]);
	}

	std::string NowIso8601()
	{
		SYSTEMTIME now;
		GetSystemTime(&now);
		char text[40];
		snprintf(text, sizeof(text), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
			now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
		return text;
	}

	std::string EventId()
	{
		SYSTEMTIME now;
		GetSystemTime(&now);
		char text[80];
		snprintf(text, sizeof(text), "%04u%02u%02uT%02u%02u%02u-%lu-%ld",
			now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
			static_cast<unsigned long>(GetCurrentProcessId()), InterlockedIncrement(&g_counter));
		return text;
	}

	bool WriteAtomic(const std::string& path, const std::string& contents)
	{
		const std::string temporary = path + ".tmp";
		std::ofstream output(temporary.c_str(), std::ios::binary | std::ios::trunc);
		if (!output) return false;
		output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
		output.close();
		if (!output) { DeleteFileA(temporary.c_str()); return false; }
		if (!MoveFileExA(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			DeleteFileA(temporary.c_str());
			return false;
		}
		return true;
	}

	bool AppendFile(const std::string& path, const std::string& contents)
	{
		std::ofstream output(path.c_str(), std::ios::binary | std::ios::app);
		if (!output) return false;
		output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
		return output.good();
	}

	bool PublishStatic(const Config& config, const pdw::publishing::PublishEvent& event)
	{
		EnsureFolder(config.outputPath);
		EnterCriticalSection(&g_lock);
		bool isNew = true;
		for (std::vector<pdw::publishing::PublishEvent>::const_iterator existing = g_feed.begin();
			existing != g_feed.end(); ++existing)
			if (existing->id == event.id) { isNew = false; break; }
		if (isNew) g_feed.insert(g_feed.begin(), event);
		if (g_feed.size() > MAX_FEED_EVENTS) g_feed.resize(MAX_FEED_EVENTS);
		const std::vector<pdw::publishing::PublishEvent> feed(g_feed);
		LeaveCriticalSection(&g_lock);
		const bool jsonl = !isNew || AppendFile(JoinPath(config.outputPath, "messages.jsonl"),
			pdw::publishing::BuildJsonObject(event) + "\n");
		return jsonl &&
			WriteAtomic(JoinPath(config.outputPath, "messages.json"), pdw::publishing::BuildJsonFeed(feed)) &&
			WriteAtomic(JoinPath(config.outputPath, "messages.rss"), pdw::publishing::BuildRssFeed(feed)) &&
			WriteAtomic(JoinPath(config.outputPath, "messages.atom"), pdw::publishing::BuildAtomFeed(feed)) &&
			WriteAtomic(JoinPath(config.outputPath, "index.html"), pdw::publishing::BuildHtmlFeed(feed));
	}

	std::string CredentialTarget(const char* name)
	{
		return std::string("PDW Publishing ") + name + ": " + szIniPathName;
	}

	bool ReadSecret(const char* name, std::string& value)
	{
		value.clear();
		PCREDENTIALA credential = NULL;
		if (!CredReadA(CredentialTarget(name).c_str(), CRED_TYPE_GENERIC, 0, &credential))
			return GetLastError() == ERROR_NOT_FOUND;
		if (credential->CredentialBlob && credential->CredentialBlobSize)
			value.assign(reinterpret_cast<const char*>(credential->CredentialBlob), credential->CredentialBlobSize);
		CredFree(credential);
		return true;
	}

	bool WriteSecret(const char* name, const char* value)
	{
		const std::string target = CredentialTarget(name);
		if (!value || !value[0])
			return CredDeleteA(target.c_str(), CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND;
		CREDENTIALA credential = {0};
		credential.Type = CRED_TYPE_GENERIC;
		credential.TargetName = const_cast<char*>(target.c_str());
		credential.CredentialBlobSize = static_cast<DWORD>(strlen(value));
		credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(value));
		credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
		credential.UserName = const_cast<char*>("PDW");
		return CredWriteA(&credential, 0) != FALSE;
	}

	std::string HmacSha256(const std::string& secret, const std::string& payload)
	{
		unsigned char digest[EVP_MAX_MD_SIZE];
		unsigned int digestLength = 0;
		HMAC(EVP_sha256(), secret.data(), static_cast<int>(secret.size()),
			reinterpret_cast<const unsigned char*>(payload.data()), payload.size(), digest, &digestLength);
		static const char hex[] = "0123456789abcdef";
		std::string output(digestLength * 2, '0');
		for (unsigned int index = 0; index < digestLength; ++index)
		{
			output[index * 2] = hex[digest[index] >> 4];
			output[index * 2 + 1] = hex[digest[index] & 15];
		}
		SecureZeroMemory(digest, sizeof(digest));
		return output;
	}

	std::size_t DiscardResponse(char* data, std::size_t size, std::size_t count, void*)
	{
		(void)data;
		return size * count;
	}

	bool DeliverWebhook(const Config& config, const PublishTask& task, std::string& error)
	{
		if (config.webhookUrl.compare(0, 8, "https://") != 0)
		{
			error = "Webhook delivery refused because the URL is not HTTPS.";
			return false;
		}
		std::string bearer = task.test ? task.testBearer : std::string();
		std::string hmac = task.test ? task.testHmac : std::string();
		if (!task.test)
		{
			ReadSecret("Bearer", bearer);
			ReadSecret("HMAC", hmac);
		}
		CURL* curl = curl_easy_init();
		if (!curl) { error = "Unable to create HTTPS publishing request."; return false; }
		struct curl_slist* headers = NULL;
		headers = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");
		const std::string idempotency = "Idempotency-Key: " + task.event.id;
		headers = curl_slist_append(headers, idempotency.c_str());
		std::string authorization;
		if (!bearer.empty())
		{
			authorization = "Authorization: Bearer " + bearer;
			headers = curl_slist_append(headers, authorization.c_str());
		}
		std::string signature;
		if (!hmac.empty())
		{
			signature = "X-PDW-Signature: sha256=" + HmacSha256(hmac, task.payload);
			headers = curl_slist_append(headers, signature.c_str());
		}
		curl_easy_setopt(curl, CURLOPT_URL, config.webhookUrl.c_str());
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, task.payload.data());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(task.payload.size()));
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "PDW/3.3 publishing");
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, DiscardResponse);
		const CURLcode result = curl_easy_perform(curl);
		long status = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
		curl_slist_free_all(headers);
		curl_easy_cleanup(curl);
		if (!bearer.empty()) SecureZeroMemory(&bearer[0], bearer.size());
		if (!hmac.empty()) SecureZeroMemory(&hmac[0], hmac.size());
		if (result == CURLE_OK && status >= 200 && status < 300) return true;
		char message[180];
		snprintf(message, sizeof(message), "Webhook failed: %s (HTTP %ld).", curl_easy_strerror(result), status);
		error = message;
		return false;
	}

	void PersistTask(PublishTask& task)
	{
		if (!task.spoolPath.empty()) return;
		EnsureFolder(QueueFolder());
		task.spoolPath = JoinPath(QueueFolder(), task.event.id + ".json");
		WriteAtomic(task.spoolPath, task.payload);
	}

	void DeadLetter(PublishTask& task)
	{
		if (task.spoolPath.empty()) return;
		EnsureFolder(DeadLetterFolder());
		MoveFileExA(task.spoolPath.c_str(),
			JoinPath(DeadLetterFolder(), task.event.id + ".json").c_str(), MOVEFILE_REPLACE_EXISTING);
	}

	unsigned int __stdcall Worker(void*)
	{
		while (WaitForSingleObject(g_stopEvent, 0) != WAIT_OBJECT_0)
		{
			PublishTask task;
			bool available = false;
			EnterCriticalSection(&g_lock);
			if (!g_queue.empty())
			{
				task = g_queue.front();
				WipeTaskSecrets(g_queue.front());
				g_queue.pop_front();
				available = true;
			}
			LeaveCriticalSection(&g_lock);
			if (!available) { HANDLE events[2] = {g_stopEvent, g_workEvent}; WaitForMultipleObjects(2, events, FALSE, 1000); continue; }

			Config config = SnapshotConfig();
			if (task.test) { config.webhookEnabled = true; config.webhookUrl = task.testUrl; }
			if ((!config.enabled || !config.acknowledged || config.paused ||
				(!config.staticEnabled && !config.webhookEnabled)) && !task.test)
			{
				EnterCriticalSection(&g_lock); g_queue.push_front(task); LeaveCriticalSection(&g_lock);
				WipeTaskSecrets(task);
				WaitForSingleObject(g_stopEvent, 1000);
				continue;
			}
			PersistTask(task);
			bool success = true;
			if (config.staticEnabled && !task.staticDone && !task.test)
			{
				task.staticDone = PublishStatic(config, task.event);
				success = task.staticDone;
			}
			else task.staticDone = true;
			std::string deliveryError;
			if (config.webhookEnabled && !task.webhookDone)
			{
				task.webhookDone = DeliverWebhook(config, task, deliveryError);
				success = success && task.webhookDone;
			}
			else task.webhookDone = true;

			if (success)
			{
				if (!task.spoolPath.empty()) DeleteFileA(task.spoolPath.c_str());
				SetStatus(task.test ? "Webhook test delivered successfully." : "Publishing delivery completed.");
				WipeTaskSecrets(task);
			}
			else
			{
				task.attempts++;
				if (task.attempts >= MAX_ATTEMPTS)
				{
					DeadLetter(task);
					SetStatus("Publishing failed after five attempts; the event was moved to DeadLetter.");
					WipeTaskSecrets(task);
				}
				else
				{
					SetStatus(deliveryError.empty() ? "Static publishing failed; retry is queued." : deliveryError);
					const DWORD delay = static_cast<DWORD>(1000u << (task.attempts - 1));
					if (WaitForSingleObject(g_stopEvent, delay) == WAIT_OBJECT_0) break;
					EnterCriticalSection(&g_lock); g_queue.push_back(task); LeaveCriticalSection(&g_lock);
					WipeTaskSecrets(task);
					SetEvent(g_workEvent);
				}
			}
			if (config.minimumInterval && WaitForSingleObject(g_stopEvent,
				config.minimumInterval * 1000u) == WAIT_OBJECT_0) break;
		}
		return 0;
	}

	void QueueTask(PublishTask& task)
	{
		EnterCriticalSection(&g_lock);
		if (g_queue.size() >= MAX_QUEUE)
		{
			LeaveCriticalSection(&g_lock);
			WipeTaskSecrets(task);
			SetStatus("Publishing queue is full; new events are being retained only in PDW's legacy outputs.");
			return;
		}
		g_queue.push_back(task);
		LeaveCriticalSection(&g_lock);
		WipeTaskSecrets(task);
		SetEvent(g_workEvent);
	}

	void LoadPersistentQueue()
	{
		EnsureFolder(QueueFolder());
		EnsureFolder(DeadLetterFolder());
		WIN32_FIND_DATAA found;
		HANDLE search = FindFirstFileA(JoinPath(QueueFolder(), "*.json").c_str(), &found);
		if (search == INVALID_HANDLE_VALUE) return;
		do
		{
			const std::string path = JoinPath(QueueFolder(), found.cFileName);
			std::ifstream input(path.c_str(), std::ios::binary);
			std::ostringstream payload; payload << input.rdbuf();
			if (!payload.str().empty())
			{
				PublishTask task;
				task.payload = payload.str();
				task.spoolPath = path;
				task.event.id = found.cFileName;
				task.staticDone = true;
				g_queue.push_back(task);
			}
		} while (FindNextFileA(search, &found));
		FindClose(search);
	}

	void EnableDialogControls(HWND dialog)
	{
		const BOOL enabled = IsDlgButtonChecked(dialog, IDC_PUBLISH_ENABLE) == BST_CHECKED;
		const BOOL staticEnabled = IsDlgButtonChecked(dialog, IDC_PUBLISH_STATIC) == BST_CHECKED;
		const BOOL webhookEnabled = IsDlgButtonChecked(dialog, IDC_PUBLISH_WEBHOOK) == BST_CHECKED;
		EnableWindow(GetDlgItem(dialog, IDC_PUBLISH_ACK), enabled);
		EnableWindow(GetDlgItem(dialog, IDC_PUBLISH_STATIC), enabled);
		EnableWindow(GetDlgItem(dialog, IDC_PUBLISH_WEBHOOK), enabled);
		EnableWindow(GetDlgItem(dialog, IDC_PUBLISH_PATH), enabled && staticEnabled);
		EnableWindow(GetDlgItem(dialog, IDC_PUBLISH_BROWSE), enabled && staticEnabled);
		EnableWindow(GetDlgItem(dialog, IDC_PUBLISH_URL), enabled && webhookEnabled);
		EnableWindow(GetDlgItem(dialog, IDC_PUBLISH_BEARER), enabled && webhookEnabled);
		EnableWindow(GetDlgItem(dialog, IDC_PUBLISH_HMAC), enabled && webhookEnabled);
		EnableWindow(GetDlgItem(dialog, IDC_PUBLISH_TEST), enabled && webhookEnabled);
	}

	int CALLBACK BrowseFolderCallback(HWND window, UINT message, LPARAM, LPARAM data)
	{
		if (message == BFFM_INITIALIZED && data) SendMessageA(window, BFFM_SETSELECTIONA, TRUE, data);
		return 0;
	}

	bool BrowseFolder(HWND owner, char* path, std::size_t pathSize)
	{
		BROWSEINFOA browse = {0};
		browse.hwndOwner = owner;
		browse.lpszTitle = "Choose the folder where PDW will maintain JSON, RSS, Atom, HTML, and JSONL files.";
		browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
		browse.lpfn = BrowseFolderCallback;
		browse.lParam = reinterpret_cast<LPARAM>(path);
		PIDLIST_ABSOLUTE item = SHBrowseForFolderA(&browse);
		if (!item) return false;
		char selected[MAX_PATH];
		const bool ok = SHGetPathFromIDListA(item, selected) != FALSE;
		CoTaskMemFree(item);
		if (ok) CopyText(path, pathSize, selected);
		return ok;
	}
}

void PublishingManagerInitialize(void)
{
	if (g_initialized) return;
	InitializeCriticalSection(&g_lock);
	g_initialized = true;
	g_workEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	g_stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	curl_global_init(CURL_GLOBAL_DEFAULT);
	LoadPersistentQueue();
	const uintptr_t thread = _beginthreadex(NULL, 0, Worker, NULL, 0, NULL);
	g_thread = reinterpret_cast<HANDLE>(thread);
	PublishingSettingsChanged();
	if (!g_queue.empty()) SetEvent(g_workEvent);
}

void PublishingManagerShutdown(void)
{
	if (!g_initialized) return;
	InterlockedExchange(&g_stopping, 1);
	SetEvent(g_stopEvent);
	if (g_thread && WaitForSingleObject(g_thread, 20000) == WAIT_OBJECT_0)
	{
		CloseHandle(g_thread);
		g_thread = NULL;
	}
	if (g_thread) return;
	EnterCriticalSection(&g_lock);
	for (std::deque<PublishTask>::iterator task = g_queue.begin(); task != g_queue.end(); ++task)
		WipeTaskSecrets(*task);
	g_queue.clear();
	LeaveCriticalSection(&g_lock);
	CloseHandle(g_workEvent);
	CloseHandle(g_stopEvent);
	g_workEvent = g_stopEvent = NULL;
	curl_global_cleanup();
	g_initialized = false;
	DeleteCriticalSection(&g_lock);
}

void PublishingSettingsChanged(void)
{
	if (!g_initialized) return;
	const Config config = SnapshotConfig();
	if (!config.enabled) SetStatus("Publishing is disabled.");
	else if (!config.acknowledged) SetStatus("Publishing cannot start until the jurisdiction and permission acknowledgement is accepted.");
	else if (!config.staticEnabled && !config.webhookEnabled) SetStatus("Choose static files, an HTTPS webhook, or both.");
	else if (config.webhookEnabled && config.webhookUrl.compare(0, 8, "https://") != 0) SetStatus("Publishing webhook must use HTTPS.");
	else if (config.paused) SetStatus("Publishing is enabled but paused; queued events are retained.");
	else SetStatus("Publishing is enabled and ready.");
	SetEvent(g_workEvent);
}

void PublishingGetStatusText(char* buffer, size_t bufferSize)
{
	if (!buffer || !bufferSize) return;
	if (!g_initialized) { CopyText(buffer, bufferSize, "Publishing has not been initialized."); return; }
	EnterCriticalSection(&g_lock); CopyText(buffer, bufferSize, g_status); LeaveCriticalSection(&g_lock);
}

void PublishingPublishDecodedMessage(const DecodedMessageNotificationContext& context)
{
	if (!g_initialized || InterlockedCompareExchange(&g_stopping, 0, 0)) return;
	const Config config = SnapshotConfig();
	if (!config.enabled || !config.acknowledged || config.paused ||
		(!config.staticEnabled && !config.webhookEnabled) || (config.filteredOnly && !context.filtered)) return;
	pdw::publishing::PublishEvent source;
	source.id = EventId();
	source.timestamp = NowIso8601();
	source.source = "PDW";
	source.address = Utf8(context.address);
	source.time = Utf8(context.time);
	source.date = Utf8(context.date);
	source.mode = Utf8(context.mode);
	source.messageType = Utf8(context.messageType);
	source.bitrate = Utf8(context.bitrate);
	source.message = Utf8(context.message);
	source.filterLabel = Utf8(context.filterLabel);
	source.filtered = context.filtered;
	pdw::publishing::TransformOptions options;
	options.sourceAlias = Utf8(config.sourceAlias.c_str());
	options.maskAddress = config.maskAddress;
	options.includeMessage = config.includeMessage;
	PublishTask task;
	task.event = pdw::publishing::ApplyTransform(source, options);
	task.payload = pdw::publishing::BuildJsonObject(task.event);
	QueueTask(task);
}

BOOL FAR PASCAL PublishingDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM)
{
	switch (uMsg)
	{
		case WM_INITDIALOG:
		{
			CenterWindow(hDlg);
			CheckDlgButton(hDlg, IDC_PUBLISH_ENABLE, Profile.publishingEnabled ? BST_CHECKED : BST_UNCHECKED);
			CheckDlgButton(hDlg, IDC_PUBLISH_ACK, Profile.publishingPermissionAcknowledged ? BST_CHECKED : BST_UNCHECKED);
			CheckRadioButton(hDlg, IDC_PUBLISH_FILTERED, IDC_PUBLISH_ALL,
				Profile.publishingFilteredOnly ? IDC_PUBLISH_FILTERED : IDC_PUBLISH_ALL);
			CheckDlgButton(hDlg, IDC_PUBLISH_STATIC, Profile.publishingStaticEnabled ? BST_CHECKED : BST_UNCHECKED);
			CheckDlgButton(hDlg, IDC_PUBLISH_WEBHOOK, Profile.publishingWebhookEnabled ? BST_CHECKED : BST_UNCHECKED);
			CheckDlgButton(hDlg, IDC_PUBLISH_MASK, Profile.publishingMaskAddress ? BST_CHECKED : BST_UNCHECKED);
			CheckDlgButton(hDlg, IDC_PUBLISH_MESSAGE, Profile.publishingIncludeMessage ? BST_CHECKED : BST_UNCHECKED);
			CheckDlgButton(hDlg, IDC_PUBLISH_PAUSED, Profile.publishingPaused ? BST_CHECKED : BST_UNCHECKED);
			SetDlgItemText(hDlg, IDC_PUBLISH_PATH, Profile.publishingOutputPath);
			SetDlgItemText(hDlg, IDC_PUBLISH_URL, Profile.publishingWebhookUrl);
			SetDlgItemText(hDlg, IDC_PUBLISH_ALIAS, Profile.publishingSourceAlias);
			SetDlgItemInt(hDlg, IDC_PUBLISH_INTERVAL, Profile.publishingMinimumIntervalSeconds, FALSE);
			SetDlgItemText(hDlg, IDC_PUBLISH_BEARER, "");
			SetDlgItemText(hDlg, IDC_PUBLISH_HMAC, "");
			char status[512]; PublishingGetStatusText(status, sizeof(status)); SetDlgItemText(hDlg, IDC_PUBLISH_STATUS, status);
			EnableDialogControls(hDlg);
			SetTimer(hDlg, DIALOG_TIMER, 500, NULL);
			return TRUE;
		}

		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
				case IDC_PUBLISH_ENABLE:
				case IDC_PUBLISH_STATIC:
				case IDC_PUBLISH_WEBHOOK:
					EnableDialogControls(hDlg); return TRUE;
				case IDC_PUBLISH_BROWSE:
				{
					char path[PUBLISH_PATH_LEN + 1]; GetDlgItemText(hDlg, IDC_PUBLISH_PATH, path, sizeof(path));
					if (BrowseFolder(hDlg, path, sizeof(path))) SetDlgItemText(hDlg, IDC_PUBLISH_PATH, path);
					return TRUE;
				}
				case IDC_PUBLISH_OPEN:
				{
					char path[PUBLISH_PATH_LEN + 1]; GetDlgItemText(hDlg, IDC_PUBLISH_PATH, path, sizeof(path));
					if (path[0]) ShellExecuteA(hDlg, "open", ResolveOutputPath(path).c_str(), NULL, NULL, SW_SHOWNORMAL);
					return TRUE;
				}
				case IDC_PUBLISH_TEST:
				{
					char url[PUBLISH_URL_LEN + 1]; GetDlgItemText(hDlg, IDC_PUBLISH_URL, url, sizeof(url));
					if (strncmp(url, "https://", 8) != 0) { MessageBox(hDlg, "Webhook tests require an HTTPS URL.", "PDW Publishing", MB_ICONERROR); return TRUE; }
					char bearer[2049], hmac[2049]; GetDlgItemText(hDlg, IDC_PUBLISH_BEARER, bearer, sizeof(bearer)); GetDlgItemText(hDlg, IDC_PUBLISH_HMAC, hmac, sizeof(hmac));
					PublishTask task; task.test = true; task.testUrl = url; task.testBearer = bearer; task.testHmac = hmac; task.event.id = EventId(); task.event.timestamp = NowIso8601(); task.event.source = "PDW"; task.event.mode = "TEST"; task.event.messageType = "Publishing"; task.event.message = "PDW webhook configuration test"; task.payload = pdw::publishing::BuildJsonObject(task.event); QueueTask(task);
					SecureZeroMemory(bearer, sizeof(bearer)); SecureZeroMemory(hmac, sizeof(hmac));
					SetDlgItemText(hDlg, IDC_PUBLISH_STATUS, "Webhook test queued without decoded message content.");
					return TRUE;
				}
				case IDOK:
				{
					const bool enabled = IsDlgButtonChecked(hDlg, IDC_PUBLISH_ENABLE) == BST_CHECKED;
					const bool acknowledged = IsDlgButtonChecked(hDlg, IDC_PUBLISH_ACK) == BST_CHECKED;
					if (enabled && !acknowledged) { MessageBox(hDlg, "To enable publishing, acknowledge that you are responsible for checking permissions and laws in your own country or jurisdiction.", "PDW Publishing", MB_ICONWARNING); return TRUE; }
					char path[PUBLISH_PATH_LEN + 1], url[PUBLISH_URL_LEN + 1], alias[PUBLISH_ALIAS_LEN + 1], bearer[2049], hmac[2049];
					GetDlgItemText(hDlg, IDC_PUBLISH_PATH, path, sizeof(path)); GetDlgItemText(hDlg, IDC_PUBLISH_URL, url, sizeof(url)); GetDlgItemText(hDlg, IDC_PUBLISH_ALIAS, alias, sizeof(alias)); GetDlgItemText(hDlg, IDC_PUBLISH_BEARER, bearer, sizeof(bearer)); GetDlgItemText(hDlg, IDC_PUBLISH_HMAC, hmac, sizeof(hmac));
					const bool staticEnabled = IsDlgButtonChecked(hDlg, IDC_PUBLISH_STATIC) == BST_CHECKED;
					const bool webhookEnabled = IsDlgButtonChecked(hDlg, IDC_PUBLISH_WEBHOOK) == BST_CHECKED;
					if (enabled && !staticEnabled && !webhookEnabled) { MessageBox(hDlg, "Choose static files, an HTTPS webhook, or both.", "PDW Publishing", MB_ICONWARNING); return TRUE; }
					if (enabled && staticEnabled && !path[0]) { MessageBox(hDlg, "Choose a static publishing folder.", "PDW Publishing", MB_ICONWARNING); return TRUE; }
					if (enabled && webhookEnabled && strncmp(url, "https://", 8) != 0) { MessageBox(hDlg, "The publishing webhook must use HTTPS.", "PDW Publishing", MB_ICONWARNING); return TRUE; }
					BOOL translated = FALSE; UINT interval = GetDlgItemInt(hDlg, IDC_PUBLISH_INTERVAL, &translated, FALSE); if (!translated || interval > 86400) { MessageBox(hDlg, "Minimum interval must be between 0 and 86400 seconds.", "PDW Publishing", MB_ICONWARNING); return TRUE; }
					Profile.publishingEnabled = enabled; Profile.publishingPermissionAcknowledged = acknowledged; Profile.publishingPaused = IsDlgButtonChecked(hDlg, IDC_PUBLISH_PAUSED) == BST_CHECKED; Profile.publishingFilteredOnly = IsDlgButtonChecked(hDlg, IDC_PUBLISH_FILTERED) == BST_CHECKED; Profile.publishingStaticEnabled = staticEnabled; Profile.publishingWebhookEnabled = webhookEnabled; Profile.publishingMaskAddress = IsDlgButtonChecked(hDlg, IDC_PUBLISH_MASK) == BST_CHECKED; Profile.publishingIncludeMessage = IsDlgButtonChecked(hDlg, IDC_PUBLISH_MESSAGE) == BST_CHECKED; Profile.publishingMinimumIntervalSeconds = interval;
					CopyText(Profile.publishingOutputPath, sizeof(Profile.publishingOutputPath), path); CopyText(Profile.publishingWebhookUrl, sizeof(Profile.publishingWebhookUrl), url); CopyText(Profile.publishingSourceAlias, sizeof(Profile.publishingSourceAlias), alias);
					if ((bearer[0] && !WriteSecret("Bearer", bearer)) || (hmac[0] && !WriteSecret("HMAC", hmac))) { SecureZeroMemory(bearer, sizeof(bearer)); SecureZeroMemory(hmac, sizeof(hmac)); MessageBox(hDlg, "Windows Credential Manager could not save the publishing secret.", "PDW Publishing", MB_ICONERROR); return TRUE; }
					SecureZeroMemory(bearer, sizeof(bearer)); SecureZeroMemory(hmac, sizeof(hmac)); WriteSettings(); PublishingSettingsChanged(); EndDialog(hDlg, TRUE); return TRUE;
				}
				case IDCANCEL: EndDialog(hDlg, FALSE); return TRUE;
			}
			break;

		case WM_TIMER:
		{
			char status[512]; PublishingGetStatusText(status, sizeof(status)); SetDlgItemText(hDlg, IDC_PUBLISH_STATUS, status); return TRUE;
		}
		case WM_DESTROY: KillTimer(hDlg, DIALOG_TIMER); return TRUE;
		case WM_CLOSE: EndDialog(hDlg, FALSE); return TRUE;
	}
	return FALSE;
}
