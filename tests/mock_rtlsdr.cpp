#include <cstdint>
#include <cstddef>
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
}

extern "C"
{
	__declspec(dllexport) std::uint32_t __cdecl rtlsdr_get_device_count() { return 1; }
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
		return 0;
	}
	__declspec(dllexport) int __cdecl rtlsdr_set_sample_rate(void*, std::uint32_t) { return 0; }
	__declspec(dllexport) int __cdecl rtlsdr_set_tuner_gain_mode(void*, int) { return 0; }
	__declspec(dllexport) int __cdecl rtlsdr_set_tuner_gain(void*, int) { return 0; }
	__declspec(dllexport) int __cdecl rtlsdr_set_freq_correction(void*, int) { return 0; }
	__declspec(dllexport) int __cdecl rtlsdr_reset_buffer(void*) { return 0; }
	__declspec(dllexport) int __cdecl rtlsdr_read_async(void*,
		void (__cdecl *callback)(unsigned char*, std::uint32_t, void*), void* context,
		std::uint32_t, std::uint32_t)
	{
		unsigned char iq[32768];
		for (std::size_t index = 0; index < sizeof(iq); index += 2)
		{
			iq[index] = static_cast<unsigned char>(96 + ((index / 2) % 64));
			iq[index + 1] = static_cast<unsigned char>(159 - ((index / 2) % 64));
		}
		if (callback) callback(iq, static_cast<std::uint32_t>(sizeof(iq)), context);
		if (InterlockedCompareExchange(&g_endCaptureEarly, 0, 0) != 0)
		{
			Sleep(50);
			InterlockedExchange(&g_readAsyncReturning, 1);
			return 0;
		}
		while (InterlockedCompareExchange(&g_cancelRequested, 0, 0) == 0) Sleep(5);
		InterlockedExchange(&g_readAsyncReturning, 1);
		return 0;
	}
	__declspec(dllexport) int __cdecl rtlsdr_cancel_async(void*)
	{
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
