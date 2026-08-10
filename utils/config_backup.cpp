#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>
#include <commdlg.h>
#include <wincred.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "config_backup_core.h"
#include "headers\config_backup.h"
#include "headers\data_outputs.h"
#include "headers\ftp.h"
#include "headers\notification.h"
#include "headers\pdw.h"
#include "headers\initapp.h"
#include "headers\publishing.h"
#include "headers\resource.h"
#include "headers\ui_theme.h"

namespace
{
	const std::size_t kMaximumConfigurationFileSize = 16u * 1024u * 1024u;
	const std::size_t kMaximumBackupFileSize = 40u * 1024u * 1024u + 128u;
	const char kBackupFilter[] =
		"PDW configuration backup (*.pdwbackup)\0*.pdwbackup\0All files (*.*)\0*.*\0\0";
	bool g_restoreCompleted = false;

	struct PasswordDialogContext
	{
		bool confirm;
		std::string password;
	};

	void WipeString(std::string& value)
	{
		if (!value.empty()) SecureZeroMemory(&value[0], value.size());
		value.clear();
	}

	bool EndsWithInsensitive(const std::string& value, const std::string& suffix)
	{
		if (value.size() < suffix.size()) return false;
		const std::size_t offset = value.size() - suffix.size();
		for (std::size_t index = 0; index < suffix.size(); ++index)
			if (tolower(static_cast<unsigned char>(value[offset + index])) !=
				tolower(static_cast<unsigned char>(suffix[index]))) return false;
		return true;
	}

	bool ReadFileBytes(const char* path, std::vector<unsigned char>& bytes,
		std::size_t maximum, std::string& error)
	{
		bytes.clear();
		HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
		if (file == INVALID_HANDLE_VALUE)
		{
			error = std::string("PDW could not open ") + path + ".";
			return false;
		}
		LARGE_INTEGER size = {};
		if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
			static_cast<unsigned long long>(size.QuadPart) > maximum)
		{
			CloseHandle(file);
			error = std::string("The file is too large or cannot be measured: ") + path;
			return false;
		}
		bytes.resize(static_cast<std::size_t>(size.QuadPart));
		std::size_t position = 0;
		while (position < bytes.size())
		{
			const DWORD wanted = static_cast<DWORD>((std::min)(bytes.size() - position,
				static_cast<std::size_t>(1024u * 1024u)));
			DWORD read = 0;
			if (!ReadFile(file, &bytes[position], wanted, &read, NULL) || read != wanted)
			{
				CloseHandle(file);
				bytes.clear();
				error = std::string("PDW could not read ") + path + ".";
				return false;
			}
			position += read;
		}
		CloseHandle(file);
		return true;
	}

	bool ReadTextFile(const char* path, std::string& text, std::string& error)
	{
		std::vector<unsigned char> bytes;
		if (!ReadFileBytes(path, bytes, kMaximumConfigurationFileSize, error)) return false;
		text.assign(bytes.begin(), bytes.end());
		if (!bytes.empty()) SecureZeroMemory(&bytes[0], bytes.size());
		return true;
	}

	bool ReadOptionalTextFile(const char* path, std::string& text, bool& existed,
		std::string& error)
	{
		existed = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
		if (!existed)
		{
			text.clear();
			return true;
		}
		return ReadTextFile(path, text, error);
	}

	bool AtomicWriteFile(const char* path, const unsigned char* data, std::size_t size,
		std::string& error)
	{
		char temporary[MAX_PATH * 2] = {};
		_snprintf_s(temporary, sizeof(temporary), _TRUNCATE, "%s.pdwtmp.%lu.%lu", path,
			static_cast<unsigned long>(GetCurrentProcessId()),
			static_cast<unsigned long>(GetTickCount()));
		HANDLE file = CreateFileA(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
		if (file == INVALID_HANDLE_VALUE)
		{
			error = std::string("PDW could not create a temporary file beside ") + path + ".";
			return false;
		}
		bool success = true;
		std::size_t position = 0;
		while (position < size)
		{
			const DWORD wanted = static_cast<DWORD>((std::min)(size - position,
				static_cast<std::size_t>(1024u * 1024u)));
			DWORD written = 0;
			if (!WriteFile(file, data + position, wanted, &written, NULL) || written != wanted)
			{
				success = false;
				break;
			}
			position += written;
		}
		if (success) success = FlushFileBuffers(file) != FALSE;
		if (!CloseHandle(file)) success = false;
		if (success)
			success = MoveFileExA(temporary, path,
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
		if (!success)
		{
			DeleteFileA(temporary);
			error = std::string("PDW could not safely write ") + path + ".";
		}
		return success;
	}

	bool AtomicWriteFile(const char* path, const std::vector<unsigned char>& bytes,
		std::string& error)
	{
		return AtomicWriteFile(path, bytes.empty() ? NULL : &bytes[0], bytes.size(), error);
	}

	bool AtomicWriteText(const char* path, const std::string& text, std::string& error)
	{
		return AtomicWriteFile(path,
			reinterpret_cast<const unsigned char*>(text.data()), text.size(), error);
	}

	bool EnumerateProfileCredentials(std::vector<pdw::backup::BackupCredential>& credentials,
		std::string& error)
	{
		credentials.clear();
		DWORD count = 0;
		PCREDENTIALA* found = NULL;
		if (!CredEnumerateA("PDW*", 0, &count, &found))
		{
			if (GetLastError() == ERROR_NOT_FOUND) return true;
			error = "Windows Credential Manager could not enumerate PDW credentials.";
			return false;
		}
		const std::string suffix = std::string(": ") + szIniPathName;
		bool success = true;
		for (DWORD index = 0; index < count; ++index)
		{
			const PCREDENTIALA item = found[index];
			if (!item || item->Type != CRED_TYPE_GENERIC || !item->TargetName) continue;
			const std::string target = item->TargetName;
			if (!EndsWithInsensitive(target, suffix)) continue;
			pdw::backup::BackupCredential credential;
			credential.name = target.substr(0, target.size() - suffix.size());
			if (!pdw::backup::IsAllowedCredentialName(credential.name))
			{
				success = false;
				error = "PDW found an unsupported Credential Manager target and stopped the backup.";
				break;
			}
			if (item->UserName) credential.username = item->UserName;
			if (item->CredentialBlob && item->CredentialBlobSize)
				credential.secret.assign(item->CredentialBlob,
					item->CredentialBlob + item->CredentialBlobSize);
			credentials.push_back(credential);
		}
		CredFree(found);
		if (!success)
		{
			pdw::backup::BackupContents wipe;
			wipe.credentials.swap(credentials);
			pdw::backup::WipeBackupContents(wipe);
		}
		return success;
	}

	bool ContainsCredential(const std::vector<pdw::backup::BackupCredential>& credentials,
		const std::string& name)
	{
		for (std::size_t index = 0; index < credentials.size(); ++index)
			if (credentials[index].name == name) return true;
		return false;
	}

	bool ApplyCredentialSet(const std::vector<pdw::backup::BackupCredential>& desired,
		std::string& error)
	{
		std::vector<pdw::backup::BackupCredential> current;
		if (!EnumerateProfileCredentials(current, error)) return false;
		const std::string suffix = std::string(": ") + szIniPathName;
		bool success = true;
		for (std::size_t index = 0; index < current.size(); ++index)
			if (!ContainsCredential(desired, current[index].name))
			{
				const std::string target = current[index].name + suffix;
				if (!CredDeleteA(target.c_str(), CRED_TYPE_GENERIC, 0) &&
					GetLastError() != ERROR_NOT_FOUND)
				{
					error = "Windows Credential Manager could not clear an existing PDW credential.";
					success = false;
					break;
				}
			}
		for (std::size_t index = 0; success && index < desired.size(); ++index)
		{
			const pdw::backup::BackupCredential& source = desired[index];
			if (!pdw::backup::IsAllowedCredentialName(source.name) ||
				source.secret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE)
			{
				error = "The backup contains a credential that Windows cannot restore.";
				success = false;
				break;
			}
			const std::string target = source.name + suffix;
			CREDENTIALA credential = {};
			credential.Type = CRED_TYPE_GENERIC;
			credential.TargetName = const_cast<char*>(target.c_str());
			credential.CredentialBlobSize = static_cast<DWORD>(source.secret.size());
			credential.CredentialBlob = source.secret.empty() ? NULL :
				const_cast<LPBYTE>(&source.secret[0]);
			credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
			credential.UserName = const_cast<char*>(source.username.empty() ?
				"PDW" : source.username.c_str());
			if (!CredWriteA(&credential, 0))
			{
				error = "Windows Credential Manager could not restore a PDW credential.";
				success = false;
			}
		}
		pdw::backup::BackupContents wipe;
		wipe.credentials.swap(current);
		pdw::backup::WipeBackupContents(wipe);
		return success;
	}

	bool CaptureCurrentConfiguration(pdw::backup::BackupContents& contents,
		std::string& error)
	{
		WriteSettings();
		WriteFilters(&Profile, 0);
		return ReadTextFile(szIniPathName, contents.settings, error) &&
			ReadTextFile(szFilterPathName, contents.filters, error) &&
			EnumerateProfileCredentials(contents.credentials, error);
	}

	bool RestoreConfiguration(const pdw::backup::BackupContents& restored,
		std::string& error)
	{
		std::string previousSettings;
		std::string previousFilters;
		std::vector<pdw::backup::BackupCredential> previousCredentials;
		bool settingsExisted = false;
		bool filtersExisted = false;
		if (!ReadOptionalTextFile(szIniPathName, previousSettings, settingsExisted, error) ||
			!ReadOptionalTextFile(szFilterPathName, previousFilters, filtersExisted, error) ||
			!EnumerateProfileCredentials(previousCredentials, error)) return false;

		bool settingsChanged = false;
		bool filtersChanged = false;
		bool credentialsChanged = false;
		if (AtomicWriteText(szIniPathName, restored.settings, error)) settingsChanged = true;
		if (settingsChanged && AtomicWriteText(szFilterPathName, restored.filters, error))
			filtersChanged = true;
		if (settingsChanged && filtersChanged &&
			ApplyCredentialSet(restored.credentials, error)) credentialsChanged = true;

		if (!settingsChanged || !filtersChanged || !credentialsChanged)
		{
			bool rollbackFailed = false;
			std::string rollbackError;
			if (settingsChanged && (settingsExisted ?
				!AtomicWriteText(szIniPathName, previousSettings, rollbackError) :
				(!DeleteFileA(szIniPathName) && GetLastError() != ERROR_FILE_NOT_FOUND)))
				rollbackFailed = true;
			rollbackError.clear();
			if (filtersChanged && (filtersExisted ?
				!AtomicWriteText(szFilterPathName, previousFilters, rollbackError) :
				(!DeleteFileA(szFilterPathName) && GetLastError() != ERROR_FILE_NOT_FOUND)))
				rollbackFailed = true;
			rollbackError.clear();
			if (!ApplyCredentialSet(previousCredentials, rollbackError)) rollbackFailed = true;
			if (rollbackFailed)
				error += " The original configuration rollback also reported an error.";
		}
		pdw::backup::BackupContents wipe;
		wipe.settings.swap(previousSettings);
		wipe.filters.swap(previousFilters);
		wipe.credentials.swap(previousCredentials);
		pdw::backup::WipeBackupContents(wipe);
		return settingsChanged && filtersChanged && credentialsChanged;
	}

	void BuildDefaultBackupName(char* name, std::size_t capacity)
	{
		SYSTEMTIME now = {};
		GetLocalTime(&now);
		_snprintf_s(name, capacity, _TRUNCATE, "PDW-settings-%04u%02u%02u-%02u%02u%02u.pdwbackup",
			static_cast<unsigned int>(now.wYear), static_cast<unsigned int>(now.wMonth),
			static_cast<unsigned int>(now.wDay), static_cast<unsigned int>(now.wHour),
			static_cast<unsigned int>(now.wMinute), static_cast<unsigned int>(now.wSecond));
	}

	bool ChooseBackupFile(HWND owner, bool save, char* path, DWORD capacity)
	{
		path[0] = '\0';
		if (save) BuildDefaultBackupName(path, capacity);
		OPENFILENAMEA file = {};
		file.lStructSize = sizeof(file);
		file.hwndOwner = owner;
		file.hInstance = ghInstance;
		file.lpstrFilter = kBackupFilter;
		file.nFilterIndex = 1;
		file.lpstrFile = path;
		file.nMaxFile = capacity;
		file.lpstrInitialDir = szPath;
		file.lpstrDefExt = "pdwbackup";
		file.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
			(save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
		return (save ? GetSaveFileNameA(&file) : GetOpenFileNameA(&file)) != FALSE;
	}

	INT_PTR CALLBACK PasswordDlgProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
	{
		PasswordDialogContext* context = reinterpret_cast<PasswordDialogContext*>(
			GetWindowLongPtr(dialog, DWLP_USER));
		switch (message)
		{
			case WM_INITDIALOG:
				context = reinterpret_cast<PasswordDialogContext*>(lParam);
				SetWindowLongPtr(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(context));
				SetWindowTextA(dialog, context && context->confirm ?
					"Protect Configuration Backup" : "Open Configuration Backup");
				SetDlgItemTextA(dialog, IDC_BACKUP_PASSWORD_INFO, context && context->confirm ?
					"Choose a password of at least 8 characters. PDW cannot recover a forgotten backup password." :
					"Enter the password used when this PDW backup was created.");
				ShowWindow(GetDlgItem(dialog, IDC_BACKUP_CONFIRM_LABEL),
					context && context->confirm ? SW_SHOW : SW_HIDE);
				ShowWindow(GetDlgItem(dialog, IDC_BACKUP_CONFIRM),
					context && context->confirm ? SW_SHOW : SW_HIDE);
				SendDlgItemMessage(dialog, IDC_BACKUP_PASSWORD, EM_LIMITTEXT, 1024, 0);
				SendDlgItemMessage(dialog, IDC_BACKUP_CONFIRM, EM_LIMITTEXT, 1024, 0);
				CenterWindow(dialog);
				return TRUE;

			case WM_COMMAND:
				if (LOWORD(wParam) == IDOK && context)
				{
					char password[1025] = {};
					char confirmation[1025] = {};
					GetDlgItemTextA(dialog, IDC_BACKUP_PASSWORD, password, sizeof(password));
					if (strlen(password) < 8)
					{
						MessageBoxA(dialog, "The backup password must contain at least 8 characters.",
							"PDW Backup", MB_OK | MB_ICONWARNING);
						SecureZeroMemory(password, sizeof(password));
						return TRUE;
					}
					if (context->confirm)
					{
						GetDlgItemTextA(dialog, IDC_BACKUP_CONFIRM, confirmation, sizeof(confirmation));
						if (strcmp(password, confirmation) != 0)
						{
							MessageBoxA(dialog, "The two backup passwords do not match.",
								"PDW Backup", MB_OK | MB_ICONWARNING);
							SecureZeroMemory(password, sizeof(password));
							SecureZeroMemory(confirmation, sizeof(confirmation));
							return TRUE;
						}
					}
					context->password = password;
					SecureZeroMemory(password, sizeof(password));
					SecureZeroMemory(confirmation, sizeof(confirmation));
					SetDlgItemTextA(dialog, IDC_BACKUP_PASSWORD, "");
					SetDlgItemTextA(dialog, IDC_BACKUP_CONFIRM, "");
					EndDialog(dialog, IDOK);
					return TRUE;
				}
				if (LOWORD(wParam) == IDCANCEL)
				{
					EndDialog(dialog, IDCANCEL);
					return TRUE;
				}
				break;
		}
		return FALSE;
	}

	bool AskForPassword(HWND owner, bool confirm, std::string& password)
	{
		PasswordDialogContext context = {};
		context.confirm = confirm;
		PdwThemeBeginDialogHook();
		const INT_PTR result = DialogBoxParamA(ghInstance,
			MAKEINTRESOURCEA(CONFIG_BACKUP_PASSWORD_DLGBOX), owner, PasswordDlgProc,
			reinterpret_cast<LPARAM>(&context));
		PdwThemeEndDialogHook();
		if (result != IDOK)
		{
			WipeString(context.password);
			return false;
		}
		password.swap(context.password);
		return true;
	}

	void SetBackupStatus(HWND dialog, const char* status)
	{
		SetDlgItemTextA(dialog, IDC_BACKUP_STATUS, status ? status : "");
		UpdateWindow(GetDlgItem(dialog, IDC_BACKUP_STATUS));
	}

	void ExportConfiguration(HWND dialog)
	{
		char path[MAX_PATH * 2] = {};
		if (!ChooseBackupFile(dialog, true, path, static_cast<DWORD>(_countof(path)))) return;
		std::string password;
		if (!AskForPassword(dialog, true, password)) return;
		SetBackupStatus(dialog, "Saving the encrypted configuration backup...");

		pdw::backup::BackupContents contents;
		std::vector<unsigned char> encrypted;
		std::string error;
		const bool success = CaptureCurrentConfiguration(contents, error) &&
			pdw::backup::CreateEncryptedBackup(contents, password, encrypted, error) &&
			AtomicWriteFile(path, encrypted, error);
		const std::size_t credentialCount = contents.credentials.size();
		WipeString(password);
		pdw::backup::WipeBackupContents(contents);
		if (!encrypted.empty()) SecureZeroMemory(&encrypted[0], encrypted.size());
		if (!success)
		{
			SetBackupStatus(dialog, "Backup failed; no readable credential file was created.");
			MessageBoxA(dialog, error.c_str(), "PDW Backup", MB_OK | MB_ICONERROR);
			return;
		}
		char message[512] = {};
		_snprintf_s(message, sizeof(message), _TRUNCATE,
			"PDW saved the encrypted configuration backup.\n\nIt includes PDW.INI, filters.ini and %u Credential Manager record(s).",
			static_cast<unsigned int>(credentialCount));
		SetBackupStatus(dialog, "Encrypted backup saved successfully.");
		MessageBoxA(dialog, message, "PDW Backup", MB_OK | MB_ICONINFORMATION);
	}

	void RestoreFromBackup(HWND dialog)
	{
		char path[MAX_PATH * 2] = {};
		if (!ChooseBackupFile(dialog, false, path, static_cast<DWORD>(_countof(path)))) return;
		std::string password;
		if (!AskForPassword(dialog, false, password)) return;
		SetBackupStatus(dialog, "Validating and decrypting the backup...");
		std::vector<unsigned char> encrypted;
		pdw::backup::BackupContents contents;
		std::string error;
		if (!ReadFileBytes(path, encrypted, kMaximumBackupFileSize, error) ||
			!pdw::backup::OpenEncryptedBackup(encrypted, password, contents, error))
		{
			WipeString(password);
			if (!encrypted.empty()) SecureZeroMemory(&encrypted[0], encrypted.size());
			pdw::backup::WipeBackupContents(contents);
			SetBackupStatus(dialog, "Restore stopped; the current configuration was not changed.");
			MessageBoxA(dialog, error.c_str(), "PDW Restore", MB_OK | MB_ICONERROR);
			return;
		}
		WipeString(password);
		if (!encrypted.empty()) SecureZeroMemory(&encrypted[0], encrypted.size());

		char confirmation[640] = {};
		_snprintf_s(confirmation, sizeof(confirmation), _TRUNCATE,
			"Restore every PDW setting, filter and saved credential from this backup?\n\n"
			"This replaces the current configuration and %u saved credential record(s). "
			"PDW will close after a successful restore so the restored configuration loads cleanly.",
			static_cast<unsigned int>(contents.credentials.size()));
		if (MessageBoxA(dialog, confirmation, "Restore PDW Configuration",
			MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
		{
			pdw::backup::WipeBackupContents(contents);
			SetBackupStatus(dialog, "Restore cancelled; the current configuration was not changed.");
			return;
		}

		SetBackupStatus(dialog, "Stopping data delivery and restoring the configuration...");
		DataOutputManagerShutdown();
		PublishingManagerShutdown();
		NotificationManagerShutdown();
		FtpShutdown();
		if (!RestoreConfiguration(contents, error))
		{
			DataOutputManagerInitialize();
			PublishingManagerInitialize();
			NotificationManagerInitialize();
			FtpInitialize();
			pdw::backup::WipeBackupContents(contents);
			SetBackupStatus(dialog, "Restore failed; PDW kept or recovered the previous configuration.");
			MessageBoxA(dialog, error.c_str(), "PDW Restore", MB_OK | MB_ICONERROR);
			return;
		}
		pdw::backup::WipeBackupContents(contents);
		g_restoreCompleted = true;
		MessageBoxA(dialog,
			"The complete PDW configuration was restored successfully.\n\n"
			"PDW will now close. Reopen it to use the restored settings and credentials.",
			"PDW Restore", MB_OK | MB_ICONINFORMATION);
		EndDialog(dialog, IDOK);
		PostMessage(ghWnd, WM_CLOSE, 0, 0);
	}

	INT_PTR CALLBACK BackupDlgProc(HWND dialog, UINT message, WPARAM wParam, LPARAM)
	{
		switch (message)
		{
			case WM_INITDIALOG:
				CenterWindow(dialog);
				SetBackupStatus(dialog, "Ready. No settings are changed until you confirm an action.");
				return TRUE;

			case WM_COMMAND:
				switch (LOWORD(wParam))
				{
					case IDC_BACKUP_EXPORT:
						ExportConfiguration(dialog);
						return TRUE;
					case IDC_BACKUP_RESTORE:
						RestoreFromBackup(dialog);
						return TRUE;
					case IDOK:
					case IDCANCEL:
						EndDialog(dialog, LOWORD(wParam));
						return TRUE;
				}
				break;
		}
		return FALSE;
	}
}

void ShowConfigurationBackupDialog(HWND owner)
{
	PdwThemeBeginDialogHook();
	DialogBoxParamA(ghInstance, MAKEINTRESOURCEA(CONFIG_BACKUP_DLGBOX), owner,
		BackupDlgProc, 0);
	PdwThemeEndDialogHook();
}

bool ConfigurationRestoreCompleted(void)
{
	return g_restoreCompleted;
}
