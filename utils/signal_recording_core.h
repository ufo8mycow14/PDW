#ifndef PDW_SIGNAL_RECORDING_CORE_H
#define PDW_SIGNAL_RECORDING_CORE_H

#include <cstdint>
#include <string>
#include <vector>

namespace pdw
{
namespace signal
{

struct SignalRecording
{
	std::uint32_t sampleRate;
	std::vector<float> samples;
};

bool ReadWavMono(const std::string& path, SignalRecording& recording, std::string& error);
bool WriteWav16Mono(const std::string& path, const SignalRecording& recording, std::string& error);

// basePath omits the .sigmf-meta and .sigmf-data suffixes.
bool ReadSigMfReal32(const std::string& basePath, SignalRecording& recording, std::string& error);
bool WriteSigMfReal32(const std::string& basePath, const SignalRecording& recording, std::string& error);

} // namespace signal
} // namespace pdw

#endif
