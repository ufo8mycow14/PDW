#include "data_output_core.h"

#include <iostream>
#include <string>

namespace
{
	int failures = 0;

	void Expect(bool condition, const char* description)
	{
		if (condition) return;
		std::cerr << "FAILED: " << description << '\n';
		++failures;
	}
}

int main()
{
	using namespace pdw::outputs;
	std::string error;

	Expect(IsSafeSqlIdentifier("pdw_messages"), "accepts a conservative table name");
	Expect(IsSafeSqlIdentifier("_archive2026"), "accepts an underscore prefix");
	Expect(!IsSafeSqlIdentifier("messages;drop"), "rejects SQL punctuation");
	Expect(!IsSafeSqlIdentifier("two words"), "rejects spaces in a table name");
	Expect(!IsSafeSqlIdentifier("9messages"), "rejects a numeric prefix");

	Expect(ValidateMqttSettings("mqtts://broker.example:8883", "pdw/messages", false, error),
		"accepts a secure MQTT endpoint");
	Expect(!ValidateMqttSettings("mqtt://broker.example:1883", "pdw/messages", false, error),
		"rejects plaintext MQTT without opt-in");
	Expect(ValidateMqttSettings("mqtt://127.0.0.1:1883", "pdw/messages", true, error),
		"accepts explicitly enabled plaintext MQTT");
	Expect(!ValidateMqttSettings("mqtts://user:pass@broker", "pdw/messages", false, error),
		"rejects credentials embedded in a broker URL");
	Expect(!ValidateMqttSettings("mqtts://broker/root", "pdw/messages", false, error),
		"rejects an ambiguous broker path");
	Expect(!ValidateMqttSettings("mqtts://broker", "pdw/+", false, error),
		"rejects publish-topic wildcards");
	Expect(BuildMqttPublishUrl("mqtts://broker:8883/", "pdw/site one") ==
		"mqtts://broker:8883/pdw/site%20one", "encodes an MQTT topic path");

	Expect(IsLoopbackBindAddress("127.0.0.1"), "recognises IPv4 loopback");
	Expect(IsLoopbackBindAddress("::1"), "recognises IPv6 loopback");
	Expect(!IsLoopbackBindAddress("0.0.0.0"), "does not treat wildcard binding as loopback");
	Expect(ValidateTelnetEndpoint("127.0.0.1", 8024, false, error),
		"accepts a loopback Telnet endpoint");
	Expect(!ValidateTelnetEndpoint("0.0.0.0", 8024, false, error),
		"blocks remote Telnet unless explicitly allowed");
	Expect(ValidateTelnetEndpoint("0.0.0.0", 8024, true, error),
		"allows acknowledged remote Telnet");

	if (failures)
	{
		std::cerr << failures << " data-output core test(s) failed.\n";
		return 1;
	}
	std::cout << "Data-output core tests passed.\n";
	return 0;
}
