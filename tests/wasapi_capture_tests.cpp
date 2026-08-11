#include "wasapi_capture.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace
{
void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		std::exit(1);
	}
}
}

int main()
{
	using namespace pdw::signal;

	Expect(WasapiThreadResourcesMayBeReleased(WAIT_OBJECT_0),
		"a signalled capture thread may be torn down");
	Expect(!WasapiThreadResourcesMayBeReleased(WAIT_TIMEOUT),
		"a timed-out capture thread must remain quarantined");
	Expect(!WasapiThreadResourcesMayBeReleased(WAIT_FAILED),
		"a failed thread wait must remain quarantined");
	{
		WasapiCaptureSource idleSource;
		Expect(idleSource.Stop(), "an idle capture source stops successfully");
		Expect(idleSource.Stop(), "stopping an idle capture source is idempotent");
	}

	WAVEFORMATEX pcm16 = {};
	pcm16.wFormatTag = WAVE_FORMAT_PCM;
	pcm16.nChannels = 2;
	pcm16.nSamplesPerSec = 48000;
	pcm16.wBitsPerSample = 16;
	pcm16.nBlockAlign = 4;
	WasapiSampleFormat described = {};
	Expect(DescribeWasapiFormat(&pcm16, described), "describe stereo PCM16");
	Expect(described.type == WASAPI_SAMPLE_PCM16, "PCM16 type");
	Expect(described.channels == 2 && described.sampleRate == 48000, "PCM16 layout");

	const short stereo16[] = { 32767, 32767, -32768, -32768, 32767, -32768 };
	std::vector<float> mono;
	Expect(ConvertWasapiFramesToMono(reinterpret_cast<const unsigned char*>(stereo16), 3, described, mono),
		"convert stereo PCM16");
	Expect(mono.size() == 3, "PCM16 frame count");
	Expect(mono[0] > 0.999f, "PCM16 positive frame");
	Expect(mono[1] == -1.0f, "PCM16 negative frame");
	Expect(std::fabs(mono[2]) < 0.0001f, "PCM16 stereo average");

	WAVEFORMATEX float32 = {};
	float32.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
	float32.nChannels = 1;
	float32.nSamplesPerSec = 44100;
	float32.wBitsPerSample = 32;
	float32.nBlockAlign = 4;
	Expect(DescribeWasapiFormat(&float32, described), "describe float32");
	const float floatSamples[] = { -2.0f, -0.25f, 0.5f, 2.0f };
	Expect(ConvertWasapiFramesToMono(reinterpret_cast<const unsigned char*>(floatSamples), 4, described, mono),
		"convert float32");
	Expect(mono[0] == -1.0f && mono[3] == 1.0f, "float32 clamps invalid range");
	Expect(mono[1] == -0.25f && mono[2] == 0.5f, "float32 preserves normal range");

	WAVEFORMATEX invalid = {};
	invalid.wFormatTag = WAVE_FORMAT_PCM;
	invalid.nChannels = 1;
	invalid.nSamplesPerSec = 48000;
	invalid.wBitsPerSample = 12;
	Expect(!DescribeWasapiFormat(&invalid, described), "reject unsupported packed PCM");

	std::cout << "WASAPI conversion tests passed\n";
	return 0;
}
