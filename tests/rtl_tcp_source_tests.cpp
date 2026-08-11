#include <winsock2.h>
#include <ws2tcpip.h>

#include "rtl_tcp_source.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
volatile LONG g_injectedWaitCalls = 0;

DWORD WINAPI ReturnWaitTimeout(HANDLE, DWORD)
{
	InterlockedIncrement(&g_injectedWaitCalls);
	return WAIT_TIMEOUT;
}

DWORD WINAPI ReturnWaitFailed(HANDLE, DWORD)
{
	InterlockedIncrement(&g_injectedWaitCalls);
	SetLastError(ERROR_INVALID_HANDLE);
	return WAIT_FAILED;
}

class CollectingSink : public pdw::signal::AudioSampleSink
{
public:
	CollectingSink() : callbacks(0), samples(0) {}
	void OnAudioSamples(const float*, std::size_t sampleCount, std::uint32_t, bool)
	{
		InterlockedIncrement(&callbacks);
		samples += sampleCount;
	}
	volatile LONG callbacks;
	std::size_t samples;
};

struct RtlSdrStopThreadContext
{
	pdw::signal::RtlSdrSource* source;
	HANDLE done;
	bool result;

	RtlSdrStopThreadContext() : source(NULL), done(NULL), result(false) {}
};

DWORD WINAPI RtlSdrStopThread(LPVOID context)
{
	RtlSdrStopThreadContext* stop = static_cast<RtlSdrStopThreadContext*>(context);
	stop->result = stop->source->Stop();
	SetEvent(stop->done);
	return 0;
}

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		std::exit(1);
	}
}

struct RtlTcpLoopbackServer
{
	SOCKET listener;

	RtlTcpLoopbackServer() : listener(INVALID_SOCKET) {}
};

bool SendAll(SOCKET socketValue, const unsigned char* bytes, int byteCount)
{
	int sent = 0;
	while (sent < byteCount)
	{
		const int result = send(socketValue,
			reinterpret_cast<const char*>(bytes + sent), byteCount - sent, 0);
		if (result <= 0) return false;
		sent += result;
	}
	return true;
}

DWORD WINAPI RtlTcpLoopbackServerThread(LPVOID context)
{
	RtlTcpLoopbackServer* server = static_cast<RtlTcpLoopbackServer*>(context);
	const SOCKET client = accept(server->listener, NULL, NULL);
	if (client == INVALID_SOCKET) return 1;
	const unsigned char header[12] = {
		'R', 'T', 'L', '0', 0, 0, 0, 1, 0, 0, 0, 0
	};
	if (!SendAll(client, header, static_cast<int>(sizeof(header))))
	{
		closesocket(client);
		return 1;
	}
	DWORD timeout = 2000;
	setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
		reinterpret_cast<const char*>(&timeout), sizeof(timeout));
	char commands[64];
	while (recv(client, commands, static_cast<int>(sizeof(commands)), 0) > 0)
	{
	}
	closesocket(client);
	return 0;
}

void ExerciseRtlTcpStopQuarantine()
{
	using namespace pdw::signal;
	WSADATA sockets = {};
	Expect(WSAStartup(MAKEWORD(2, 2), &sockets) == 0,
		"RTL-TCP lifecycle test initializes Windows sockets");

	RtlTcpLoopbackServer server;
	server.listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	Expect(server.listener != INVALID_SOCKET,
		"RTL-TCP lifecycle test creates its loopback listener");
	sockaddr_in address = {};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = 0;
	Expect(bind(server.listener, reinterpret_cast<const sockaddr*>(&address),
		static_cast<int>(sizeof(address))) == 0,
		"RTL-TCP lifecycle test binds its loopback listener");
	Expect(listen(server.listener, 1) == 0,
		"RTL-TCP lifecycle test listens on loopback");
	int addressLength = static_cast<int>(sizeof(address));
	Expect(getsockname(server.listener, reinterpret_cast<sockaddr*>(&address),
		&addressLength) == 0,
		"RTL-TCP lifecycle test resolves its allocated port");
	HANDLE serverThread = CreateThread(NULL, 0, RtlTcpLoopbackServerThread,
		&server, 0, NULL);
	Expect(serverThread != NULL,
		"RTL-TCP lifecycle test starts its loopback server");

	RtlTcpConfig config;
	config.host = "127.0.0.1";
	config.port = ntohs(address.sin_port);
	CollectingSink sink;
	RtlTcpSource source;
	Expect(source.Stop(), "an idle RTL-TCP source stops successfully");
	Expect(source.Start(config, &sink), "RTL-TCP source starts against loopback");

	InterlockedExchange(&g_injectedWaitCalls, 0);
	SetRtlStopThreadWaitFunctionForTesting(ReturnWaitTimeout);
	Expect(!source.Stop(), "RTL-TCP stop reports an injected thread timeout");
	Expect(source.state() == RTL_TCP_FAILED,
		"RTL-TCP timeout moves the source to failed quarantine");
	Expect(source.lastError().find("quarantined") != std::string::npos,
		"RTL-TCP timeout retains an actionable quarantine error");
	Expect(!source.Start(config, &sink),
		"RTL-TCP source cannot be reused before a confirmed thread exit");
	Expect(InterlockedCompareExchange(&g_injectedWaitCalls, 0, 0) >= 2,
		"RTL-TCP restart rechecks the quarantined thread");

	source.FinalizeForShutdown();
	Expect(source.Stop(),
		"RTL-TCP final shutdown join releases quarantined resources");
	SetRtlStopThreadWaitFunctionForTesting(NULL);
	Expect(WaitForSingleObject(serverThread, 5000) == WAIT_OBJECT_0,
		"RTL-TCP loopback server observes source shutdown");
	DWORD serverExitCode = 1;
	Expect(GetExitCodeThread(serverThread, &serverExitCode) != FALSE &&
		serverExitCode == 0,
		"RTL-TCP loopback server completed without an error");
	CloseHandle(serverThread);
	closesocket(server.listener);
	WSACleanup();
}

void ExerciseRtlSdrCancellationOwnership(pdw::signal::RtlSdrSource& source,
	const char* mockLibraryPath)
{
	typedef void (__cdecl *MockControlFunction)();
	typedef LONG (__cdecl *MockStateFunction)();
	HMODULE controlLibrary = LoadLibraryA(mockLibraryPath);
	Expect(controlLibrary != NULL,
		"RTL-SDR lifecycle test pins its mock control library");
	MockControlFunction blockCancel = reinterpret_cast<MockControlFunction>(
		GetProcAddress(controlLibrary, "rtlsdr_test_block_cancel_return"));
	MockControlFunction allowCancel = reinterpret_cast<MockControlFunction>(
		GetProcAddress(controlLibrary, "rtlsdr_test_allow_cancel_return"));
	MockStateFunction cancelEntered = reinterpret_cast<MockStateFunction>(
		GetProcAddress(controlLibrary, "rtlsdr_test_cancel_entered"));
	MockStateFunction readAsyncReturning = reinterpret_cast<MockStateFunction>(
		GetProcAddress(controlLibrary, "rtlsdr_test_read_async_returning"));
	MockStateFunction closeCount = reinterpret_cast<MockStateFunction>(
		GetProcAddress(controlLibrary, "rtlsdr_test_close_count"));
	Expect(blockCancel && allowCancel && cancelEntered && readAsyncReturning && closeCount,
		"RTL-SDR lifecycle mock exposes cancellation ownership controls");

	blockCancel();
	RtlSdrStopThreadContext stop;
	stop.source = &source;
	stop.done = CreateEvent(NULL, TRUE, FALSE, NULL);
	Expect(stop.done != NULL,
		"RTL-SDR lifecycle test creates its stop completion event");
	HANDLE stopThread = CreateThread(NULL, 0, RtlSdrStopThread, &stop, 0, NULL);
	Expect(stopThread != NULL,
		"RTL-SDR lifecycle test starts its stop worker");

	const DWORD waitStarted = GetTickCount();
	while (!cancelEntered() && GetTickCount() - waitStarted < 2000) Sleep(1);
	Expect(cancelEntered() != 0,
		"RTL-SDR cancellation entered the injected blocking call");
	while (!readAsyncReturning() && GetTickCount() - waitStarted < 2000) Sleep(1);
	Expect(readAsyncReturning() != 0,
		"RTL-SDR device worker reached its post-capture cleanup boundary");
	Expect(closeCount() == 0,
		"RTL-SDR device cannot close while cancellation still owns its DLL call");

	allowCancel();
	Expect(WaitForSingleObject(stop.done, 5000) == WAIT_OBJECT_0,
		"RTL-SDR stop completes after cancellation ownership is released");
	Expect(WaitForSingleObject(stopThread, 5000) == WAIT_OBJECT_0 && stop.result,
		"RTL-SDR stop confirms the device thread exited");
	Expect(closeCount() == 1,
		"RTL-SDR device closes exactly once after cancellation returns");
	CloseHandle(stopThread);
	CloseHandle(stop.done);
	FreeLibrary(controlLibrary);
}

float ClampNormalizedForReference(float value)
{
	if (value < -1.0f) return -1.0f;
	if (value > 1.0f) return 1.0f;
	return value;
}

std::vector<float> LegacyReferenceDemodulate(const std::vector<unsigned char>& iq,
	std::uint32_t iqRate, std::uint32_t audioRate, std::uint32_t cutoffHz)
{
	const double pi = 3.14159265358979323846;
	const float cutoff = (std::min)(static_cast<float>(cutoffHz),
		static_cast<float>(iqRate) * 0.45f);
	const float alpha = 1.0f - std::exp(-2.0f * static_cast<float>(pi) * cutoff /
		static_cast<float>(iqRate));
	std::vector<float> output;
	std::uint64_t phase = 0;
	float previousI = 0.0f;
	float previousQ = 0.0f;
	float accumulator = 0.0f;
	unsigned int accumulatorCount = 0;
	float lowPassState = 0.0f;
	bool havePrevious = false;
	for (std::size_t byte = 0; byte + 1 < iq.size(); byte += 2)
	{
		const float currentI = (static_cast<int>(iq[byte]) - 127.5f) / 128.0f;
		const float currentQ = (static_cast<int>(iq[byte + 1]) - 127.5f) / 128.0f;
		if (havePrevious)
		{
			const float cross = previousI * currentQ - previousQ * currentI;
			const float dot = previousI * currentI + previousQ * currentQ;
			const float discriminator = static_cast<float>(std::atan2(cross, dot) / pi);
			lowPassState += alpha * (discriminator - lowPassState);
			accumulator += lowPassState;
			++accumulatorCount;
			phase += audioRate;
			if (phase >= iqRate)
			{
				phase -= iqRate;
				output.push_back(ClampNormalizedForReference(
					accumulator / accumulatorCount));
				accumulator = 0.0f;
				accumulatorCount = 0;
			}
		}
		previousI = currentI;
		previousQ = currentQ;
		havePrevious = true;
	}
	return output;
}

unsigned char QuantizeIq(double value)
{
	int quantized = static_cast<int>(std::floor(127.5 + value * 120.0 + 0.5));
	if (quantized < 0) quantized = 0;
	if (quantized > 255) quantized = 255;
	return static_cast<unsigned char>(quantized);
}

std::vector<unsigned char> GenerateFskWithInterferer(std::uint32_t sampleRate,
	std::size_t sampleCount, double baud, double deviationHz,
	double wantedAmplitude, double interfererHz, double interfererAmplitude)
{
	const double twoPi = 6.28318530717958647692;
	std::vector<unsigned char> iq;
	iq.reserve(sampleCount * 2);
	double wantedPhase = 0.0;
	double interfererPhase = 0.0;
	for (std::size_t index = 0; index < sampleCount; ++index)
	{
		const std::uint64_t symbol = static_cast<std::uint64_t>(
			static_cast<double>(index) * baud / sampleRate);
		const double wantedFrequency = (symbol & 1U) ? deviationHz : -deviationHz;
		wantedPhase += twoPi * wantedFrequency / sampleRate;
		interfererPhase += twoPi * interfererHz / sampleRate;
		const double i = wantedAmplitude * std::cos(wantedPhase) +
			interfererAmplitude * std::cos(interfererPhase);
		const double q = wantedAmplitude * std::sin(wantedPhase) +
			interfererAmplitude * std::sin(interfererPhase);
		iq.push_back(QuantizeIq(i));
		iq.push_back(QuantizeIq(q));
	}
	return iq;
}

double FskSignAccuracy(const std::vector<float>& audio,
	std::uint32_t sampleRate, double baud)
{
	std::size_t correct = 0;
	std::size_t considered = 0;
	for (std::size_t index = 1000; index < audio.size(); ++index)
	{
		const double symbolPosition = static_cast<double>(index) * baud / sampleRate;
		const double fraction = symbolPosition - std::floor(symbolPosition);
		if (fraction < 0.2 || fraction > 0.8) continue;
		const std::uint64_t symbol = static_cast<std::uint64_t>(symbolPosition);
		const bool expectedPositive = (symbol & 1U) != 0;
		if ((audio[index] >= 0.0f) == expectedPositive) ++correct;
		++considered;
	}
	return considered ? static_cast<double>(correct) / considered : 0.0;
}
}

int main(int argc, char** argv)
{
	using namespace pdw::signal;
	Expect(RtlThreadResourcesMayBeReleased(WAIT_OBJECT_0),
		"a signalled RTL source thread may be torn down");
	Expect(!RtlThreadResourcesMayBeReleased(WAIT_TIMEOUT),
		"a timed-out RTL source thread must remain quarantined");
	Expect(!RtlThreadResourcesMayBeReleased(WAIT_FAILED),
		"a failed RTL source wait must remain quarantined");
	ExerciseRtlTcpStopQuarantine();

	const std::uint32_t iqRate = 960000;
	const std::uint32_t audioRate = 48000;
	RtlFmDemodulator demodulator(iqRate, audioRate);
	std::vector<unsigned char> iq;
	const std::size_t complexSamples = 96000;
	iq.reserve(complexSamples * 2);
	double phase = 0.0;
	const double phaseStep = 0.08;
	for (std::size_t index = 0; index < complexSamples; ++index)
	{
		const int i = static_cast<int>(127.5 + 100.0 * std::cos(phase));
		const int q = static_cast<int>(127.5 + 100.0 * std::sin(phase));
		iq.push_back(static_cast<unsigned char>(i));
		iq.push_back(static_cast<unsigned char>(q));
		phase += phaseStep;
	}
	std::vector<float> audio;
	demodulator.ProcessUnsignedIq(&iq[0], iq.size(), audio);
	Expect(audio.size() >= 4798 && audio.size() <= 4801, "RTL-TCP resampling ratio");
	double average = 0.0;
	for (std::size_t index = 10; index < audio.size(); ++index) average += audio[index];
	average /= static_cast<double>(audio.size() - 10);
	Expect(std::fabs(average - phaseStep / 3.14159265358979323846) < 0.001,
		"FM discriminator preserves phase direction and magnitude");
	const std::vector<float> legacyReference = LegacyReferenceDemodulate(iq,
		iqRate, audioRate, 12000);
	Expect(audio.size() == legacyReference.size(), "legacy bypass preserves output length");
	for (std::size_t index = 0; index < audio.size(); ++index)
		Expect(audio[index] == legacyReference[index],
			"disabled conditioner is bit-identical to legacy RTL demodulation");

	demodulator.Reset();
	demodulator.ProcessUnsignedIq(NULL, 0, audio);
	Expect(audio.empty(), "empty IQ input is safe");

	const std::vector<unsigned char> impaired = GenerateFskWithInterferer(iqRate,
		static_cast<std::size_t>(iqRate / 4), 1200.0, 4500.0, 0.35, 60000.0, 0.60);
	RtlFmDemodulator legacyImpaired(iqRate, audioRate, 12000, false);
	std::vector<float> legacyImpairedAudio;
	legacyImpaired.ProcessUnsignedIq(&impaired[0], impaired.size(), legacyImpairedAudio);
	RtlFmDemodulator conditioned(iqRate, audioRate, 12000, true);
	std::vector<float> conditionedAudio;
	conditioned.ProcessUnsignedIq(&impaired[0], impaired.size(), conditionedAudio);
	Expect(conditionedAudio.size() > static_cast<std::size_t>(audioRate / 5),
		"conditioner sustains the configured output rate after FIR latency");
	const double legacyAccuracy = FskSignAccuracy(legacyImpairedAudio, audioRate, 1200.0);
	const double conditionedAccuracy = FskSignAccuracy(conditionedAudio, audioRate, 1200.0);
	Expect(conditionedAccuracy > 0.90,
		"IQ channel FIR preserves the wanted FSK symbol polarity");
	Expect(conditionedAccuracy > legacyAccuracy + 0.20,
		"IQ channel FIR rejects a strong out-of-channel interferer");

	RtlFmDemodulator chunked(iqRate, audioRate, 12000, true);
	std::vector<float> chunkedAudio;
	std::vector<float> blockAudio;
	for (std::size_t offset = 0; offset < impaired.size();)
	{
		const std::size_t remaining = impaired.size() - offset;
		std::size_t blockBytes = (std::min)(remaining, static_cast<std::size_t>(8190));
		blockBytes &= ~static_cast<std::size_t>(1);
		if (!blockBytes) blockBytes = remaining;
		chunked.ProcessUnsignedIq(&impaired[offset], blockBytes, blockAudio);
		chunkedAudio.insert(chunkedAudio.end(), blockAudio.begin(), blockAudio.end());
		offset += blockBytes;
	}
	Expect(chunkedAudio.size() == conditionedAudio.size(),
		"conditioner output is independent of RTL callback block size");
	for (std::size_t index = 0; index < chunkedAudio.size(); ++index)
		Expect(std::fabs(chunkedAudio[index] - conditionedAudio[index]) < 1.0e-6f,
			"conditioner retains FIR and resampler state across callbacks");

	if (argc > 1)
	{
		RtlTcpConfig config;
		config.receiverLibraryPath = argv[1];
		config.frequencyHz = 148812500;
		config.sampleRate = 2400000;
		config.audioSampleRate = 44100;
		config.gainTenthsDb = 328;
		config.nfmBandwidthHz = 12000;
		CollectingSink sink;
		RtlSdrSource source;
		Expect(!source.Start(config, 99, &sink), "unavailable RTL-SDR device reports failure");
		Expect(source.state() == RTL_TCP_FAILED, "unavailable RTL-SDR state is failed");

		RtlTcpConfig lossConfig = config;
		lossConfig.frequencyHz = 148812501;
		Expect(source.Start(lossConfig, 0, &sink), "RTL-SDR starts before simulated async loss");
		const DWORD lossWaitStarted = GetTickCount();
		while (source.state() != RTL_TCP_FAILED && GetTickCount() - lossWaitStarted < 2000) Sleep(5);
		Expect(source.state() == RTL_TCP_FAILED, "unexpected async end is reported as failed");
		Expect(source.lastError().find("ended unexpectedly") != std::string::npos,
			"unexpected async end retains an actionable error");

		const bool started = source.Start(config, 0, &sink);
		if (!started) std::cerr << "RTL-SDR start error: " << source.lastError() << '\n';
		Expect(started, "RTL-SDR source starts");
		const DWORD waitStarted = GetTickCount();
		while (!source.lastIqCallbackTick() && GetTickCount() - waitStarted < 2000) Sleep(5);
		Expect(source.lastIqCallbackTick() != 0, "RTL-SDR exposes last IQ callback time");
		while (InterlockedCompareExchange(&sink.callbacks, 0, 0) == 0 &&
			GetTickCount() - waitStarted < 2000) Sleep(5);
		Expect(InterlockedCompareExchange(&sink.callbacks, 0, 0) > 0,
			"RTL-SDR callback reaches the audio sink");
		ExerciseRtlSdrCancellationOwnership(source, argv[1]);
		Expect(source.Start(config, 0, &sink),
			"RTL-SDR source restarts after cancellation ownership test");

		InterlockedExchange(&g_injectedWaitCalls, 0);
		SetRtlStopThreadWaitFunctionForTesting(ReturnWaitFailed);
		Expect(!source.Stop(), "RTL-SDR stop reports an injected wait failure");
		Expect(source.state() == RTL_TCP_FAILED,
			"RTL-SDR wait failure moves the source to failed quarantine");
		Expect(source.lastError().find("quarantined") != std::string::npos,
			"RTL-SDR wait failure retains an actionable quarantine error");
		Expect(!source.Start(config, 0, &sink),
			"RTL-SDR source cannot be reused before a confirmed thread exit");
		Expect(InterlockedCompareExchange(&g_injectedWaitCalls, 0, 0) >= 2,
			"RTL-SDR restart rechecks the quarantined thread");

		SetRtlStopThreadWaitFunctionForTesting(NULL);
		Expect(source.Stop(),
			"RTL-SDR quarantine releases resources after a confirmed thread exit");
		Expect(source.Start(config, 0, &sink),
			"RTL-SDR source can restart after confirmed quarantine cleanup");
		source.FinalizeForShutdown();
		Expect(source.Stop(), "RTL-SDR final shutdown join is idempotent");
		Expect(source.state() == RTL_TCP_STOPPED, "mock RTL-SDR stops cleanly");
	}

	std::cout << "RTL-TCP demodulator tests passed\n";
	return 0;
}
