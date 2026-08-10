#include <winsock2.h>
#include <ws2tcpip.h>

#include "rtl_tcp_source.h"

#include "audio_signal_core.h"

#include <cmath>
#include <cstring>

namespace pdw
{
namespace signal
{

namespace
{
const UINT_PTR INVALID_SOCKET_VALUE = static_cast<UINT_PTR>(INVALID_SOCKET);

bool StopRequested(HANDLE eventHandle)
{
	return eventHandle && WaitForSingleObject(eventHandle, 0) == WAIT_OBJECT_0;
}

bool ReceiveExact(SOCKET socketValue, unsigned char* destination, int bytes, HANDLE stopEvent)
{
	int received = 0;
	while (received < bytes && !StopRequested(stopEvent))
	{
		const int result = recv(socketValue, reinterpret_cast<char*>(destination + received), bytes - received, 0);
		if (result <= 0) return false;
		received += result;
	}
	return received == bytes;
}

bool SendCommand(SOCKET socketValue, unsigned char command, std::uint32_t value)
{
	unsigned char packet[5] = {
		command,
		static_cast<unsigned char>((value >> 24) & 0xff),
		static_cast<unsigned char>((value >> 16) & 0xff),
		static_cast<unsigned char>((value >> 8) & 0xff),
		static_cast<unsigned char>(value & 0xff)
	};
	int sent = 0;
	while (sent < 5)
	{
		const int result = send(socketValue, reinterpret_cast<const char*>(packet + sent), 5 - sent, 0);
		if (result <= 0) return false;
		sent += result;
	}
	return true;
}
}

RtlTcpConfig::RtlTcpConfig()
	: host("127.0.0.1"), port(1234), frequencyHz(148000000),
	  sampleRate(1024000), audioSampleRate(48000), gainTenthsDb(0),
	  frequencyCorrectionPpm(0), nfmBandwidthHz(12000), automaticGain(true)
{
}

RtlFmDemodulator::RtlFmDemodulator(std::uint32_t iqSampleRate,
	std::uint32_t audioSampleRate, std::uint32_t nfmBandwidthHz)
{
	Configure(iqSampleRate, audioSampleRate, nfmBandwidthHz);
}

void RtlFmDemodulator::Configure(std::uint32_t iqSampleRate, std::uint32_t audioSampleRate,
	std::uint32_t nfmBandwidthHz)
{
	iqSampleRate_ = iqSampleRate ? iqSampleRate : 1024000;
	audioSampleRate_ = audioSampleRate && audioSampleRate <= iqSampleRate_
		? audioSampleRate : 48000;
	nfmBandwidthHz_ = (std::max)(static_cast<std::uint32_t>(5000),
		(std::min)(static_cast<std::uint32_t>(25000), nfmBandwidthHz));
	const float cutoff = (std::min)(static_cast<float>(nfmBandwidthHz_),
		static_cast<float>(iqSampleRate_) * 0.45f);
	lowPassAlpha_ = 1.0f - std::exp(-2.0f * 3.14159265358979323846f * cutoff /
		static_cast<float>(iqSampleRate_));
	Reset();
}

void RtlFmDemodulator::Reset()
{
	resamplePhase_ = 0;
	previousI_ = 0.0f;
	previousQ_ = 0.0f;
	accumulator_ = 0.0f;
	accumulatorCount_ = 0;
	havePrevious_ = false;
	lowPassState_ = 0.0f;
}

void RtlFmDemodulator::ProcessUnsignedIq(const unsigned char* iqBytes,
	std::size_t byteCount,
	std::vector<float>& audio)
{
	audio.clear();
	if (!iqBytes || byteCount < 2) return;
	audio.reserve((byteCount / 2) * audioSampleRate_ / iqSampleRate_ + 2);
	for (std::size_t byte = 0; byte + 1 < byteCount; byte += 2)
	{
		const float currentI = (static_cast<int>(iqBytes[byte]) - 127.5f) / 128.0f;
		const float currentQ = (static_cast<int>(iqBytes[byte + 1]) - 127.5f) / 128.0f;
		if (havePrevious_)
		{
			const float cross = previousI_ * currentQ - previousQ_ * currentI;
			const float dot = previousI_ * currentI + previousQ_ * currentQ;
			const float discriminator = static_cast<float>(std::atan2(cross, dot) / 3.14159265358979323846);
			lowPassState_ += lowPassAlpha_ * (discriminator - lowPassState_);
			accumulator_ += lowPassState_;
			accumulatorCount_++;
			resamplePhase_ += audioSampleRate_;
			if (resamplePhase_ >= iqSampleRate_)
			{
				resamplePhase_ -= iqSampleRate_;
				audio.push_back(ClampNormalized(accumulator_ / accumulatorCount_));
				accumulator_ = 0.0f;
				accumulatorCount_ = 0;
			}
		}
		previousI_ = currentI;
		previousQ_ = currentQ;
		havePrevious_ = true;
	}
}

RtlTcpSource::RtlTcpSource()
	: stopEvent_(NULL), readyEvent_(NULL), thread_(NULL),
	  socketValue_(INVALID_SOCKET_VALUE), sink_(NULL), state_(RTL_TCP_STOPPED)
{
	InitializeCriticalSection(&lock_);
}

RtlTcpSource::~RtlTcpSource()
{
	Stop();
	DeleteCriticalSection(&lock_);
}

bool RtlTcpSource::Start(const RtlTcpConfig& config, AudioSampleSink* sink)
{
	if (!sink || config.host.empty() || config.port == 0 || config.sampleRate < 48000)
		return false;
	Stop();
	config_ = config;
	sink_ = sink;
	stopEvent_ = CreateEvent(NULL, TRUE, FALSE, NULL);
	readyEvent_ = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!stopEvent_ || !readyEvent_)
	{
		SetState(RTL_TCP_FAILED, "Unable to create RTL-TCP synchronization events.");
		Stop();
		return false;
	}
	SetState(RTL_TCP_CONNECTING, NULL);
	thread_ = CreateThread(NULL, 0, ThreadEntry, this, 0, NULL);
	if (!thread_)
	{
		SetState(RTL_TCP_FAILED, "Unable to create the RTL-TCP network thread.");
		Stop();
		return false;
	}
	if (WaitForSingleObject(readyEvent_, 5000) != WAIT_OBJECT_0)
	{
		SetState(RTL_TCP_FAILED, "RTL-TCP did not connect in time.");
		Stop();
		return false;
	}
	return state() == RTL_TCP_RUNNING;
}

void RtlTcpSource::Stop()
{
	if (stopEvent_) SetEvent(stopEvent_);
	if (socketValue_ != INVALID_SOCKET_VALUE)
		shutdown(static_cast<SOCKET>(socketValue_), SD_BOTH);
	if (thread_)
	{
		WaitForSingleObject(thread_, 5000);
		CloseHandle(thread_);
		thread_ = NULL;
	}
	if (readyEvent_) { CloseHandle(readyEvent_); readyEvent_ = NULL; }
	if (stopEvent_) { CloseHandle(stopEvent_); stopEvent_ = NULL; }
	socketValue_ = INVALID_SOCKET_VALUE;
	sink_ = NULL;
	if (state() != RTL_TCP_FAILED) SetState(RTL_TCP_STOPPED, NULL);
}

RtlTcpState RtlTcpSource::state() const
{
	EnterCriticalSection(&lock_);
	const RtlTcpState value = state_;
	LeaveCriticalSection(&lock_);
	return value;
}

std::string RtlTcpSource::lastError() const
{
	EnterCriticalSection(&lock_);
	const std::string value = lastError_;
	LeaveCriticalSection(&lock_);
	return value;
}

void RtlTcpSource::SetState(RtlTcpState state, const char* error)
{
	EnterCriticalSection(&lock_);
	state_ = state;
	if (error) lastError_ = error;
	else if (state == RTL_TCP_CONNECTING || state == RTL_TCP_RUNNING || state == RTL_TCP_STOPPED)
		lastError_.clear();
	LeaveCriticalSection(&lock_);
}

DWORD WINAPI RtlTcpSource::ThreadEntry(LPVOID context)
{
	return static_cast<RtlTcpSource*>(context)->NetworkThread();
}

DWORD RtlTcpSource::NetworkThread()
{
	WSADATA winsock = {};
	if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0)
	{
		SetState(RTL_TCP_FAILED, "Windows networking could not initialize.");
		SetEvent(readyEvent_);
		return 1;
	}
	bool firstConnection = true;
	while (!StopRequested(stopEvent_))
	{
		ConnectAndReceive();
		if (StopRequested(stopEvent_)) break;
		if (firstConnection && state() != RTL_TCP_RUNNING) break;
		firstConnection = false;
		SetState(RTL_TCP_RECONNECTING, "RTL-TCP connection ended; reconnecting.");
		if (WaitForSingleObject(stopEvent_, 2000) != WAIT_TIMEOUT) break;
	}
	WSACleanup();
	return 0;
}

bool RtlTcpSource::ConnectAndReceive()
{
	char portText[16];
	snprintf(portText, sizeof(portText), "%u", static_cast<unsigned int>(config_.port));
	addrinfo hints = {};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	addrinfo* addresses = NULL;
	if (getaddrinfo(config_.host.c_str(), portText, &hints, &addresses) != 0)
	{
		SetState(RTL_TCP_FAILED, "RTL-TCP host name could not be resolved.");
		SetEvent(readyEvent_);
		return false;
	}

	SOCKET socketValue = INVALID_SOCKET;
	for (addrinfo* address = addresses; address && socketValue == INVALID_SOCKET; address = address->ai_next)
	{
		SOCKET candidate = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
		if (candidate == INVALID_SOCKET) continue;
		DWORD timeout = 2000;
		setsockopt(candidate, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
		setsockopt(candidate, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
		if (connect(candidate, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0)
			socketValue = candidate;
		else
			closesocket(candidate);
	}
	freeaddrinfo(addresses);
	if (socketValue == INVALID_SOCKET)
	{
		SetState(RTL_TCP_FAILED, "Unable to connect to the RTL-TCP server.");
		SetEvent(readyEvent_);
		return false;
	}
	socketValue_ = static_cast<UINT_PTR>(socketValue);

	unsigned char dongleInfo[12];
	if (!ReceiveExact(socketValue, dongleInfo, sizeof(dongleInfo), stopEvent_) ||
		std::memcmp(dongleInfo, "RTL0", 4) != 0)
	{
		SetState(RTL_TCP_FAILED, "The server did not provide a valid RTL-TCP header.");
		SetEvent(readyEvent_);
		closesocket(socketValue);
		socketValue_ = INVALID_SOCKET_VALUE;
		return false;
	}

	const bool configured =
		SendCommand(socketValue, 0x01, config_.frequencyHz) &&
		SendCommand(socketValue, 0x02, config_.sampleRate) &&
		SendCommand(socketValue, 0x03, config_.automaticGain ? 0u : 1u) &&
		(config_.automaticGain || SendCommand(socketValue, 0x04,
			static_cast<std::uint32_t>(config_.gainTenthsDb))) &&
		SendCommand(socketValue, 0x05, static_cast<std::uint32_t>(config_.frequencyCorrectionPpm));
	if (!configured)
	{
		SetState(RTL_TCP_FAILED, "RTL-TCP rejected the tuner configuration.");
		SetEvent(readyEvent_);
		closesocket(socketValue);
		socketValue_ = INVALID_SOCKET_VALUE;
		return false;
	}

	SetState(RTL_TCP_RUNNING, NULL);
	SetEvent(readyEvent_);
	RtlFmDemodulator demodulator(config_.sampleRate, config_.audioSampleRate,
		config_.nfmBandwidthHz);
	std::vector<unsigned char> iqBytes(32768);
	std::vector<float> audio;
	bool discontinuity = true;
	while (!StopRequested(stopEvent_))
	{
		const int bytes = recv(socketValue, reinterpret_cast<char*>(&iqBytes[0]),
			static_cast<int>(iqBytes.size()), 0);
		if (bytes > 0)
		{
			demodulator.ProcessUnsignedIq(&iqBytes[0], static_cast<std::size_t>(bytes), audio);
			if (sink_ && !audio.empty())
				sink_->OnAudioSamples(&audio[0], audio.size(), config_.audioSampleRate, discontinuity);
			discontinuity = false;
		}
		else if (bytes == 0)
		{
			break;
		}
		else if (WSAGetLastError() != WSAETIMEDOUT)
		{
			break;
		}
	}
	closesocket(socketValue);
	socketValue_ = INVALID_SOCKET_VALUE;
	return StopRequested(stopEvent_);
}

namespace
{
	typedef int (__cdecl *RtlOpenFunction)(void**, std::uint32_t);
	typedef std::uint32_t (__cdecl *RtlDeviceCountFunction)();
	typedef int (__cdecl *RtlCloseFunction)(void*);
	typedef int (__cdecl *RtlSetUnsignedFunction)(void*, std::uint32_t);
	typedef int (__cdecl *RtlSetIntegerFunction)(void*, int);
	typedef int (__cdecl *RtlReadAsyncFunction)(void*, void (__cdecl *)(unsigned char*, std::uint32_t, void*),
		void*, std::uint32_t, std::uint32_t);
	typedef int (__cdecl *RtlCancelAsyncFunction)(void*);

	HMODULE LoadRtlLibrary(const std::string& configuredPath)
	{
		if (!configuredPath.empty())
		{
			HMODULE configured = LoadLibraryExA(configuredPath.c_str(), NULL,
				LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
			if (!configured && GetLastError() == ERROR_INVALID_PARAMETER)
				configured = LoadLibraryExA(configuredPath.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
			return configured;
		}
		const char* names[] = { "rtlsdr.dll", "librtlsdr.dll" };
		for (std::size_t index = 0; index < sizeof(names) / sizeof(names[0]); ++index)
		{
			HMODULE library = LoadLibraryExA(names[index], NULL,
				LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
			if (library) return library;
		}
		return NULL;
	}
}

RtlSdrSource::RtlSdrSource()
	: stopEvent_(NULL), readyEvent_(NULL), thread_(NULL), library_(NULL),
	  device_(NULL), cancelFunction_(NULL), deviceIndex_(0), sink_(NULL),
	  demodulator_(1024000, 48000), state_(RTL_TCP_STOPPED)
{
	InitializeCriticalSection(&lock_);
}

RtlSdrSource::~RtlSdrSource()
{
	Stop();
	DeleteCriticalSection(&lock_);
}

bool RtlSdrSource::Start(const RtlTcpConfig& config, unsigned int deviceIndex, AudioSampleSink* sink)
{
	if (!sink || config.sampleRate < 48000) return false;
	Stop();
	config_ = config;
	deviceIndex_ = deviceIndex;
	sink_ = sink;
	demodulator_.Configure(config.sampleRate, config.audioSampleRate, config.nfmBandwidthHz);
	stopEvent_ = CreateEvent(NULL, TRUE, FALSE, NULL);
	readyEvent_ = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!stopEvent_ || !readyEvent_)
	{
		SetState(RTL_TCP_FAILED, "Unable to create RTL-SDR synchronization events.");
		Stop();
		return false;
	}
	SetState(RTL_TCP_CONNECTING, NULL);
	thread_ = CreateThread(NULL, 0, ThreadEntry, this, 0, NULL);
	if (!thread_)
	{
		SetState(RTL_TCP_FAILED, "Unable to create the RTL-SDR capture thread.");
		Stop();
		return false;
	}
	if (WaitForSingleObject(readyEvent_, 5000) != WAIT_OBJECT_0)
	{
		SetState(RTL_TCP_FAILED, "RTL-SDR did not start in time.");
		Stop();
		return false;
	}
	return state() == RTL_TCP_RUNNING;
}

void RtlSdrSource::Stop()
{
	if (stopEvent_) SetEvent(stopEvent_);
	EnterCriticalSection(&lock_);
	RtlCancelAsyncFunction cancel = reinterpret_cast<RtlCancelAsyncFunction>(cancelFunction_);
	void* device = device_;
	LeaveCriticalSection(&lock_);
	if (cancel && device) cancel(device);
	if (thread_)
	{
		WaitForSingleObject(thread_, 5000);
		CloseHandle(thread_);
		thread_ = NULL;
	}
	if (readyEvent_) { CloseHandle(readyEvent_); readyEvent_ = NULL; }
	if (stopEvent_) { CloseHandle(stopEvent_); stopEvent_ = NULL; }
	sink_ = NULL;
	if (state() != RTL_TCP_FAILED) SetState(RTL_TCP_STOPPED, NULL);
}

RtlTcpState RtlSdrSource::state() const
{
	EnterCriticalSection(&lock_);
	const RtlTcpState value = state_;
	LeaveCriticalSection(&lock_);
	return value;
}

std::string RtlSdrSource::lastError() const
{
	EnterCriticalSection(&lock_);
	const std::string value = lastError_;
	LeaveCriticalSection(&lock_);
	return value;
}

void RtlSdrSource::SetState(RtlTcpState state, const char* error)
{
	EnterCriticalSection(&lock_);
	state_ = state;
	if (error) lastError_ = error;
	else if (state == RTL_TCP_CONNECTING || state == RTL_TCP_RUNNING || state == RTL_TCP_STOPPED)
		lastError_.clear();
	LeaveCriticalSection(&lock_);
}

DWORD WINAPI RtlSdrSource::ThreadEntry(LPVOID context)
{
	return static_cast<RtlSdrSource*>(context)->DeviceThread();
}

void __cdecl RtlSdrSource::ReadCallback(unsigned char* buffer, std::uint32_t length, void* context)
{
	RtlSdrSource* source = static_cast<RtlSdrSource*>(context);
	if (!source || StopRequested(source->stopEvent_)) return;
	std::vector<float> audio;
	source->demodulator_.ProcessUnsignedIq(buffer, length, audio);
	if (source->sink_ && !audio.empty())
		source->sink_->OnAudioSamples(&audio[0], audio.size(), source->config_.audioSampleRate, false);
}

DWORD RtlSdrSource::DeviceThread()
{
	library_ = LoadRtlLibrary(config_.receiverLibraryPath);
	if (!library_)
	{
		SetState(RTL_TCP_FAILED, "Compatible rtlsdr.dll or librtlsdr.dll was not found.");
		SetEvent(readyEvent_);
		return 1;
	}

#define PDW_RTL_FUNCTION(type, name) reinterpret_cast<type>(GetProcAddress(library_, name))
	RtlOpenFunction openDevice = PDW_RTL_FUNCTION(RtlOpenFunction, "rtlsdr_open");
	RtlDeviceCountFunction getDeviceCount = PDW_RTL_FUNCTION(RtlDeviceCountFunction, "rtlsdr_get_device_count");
	RtlCloseFunction closeDevice = PDW_RTL_FUNCTION(RtlCloseFunction, "rtlsdr_close");
	RtlSetUnsignedFunction setFrequency = PDW_RTL_FUNCTION(RtlSetUnsignedFunction, "rtlsdr_set_center_freq");
	RtlSetUnsignedFunction setSampleRate = PDW_RTL_FUNCTION(RtlSetUnsignedFunction, "rtlsdr_set_sample_rate");
	RtlSetIntegerFunction setGainMode = PDW_RTL_FUNCTION(RtlSetIntegerFunction, "rtlsdr_set_tuner_gain_mode");
	RtlSetIntegerFunction setGain = PDW_RTL_FUNCTION(RtlSetIntegerFunction, "rtlsdr_set_tuner_gain");
	RtlSetIntegerFunction setCorrection = PDW_RTL_FUNCTION(RtlSetIntegerFunction, "rtlsdr_set_freq_correction");
	RtlCloseFunction resetBuffer = PDW_RTL_FUNCTION(RtlCloseFunction, "rtlsdr_reset_buffer");
	RtlReadAsyncFunction readAsync = PDW_RTL_FUNCTION(RtlReadAsyncFunction, "rtlsdr_read_async");
	RtlCancelAsyncFunction cancelAsync = PDW_RTL_FUNCTION(RtlCancelAsyncFunction, "rtlsdr_cancel_async");
#undef PDW_RTL_FUNCTION

	if (!getDeviceCount || !openDevice || !closeDevice || !setFrequency || !setSampleRate || !setGainMode ||
		!setGain || !setCorrection || !resetBuffer || !readAsync || !cancelAsync)
	{
		SetState(RTL_TCP_FAILED, "RTL-SDR DLL is missing required API functions.");
		SetEvent(readyEvent_);
		FreeLibrary(library_);
		library_ = NULL;
		return 1;
	}
	const std::uint32_t deviceCount = getDeviceCount();
	if (deviceCount == 0 || deviceIndex_ >= deviceCount)
	{
		SetState(RTL_TCP_FAILED, deviceCount == 0 ?
			"No compatible RTL-SDR USB receiver was detected. Connect it and install WinUSB for that receiver if required." :
			"The selected RTL-SDR USB device index is not available. Choose a connected receiver from the list.");
		SetEvent(readyEvent_);
		FreeLibrary(library_);
		library_ = NULL;
		return 1;
	}

	void* device = NULL;
	int result = openDevice(&device, deviceIndex_);
	const bool deviceOpened = result == 0 && device != NULL;
	if (result == 0) result = setSampleRate(device, config_.sampleRate);
	if (result == 0) result = setFrequency(device, config_.frequencyHz);
	if (result == 0) result = setCorrection(device, config_.frequencyCorrectionPpm);
	if (result == 0) result = setGainMode(device, config_.automaticGain ? 0 : 1);
	if (result == 0 && !config_.automaticGain) result = setGain(device, config_.gainTenthsDb);
	if (result == 0) result = resetBuffer(device);
	if (result != 0)
	{
		if (device) closeDevice(device);
		char configurationError[256];
		if (deviceOpened)
			snprintf(configurationError, sizeof(configurationError),
				"RTL-SDR receiver opened but rejected a tuner setting (error %d). Check frequency, IQ rate, gain and PPM.", result);
		else
			snprintf(configurationError, sizeof(configurationError),
				"RTL-SDR receiver could not be opened (error %d). Check its WinUSB driver and close other SDR programs using it.", result);
		SetState(RTL_TCP_FAILED, configurationError);
		SetEvent(readyEvent_);
		FreeLibrary(library_);
		library_ = NULL;
		return 1;
	}

	EnterCriticalSection(&lock_);
	device_ = device;
	cancelFunction_ = reinterpret_cast<void*>(cancelAsync);
	LeaveCriticalSection(&lock_);
	SetState(RTL_TCP_RUNNING, NULL);
	SetEvent(readyEvent_);
	readAsync(device, ReadCallback, this, 0, 32768);

	EnterCriticalSection(&lock_);
	device_ = NULL;
	cancelFunction_ = NULL;
	LeaveCriticalSection(&lock_);
	closeDevice(device);
	FreeLibrary(library_);
	library_ = NULL;
	if (!StopRequested(stopEvent_)) SetState(RTL_TCP_FAILED, "RTL-SDR capture ended unexpectedly.");
	return 0;
}

bool IsRtlSdrLibraryAvailable(const std::string& libraryPath, std::string* loadedPath)
{
	HMODULE library = LoadRtlLibrary(libraryPath);
	if (library)
	{
		const bool compatible = GetProcAddress(library, "rtlsdr_get_device_count") != NULL &&
			GetProcAddress(library, "rtlsdr_open") != NULL &&
			GetProcAddress(library, "rtlsdr_read_async") != NULL;
		if (compatible && loadedPath) *loadedPath = libraryPath.empty() ?
			"RTL-SDR application DLL" : libraryPath;
		FreeLibrary(library);
		if (compatible) return true;
	}
	return false;
}

} // namespace signal
} // namespace pdw
