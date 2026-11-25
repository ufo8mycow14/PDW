#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <windows.h>

namespace
{
	volatile LONG g_cancelRequested = 0;
	volatile LONG g_endCaptureEarly = 0;
	volatile LONG g_blockCancelReturn = 0;
	volatile LONG g_cancelEntered = 0;
	volatile LONG g_allowCancelReturn = 0;
	volatile LONG g_readAsyncReturning = 0;
	volatile LONG g_closeCount = 0;
	volatile LONG g_ignoreOneCancel = 0;

	BOOL CALLBACK CloseOwnFixtureWindow(HWND window, LPARAM)
	{
		DWORD owner = 0;
		GetWindowThreadProcessId(window, &owner);
		wchar_t name[64];
		if (owner != GetCurrentProcessId() || !GetClassNameW(window, name, 64) ||
			lstrcmpW(name, L"WinPDWWndClass") != 0) return TRUE;
		PostMessageW(window, WM_CLOSE, 0, 0);
		return FALSE;
	}
}

extern "C"
{
	__declspec(dllexport) std::uint32_t __cdecl rtlsdr_get_device_count()
	{
		char readyPath[MAX_PATH] = {};
		if (GetEnvironmentVariableA("PDW_TEST_RTL_READY_FILE", readyPath, MAX_PATH))
			return GetFileAttributesA(readyPath) == INVALID_FILE_ATTRIBUTES ? 0 : 1;
		return 1;
	}
	__declspec(dllexport) const char* __cdecl rtlsdr_get_device_name(std::uint32_t) { return "PDW test receiver"; }
	__declspec(dllexport) int __cdecl rtlsdr_get_device_usb_strings(std::uint32_t,
		char* manufacturer, char* product, char* serial)
	{
		if (manufacturer) { manufacturer[0] = 'P'; manufacturer[1] = 'D'; manufacturer[2] = 'W'; manufacturer[3] = 0; }
		if (product) { product[0] = 'T'; product[1] = 'e'; product[2] = 's'; product[3] = 't'; product[4] = 0; }
		if (serial) { serial[0] = '1'; serial[1] = 0; }
		return 0;
	}
	__declspec(dllexport) int __cdecl rtlsdr_open(void** device, std::uint32_t)
	{
		InterlockedExchange(&g_cancelRequested, 0);
		InterlockedExchange(&g_endCaptureEarly, 0);
		InterlockedExchange(&g_blockCancelReturn, 0);
		InterlockedExchange(&g_cancelEntered, 0);
		InterlockedExchange(&g_allowCancelReturn, 0);
		InterlockedExchange(&g_readAsyncReturning, 0);
		InterlockedExchange(&g_closeCount, 0);
		if (device) *device = reinterpret_cast<void*>(1);
		return 0;
	}
	__declspec(dllexport) int __cdecl rtlsdr_close(void*)
	{
		InterlockedIncrement(&g_closeCount);
		return 0;
	}
	__declspec(dllexport) int __cdecl rtlsdr_set_center_freq(void*, std::uint32_t frequency)
	{
		InterlockedExchange(&g_endCaptureEarly, frequency == 148812501 ? 1 : 0);
		InterlockedExchange(&g_ignoreOneCancel, frequency == 148812502 ? 1 : 0);
		return 0;
	}
	__declspec(dllexport) int __cdecl rtlsdr_set_sample_rate(void*, std::uint32_t) { return 0; }
	__declspec(dllexport) int __cdecl rtlsdr_set_tuner_gain_mode(void*, int) { return 0; }
	__declspec(dllexport) int __cdecl rtlsdr_set_tuner_gain(void*, int) { return 0; }
	__declspec(dllexport) int __cdecl rtlsdr_set_freq_correction(void*, int ppm)
	{
		if (ppm == 1000000) return -3;
		return ppm == 0 ? -2 : 0;
	}
	__declspec(dllexport) int __cdecl rtlsdr_reset_buffer(void*) { return 0; }
	__declspec(dllexport) int __cdecl rtlsdr_read_async(void*,
		void (__cdecl *callback)(unsigned char*, std::uint32_t, void*), void* context,
		std::uint32_t, std::uint32_t)
	{
		unsigned char iq[32768];
		wchar_t stallFile[MAX_PATH] = {}, closeFile[MAX_PATH] = {}, crashFile[MAX_PATH] = {};
		const bool lifecycle = GetEnvironmentVariableW(L"PDW_TEST_RTL_CLOSE_REQUEST", closeFile, MAX_PATH) > 0 &&
			GetEnvironmentVariableW(L"PDW_TEST_RTL_CRASH_REQUEST", crashFile, MAX_PATH) > 0;
		if (lifecycle && GetEnvironmentVariableW(L"PDW_TEST_RTL_STALL_ONCE", stallFile, MAX_PATH))
		{
			HANDLE first = CreateFileW(stallFile, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
			if (first != INVALID_HANDLE_VALUE)
			{
				CloseHandle(first);
				while (!InterlockedCompareExchange(&g_cancelRequested, 0, 0))
				{
					if (callback) callback(NULL, 0, context);
					Sleep(16);
				}
				return 0;
			}
			wchar_t lateStall[MAX_PATH] = {};
			if (GetEnvironmentVariableW(L"PDW_TEST_RTL_STALL_AFTER_IQ", lateStall, MAX_PATH) && DeleteFileW(lateStall))
			{
				std::memset(iq, 128, sizeof(iq));
				if (callback) callback(iq, sizeof(iq), context);
				while (!InterlockedCompareExchange(&g_cancelRequested, 0, 0)) Sleep(10);
				return 0;
			}
		}
		wchar_t fixturePath[MAX_PATH] = {};
		const DWORD pathLength = GetEnvironmentVariableW(L"PDW_TEST_RTL_SYNTHETIC_IQ", fixturePath, MAX_PATH);
		if (pathLength && pathLength < MAX_PATH && callback)
		{
			HANDLE fixture = CreateFileW(fixturePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if (fixture == INVALID_HANDLE_VALUE) return -1;
			DWORD bytes = 0;
			wchar_t timing[16] = {};
			GetEnvironmentVariableW(L"PDW_TEST_RTL_CADENCE", timing, 16);
			const DWORD callbackDelay = lstrcmpW(timing, L"delayed") == 0 ? 32 : lstrcmpW(timing, L"fast") == 0 ? 4 : 16;
			while (InterlockedCompareExchange(&g_cancelRequested, 0, 0) == 0 &&
				ReadFile(fixture, iq, sizeof(iq), &bytes, NULL) && bytes)
			{
				callback(iq, bytes, context);
				Sleep(callbackDelay);
			}
			CloseHandle(fixture);
		}
		for (std::size_t index = 0; index < sizeof(iq); index += 2)
		{
			iq[index] = static_cast<unsigned char>(96 + ((index / 2) % 64));
			iq[index + 1] = static_cast<unsigned char>(159 - ((index / 2) % 64));
		}
		if (lifecycle) std::memset(iq, 128, sizeof(iq));
		if (callback) callback(iq, static_cast<std::uint32_t>(sizeof(iq)), context);
		char markerPath[MAX_PATH] = {};
		if (GetEnvironmentVariableA("PDW_TEST_RTL_CALLBACK_MARKER", markerPath, MAX_PATH))
		{
			HANDLE marker = CreateFileA(markerPath, GENERIC_WRITE, FILE_SHARE_READ,
				NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (marker != INVALID_HANDLE_VALUE)
			{
				char text[32];
				const int length = sprintf_s(text, "%lu", GetCurrentProcessId());
				DWORD written = 0;
				if (length > 0) WriteFile(marker, text, static_cast<DWORD>(length), &written, NULL);
				CloseHandle(marker);
			}
		}
		if (InterlockedCompareExchange(&g_endCaptureEarly, 0, 0) != 0)
		{
			Sleep(50);
			InterlockedExchange(&g_readAsyncReturning, 1);
			return 0;
		}
		while (InterlockedCompareExchange(&g_cancelRequested, 0, 0) == 0)
		{
			if (lifecycle)
			{
				if (DeleteFileW(crashFile)) TerminateProcess(GetCurrentProcess(), EXCEPTION_ACCESS_VIOLATION);
				if (DeleteFileW(closeFile)) EnumWindows(CloseOwnFixtureWindow, 0);
				if (callback) callback(iq, sizeof(iq), context);
			}
			Sleep(lifecycle ? 16 : 5);
		}
		InterlockedExchange(&g_readAsyncReturning, 1);
		return 0;
	}
	__declspec(dllexport) int __cdecl rtlsdr_cancel_async(void*)
	{
		if (InterlockedExchange(&g_ignoreOneCancel, 0)) return -2;
		InterlockedExchange(&g_cancelRequested, 1);
		if (InterlockedCompareExchange(&g_blockCancelReturn, 0, 0) != 0)
		{
			InterlockedExchange(&g_cancelEntered, 1);
			while (InterlockedCompareExchange(&g_allowCancelReturn, 0, 0) == 0) Sleep(1);
		}
		return 0;
	}

	__declspec(dllexport) void __cdecl rtlsdr_test_block_cancel_return()
	{
		InterlockedExchange(&g_cancelEntered, 0);
		InterlockedExchange(&g_allowCancelReturn, 0);
		InterlockedExchange(&g_readAsyncReturning, 0);
		InterlockedExchange(&g_blockCancelReturn, 1);
	}
	__declspec(dllexport) void __cdecl rtlsdr_test_allow_cancel_return()
	{
		InterlockedExchange(&g_allowCancelReturn, 1);
	}
	__declspec(dllexport) LONG __cdecl rtlsdr_test_cancel_entered()
	{
		return InterlockedCompareExchange(&g_cancelEntered, 0, 0);
	}
	__declspec(dllexport) LONG __cdecl rtlsdr_test_read_async_returning()
	{
		return InterlockedCompareExchange(&g_readAsyncReturning, 0, 0);
	}
	__declspec(dllexport) LONG __cdecl rtlsdr_test_close_count()
	{
		return InterlockedCompareExchange(&g_closeCount, 0, 0);
	}
}
