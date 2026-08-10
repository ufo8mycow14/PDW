#ifndef PDW_RTL_TCP_SOURCE_H
#define PDW_RTL_TCP_SOURCE_H

#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "audio_capture_core.h"

namespace pdw
{
namespace signal
{

struct RtlTcpConfig
{
	std::string host;
	std::uint16_t port;
	std::uint32_t frequencyHz;
	std::uint32_t sampleRate;
	std::uint32_t audioSampleRate;
	int gainTenthsDb;
	int frequencyCorrectionPpm;
	std::uint32_t nfmBandwidthHz;
	bool automaticGain;
	std::string receiverLibraryPath;

	RtlTcpConfig();
};

class RtlFmDemodulator
{
public:
	RtlFmDemodulator(std::uint32_t iqSampleRate = 1024000,
		std::uint32_t audioSampleRate = 48000,
		std::uint32_t nfmBandwidthHz = 12000);

	void Reset();
	void Configure(std::uint32_t iqSampleRate, std::uint32_t audioSampleRate,
		std::uint32_t nfmBandwidthHz = 12000);
	void ProcessUnsignedIq(const unsigned char* iqBytes,
		std::size_t byteCount,
		std::vector<float>& audio);

private:
	std::uint32_t iqSampleRate_;
	std::uint32_t audioSampleRate_;
	std::uint64_t resamplePhase_;
	float previousI_;
	float previousQ_;
	float accumulator_;
	unsigned int accumulatorCount_;
	bool havePrevious_;
	std::uint32_t nfmBandwidthHz_;
	float lowPassAlpha_;
	float lowPassState_;
};

enum RtlTcpState
{
	RTL_TCP_STOPPED,
	RTL_TCP_CONNECTING,
	RTL_TCP_RUNNING,
	RTL_TCP_RECONNECTING,
	RTL_TCP_FAILED
};

class RtlTcpSource
{
public:
	RtlTcpSource();
	~RtlTcpSource();

	bool Start(const RtlTcpConfig& config, AudioSampleSink* sink);
	void Stop();
	RtlTcpState state() const;
	std::string lastError() const;

private:
	RtlTcpSource(const RtlTcpSource&);
	RtlTcpSource& operator=(const RtlTcpSource&);

	static DWORD WINAPI ThreadEntry(LPVOID context);
	DWORD NetworkThread();
	bool ConnectAndReceive();
	void SetState(RtlTcpState state, const char* error);

	mutable CRITICAL_SECTION lock_;
	HANDLE stopEvent_;
	HANDLE readyEvent_;
	HANDLE thread_;
	UINT_PTR socketValue_;
	RtlTcpConfig config_;
	AudioSampleSink* sink_;
	RtlTcpState state_;
	std::string lastError_;
};

class RtlSdrSource
{
public:
	RtlSdrSource();
	~RtlSdrSource();

	bool Start(const RtlTcpConfig& config, unsigned int deviceIndex, AudioSampleSink* sink);
	void Stop();
	RtlTcpState state() const;
	std::string lastError() const;

private:
	RtlSdrSource(const RtlSdrSource&);
	RtlSdrSource& operator=(const RtlSdrSource&);

	static DWORD WINAPI ThreadEntry(LPVOID context);
	static void __cdecl ReadCallback(unsigned char* buffer, std::uint32_t length, void* context);
	DWORD DeviceThread();
	void SetState(RtlTcpState state, const char* error);

	mutable CRITICAL_SECTION lock_;
	HANDLE stopEvent_;
	HANDLE readyEvent_;
	HANDLE thread_;
	HMODULE library_;
	void* device_;
	void* cancelFunction_;
	RtlTcpConfig config_;
	unsigned int deviceIndex_;
	AudioSampleSink* sink_;
	RtlFmDemodulator demodulator_;
	RtlTcpState state_;
	std::string lastError_;
};

// Optional in-process support is loaded dynamically so absence of rtlsdr.dll
// cannot prevent PDW or any legacy input from starting.
bool IsRtlSdrLibraryAvailable(const std::string& libraryPath = std::string(),
	std::string* loadedPath = NULL);

} // namespace signal
} // namespace pdw

#endif
