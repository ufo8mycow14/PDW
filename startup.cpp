#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <cstdint>
#include <vector>

#include "headers\startup.h"

namespace
{
	const char* kRunKey = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
	const char* kValueName = "PDW";
	const unsigned kMaxRecoveryAttempts = 3;
	HANDLE receiverClosing = NULL;

	bool GetOwnImage(wchar_t (&image)[MAX_PATH])
	{
		const DWORD length = GetModuleFileNameW(NULL, image, MAX_PATH);
		return length != 0 && length < MAX_PATH;
	}

	bool LaunchRecoveryProcess(const wchar_t* options, HANDLE* handles, size_t count)
	{
		wchar_t image[MAX_PATH], command[MAX_PATH + 160];
		if (!GetOwnImage(image) || swprintf_s(command, L"\"%ls\" %ls", image, options) < 0) return false;
		STARTUPINFOEXW startup = {};
		startup.StartupInfo.cb = count ? sizeof(startup) : sizeof(STARTUPINFOW);
		SIZE_T bytes = 0;
		std::vector<unsigned char> storage;
		if (count)
		{
			InitializeProcThreadAttributeList(NULL, 1, 0, &bytes);
			if (!bytes) return false;
			storage.resize(bytes);
			startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
			if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes)) return false;
			if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
				handles, sizeof(HANDLE) * count, NULL, NULL))
			{
				DeleteProcThreadAttributeList(startup.lpAttributeList);
				return false;
			}
		}
		PROCESS_INFORMATION child = {};
		const BOOL started = CreateProcessW(image, command, NULL, NULL, count != 0,
			count ? EXTENDED_STARTUPINFO_PRESENT : 0, NULL, NULL, &startup.StartupInfo, &child);
		if (startup.lpAttributeList) DeleteProcThreadAttributeList(startup.lpAttributeList);
		if (!started) return false;
		CloseHandle(child.hThread);
		CloseHandle(child.hProcess);
		return true;
	}

	bool IsCrashExit(DWORD code)
	{
		return ((code & 0xf0000000UL) == 0xc0000000UL && code != CONTROL_C_EXIT) ||
			code == 0xe06d7363UL || code == 0x40000015UL || code == 3;
	}

	BOOL BuildStartupCommand(char* command, size_t commandSize)
	{
		char executable[MAX_PATH];
		DWORD length = GetModuleFileNameA(NULL, executable, sizeof(executable));
		if (!length || length >= sizeof(executable)) return FALSE;
		return snprintf(command, commandSize, "\"%s\" /startup", executable) > 0;
	}

	void SetRegistryError(char* errorText, size_t errorTextSize, LONG error)
	{
		if (!errorText || !errorTextSize) return;
		char systemMessage[256] = { 0 };
		FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, error, 0, systemMessage, sizeof(systemMessage), NULL);
		snprintf(errorText, errorTextSize,
			"Windows could not update the startup setting. %s (error %ld)",
			systemMessage[0] ? systemMessage : "Registry operation failed.", error);
	}
}

unsigned ReceiverRecoveryAttempt(const char* command)
{
	unsigned attempt = 0;
	char extra = 0;
	return command && sscanf_s(command, "/receiver-recovered %u%c", &attempt, &extra, 1u) == 1 &&
		attempt <= kMaxRecoveryAttempts ? attempt : 0;
}

bool RunReceiverWatchMode(const char* command, int& exitCode)
{
	if (!command || strncmp(command, "/receiver-watch", 15) != 0) return false;
	exitCode = ERROR_INVALID_PARAMETER;
	unsigned long long processValue = 0, closingValue = 0;
	unsigned attempt = 0;
	char extra = 0;
	if (sscanf_s(command, "/receiver-watch %llu %llu %u%c", &processValue, &closingValue,
		&attempt, &extra, 1u) != 3 || !processValue || !closingValue || processValue > UINTPTR_MAX ||
		closingValue > UINTPTR_MAX || attempt > kMaxRecoveryAttempts) return true;
	const HANDLE parent = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(processValue));
	const HANDLE closing = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(closingValue));
	wchar_t image[MAX_PATH], parentImage[MAX_PATH];
	DWORD length = MAX_PATH;
	if (!GetOwnImage(image) || !QueryFullProcessImageNameW(parent, 0, parentImage, &length) ||
		_wcsicmp(image, parentImage) != 0) return true;
	const HANDLE signals[] = { closing, parent };
	const DWORD wait = WaitForMultipleObjects(2, signals, FALSE, INFINITE);
	DWORD code = 0;
	const bool crashed = wait == WAIT_OBJECT_0 + 1 && WaitForSingleObject(closing, 0) == WAIT_TIMEOUT &&
		GetExitCodeProcess(parent, &code) && IsCrashExit(code);
	FILETIME created, ended, kernel, user;
	if (crashed && GetProcessTimes(parent, &created, &ended, &kernel, &user))
	{
		const ULONGLONG start = (static_cast<ULONGLONG>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
		const ULONGLONG end = (static_cast<ULONGLONG>(ended.dwHighDateTime) << 32) | ended.dwLowDateTime;
		if (end >= start && end - start >= 600000000ULL) attempt = 0;
	}
	CloseHandle(parent);
	CloseHandle(closing);
	exitCode = 0;
	if (crashed && attempt < kMaxRecoveryAttempts)
	{
		Sleep(1000u << attempt);
		wchar_t options[48];
		swprintf_s(options, L"/receiver-recovered %u", attempt + 1);
		if (!LaunchRecoveryProcess(options, NULL, 0)) exitCode = ERROR_PROCESS_ABORTED;
	}
	return true;
}

void ArmReceiverRecovery(unsigned attempt)
{
	if (receiverClosing) return;
	HANDLE closing = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (!closing) return;
	HANDLE inherited[2] = {};
	HANDLE current = GetCurrentProcess();
	if (DuplicateHandle(current, current, current, &inherited[0], SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, 0) &&
		DuplicateHandle(current, closing, current, &inherited[1], SYNCHRONIZE, TRUE, 0))
	{
		wchar_t options[112];
		swprintf_s(options, L"/receiver-watch %llu %llu %u",
			static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(inherited[0])),
			static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(inherited[1])), attempt);
		if (LaunchRecoveryProcess(options, inherited, 2)) receiverClosing = closing;
	}
	for (HANDLE handle : inherited) if (handle) CloseHandle(handle);
	if (!receiverClosing)
	{
		CloseHandle(closing);
		OutputDebugStringA("PDW could not arm radio crash recovery.\n");
	}
}

void DisarmReceiverRecovery()
{
	if (!receiverClosing) return;
	SetEvent(receiverClosing);
	CloseHandle(receiverClosing);
	receiverClosing = NULL;
}

BOOL IsStartWithWindowsEnabled(void)
{
	char expected[MAX_PATH + 32];
	char current[MAX_PATH + 32];
	if (!BuildStartupCommand(expected, sizeof(expected))) return FALSE;

	HKEY key = NULL;
	if (RegOpenKeyExA(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
		return FALSE;

	DWORD type = 0;
	DWORD size = sizeof(current);
	LONG result = RegQueryValueExA(key, kValueName, NULL, &type,
		reinterpret_cast<LPBYTE>(current), &size);
	RegCloseKey(key);
	if (result != ERROR_SUCCESS || type != REG_SZ || size == 0 || size > sizeof(current))
		return FALSE;

	current[sizeof(current) - 1] = '\0';
	return lstrcmpiA(current, expected) == 0;
}

BOOL SetStartWithWindowsEnabled(BOOL enabled, char* errorText, size_t errorTextSize)
{
	if (errorText && errorTextSize) errorText[0] = '\0';
	HKEY key = NULL;
	LONG result = RegCreateKeyExA(HKEY_CURRENT_USER, kRunKey, 0, NULL, 0,
		KEY_SET_VALUE, NULL, &key, NULL);
	if (result != ERROR_SUCCESS)
	{
		SetRegistryError(errorText, errorTextSize, result);
		return FALSE;
	}

	if (enabled)
	{
		char command[MAX_PATH + 32];
		if (!BuildStartupCommand(command, sizeof(command)))
		{
			RegCloseKey(key);
			if (errorText && errorTextSize)
				snprintf(errorText, errorTextSize, "Windows could not determine the PDW executable path.");
			return FALSE;
		}
		result = RegSetValueExA(key, kValueName, 0, REG_SZ,
			reinterpret_cast<const BYTE*>(command), static_cast<DWORD>(strlen(command) + 1));
	}
	else
	{
		result = RegDeleteValueA(key, kValueName);
		if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
	}

	RegCloseKey(key);
	if (result != ERROR_SUCCESS)
	{
		SetRegistryError(errorText, errorTextSize, result);
		return FALSE;
	}
	return TRUE;
}
