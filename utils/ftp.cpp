#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>
#include <commdlg.h>
#include <wincred.h>
#include <wincrypt.h>
#include <process.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <new>
#include <string>
#include <vector>

#include <curl/curl.h>

#include "headers\resource.h"
#include "headers\pdw.h"
#include "headers\ftp.h"
#include "headers\initapp.h"
#include "curl_runtime.h"

using namespace std;

namespace
{
	const long FTP_CONNECT_TIMEOUT_MS = 15000L;
	const long FTP_LOW_SPEED_SECONDS = 30L;
	const UINT_PTR FTP_DIALOG_TIMER = 1;
	const size_t FTP_STATUS_LEN = 511;
	const size_t FTP_PASSWORD_LEN = 511;
	const char FTP_PASSWORD_PLACEHOLDER[] = "********";

	struct FtpUploadTask
	{
		int protocol;
		string server;
		unsigned int port;
		string username;
		string password;
		string remoteDirectory;
		string sshHostKeySha256;
		bool passive;
		unsigned int intervalSeconds;
		LONG settingsGeneration;
		vector<string> files;
	};

	struct FtpDialogValues
	{
		int enabled;
		int protocol;
		char server[FTP_SERVER_LEN+1];
		unsigned int port;
		char username[FTP_USERNAME_LEN+1];
		char password[FTP_PASSWORD_LEN+1];
		char remoteDirectory[FTP_REMOTE_DIR_LEN+1];
		char sshHostKeySha256[FTP_SSH_HOST_KEY_LEN+1];
		int passive;
		unsigned int intervalSeconds;
	};

	CRITICAL_SECTION g_ftpStateLock;
	bool g_ftpInitialized = false;
	bool g_curlInitialized = false;
	volatile LONG g_uploadInProgress = 0;
	volatile LONG g_shuttingDown = 0;
	volatile LONG g_nextUploadTick = 0;
	volatile LONG g_settingsGeneration = 0;
	HANDLE g_workerThread = NULL;
	char g_ftpStatus[FTP_STATUS_LEN+1] = "File-transfer uploader has not been initialized.";
	vector<string> g_dialogFiles;
	bool g_dialogCredentialExists = false;
	int g_dialogProtocol = FTP_PROTOCOL_FTP;

	void CopyText(char *destination, size_t destinationSize, const char *source)
	{
		if (destination == NULL || destinationSize == 0) return;
		if (source == NULL) source = "";
		strncpy(destination, source, destinationSize-1);
		destination[destinationSize-1] = '\0';
	}

	void WipeString(string &value)
	{
		if (!value.empty())
		{
			SecureZeroMemory(&value[0], value.size());
			value.clear();
		}
	}

	string TrimMessage(const string &message)
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
			string result = TrimMessage(systemMessage);
			LocalFree(systemMessage);
			return result;
		}

		char fallback[64];
		snprintf(fallback, sizeof(fallback), "Windows error %lu", (unsigned long) errorCode);
		fallback[sizeof(fallback)-1] = '\0';
		return fallback;
	}

	void SetFtpStatus(const char *text)
	{
		if (!g_ftpInitialized) return;
		EnterCriticalSection(&g_ftpStateLock);
		CopyText(g_ftpStatus, sizeof(g_ftpStatus), text);
		LeaveCriticalSection(&g_ftpStateLock);
	}

	void AppendFtpLog(const char *format, ...)
	{
		char logPath[MAX_PATH];
		snprintf(logPath, sizeof(logPath), "%s\\FileTransfer.log", szPath);
		logPath[sizeof(logPath)-1] = '\0';

		FILE *logFile = fopen(logPath, "a");
		if (logFile == NULL) return;

		SYSTEMTIME now;
		GetLocalTime(&now);
		fprintf(logFile, "%04u-%02u-%02u %02u:%02u:%02u  ",
			(unsigned int) now.wYear, (unsigned int) now.wMonth, (unsigned int) now.wDay,
			(unsigned int) now.wHour, (unsigned int) now.wMinute, (unsigned int) now.wSecond);

		va_list arguments;
		va_start(arguments, format);
		vfprintf(logFile, format, arguments);
		va_end(arguments);

		fprintf(logFile, "\n");
		fclose(logFile);
	}

	string CredentialTargetName(void)
	{
		return string("PDW File Transfer: ") + szIniPathName;
	}

	string LegacyCredentialTargetName(void)
	{
		return string("PDW FTP: ") + szIniPathName;
	}

	bool ReadPasswordFromTarget(const string &target, string &password)
	{
		password.clear();
		PCREDENTIALA credential = NULL;

		if (!CredReadA(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) return false;

		if (credential->CredentialBlob != NULL && credential->CredentialBlobSize > 0 &&
			credential->CredentialBlobSize <= FTP_PASSWORD_LEN)
		{
			password.assign((const char *) credential->CredentialBlob, credential->CredentialBlobSize);
		}
		CredFree(credential);
		return !password.empty();
	}

	bool ReadSavedPassword(string &password)
	{
		if (ReadPasswordFromTarget(CredentialTargetName(), password)) return true;
		return ReadPasswordFromTarget(LegacyCredentialTargetName(), password);
	}

	bool HasSavedPassword(void)
	{
		string password;
		bool found = ReadSavedPassword(password);
		WipeString(password);
		return found;
	}

	bool SavePassword(const char *username, const char *password, string &error)
	{
		if (password == NULL || password[0] == '\0')
		{
			error = "Enter a file-transfer password before saving it.";
			return false;
		}

		string target = CredentialTargetName();
		CREDENTIALA credential;
		ZeroMemory(&credential, sizeof(credential));
		credential.Type = CRED_TYPE_GENERIC;
		credential.TargetName = const_cast<LPSTR>(target.c_str());
		credential.CredentialBlobSize = (DWORD) strlen(password);
		credential.CredentialBlob = (LPBYTE) password;
		credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
		credential.UserName = const_cast<LPSTR>(username);

		if (!CredWriteA(&credential, 0))
		{
			error = "Windows could not save the file-transfer password: " + WindowsErrorText(GetLastError());
			return false;
		}
		return true;
	}

	bool DeleteSavedPassword(string &error)
	{
		string targets[2] = { CredentialTargetName(), LegacyCredentialTargetName() };
		for (int index = 0; index < 2; index++)
		{
			if (CredDeleteA(targets[index].c_str(), CRED_TYPE_GENERIC, 0)) continue;
			DWORD errorCode = GetLastError();
			if (errorCode != ERROR_NOT_FOUND)
			{
				error = "Windows could not remove the file-transfer password: " + WindowsErrorText(errorCode);
				return false;
			}
		}
		return true;
	}

	const char *ProtocolName(int protocol)
	{
		switch (protocol)
		{
			case FTP_PROTOCOL_FTP: return "FTP";
			case FTP_PROTOCOL_FTPS_EXPLICIT: return "FTPS (explicit TLS)";
			case FTP_PROTOCOL_FTPS_IMPLICIT: return "FTPS (implicit TLS)";
			case FTP_PROTOCOL_SFTP: return "SFTP";
		}
		return "Unknown protocol";
	}

	const char *ProtocolScheme(int protocol)
	{
		if (protocol == FTP_PROTOCOL_FTPS_IMPLICIT) return "ftps";
		if (protocol == FTP_PROTOCOL_SFTP) return "sftp";
		return "ftp";
	}

	unsigned int DefaultProtocolPort(int protocol)
	{
		if (protocol == FTP_PROTOCOL_FTPS_IMPLICIT) return 990;
		if (protocol == FTP_PROTOCOL_SFTP) return 22;
		return 21;
	}

	bool ProtocolIsFtpFamily(int protocol)
	{
		return protocol == FTP_PROTOCOL_FTP ||
			protocol == FTP_PROTOCOL_FTPS_EXPLICIT ||
			protocol == FTP_PROTOCOL_FTPS_IMPLICIT;
	}

	bool CurlSupportsProtocol(const char *protocol)
	{
		curl_version_info_data *version = curl_version_info(CURLVERSION_NOW);
		if (version == NULL || version->protocols == NULL) return false;
		for (const char * const *entry = version->protocols; *entry != NULL; entry++)
			if (_stricmp(*entry, protocol) == 0) return true;
		return false;
	}

	bool SelectedProtocolIsAvailable(int protocol)
	{
		if (protocol == FTP_PROTOCOL_SFTP) return CurlSupportsProtocol("sftp");
		if (protocol == FTP_PROTOCOL_FTPS_IMPLICIT) return CurlSupportsProtocol("ftps");
		if (protocol == FTP_PROTOCOL_FTPS_EXPLICIT)
			return CurlSupportsProtocol("ftp") && CurlSupportsProtocol("ftps");
		return CurlSupportsProtocol("ftp");
	}

	bool NormalizeSshHostKey(const char *source, string &normalized, string &error)
	{
		normalized = source == NULL ? "" : source;
		size_t first = normalized.find_first_not_of(" \t\r\n");
		size_t last = normalized.find_last_not_of(" \t\r\n");
		if (first == string::npos)
		{
			normalized.clear();
			error = "Enter the SFTP server's SHA256 host-key fingerprint.";
			return false;
		}
		normalized = normalized.substr(first, last-first+1);
		if (normalized.size() >= 7 && _strnicmp(normalized.c_str(), "SHA256:", 7) == 0)
			normalized.erase(0, 7);

		if ((normalized.size() != 43 && normalized.size() != 44) ||
			normalized.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=") != string::npos ||
			(normalized.size() == 44 && normalized[43] != '='))
		{
			error = "The SFTP host-key fingerprint is not a valid SHA256 Base64 value.";
			return false;
		}
		if (normalized.size() == 43) normalized += "=";

		DWORD decodedSize = 0;
		if (!CryptStringToBinaryA(normalized.c_str(), (DWORD) normalized.size(), CRYPT_STRING_BASE64,
			NULL, &decodedSize, NULL, NULL) || decodedSize != 32)
		{
			error = "The SFTP host-key fingerprint must be a SHA256 fingerprint copied from the hosting provider.";
			return false;
		}
		return true;
	}

	bool BuildRemoteUrl(int protocol, const string &server, unsigned int port,
		const string &remoteDirectory, const string &remoteName, string &urlText, string &error)
	{
		CURLU *url = curl_url();
		if (url == NULL)
		{
			error = "libcurl could not allocate a URL.";
			return false;
		}

		char portText[16];
		snprintf(portText, sizeof(portText), "%u", port);
		portText[sizeof(portText)-1] = '\0';

		string path = remoteDirectory;
		for (size_t index = 0; index < path.size(); index++)
			if (path[index] == '\\') path[index] = '/';

		if (protocol == FTP_PROTOCOL_SFTP)
		{
			// libcurl treats SFTP paths as absolute unless /~/ anchors them at
			// the authenticated user's home directory.
			if (path.empty()) path = "/~/";
			else if (path[0] == '~') path.insert(path.begin(), '/');
			else if (path[0] != '/') path = "/~/" + path;
		}
		else if (path.empty() || path[0] != '/') path.insert(path.begin(), '/');

		if (path[path.size()-1] != '/') path += "/";
		path += remoteName;

		CURLUcode result = curl_url_set(url, CURLUPART_SCHEME, ProtocolScheme(protocol), 0);
		if (result == CURLUE_OK) result = curl_url_set(url, CURLUPART_HOST, server.c_str(), 0);
		if (result == CURLUE_OK) result = curl_url_set(url, CURLUPART_PORT, portText, 0);
		if (result == CURLUE_OK) result = curl_url_set(url, CURLUPART_PATH, path.c_str(), CURLU_URLENCODE);

		char *generatedUrl = NULL;
		if (result == CURLUE_OK) result = curl_url_get(url, CURLUPART_URL, &generatedUrl, 0);
		if (result == CURLUE_OK && generatedUrl != NULL) urlText = generatedUrl;
		else error = string("Could not create the remote upload URL: ") + curl_url_strerror(result);

		if (generatedUrl != NULL) curl_free(generatedUrl);
		curl_url_cleanup(url);
		return result == CURLUE_OK && !urlText.empty();
	}

	int TransferProgress(void *, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
	{
		return InterlockedCompareExchange(&g_shuttingDown, 0, 0) != 0 ? 1 : 0;
	}

	string CurlErrorText(CURLcode code, const char *errorBuffer)
	{
		if (errorBuffer != NULL && errorBuffer[0] != '\0') return TrimMessage(errorBuffer);
		return curl_easy_strerror(code);
	}

	string BaseName(const string &path)
	{
		size_t slash = path.find_last_of("\\/");
		if (slash == string::npos) return path;
		return path.substr(slash+1);
	}

	bool SameText(const string &left, const string &right)
	{
		return _stricmp(left.c_str(), right.c_str()) == 0;
	}

	bool HasDuplicateRemoteNames(const vector<string> &files, string &duplicateName)
	{
		for (size_t outer = 0; outer < files.size(); outer++)
		{
			string outerName = BaseName(files[outer]);
			for (size_t inner = outer+1; inner < files.size(); inner++)
			{
				if (SameText(outerName, BaseName(files[inner])))
				{
					duplicateName = outerName;
					return true;
				}
			}
		}
		return false;
	}

	bool ValidateProfile(string &error)
	{
		if (Profile.ftpProtocol < FTP_PROTOCOL_FTP || Profile.ftpProtocol > FTP_PROTOCOL_SFTP)
			error = "Select a supported file-transfer protocol.";
		else if (!SelectedProtocolIsAvailable(Profile.ftpProtocol))
			error = string(ProtocolName(Profile.ftpProtocol)) + " support is missing from this PDW build.";
		else if (Profile.ftpServer[0] == '\0') error = "Enter the hosting server hostname.";
		else if (strstr(Profile.ftpServer, "://") != NULL || strpbrk(Profile.ftpServer, ":/\\@?# \t\r\n") != NULL)
			error = "Enter only the hosting server hostname, without a protocol or path.";
		else if (Profile.ftpPort < 1 || Profile.ftpPort > 65535) error = "The server port must be between 1 and 65535.";
		else if (Profile.ftpUsername[0] == '\0') error = "Enter the hosting username.";
		else if (Profile.ftpIntervalSeconds < FTP_MIN_INTERVAL || Profile.ftpIntervalSeconds > FTP_MAX_INTERVAL)
			error = "The upload interval must be between 10 and 86400 seconds.";
		else if (Profile.ftpFiles.empty()) error = "Select at least one file to upload.";
		else if (Profile.ftpFiles.size() > FTP_MAX_FILES) error = "No more than 64 files can be uploaded.";
		else if (Profile.ftpProtocol == FTP_PROTOCOL_SFTP)
		{
			string normalized;
			NormalizeSshHostKey(Profile.ftpSshHostKeySha256, normalized, error);
		}

		if (error.empty())
		{
			string duplicateName;
			if (HasDuplicateRemoteNames(Profile.ftpFiles, duplicateName))
				error = "Two selected files have the same filename (" + duplicateName + ") and would overwrite each other.";
		}
		return error.empty();
	}

	bool CreateFileSnapshot(const string &sourcePath, string &snapshotPath, string &error)
	{
		char tempDirectory[MAX_PATH];
		char tempFile[MAX_PATH];
		DWORD tempLength = GetTempPathA(MAX_PATH, tempDirectory);
		if (tempLength == 0)
		{
			error = WindowsErrorText(GetLastError());
			return false;
		}
		if (tempLength >= MAX_PATH)
		{
			error = "The Windows temporary-file path is too long.";
			return false;
		}

		if (GetTempFileNameA(tempDirectory, "PDW", 0, tempFile) == 0)
		{
			error = WindowsErrorText(GetLastError());
			return false;
		}

		if (!CopyFileA(sourcePath.c_str(), tempFile, FALSE))
		{
			error = WindowsErrorText(GetLastError());
			DeleteFileA(tempFile);
			return false;
		}

		snapshotPath = tempFile;
		return true;
	}

	bool RunUpload(FtpUploadTask &task, unsigned int &uploaded, unsigned int &failed)
	{
		uploaded = 0;
		failed = 0;

		if (InterlockedCompareExchange(&g_shuttingDown, 0, 0) != 0) return false;

		for (size_t index = 0; index < task.files.size(); index++)
		{
			if (InterlockedCompareExchange(&g_shuttingDown, 0, 0) != 0)
			{
				failed += (unsigned int) (task.files.size()-index);
				break;
			}

			string snapshot;
			string error;
			if (!CreateFileSnapshot(task.files[index], snapshot, error))
			{
				AppendFtpLog("ERROR: Could not snapshot '%s': %s", task.files[index].c_str(), error.c_str());
				failed++;
				continue;
			}

			string remoteName = BaseName(task.files[index]);
			string remoteUrl;
			if (!BuildRemoteUrl(task.protocol, task.server, task.port, task.remoteDirectory,
				remoteName, remoteUrl, error))
			{
				AppendFtpLog("ERROR: Could not prepare upload for '%s': %s", task.files[index].c_str(), error.c_str());
				DeleteFileA(snapshot.c_str());
				failed++;
				continue;
			}

			FILE *uploadFile = fopen(snapshot.c_str(), "rb");
			if (uploadFile == NULL)
			{
				AppendFtpLog("ERROR: Could not open snapshot for '%s'.", task.files[index].c_str());
				DeleteFileA(snapshot.c_str());
				failed++;
				continue;
			}

			_fseeki64(uploadFile, 0, SEEK_END);
			__int64 fileSize = _ftelli64(uploadFile);
			_fseeki64(uploadFile, 0, SEEK_SET);
			if (fileSize < 0)
			{
				AppendFtpLog("ERROR: Could not determine the size of '%s'.", task.files[index].c_str());
				fclose(uploadFile);
				DeleteFileA(snapshot.c_str());
				failed++;
				continue;
			}

			CURL *curl = curl_easy_init();
			if (curl == NULL)
			{
				AppendFtpLog("ERROR: libcurl could not create an upload session for '%s'.", task.files[index].c_str());
				fclose(uploadFile);
				DeleteFileA(snapshot.c_str());
				failed++;
				continue;
			}

			char curlError[CURL_ERROR_SIZE];
			ZeroMemory(curlError, sizeof(curlError));
			CURLcode result = CURLE_OK;

#define PDW_CURL_SETOPT(option, value) \
			do { if (result == CURLE_OK) result = curl_easy_setopt(curl, option, value); } while (0)
			PDW_CURL_SETOPT(CURLOPT_ERRORBUFFER, curlError);
			PDW_CURL_SETOPT(CURLOPT_URL, remoteUrl.c_str());
			PDW_CURL_SETOPT(CURLOPT_PROTOCOLS_STR, ProtocolScheme(task.protocol));
			PDW_CURL_SETOPT(CURLOPT_USERNAME, task.username.c_str());
			PDW_CURL_SETOPT(CURLOPT_PASSWORD, task.password.c_str());
			PDW_CURL_SETOPT(CURLOPT_UPLOAD, 1L);
			PDW_CURL_SETOPT(CURLOPT_READDATA, uploadFile);
			PDW_CURL_SETOPT(CURLOPT_INFILESIZE_LARGE, (curl_off_t) fileSize);
			PDW_CURL_SETOPT(CURLOPT_CONNECTTIMEOUT_MS, FTP_CONNECT_TIMEOUT_MS);
			PDW_CURL_SETOPT(CURLOPT_LOW_SPEED_LIMIT, 1L);
			PDW_CURL_SETOPT(CURLOPT_LOW_SPEED_TIME, FTP_LOW_SPEED_SECONDS);
			PDW_CURL_SETOPT(CURLOPT_NOSIGNAL, 1L);
			PDW_CURL_SETOPT(CURLOPT_NOPROGRESS, 0L);
			PDW_CURL_SETOPT(CURLOPT_XFERINFOFUNCTION, TransferProgress);
			PDW_CURL_SETOPT(CURLOPT_SSL_VERIFYPEER, 1L);
			PDW_CURL_SETOPT(CURLOPT_SSL_VERIFYHOST, 2L);
			PDW_CURL_SETOPT(CURLOPT_TCP_KEEPALIVE, 1L);

			if (task.protocol == FTP_PROTOCOL_FTPS_EXPLICIT || task.protocol == FTP_PROTOCOL_FTPS_IMPLICIT)
			{
				PDW_CURL_SETOPT(CURLOPT_USE_SSL, CURLUSESSL_ALL);
				PDW_CURL_SETOPT(CURLOPT_FTPSSLAUTH, CURLFTPAUTH_TLS);
				PDW_CURL_SETOPT(CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
			}

			if (ProtocolIsFtpFamily(task.protocol) && !task.passive)
				PDW_CURL_SETOPT(CURLOPT_FTPPORT, "-");

			if (task.protocol == FTP_PROTOCOL_SFTP)
			{
				PDW_CURL_SETOPT(CURLOPT_SSH_AUTH_TYPES,
					CURLSSH_AUTH_PASSWORD | CURLSSH_AUTH_KEYBOARD);
				PDW_CURL_SETOPT(CURLOPT_SSH_HOST_PUBLIC_KEY_SHA256, task.sshHostKeySha256.c_str());
			}
#undef PDW_CURL_SETOPT

			if (result == CURLE_OK) result = curl_easy_perform(curl);
			string uploadError = result == CURLE_OK ? "" : CurlErrorText(result, curlError);
			curl_easy_cleanup(curl);
			fclose(uploadFile);
			DeleteFileA(snapshot.c_str());

			if (result == CURLE_OK)
			{
				AppendFtpLog("Uploaded '%s' as '%s' using %s", task.files[index].c_str(),
					remoteName.c_str(), ProtocolName(task.protocol));
				uploaded++;
			}
			else
			{
				AppendFtpLog("ERROR: Could not upload '%s': %s", task.files[index].c_str(),
					uploadError.c_str());
				failed++;
			}
		}

		return failed == 0 && uploaded == task.files.size();
	}

	unsigned __stdcall FtpWorker(void *context)
	{
		FtpUploadTask *task = (FtpUploadTask *) context;
		char status[FTP_STATUS_LEN+1];
		snprintf(status, sizeof(status), "Uploading %u file(s) to %s...",
			(unsigned int) task->files.size(), task->server.c_str());
		status[sizeof(status)-1] = '\0';
		SetFtpStatus(status);

		AppendFtpLog("Starting %s upload of %u file(s) to %s:%u%s",
			ProtocolName(task->protocol), (unsigned int) task->files.size(), task->server.c_str(),
			(unsigned int) task->port,
			ProtocolIsFtpFamily(task->protocol) ? (task->passive ? " (passive mode)" : " (active mode)") : "");

		unsigned int uploaded = 0;
		unsigned int failed = 0;
		bool success = RunUpload(*task, uploaded, failed);

		SYSTEMTIME now;
		GetLocalTime(&now);
		if (success)
		{
			snprintf(status, sizeof(status), "Last upload succeeded at %02u:%02u:%02u (%u file(s)).",
				(unsigned int) now.wHour, (unsigned int) now.wMinute, (unsigned int) now.wSecond, uploaded);
			AppendFtpLog("Upload completed successfully (%u file(s))", uploaded);
		}
		else
		{
			snprintf(status, sizeof(status), "Last upload had errors at %02u:%02u:%02u (%u uploaded, %u failed). See FileTransfer.log.",
				(unsigned int) now.wHour, (unsigned int) now.wMinute, (unsigned int) now.wSecond, uploaded, failed);
			AppendFtpLog("Upload completed with errors (%u uploaded, %u failed)", uploaded, failed);
		}
		status[sizeof(status)-1] = '\0';
		SetFtpStatus(status);

		if (task->settingsGeneration == InterlockedCompareExchange(&g_settingsGeneration, 0, 0))
		{
			DWORD nextTick = GetTickCount() + (task->intervalSeconds * 1000UL);
			InterlockedExchange(&g_nextUploadTick, (LONG) nextTick);
		}

		WipeString(task->password);
		delete task;
		InterlockedExchange(&g_uploadInProgress, 0);
		return 0;
	}

	void PopulateFileList(HWND hDlg)
	{
		HWND list = GetDlgItem(hDlg, IDC_FTP_FILES);
		SendMessageA(list, LB_RESETCONTENT, 0, 0);
		for (size_t index = 0; index < g_dialogFiles.size(); index++)
			SendMessageA(list, LB_ADDSTRING, 0, (LPARAM) g_dialogFiles[index].c_str());
		SendMessageA(list, LB_SETHORIZONTALEXTENT, 1200, 0);
	}

	bool DialogContainsPath(const string &path)
	{
		for (size_t index = 0; index < g_dialogFiles.size(); index++)
			if (SameText(g_dialogFiles[index], path)) return true;
		return false;
	}

	bool DialogContainsRemoteName(const string &path)
	{
		string name = BaseName(path);
		for (size_t index = 0; index < g_dialogFiles.size(); index++)
			if (SameText(BaseName(g_dialogFiles[index]), name)) return true;
		return false;
	}

	void AddDialogPath(HWND hDlg, const string &path)
	{
		if (g_dialogFiles.size() >= FTP_MAX_FILES)
		{
			MessageBoxA(hDlg, "PDW can upload no more than 64 files.", "PDW File Transfer", MB_ICONWARNING | MB_OK);
			return;
		}
		if (path.size() >= MAX_PATH)
		{
			MessageBoxA(hDlg, "That path is too long for this version of PDW.", "PDW File Transfer", MB_ICONWARNING | MB_OK);
			return;
		}
		if (DialogContainsPath(path)) return;
		if (DialogContainsRemoteName(path))
		{
			string message = "A selected file already uses the remote filename '" + BaseName(path) + "'.\n\nChoose only one of those files.";
			MessageBoxA(hDlg, message.c_str(), "PDW File Transfer", MB_ICONWARNING | MB_OK);
			return;
		}
		g_dialogFiles.push_back(path);
	}

	void SelectFiles(HWND hDlg)
	{
		vector<char> selection(65536, 0);
		char filter[] = "All files (*.*)\0*.*\0\0";
		OPENFILENAMEA openFile;
		ZeroMemory(&openFile, sizeof(openFile));
		openFile.lStructSize = sizeof(openFile);
		openFile.hwndOwner = hDlg;
		openFile.lpstrFilter = filter;
		openFile.lpstrFile = &selection[0];
		openFile.nMaxFile = (DWORD) selection.size();
		openFile.lpstrInitialDir = szPath;
		openFile.lpstrTitle = "Select files for continuous upload";
		openFile.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST |
			OFN_HIDEREADONLY | OFN_NOCHANGEDIR;

		if (!GetOpenFileNameA(&openFile)) return;

		char *first = &selection[0];
		char *next = first + strlen(first) + 1;
		if (*next == '\0')
		{
			AddDialogPath(hDlg, first);
		}
		else
		{
			string directory = first;
			while (*next != '\0')
			{
				string fullPath = directory;
				if (!fullPath.empty() && fullPath[fullPath.size()-1] != '\\') fullPath += "\\";
				fullPath += next;
				AddDialogPath(hDlg, fullPath);
				next += strlen(next) + 1;
			}
		}

		PopulateFileList(hDlg);
	}

	void RemoveSelectedFiles(HWND hDlg)
	{
		HWND list = GetDlgItem(hDlg, IDC_FTP_FILES);
		for (int index = (int) g_dialogFiles.size()-1; index >= 0; index--)
		{
			if (SendMessageA(list, LB_GETSEL, index, 0) > 0)
				g_dialogFiles.erase(g_dialogFiles.begin()+index);
		}
		PopulateFileList(hDlg);
	}

	void UpdateDialogStatus(HWND hDlg)
	{
		char status[FTP_STATUS_LEN+1];
		FtpGetStatusText(status, sizeof(status));
		SetDlgItemTextA(hDlg, IDC_FTP_STATUS, status);
	}

	void PopulateProtocolList(HWND hDlg)
	{
		HWND protocolList = GetDlgItem(hDlg, IDC_FTP_PROTOCOL);
		SendMessageA(protocolList, CB_RESETCONTENT, 0, 0);
		for (int protocol = FTP_PROTOCOL_FTP; protocol <= FTP_PROTOCOL_SFTP; protocol++)
			SendMessageA(protocolList, CB_ADDSTRING, 0, (LPARAM) ProtocolName(protocol));

		g_dialogProtocol = Profile.ftpProtocol;
		if (g_dialogProtocol < FTP_PROTOCOL_FTP || g_dialogProtocol > FTP_PROTOCOL_SFTP)
			g_dialogProtocol = FTP_PROTOCOL_FTP;
		SendMessageA(protocolList, CB_SETCURSEL, g_dialogProtocol, 0);
	}

	void UpdateProtocolControls(HWND hDlg, bool updateDefaultPort)
	{
		int selected = (int) SendDlgItemMessageA(hDlg, IDC_FTP_PROTOCOL, CB_GETCURSEL, 0, 0);
		if (selected < FTP_PROTOCOL_FTP || selected > FTP_PROTOCOL_SFTP) selected = FTP_PROTOCOL_FTP;

		if (updateDefaultPort)
		{
			BOOL portValid = FALSE;
			unsigned int currentPort = GetDlgItemInt(hDlg, IDC_FTP_PORT, &portValid, FALSE);
			if (!portValid || currentPort == DefaultProtocolPort(g_dialogProtocol))
				SetDlgItemInt(hDlg, IDC_FTP_PORT, DefaultProtocolPort(selected), FALSE);
		}
		g_dialogProtocol = selected;

		bool sftp = selected == FTP_PROTOCOL_SFTP;
		EnableWindow(GetDlgItem(hDlg, IDC_FTP_PASSIVE), ProtocolIsFtpFamily(selected) ? TRUE : FALSE);
		EnableWindow(GetDlgItem(hDlg, IDC_FTP_SSH_HOST_KEY), sftp ? TRUE : FALSE);

		const char *notice = "FTP is unencrypted. Prefer FTPS or SFTP whenever the hosting provider supports it.";
		if (selected == FTP_PROTOCOL_FTPS_EXPLICIT || selected == FTP_PROTOCOL_FTPS_IMPLICIT)
			notice = "FTPS encrypts credentials and file data. PDW verifies the server's Windows-trusted TLS certificate.";
		else if (sftp)
			notice = "SFTP encrypts the connection. Paste the provider's SHA256 SSH host-key fingerprint to prevent impersonation.";
		SetDlgItemTextA(hDlg, IDC_FTP_SECURITY_NOTICE, notice);
	}

	bool ReadDialogValues(HWND hDlg, bool requireComplete, FtpDialogValues &values, string &error)
	{
		ZeroMemory(&values, sizeof(values));
		values.enabled = IsDlgButtonChecked(hDlg, IDC_FTP_ENABLED) == BST_CHECKED;
		values.protocol = (int) SendDlgItemMessageA(hDlg, IDC_FTP_PROTOCOL, CB_GETCURSEL, 0, 0);
		values.passive = IsDlgButtonChecked(hDlg, IDC_FTP_PASSIVE) == BST_CHECKED;
		GetDlgItemTextA(hDlg, IDC_FTP_SERVER, values.server, sizeof(values.server));
		GetDlgItemTextA(hDlg, IDC_FTP_USERNAME, values.username, sizeof(values.username));
		GetDlgItemTextA(hDlg, IDC_FTP_PASSWORD, values.password, sizeof(values.password));
		GetDlgItemTextA(hDlg, IDC_FTP_REMOTE_DIR, values.remoteDirectory, sizeof(values.remoteDirectory));
		GetDlgItemTextA(hDlg, IDC_FTP_SSH_HOST_KEY, values.sshHostKeySha256, sizeof(values.sshHostKeySha256));

		BOOL portValid = FALSE;
		BOOL intervalValid = FALSE;
		values.port = GetDlgItemInt(hDlg, IDC_FTP_PORT, &portValid, FALSE);
		values.intervalSeconds = GetDlgItemInt(hDlg, IDC_FTP_INTERVAL, &intervalValid, FALSE);

		if (values.protocol < FTP_PROTOCOL_FTP || values.protocol > FTP_PROTOCOL_SFTP)
			error = "Select FTP, FTPS or SFTP.";
		else if (!SelectedProtocolIsAvailable(values.protocol))
			error = string(ProtocolName(values.protocol)) + " support is missing from this PDW build.";
		else if (!portValid || values.port < 1 || values.port > 65535)
			error = "The server port must be between 1 and 65535.";
		else if (!intervalValid || values.intervalSeconds < FTP_MIN_INTERVAL || values.intervalSeconds > FTP_MAX_INTERVAL)
			error = "The upload interval must be between 10 and 86400 seconds.";

		bool needsCompleteSettings = requireComplete || values.enabled != 0;
		bool enteredNewPassword = values.password[0] != '\0' && strcmp(values.password, FTP_PASSWORD_PLACEHOLDER) != 0;

		if (error.empty() && values.server[0] != '\0' &&
			(strstr(values.server, "://") != NULL || strpbrk(values.server, ":/\\@?# \t\r\n") != NULL))
			error = "Enter only the hosting server hostname, without a protocol or path.";
		else if (error.empty() && enteredNewPassword && values.username[0] == '\0')
			error = "Enter the hosting username before saving its password.";
		else if (error.empty() && needsCompleteSettings && values.server[0] == '\0')
			error = "Enter the hosting server hostname.";
		else if (error.empty() && needsCompleteSettings && values.username[0] == '\0')
			error = "Enter the hosting username.";
		else if (error.empty() && needsCompleteSettings && !enteredNewPassword && !g_dialogCredentialExists)
			error = "Enter the hosting password. PDW will save it securely in Windows Credential Manager.";
		else if (error.empty() && needsCompleteSettings && g_dialogFiles.empty())
			error = "Select at least one file to upload.";
		else if (error.empty() && needsCompleteSettings && values.protocol == FTP_PROTOCOL_SFTP)
		{
			string normalized;
			NormalizeSshHostKey(values.sshHostKeySha256, normalized, error);
		}

		if (error.empty())
		{
			string duplicateName;
			if (HasDuplicateRemoteNames(g_dialogFiles, duplicateName))
				error = "Two selected files have the same filename (" + duplicateName + ") and would overwrite each other.";
		}

		return error.empty();
	}

	bool SaveDialogSettings(HWND hDlg, bool requireComplete)
	{
		FtpDialogValues values;
		string error;
		if (!ReadDialogValues(hDlg, requireComplete, values, error))
		{
			SecureZeroMemory(values.password, sizeof(values.password));
			MessageBoxA(hDlg, error.c_str(), "PDW File Transfer", MB_ICONERROR | MB_OK);
			return false;
		}

		bool enteredNewPassword = values.password[0] != '\0' && strcmp(values.password, FTP_PASSWORD_PLACEHOLDER) != 0;
		if (enteredNewPassword)
		{
			if (!SavePassword(values.username, values.password, error))
			{
				SecureZeroMemory(values.password, sizeof(values.password));
				MessageBoxA(hDlg, error.c_str(), "PDW File Transfer", MB_ICONERROR | MB_OK);
				return false;
			}
			g_dialogCredentialExists = true;
			SetDlgItemTextA(hDlg, IDC_FTP_PASSWORD, FTP_PASSWORD_PLACEHOLDER);
			EnableWindow(GetDlgItem(hDlg, IDC_FTP_CLEAR_PASSWORD), TRUE);
		}

		Profile.ftpEnabled = values.enabled;
		Profile.ftpProtocol = values.protocol;
		CopyText(Profile.ftpServer, sizeof(Profile.ftpServer), values.server);
		Profile.ftpPort = (int) values.port;
		CopyText(Profile.ftpUsername, sizeof(Profile.ftpUsername), values.username);
		CopyText(Profile.ftpRemoteDirectory, sizeof(Profile.ftpRemoteDirectory), values.remoteDirectory);
		string normalizedHostKey;
		if (values.sshHostKeySha256[0] != '\0')
		{
			string ignoredError;
			if (NormalizeSshHostKey(values.sshHostKeySha256, normalizedHostKey, ignoredError))
				CopyText(Profile.ftpSshHostKeySha256, sizeof(Profile.ftpSshHostKeySha256), normalizedHostKey.c_str());
			else
				CopyText(Profile.ftpSshHostKeySha256, sizeof(Profile.ftpSshHostKeySha256), values.sshHostKeySha256);
		}
		else Profile.ftpSshHostKeySha256[0] = '\0';
		Profile.ftpPassive = values.passive;
		Profile.ftpIntervalSeconds = values.intervalSeconds;
		Profile.ftpFiles = g_dialogFiles;

		SecureZeroMemory(values.password, sizeof(values.password));
		WriteSettings();
		FtpSettingsChanged();
		SetFtpStatus(Profile.ftpEnabled ? "File-transfer settings saved. The next automatic upload is scheduled." :
			"File-transfer settings saved. Automatic uploads are disabled.");
		UpdateDialogStatus(hDlg);
		return true;
	}
}

void FtpInitialize(void)
{
	if (g_ftpInitialized) return;
	InitializeCriticalSection(&g_ftpStateLock);
	g_ftpInitialized = true;
	InterlockedExchange(&g_uploadInProgress, 0);
	InterlockedExchange(&g_shuttingDown, 0);
	g_curlInitialized = CurlRuntimeAcquire();
	if (g_curlInitialized && CurlSupportsProtocol("ftp") && CurlSupportsProtocol("ftps") && CurlSupportsProtocol("sftp"))
		CopyText(g_ftpStatus, sizeof(g_ftpStatus), "FTP, FTPS and SFTP uploader is ready.");
	else if (g_curlInitialized)
		CopyText(g_ftpStatus, sizeof(g_ftpStatus), "This build is missing one or more required transfer protocols.");
	else
		CopyText(g_ftpStatus, sizeof(g_ftpStatus), "The file-transfer library could not be initialized.");
	FtpSettingsChanged();
}

void FtpShutdown(void)
{
	if (!g_ftpInitialized) return;
	InterlockedExchange(&g_shuttingDown, 1);

	HANDLE worker = NULL;
	EnterCriticalSection(&g_ftpStateLock);
	worker = g_workerThread;
	LeaveCriticalSection(&g_ftpStateLock);

	DWORD waitResult = worker == NULL ? WAIT_OBJECT_0 : WaitForSingleObject(worker, 20000);

	EnterCriticalSection(&g_ftpStateLock);
	if (g_workerThread != NULL)
	{
		CloseHandle(g_workerThread);
		g_workerThread = NULL;
	}
	LeaveCriticalSection(&g_ftpStateLock);

	if (waitResult == WAIT_OBJECT_0 && g_curlInitialized)
	{
		CurlRuntimeRelease();
		g_curlInitialized = false;
	}
}

void FtpSettingsChanged(void)
{
	if (!g_ftpInitialized) return;
	InterlockedIncrement(&g_settingsGeneration);
	unsigned int interval = Profile.ftpIntervalSeconds;
	if (interval < FTP_MIN_INTERVAL || interval > FTP_MAX_INTERVAL) interval = 60;
	DWORD nextTick = GetTickCount() + interval * 1000UL;
	InterlockedExchange(&g_nextUploadTick, (LONG) nextTick);
}

void FtpSchedulerTick(void)
{
	if (!g_ftpInitialized || !Profile.ftpEnabled) return;
	if (InterlockedCompareExchange(&g_shuttingDown, 0, 0) != 0) return;

	DWORD now = GetTickCount();
	DWORD nextTick = (DWORD) InterlockedCompareExchange(&g_nextUploadTick, 0, 0);
	if ((LONG) (now-nextTick) < 0) return;

	if (InterlockedCompareExchange(&g_uploadInProgress, 0, 0) != 0)
	{
		InterlockedExchange(&g_nextUploadTick, (LONG) (now+1000));
		return;
	}

	unsigned int interval = Profile.ftpIntervalSeconds;
	if (interval < FTP_MIN_INTERVAL || interval > FTP_MAX_INTERVAL) interval = 60;
	InterlockedExchange(&g_nextUploadTick, (LONG) (now+interval*1000UL));
	FtpQueueUploadNow();
}

bool FtpQueueUploadNow(void)
{
	if (!g_ftpInitialized || InterlockedCompareExchange(&g_shuttingDown, 0, 0) != 0) return false;
	if (!g_curlInitialized)
	{
		SetFtpStatus("The file-transfer library is unavailable in this PDW build.");
		return false;
	}

	string error;
	if (!ValidateProfile(error))
	{
		SetFtpStatus(error.c_str());
		return false;
	}

	string password;
	if (!ReadSavedPassword(password))
	{
		SetFtpStatus("No hosting password is saved. Open Options > File Transfer to enter it.");
		return false;
	}

	if (InterlockedCompareExchange(&g_uploadInProgress, 1, 0) != 0)
	{
		WipeString(password);
		SetFtpStatus("A file upload is already in progress.");
		return false;
	}

	FtpUploadTask *task = new (nothrow) FtpUploadTask;
	if (task == NULL)
	{
		WipeString(password);
		InterlockedExchange(&g_uploadInProgress, 0);
		SetFtpStatus("PDW could not allocate memory for the file upload.");
		return false;
	}

	task->protocol = Profile.ftpProtocol;
	task->server = Profile.ftpServer;
	task->port = (unsigned int) Profile.ftpPort;
	task->username = Profile.ftpUsername;
	task->password.swap(password);
	task->remoteDirectory = Profile.ftpRemoteDirectory;
	if (task->protocol == FTP_PROTOCOL_SFTP)
	{
		string hostKeyError;
		NormalizeSshHostKey(Profile.ftpSshHostKeySha256, task->sshHostKeySha256, hostKeyError);
	}
	task->passive = Profile.ftpPassive != 0;
	task->intervalSeconds = Profile.ftpIntervalSeconds;
	task->settingsGeneration = InterlockedCompareExchange(&g_settingsGeneration, 0, 0);
	task->files = Profile.ftpFiles;

	SetFtpStatus("File upload queued. PDW will continue decoding in the background.");
	uintptr_t worker = _beginthreadex(NULL, 0, FtpWorker, task, 0, NULL);
	if (worker == 0)
	{
		WipeString(task->password);
		delete task;
		InterlockedExchange(&g_uploadInProgress, 0);
		SetFtpStatus("PDW could not start the file-upload worker thread.");
		return false;
	}

	EnterCriticalSection(&g_ftpStateLock);
	if (g_workerThread != NULL) CloseHandle(g_workerThread);
	g_workerThread = (HANDLE) worker;
	LeaveCriticalSection(&g_ftpStateLock);
	return true;
}

void FtpGetStatusText(char *buffer, size_t bufferSize)
{
	if (buffer == NULL || bufferSize == 0) return;
	if (!g_ftpInitialized)
	{
		CopyText(buffer, bufferSize, "File-transfer uploader has not been initialized.");
		return;
	}

	EnterCriticalSection(&g_ftpStateLock);
	CopyText(buffer, bufferSize, g_ftpStatus);
	LeaveCriticalSection(&g_ftpStateLock);
}

BOOL FAR PASCAL FtpDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	(void) lParam;

	switch (uMsg)
	{
		case WM_INITDIALOG:
			if (!CenterWindow(hDlg)) return FALSE;
			g_dialogFiles = Profile.ftpFiles;
			g_dialogCredentialExists = HasSavedPassword();
			PopulateProtocolList(hDlg);

			CheckDlgButton(hDlg, IDC_FTP_ENABLED, Profile.ftpEnabled ? BST_CHECKED : BST_UNCHECKED);
			CheckDlgButton(hDlg, IDC_FTP_PASSIVE, Profile.ftpPassive ? BST_CHECKED : BST_UNCHECKED);
			SetDlgItemTextA(hDlg, IDC_FTP_SERVER, Profile.ftpServer);
			SetDlgItemInt(hDlg, IDC_FTP_PORT, Profile.ftpPort, FALSE);
			SetDlgItemTextA(hDlg, IDC_FTP_USERNAME, Profile.ftpUsername);
			SetDlgItemTextA(hDlg, IDC_FTP_PASSWORD, g_dialogCredentialExists ? FTP_PASSWORD_PLACEHOLDER : "");
			SetDlgItemTextA(hDlg, IDC_FTP_REMOTE_DIR, Profile.ftpRemoteDirectory);
			SetDlgItemTextA(hDlg, IDC_FTP_SSH_HOST_KEY, Profile.ftpSshHostKeySha256);
			SetDlgItemInt(hDlg, IDC_FTP_INTERVAL, Profile.ftpIntervalSeconds, FALSE);

			SendDlgItemMessageA(hDlg, IDC_FTP_SERVER, EM_SETLIMITTEXT, FTP_SERVER_LEN, 0);
			SendDlgItemMessageA(hDlg, IDC_FTP_USERNAME, EM_SETLIMITTEXT, FTP_USERNAME_LEN, 0);
			SendDlgItemMessageA(hDlg, IDC_FTP_PASSWORD, EM_SETLIMITTEXT, FTP_PASSWORD_LEN, 0);
			SendDlgItemMessageA(hDlg, IDC_FTP_REMOTE_DIR, EM_SETLIMITTEXT, FTP_REMOTE_DIR_LEN, 0);
			SendDlgItemMessageA(hDlg, IDC_FTP_SSH_HOST_KEY, EM_SETLIMITTEXT, FTP_SSH_HOST_KEY_LEN, 0);
			EnableWindow(GetDlgItem(hDlg, IDC_FTP_CLEAR_PASSWORD), g_dialogCredentialExists ? TRUE : FALSE);
			UpdateProtocolControls(hDlg, false);
			PopulateFileList(hDlg);
			UpdateDialogStatus(hDlg);
			SetTimer(hDlg, FTP_DIALOG_TIMER, 1000, NULL);
			return TRUE;

		case WM_TIMER:
			if (wParam == FTP_DIALOG_TIMER) UpdateDialogStatus(hDlg);
			return TRUE;

		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
				case IDC_FTP_PROTOCOL:
					if (HIWORD(wParam) == CBN_SELCHANGE) UpdateProtocolControls(hDlg, true);
					return TRUE;

				case IDC_FTP_ADD_FILES:
					SelectFiles(hDlg);
					return TRUE;

				case IDC_FTP_REMOVE_FILES:
					RemoveSelectedFiles(hDlg);
					return TRUE;

				case IDC_FTP_CLEAR_PASSWORD:
					if (MessageBoxA(hDlg, "Remove the saved hosting password from Windows Credential Manager?",
						"PDW File Transfer", MB_ICONQUESTION | MB_YESNO) == IDYES)
					{
						string error;
						if (!DeleteSavedPassword(error))
							MessageBoxA(hDlg, error.c_str(), "PDW File Transfer", MB_ICONERROR | MB_OK);
						else
						{
							g_dialogCredentialExists = false;
							SetDlgItemTextA(hDlg, IDC_FTP_PASSWORD, "");
							EnableWindow(GetDlgItem(hDlg, IDC_FTP_CLEAR_PASSWORD), FALSE);
							CheckDlgButton(hDlg, IDC_FTP_ENABLED, BST_UNCHECKED);
							Profile.ftpEnabled = 0;
							WriteSettings();
							FtpSettingsChanged();
							SetFtpStatus("The saved hosting password was removed. Automatic uploads are disabled.");
							UpdateDialogStatus(hDlg);
						}
					}
					return TRUE;

				case IDC_FTP_UPLOAD_NOW:
					if (SaveDialogSettings(hDlg, true))
					{
						FtpQueueUploadNow();
						UpdateDialogStatus(hDlg);
					}
					return TRUE;

				case IDOK:
					if (SaveDialogSettings(hDlg, false))
					{
						KillTimer(hDlg, FTP_DIALOG_TIMER);
						EndDialog(hDlg, TRUE);
					}
					return TRUE;

				case IDCANCEL:
					KillTimer(hDlg, FTP_DIALOG_TIMER);
					EndDialog(hDlg, FALSE);
					return TRUE;
			}
			break;

		case WM_CLOSE:
			KillTimer(hDlg, FTP_DIALOG_TIMER);
			EndDialog(hDlg, FALSE);
			return TRUE;
	}

	return FALSE;
}
