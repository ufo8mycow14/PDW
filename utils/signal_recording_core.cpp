#include "signal_recording_core.h"

#include "audio_signal_core.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>

namespace pdw
{
namespace signal
{

namespace
{
const std::uint32_t MAX_RECORDING_BYTES = 512u * 1024u * 1024u;

bool ReadExact(std::istream& input, void* destination, std::size_t bytes)
{
	input.read(static_cast<char*>(destination), static_cast<std::streamsize>(bytes));
	return input.good() || input.gcount() == static_cast<std::streamsize>(bytes);
}

std::uint16_t ReadLe16(const unsigned char* value)
{
	return static_cast<std::uint16_t>(value[0]) |
		(static_cast<std::uint16_t>(value[1]) << 8);
}

std::uint32_t ReadLe32(const unsigned char* value)
{
	return static_cast<std::uint32_t>(value[0]) |
		(static_cast<std::uint32_t>(value[1]) << 8) |
		(static_cast<std::uint32_t>(value[2]) << 16) |
		(static_cast<std::uint32_t>(value[3]) << 24);
}

void WriteLe16(std::ostream& output, std::uint16_t value)
{
	const unsigned char bytes[2] = {
		static_cast<unsigned char>(value & 0xff),
		static_cast<unsigned char>((value >> 8) & 0xff)
	};
	output.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

void WriteLe32(std::ostream& output, std::uint32_t value)
{
	const unsigned char bytes[4] = {
		static_cast<unsigned char>(value & 0xff),
		static_cast<unsigned char>((value >> 8) & 0xff),
		static_cast<unsigned char>((value >> 16) & 0xff),
		static_cast<unsigned char>((value >> 24) & 0xff)
	};
	output.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

bool IsFourCc(const unsigned char* value, const char* expected)
{
	return std::memcmp(value, expected, 4) == 0;
}

bool ParseSampleRate(const std::string& metadata, std::uint32_t& sampleRate)
{
	const std::string key = "\"core:sample_rate\"";
	const std::size_t keyPosition = metadata.find(key);
	if (keyPosition == std::string::npos) return false;
	const std::size_t colon = metadata.find(':', keyPosition + key.size());
	if (colon == std::string::npos) return false;
	std::istringstream value(metadata.substr(colon + 1));
	double parsed = 0.0;
	value >> parsed;
	if (!value || parsed < 1.0 || parsed > 10000000.0) return false;
	sampleRate = static_cast<std::uint32_t>(parsed + 0.5);
	return true;
}
}

bool ReadWavMono(const std::string& path, SignalRecording& recording, std::string& error)
{
	recording.samples.clear();
	error.clear();
	std::ifstream input(path.c_str(), std::ios::binary);
	if (!input)
	{
		error = "Unable to open WAV file.";
		return false;
	}

	unsigned char riffHeader[12];
	if (!ReadExact(input, riffHeader, sizeof(riffHeader)) ||
		!IsFourCc(riffHeader, "RIFF") || !IsFourCc(riffHeader + 8, "WAVE"))
	{
		error = "Not a RIFF/WAVE recording.";
		return false;
	}

	std::uint16_t formatTag = 0;
	std::uint16_t channels = 0;
	std::uint16_t bitsPerSample = 0;
	std::uint32_t sampleRate = 0;
	std::vector<unsigned char> audioBytes;

	while (input && (formatTag == 0 || audioBytes.empty()))
	{
		unsigned char chunkHeader[8];
		if (!ReadExact(input, chunkHeader, sizeof(chunkHeader))) break;
		const std::uint32_t chunkSize = ReadLe32(chunkHeader + 4);
		if (chunkSize > MAX_RECORDING_BYTES)
		{
			error = "WAV chunk is too large.";
			return false;
		}

		if (IsFourCc(chunkHeader, "fmt "))
		{
			if (chunkSize < 16)
			{
				error = "WAV format chunk is incomplete.";
				return false;
			}
			std::vector<unsigned char> formatBytes(chunkSize);
			if (!ReadExact(input, &formatBytes[0], formatBytes.size()))
			{
				error = "Unable to read WAV format chunk.";
				return false;
			}
			formatTag = ReadLe16(&formatBytes[0]);
			channels = ReadLe16(&formatBytes[2]);
			sampleRate = ReadLe32(&formatBytes[4]);
			bitsPerSample = ReadLe16(&formatBytes[14]);
		}
		else if (IsFourCc(chunkHeader, "data"))
		{
			audioBytes.resize(chunkSize);
			if (chunkSize && !ReadExact(input, &audioBytes[0], audioBytes.size()))
			{
				error = "Unable to read WAV audio data.";
				return false;
			}
		}
		else
		{
			input.seekg(static_cast<std::streamoff>(chunkSize), std::ios::cur);
		}
		if (chunkSize & 1u) input.seekg(1, std::ios::cur);
	}

	if (channels == 0 || sampleRate == 0 || audioBytes.empty())
	{
		error = "WAV is missing format or audio data.";
		return false;
	}
	if (!((formatTag == 1 && (bitsPerSample == 8 || bitsPerSample == 16)) ||
		(formatTag == 3 && bitsPerSample == 32)))
	{
		error = "WAV must contain PCM8, PCM16, or IEEE float32 samples.";
		return false;
	}

	const std::size_t bytesPerSample = bitsPerSample / 8;
	const std::size_t frameBytes = bytesPerSample * channels;
	if (frameBytes == 0 || audioBytes.size() % frameBytes != 0)
	{
		error = "WAV audio data is not frame aligned.";
		return false;
	}

	const std::size_t frameCount = audioBytes.size() / frameBytes;
	recording.sampleRate = sampleRate;
	recording.samples.reserve(frameCount);
	for (std::size_t frame = 0; frame < frameCount; ++frame)
	{
		float mono = 0.0f;
		for (std::size_t channel = 0; channel < channels; ++channel)
		{
			const unsigned char* sample = &audioBytes[(frame * channels + channel) * bytesPerSample];
			if (formatTag == 1 && bitsPerSample == 8)
				mono += NormalizePcm8(sample[0]);
			else if (formatTag == 1 && bitsPerSample == 16)
				mono += NormalizePcm16(static_cast<std::int16_t>(ReadLe16(sample)));
			else
			{
				float value = 0.0f;
				std::memcpy(&value, sample, sizeof(value));
				mono += ClampNormalized(value);
			}
		}
		recording.samples.push_back(ClampNormalized(mono / channels));
	}
	return true;
}

bool WriteWav16Mono(const std::string& path, const SignalRecording& recording, std::string& error)
{
	error.clear();
	if (recording.sampleRate == 0 || recording.samples.empty() ||
		recording.samples.size() > (std::numeric_limits<std::uint32_t>::max)() / 2u)
	{
		error = "Recording is empty or too large.";
		return false;
	}
	std::ofstream output(path.c_str(), std::ios::binary | std::ios::trunc);
	if (!output)
	{
		error = "Unable to create WAV file.";
		return false;
	}
	const std::uint32_t dataBytes = static_cast<std::uint32_t>(recording.samples.size() * 2u);
	output.write("RIFF", 4);
	WriteLe32(output, 36u + dataBytes);
	output.write("WAVEfmt ", 8);
	WriteLe32(output, 16);
	WriteLe16(output, 1);
	WriteLe16(output, 1);
	WriteLe32(output, recording.sampleRate);
	WriteLe32(output, recording.sampleRate * 2u);
	WriteLe16(output, 2);
	WriteLe16(output, 16);
	output.write("data", 4);
	WriteLe32(output, dataBytes);
	for (std::vector<float>::const_iterator sample = recording.samples.begin();
		sample != recording.samples.end(); ++sample)
	{
		const float clamped = ClampNormalized(*sample);
		const int scaled = static_cast<int>(std::floor(clamped * 32767.0f + (clamped >= 0 ? 0.5f : -0.5f)));
		WriteLe16(output, static_cast<std::uint16_t>(static_cast<std::int16_t>(scaled)));
	}
	if (!output)
	{
		error = "Unable to finish WAV file.";
		return false;
	}
	return true;
}

bool ReadSigMfReal32(const std::string& basePath, SignalRecording& recording, std::string& error)
{
	recording.samples.clear();
	error.clear();
	std::ifstream metadata((basePath + ".sigmf-meta").c_str(), std::ios::binary);
	if (!metadata)
	{
		error = "Unable to open SigMF metadata.";
		return false;
	}
	std::ostringstream metadataText;
	metadataText << metadata.rdbuf();
	if (metadataText.str().find("\"core:datatype\": \"rf32_le\"") == std::string::npos ||
		!ParseSampleRate(metadataText.str(), recording.sampleRate))
	{
		error = "SigMF recording must use rf32_le and declare core:sample_rate.";
		return false;
	}
	std::ifstream data((basePath + ".sigmf-data").c_str(), std::ios::binary | std::ios::ate);
	if (!data)
	{
		error = "Unable to open SigMF data.";
		return false;
	}
	const std::streamoff length = data.tellg();
	if (length <= 0 || length > MAX_RECORDING_BYTES || length % 4 != 0)
	{
		error = "SigMF data length is invalid.";
		return false;
	}
	data.seekg(0, std::ios::beg);
	recording.samples.resize(static_cast<std::size_t>(length / 4));
	if (!ReadExact(data, &recording.samples[0], static_cast<std::size_t>(length)))
	{
		error = "Unable to read SigMF data.";
		return false;
	}
	for (std::vector<float>::iterator sample = recording.samples.begin();
		sample != recording.samples.end(); ++sample)
		*sample = ClampNormalized(*sample);
	return true;
}

bool WriteSigMfReal32(const std::string& basePath, const SignalRecording& recording, std::string& error)
{
	error.clear();
	if (recording.sampleRate == 0 || recording.samples.empty())
	{
		error = "Recording is empty.";
		return false;
	}
	std::ofstream metadata((basePath + ".sigmf-meta").c_str(), std::ios::binary | std::ios::trunc);
	if (!metadata)
	{
		error = "Unable to create SigMF metadata.";
		return false;
	}
	metadata << "{\n"
		"  \"global\": {\n"
		"    \"core:datatype\": \"rf32_le\",\n"
		"    \"core:sample_rate\": " << recording.sampleRate << ",\n"
		"    \"core:version\": \"1.2.0\",\n"
		"    \"core:description\": \"PDW normalized discriminator audio\"\n"
		"  },\n"
		"  \"captures\": [{ \"core:sample_start\": 0 }],\n"
		"  \"annotations\": []\n"
		"}\n";
	if (!metadata)
	{
		error = "Unable to finish SigMF metadata.";
		return false;
	}
	std::ofstream data((basePath + ".sigmf-data").c_str(), std::ios::binary | std::ios::trunc);
	if (!data)
	{
		error = "Unable to create SigMF data.";
		return false;
	}
	for (std::vector<float>::const_iterator sample = recording.samples.begin();
		sample != recording.samples.end(); ++sample)
	{
		const float clamped = ClampNormalized(*sample);
		data.write(reinterpret_cast<const char*>(&clamped), sizeof(clamped));
	}
	if (!data)
	{
		error = "Unable to finish SigMF data.";
		return false;
	}
	return true;
}

} // namespace signal
} // namespace pdw
