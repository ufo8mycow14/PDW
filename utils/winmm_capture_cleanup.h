#ifndef PDW_WINMM_CAPTURE_CLEANUP_H
#define PDW_WINMM_CAPTURE_CLEANUP_H

#include <windows.h>
#include <mmsystem.h>

#include <cstddef>

namespace pdw
{
namespace signal
{

typedef MMRESULT (WINAPI *WinmmResetFunction)(HWAVEIN input);
typedef MMRESULT (WINAPI *WinmmUnprepareFunction)(HWAVEIN input,
	LPWAVEHDR header, UINT headerSize);
typedef MMRESULT (WINAPI *WinmmInputCloseFunction)(HWAVEIN input);
typedef MMRESULT (WINAPI *WinmmOutputCloseFunction)(HWAVEOUT output);

struct WinmmCaptureCleanupApi
{
	WinmmResetFunction reset;
	WinmmUnprepareFunction unprepare;
	WinmmInputCloseFunction closeInput;
};

inline bool ReleaseWinmmCaptureFailClosed(HWAVEIN input, WAVEHDR* headers,
	std::size_t preparedHeaderCount, const WinmmCaptureCleanupApi& api)
{
	if (!input) return true;
	if (!api.reset || !api.unprepare || !api.closeInput ||
		(preparedHeaderCount != 0 && !headers)) return false;
	if (api.reset(input) != MMSYSERR_NOERROR) return false;

	bool headersReleased = true;
	for (std::size_t index = 0; index < preparedHeaderCount; ++index)
	{
		const MMRESULT result = api.unprepare(input, &headers[index],
			static_cast<UINT>(sizeof(WAVEHDR)));
		if (result != MMSYSERR_NOERROR && result != WAVERR_UNPREPARED)
			headersReleased = false;
	}
	if (!headersReleased) return false;
	return api.closeInput(input) == MMSYSERR_NOERROR;
}

inline bool ReleaseWinmmOutputFailClosed(HWAVEOUT output,
	WinmmOutputCloseFunction closeOutput)
{
	return !output || (closeOutput && closeOutput(output) == MMSYSERR_NOERROR);
}

} // namespace signal
} // namespace pdw

#endif
