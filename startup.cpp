#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "headers\startup.h"

namespace
{
	const char* kRunKey = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
	const char* kValueName = "PDW";

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
