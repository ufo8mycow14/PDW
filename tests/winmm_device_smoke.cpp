#include <windows.h>
#include <mmsystem.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
std::string WaveError(MMRESULT result)
{
	char text[MAXERRORLENGTH] = {};
	if (waveInGetErrorTextA(result, text, MAXERRORLENGTH) == MMSYSERR_NOERROR)
		return text;
	return "WinMM error " + std::to_string(result);
}

int Fail(const char* operation, MMRESULT result)
{
	std::cerr << operation << " failed: " << WaveError(result) << '\n';
	return 1;
}
}

int main()
{
	const UINT deviceCount = waveInGetNumDevs();
	if (deviceCount == 0)
	{
		std::cerr << "WinMM did not report an audio capture device.\n";
		return 1;
	}

	WAVEFORMATEX format = {};
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = 1;
	format.nSamplesPerSec = 44100;
	format.wBitsPerSample = 8;
	format.nBlockAlign = 1;
	format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

	MMRESULT result = waveInOpen(NULL, WAVE_MAPPER, &format, 0, 0, WAVE_FORMAT_QUERY);
	if (result != MMSYSERR_NOERROR) return Fail("WinMM legacy-format query", result);

	HANDLE bufferReady = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (!bufferReady)
	{
		std::cerr << "WinMM smoke test could not create its completion event.\n";
		return 1;
	}

	HWAVEIN input = NULL;
	result = waveInOpen(&input, WAVE_MAPPER, &format,
		reinterpret_cast<DWORD_PTR>(bufferReady), 0, CALLBACK_EVENT);
	if (result != MMSYSERR_NOERROR)
	{
		CloseHandle(bufferReady);
		return Fail("WinMM device open", result);
	}

	std::vector<unsigned char> samples(format.nAvgBytesPerSec);
	WAVEHDR header = {};
	header.lpData = reinterpret_cast<LPSTR>(&samples[0]);
	header.dwBufferLength = static_cast<DWORD>(samples.size());

	result = waveInPrepareHeader(input, &header, sizeof(header));
	if (result == MMSYSERR_NOERROR)
		result = waveInAddBuffer(input, &header, sizeof(header));
	if (result == MMSYSERR_NOERROR)
	{
		ResetEvent(bufferReady); // Ignore the CALLBACK_EVENT open notification.
		result = waveInStart(input);
	}

	DWORD waitResult = WAIT_FAILED;
	if (result == MMSYSERR_NOERROR)
		waitResult = WaitForSingleObject(bufferReady, 4000);

	waveInReset(input);
	const DWORD bytesRecorded = header.dwBytesRecorded;
	const MMRESULT unprepareResult = waveInUnprepareHeader(input, &header, sizeof(header));
	waveInClose(input);
	CloseHandle(bufferReady);

	if (result != MMSYSERR_NOERROR) return Fail("WinMM capture setup", result);
	if (waitResult != WAIT_OBJECT_0)
	{
		std::cerr << "WinMM capture did not complete within four seconds.\n";
		return 2;
	}
	if (unprepareResult != MMSYSERR_NOERROR) return Fail("WinMM buffer cleanup", unprepareResult);

	std::cout << "devices=" << deviceCount
		<< " bytes=" << bytesRecorded
		<< " sample_rate=" << format.nSamplesPerSec
		<< " bits=" << format.wBitsPerSample << '\n';
	return bytesRecorded > 0 ? 0 : 2;
}
