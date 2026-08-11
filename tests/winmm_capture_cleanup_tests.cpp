#include "winmm_capture_cleanup.h"

#include <cstdlib>
#include <iostream>

namespace
{
MMRESULT g_resetResult = MMSYSERR_NOERROR;
MMRESULT g_closeInputResult = MMSYSERR_NOERROR;
MMRESULT g_closeOutputResult = MMSYSERR_NOERROR;
MMRESULT g_unprepareResults[4] = {};
std::size_t g_unprepareResultCount = 0;
std::size_t g_resetCalls = 0;
std::size_t g_unprepareCalls = 0;
std::size_t g_closeInputCalls = 0;
std::size_t g_closeOutputCalls = 0;

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		std::exit(1);
	}
}

void ResetFakes()
{
	g_resetResult = MMSYSERR_NOERROR;
	g_closeInputResult = MMSYSERR_NOERROR;
	g_closeOutputResult = MMSYSERR_NOERROR;
	for (std::size_t index = 0; index < 4; ++index)
		g_unprepareResults[index] = MMSYSERR_NOERROR;
	g_unprepareResultCount = 0;
	g_resetCalls = 0;
	g_unprepareCalls = 0;
	g_closeInputCalls = 0;
	g_closeOutputCalls = 0;
}

MMRESULT WINAPI FakeReset(HWAVEIN)
{
	++g_resetCalls;
	return g_resetResult;
}

MMRESULT WINAPI FakeUnprepare(HWAVEIN, LPWAVEHDR, UINT)
{
	const std::size_t index = g_unprepareCalls++;
	return index < g_unprepareResultCount ?
		g_unprepareResults[index] : MMSYSERR_NOERROR;
}

MMRESULT WINAPI FakeCloseInput(HWAVEIN)
{
	++g_closeInputCalls;
	return g_closeInputResult;
}

MMRESULT WINAPI FakeCloseOutput(HWAVEOUT)
{
	++g_closeOutputCalls;
	return g_closeOutputResult;
}
}

int main()
{
	using namespace pdw::signal;
	const HWAVEIN input = reinterpret_cast<HWAVEIN>(static_cast<UINT_PTR>(1));
	const HWAVEOUT output = reinterpret_cast<HWAVEOUT>(static_cast<UINT_PTR>(1));
	WAVEHDR headers[3] = {};
	const WinmmCaptureCleanupApi api = { FakeReset, FakeUnprepare, FakeCloseInput };

	ResetFakes();
	Expect(ReleaseWinmmCaptureFailClosed(NULL, headers, 3, api),
		"a missing input handle is already released");
	Expect(g_resetCalls == 0 && g_unprepareCalls == 0 && g_closeInputCalls == 0,
		"a missing input handle must not call WinMM");

	ResetFakes();
	g_resetResult = MMSYSERR_ERROR;
	Expect(!ReleaseWinmmCaptureFailClosed(input, headers, 3, api),
		"reset failure must quarantine the input");
	Expect(g_resetCalls == 1 && g_unprepareCalls == 0 && g_closeInputCalls == 0,
		"reset failure must not unprepare, close, or release live resources");

	ResetFakes();
	g_unprepareResultCount = 3;
	g_unprepareResults[0] = MMSYSERR_NOERROR;
	g_unprepareResults[1] = WAVERR_STILLPLAYING;
	g_unprepareResults[2] = WAVERR_UNPREPARED;
	Expect(!ReleaseWinmmCaptureFailClosed(input, headers, 3, api),
		"a prepared-header release failure must quarantine the input");
	Expect(g_unprepareCalls == 3 && g_closeInputCalls == 0,
		"all headers should be attempted, but the handle must remain open on failure");

	ResetFakes();
	g_unprepareResultCount = 3;
	g_unprepareResults[0] = WAVERR_UNPREPARED;
	g_unprepareResults[1] = MMSYSERR_NOERROR;
	g_unprepareResults[2] = WAVERR_UNPREPARED;
	Expect(ReleaseWinmmCaptureFailClosed(input, headers, 3, api),
		"already-unprepared headers should permit a confirmed close");
	Expect(g_closeInputCalls == 1, "confirmed header release must close the input once");

	ResetFakes();
	g_closeInputResult = MMSYSERR_ERROR;
	Expect(!ReleaseWinmmCaptureFailClosed(input, headers, 3, api),
		"input close failure must preserve ownership");
	Expect(g_closeInputCalls == 1, "input close should be attempted exactly once");

	ResetFakes();
	Expect(ReleaseWinmmOutputFailClosed(NULL, FakeCloseOutput),
		"a missing output handle is already released");
	Expect(g_closeOutputCalls == 0, "a missing output handle must not call WinMM");
	Expect(ReleaseWinmmOutputFailClosed(output, FakeCloseOutput),
		"successful output close should release ownership");
	Expect(g_closeOutputCalls == 1, "output close should be attempted once");
	g_closeOutputResult = MMSYSERR_ERROR;
	Expect(!ReleaseWinmmOutputFailClosed(output, FakeCloseOutput),
		"output close failure must preserve ownership");
	Expect(g_closeOutputCalls == 2, "failed output close should still be attempted once");

	std::cout << "WinMM fail-closed cleanup tests passed\n";
	return 0;
}
