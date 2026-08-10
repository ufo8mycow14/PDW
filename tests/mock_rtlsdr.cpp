#include <cstdint>

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
		if (device) *device = reinterpret_cast<void*>(1);
		return 0;
	}
	__declspec(dllexport) int __cdecl rtlsdr_close(void*) { return 0; }
	__declspec(dllexport) int __cdecl rtlsdr_set_center_freq(void*, std::uint32_t) { return 0; }
	__declspec(dllexport) int __cdecl rtlsdr_set_sample_rate(void*, std::uint32_t) { return 0; }
	__declspec(dllexport) int __cdecl rtlsdr_set_tuner_gain_mode(void*, int) { return 0; }
	__declspec(dllexport) int __cdecl rtlsdr_set_tuner_gain(void*, int) { return 0; }
	__declspec(dllexport) int __cdecl rtlsdr_set_freq_correction(void*, int) { return 0; }
	__declspec(dllexport) int __cdecl rtlsdr_reset_buffer(void*) { return 0; }
	__declspec(dllexport) int __cdecl rtlsdr_read_async(void*,
		void (__cdecl *)(unsigned char*, std::uint32_t, void*), void*, std::uint32_t, std::uint32_t) { return 0; }
	__declspec(dllexport) int __cdecl rtlsdr_cancel_async(void*) { return 0; }
}
