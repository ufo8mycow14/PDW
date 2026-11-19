#include "audio_signal_core.h"
#include "signal_recording_core.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <new>

// Make the malformed-file regression safe even against the old reader: a tiny
// input must be rejected before it can request hundreds of megabytes.
namespace
{
bool g_limitRecordingAllocations = false;
bool g_blockedRecordingAllocation = false;
}

void* operator new(std::size_t size)
{
	if (g_limitRecordingAllocations && size > 1024 * 1024)
	{
		g_blockedRecordingAllocation = true;
		throw std::bad_alloc();
	}
	if (void* memory = std::malloc(size ? size : 1)) return memory;
	throw std::bad_alloc();
}

void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

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

void AppendLe32(std::string& bytes, std::uint32_t value)
{
	for (unsigned int shift = 0; shift < 32; shift += 8)
		bytes.push_back(static_cast<char>((value >> shift) & 0xff));
}

std::string WaveFile(const std::string& chunks)
{
	std::string bytes("RIFF");
	AppendLe32(bytes, static_cast<std::uint32_t>(4 + chunks.size()));
	return bytes + "WAVE" + chunks;
}

std::string WaveChunk(const char* type, const std::string& data)
{
	std::string bytes(type, 4);
	AppendLe32(bytes, static_cast<std::uint32_t>(data.size()));
	bytes += data;
	if (data.size() & 1) bytes.push_back('\0');
	return bytes;
}

void WriteFixture(const std::string& path, const std::string& bytes)
{
	std::ofstream output(path.c_str(), std::ios::binary | std::ios::trunc);
	output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	Expect(output.good(), "synthetic WAV fixture writes");
}

void CheckWavChunkBounds()
{
	const std::string path("pdw-synthetic-chunk-bounds.wav");
	pdw::signal::SignalRecording recording;
	std::string error;
	for (const char* type : { "fmt ", "data" })
	{
		std::string chunk(type, 4);
		AppendLe32(chunk, 512u * 1024u * 1024u);
		WriteFixture(path, WaveFile(chunk));
		g_blockedRecordingAllocation = false;
		g_limitRecordingAllocations = true;
		bool accepted = false;
		try { accepted = pdw::signal::ReadWavMono(path, recording, error); }
		catch (const std::bad_alloc&) { }
		g_limitRecordingAllocations = false;
		Expect(!g_blockedRecordingAllocation,
			"truncated WAV is rejected before a size-based allocation");
		Expect(!accepted && !error.empty() && recording.samples.empty(),
			"truncated WAV returns an error without samples");
	}

	// PCM8 mono with an extended format chunk and odd-length ancillary/audio
	// chunks. Chunk padding and either order of format/data stay supported.
	std::string format;
	format.push_back(1); format.push_back(0); // PCM
	format.push_back(1); format.push_back(0); // mono
	AppendLe32(format, 48000); AppendLe32(format, 48000);
	format.push_back(1); format.push_back(0); // block alignment
	format.push_back(8); format.push_back(0); // bits per sample
	format.append(1024, '\0');
	const std::string data("\0\x80\xff", 3);
	const std::string chunks = WaveChunk("JUNK", "odd") +
		WaveChunk("data", data) + WaveChunk("fmt ", format);
	WriteFixture(path, WaveFile(chunks));
	Expect(pdw::signal::ReadWavMono(path, recording, error) &&
		recording.sampleRate == 48000 && recording.samples.size() == 3 &&
		recording.samples[0] == pdw::signal::NormalizePcm8(0) &&
		recording.samples[2] == pdw::signal::NormalizePcm8(255),
		"extended format, odd padding, unknown chunk and data-before-format work");

	std::string truncated = WaveFile(chunks);
	truncated.pop_back();
	WriteFixture(path, truncated);
	Expect(!pdw::signal::ReadWavMono(path, recording, error) && recording.samples.empty(),
		"RIFF length beyond the physical file is rejected");
	std::string shortContainer("RIFF");
	AppendLe32(shortContainer, 4);
	shortContainer += "WAVE" + chunks;
	WriteFixture(path, shortContainer);
	Expect(!pdw::signal::ReadWavMono(path, recording, error),
		"audio outside the declared RIFF container is not consumed");
	std::string missingPad = WaveChunk("fmt ", format) + WaveChunk("data", data);
	missingPad.pop_back();
	WriteFixture(path, WaveFile(missingPad));
	Expect(!pdw::signal::ReadWavMono(path, recording, error),
		"odd chunk must include its padding within the RIFF container");
	std::string undersizedRiff("RIFF");
	AppendLe32(undersizedRiff, 3);
	WriteFixture(path, undersizedRiff + "WAVE");
	Expect(!pdw::signal::ReadWavMono(path, recording, error),
		"RIFF size must include the WAVE form type");
	WriteFixture(path, WaveFile(WaveChunk("fmt ", format) + WaveChunk("data", data)) + "trailing");
	Expect(pdw::signal::ReadWavMono(path, recording, error),
		"valid RIFF data can have trailing bytes outside the container");
	std::remove(path.c_str());
}
}

int main()
{
	using namespace pdw::signal;
	CheckWavChunkBounds();
	const std::string wavPath = "pdw-synthetic-pocsag.wav";
	const std::string sigmfBase = "pdw-synthetic-pocsag";
	std::remove(wavPath.c_str());
	std::remove((sigmfBase + ".sigmf-meta").c_str());
	std::remove((sigmfBase + ".sigmf-data").c_str());

	SignalRecording generated;
	generated.sampleRate = 48000;
	const int samplesPerSymbol = 40; // 1200 baud
	const int symbolCount = 240;
	for (int symbol = 0; symbol < symbolCount; ++symbol)
	{
		const float level = (symbol & 1) ? 0.62f : -0.38f; // includes +0.12 DC
		for (int sample = 0; sample < samplesPerSymbol; ++sample)
		{
			const float noise = static_cast<float>(((symbol * 17 + sample * 13) % 11) - 5) * 0.002f;
			generated.samples.push_back(level + noise);
		}
	}

	std::string error;
	Expect(WriteWav16Mono(wavPath, generated, error), error.c_str());
	SignalRecording wavRoundTrip;
	Expect(ReadWavMono(wavPath, wavRoundTrip, error), error.c_str());
	Expect(wavRoundTrip.sampleRate == generated.sampleRate, "WAV sample rate round trip");
	Expect(wavRoundTrip.samples.size() == generated.samples.size(), "WAV sample count round trip");
	for (std::size_t index = 0; index < generated.samples.size(); ++index)
		Expect(std::fabs(wavRoundTrip.samples[index] - generated.samples[index]) < 0.0001f,
			"WAV16 sample tolerance");

	AdaptiveSlicer slicer;
	for (int symbol = 0; symbol < symbolCount; ++symbol)
	{
		unsigned char sliced = 0;
		for (int sample = 0; sample < samplesPerSymbol; ++sample)
			sliced = slicer.SliceTwoLevel(wavRoundTrip.samples[symbol * samplesPerSymbol + sample]);
		if (symbol > 10)
			Expect(sliced == ((symbol & 1) ? 0 : 3), "synthetic POCSAG preamble slicing");
	}

	Expect(WriteSigMfReal32(sigmfBase, wavRoundTrip, error), error.c_str());
	SignalRecording sigmfRoundTrip;
	Expect(ReadSigMfReal32(sigmfBase, sigmfRoundTrip, error), error.c_str());
	Expect(sigmfRoundTrip.sampleRate == generated.sampleRate, "SigMF sample rate round trip");
	Expect(sigmfRoundTrip.samples.size() == generated.samples.size(), "SigMF sample count round trip");
	for (std::size_t index = 0; index < generated.samples.size(); ++index)
		Expect(sigmfRoundTrip.samples[index] == wavRoundTrip.samples[index], "SigMF float sample round trip");
	for (const char* metadata : {
		"{\"global\":{\"core:datatype\":\"rf32_le\",\"core:sample_rate\":48000}}",
		"{\n\"annotations\":[{\"custom\":[true,false,null,1.5]}],\"global\":{\"core:sample_rate\":4.8e4,\"core:datatype\":\"rf32_le\"}}",
		"{\"global\":{\"core:datatype\":\"rf32_\\u006ce\",\"core:sample_rate\":48000}}"})
	{
		std::ofstream file(sigmfBase + ".sigmf-meta", std::ios::binary); file << metadata; file.close();
		Expect(ReadSigMfReal32(sigmfBase, sigmfRoundTrip, error) && sigmfRoundTrip.sampleRate == 48000,
			"equivalent compact/reordered/escaped SigMF metadata is accepted");
	}
	for (const char* metadata : {
		"{\"global\":{\"core:datatype\":\"rf32_le\",\"core:sample_rate\":NaN}}",
		"{\"global\":{\"core:datatype\":\"rf32_le\",\"core:sample_rate\":1e999}}",
		"{\"global\":{\"core:datatype\":\"rf32_le\",\"core:sample_rate\":48000.5}}",
		"{\"global\":{\"core:datatype\":\"rf32_le\",\"core:sample_rate\":0}}",
		"{\"global\":{\"core:datatype\":\"rf32_le\",\"core:sample_rate\":48000,}}",
		"{\"core:datatype\":\"rf32_le\",\"core:sample_rate\":48000}",
		"{\"global\":{\"core:datatype\":\"rf32_le\",\"core:sample_rate\":48000,\"core:sample_rate\":8000}}"})
	{
		std::ofstream file(sigmfBase + ".sigmf-meta", std::ios::binary); file << metadata; file.close();
		Expect(!ReadSigMfReal32(sigmfBase, sigmfRoundTrip, error), "invalid or ambiguous SigMF metadata is rejected");
	}
	{
		std::ofstream file(sigmfBase + ".sigmf-meta", std::ios::binary); file << std::string(1024 * 1024 + 1, ' '); file.close();
		Expect(!ReadSigMfReal32(sigmfBase, sigmfRoundTrip, error), "metadata size is bounded before allocation/parsing");
	}

	AdaptiveSlicer flexSlicer;
	for (int symbol = 0; symbol < 320; ++symbol)
	{
		const unsigned char expected = static_cast<unsigned char>(symbol % 4);
		const float levels[4] = { 0.90f, 0.36f, -0.18f, -0.72f };
		unsigned char sliced = 0;
		for (int sample = 0; sample < 15; ++sample) // 3200 symbols/s at 48 kHz
		{
			const float noise = static_cast<float>(((symbol * 7 + sample * 5) % 9) - 4) * 0.0015f;
			sliced = flexSlicer.SliceFourLevel(levels[expected] + noise);
		}
		if (symbol > 40) Expect(sliced == expected, "synthetic four-level FLEX slicing");
	}

	std::remove(wavPath.c_str());
	std::remove((sigmfBase + ".sigmf-meta").c_str());
	std::remove((sigmfBase + ".sigmf-data").c_str());
	std::cout << "signal recording core tests passed\n";
	return 0;
}
