#include "multi_channel_core.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	void Expect(bool condition, const char* message)
	{
		if (condition) return;
		std::cerr << "FAILED: " << message << '\n';
		std::exit(1);
	}
}

int main()
{
	using namespace pdw::multichannel;
	std::string error;
	std::vector<ChannelConfig> channels(4);
	channels[0].enabled = true;
	channels[0].host = "127.0.0.1";
	channels[0].port = 1234;
	Expect(ValidateChannels(channels, NULL, error), "one valid rtl_tcp channel accepted");

	channels[1] = channels[0];
	channels[1].enabled = true;
	Expect(!ValidateChannels(channels, NULL, error), "duplicate rtl_tcp endpoint rejected");
	channels[0].host = "localhost";
	channels[1].host = "LOCALHOST.";
	Expect(!ValidateChannels(channels, NULL, error), "localhost case and root-dot aliases rejected");
	channels[1].host = "127.0.0.1";
	Expect(!ValidateChannels(channels, NULL, error), "localhost and IPv4 loopback aliases rejected");
	channels[0].host = "Receiver.Example";
	channels[1].host = "receiver.example";
	Expect(!ValidateChannels(channels, NULL, error), "DNS host case cannot bypass duplicate detection");
	channels[1].port = 1235;
	Expect(ValidateChannels(channels, NULL, error), "distinct rtl_tcp endpoints accepted");
	Expect(NormalizeRtlTcpHost("  [::1]  ") == "127.0.0.1",
		"bracketed IPv6 loopback has a deterministic local canonical form");
	Expect(NormalizeRtlTcpHost("Receiver.Example.") == "receiver.example",
		"DNS host normalization is case-insensitive and removes the root dot");

	ActiveReceiver mainReceiver;
	mainReceiver.active = true;
	mainReceiver.source = RECEIVER_SOURCE_RTL_TCP;
	mainReceiver.host = "LOCALHOST";
	mainReceiver.port = 1234;
	channels[0].host = "127.0.0.1";
	Expect(!ValidateChannels(channels, &mainReceiver, error), "main rtl_tcp endpoint cannot be reused");

	channels[0].source = RECEIVER_SOURCE_RTL_SDR;
	channels[0].deviceIndex = 2;
	channels[1].enabled = false;
	mainReceiver.source = RECEIVER_SOURCE_RTL_SDR;
	mainReceiver.deviceIndex = 2;
	Expect(!ValidateChannels(channels, &mainReceiver, error), "main RTL-SDR device cannot be reused");
	mainReceiver.deviceIndex = 3;
	Expect(ValidateChannels(channels, &mainReceiver, error), "different RTL-SDR device accepted");

	channels[0].frequencyHz = 1;
	Expect(!ValidateChannels(channels, NULL, error), "out-of-range frequency rejected");
	channels[0].frequencyHz = 148000000;
	channels[0].source = 99;
	Expect(!ValidateChannels(channels, NULL, error), "unknown receiver source rejected");
	Expect(!ValidateChannels(std::vector<ChannelConfig>(3), NULL, error), "wrong slot count rejected");
	const std::string mainMutex = BuildInstanceMutexName("C:\\PDW Update\\PDW-source", 0);
	const std::string workerMutex = BuildInstanceMutexName("C:\\PDW Update\\PDW-source", 2);
	Expect(mainMutex.find("Local\\PDW-") == 0 && mainMutex.find("PDW Update") == std::string::npos,
		"main mutex uses a valid hashed kernel-object name");
	Expect(workerMutex != mainMutex && workerMutex.find("-channel-2") != std::string::npos,
		"worker mutex is isolated by slot");

	std::cout << "Multi-channel core tests passed\n";
	return 0;
}
