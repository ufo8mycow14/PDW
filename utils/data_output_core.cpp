#include "data_output_core.h"

#include <cctype>
#include <iomanip>
#include <sstream>

namespace pdw
{
namespace outputs
{
namespace
{
	bool HasControlOrSpace(const std::string& value)
	{
		for (std::string::const_iterator character = value.begin(); character != value.end(); ++character)
			if (static_cast<unsigned char>(*character) <= 0x20 || *character == 0x7f) return true;
		return false;
	}

	bool IsUnreservedTopicByte(unsigned char value)
	{
		return std::isalnum(value) != 0 || value == '-' || value == '_' || value == '.' || value == '~';
	}
}

bool IsSafeSqlIdentifier(const std::string& value)
{
	if (value.empty() || value.size() > 63) return false;
	const unsigned char first = static_cast<unsigned char>(value[0]);
	if (!(std::isalpha(first) || first == '_')) return false;
	for (std::size_t index = 1; index < value.size(); ++index)
	{
		const unsigned char character = static_cast<unsigned char>(value[index]);
		if (!(std::isalnum(character) || character == '_')) return false;
	}
	return true;
}

bool IsLoopbackBindAddress(const std::string& value)
{
	if (value == "localhost" || value == "::1") return true;
	if (value.compare(0, 4, "127.") != 0) return false;
	if (value.size() <= 4) return false;
	for (std::size_t index = 4; index < value.size(); ++index)
		if (!(std::isdigit(static_cast<unsigned char>(value[index])) || value[index] == '.')) return false;
	return true;
}

bool ValidateTelnetEndpoint(const std::string& bindAddress, int port,
	bool allowRemote, std::string& error)
{
	error.clear();
	if (bindAddress.empty() || bindAddress.size() > 63 || HasControlOrSpace(bindAddress))
	{
		error = "Enter a valid IP address or localhost for the Telnet bind address.";
		return false;
	}
	if (port < 1 || port > 65535)
	{
		error = "Telnet port must be between 1 and 65535.";
		return false;
	}
	if (!IsLoopbackBindAddress(bindAddress) && !allowRemote)
	{
		error = "A non-loopback Telnet address requires the explicit remote-access option.";
		return false;
	}
	return true;
}

bool ValidateMqttSettings(const std::string& brokerUrl, const std::string& topic,
	bool allowInsecure, std::string& error)
{
	error.clear();
	const bool secure = brokerUrl.compare(0, 8, "mqtts://") == 0;
	const bool insecure = brokerUrl.compare(0, 7, "mqtt://") == 0;
	if (!secure && !insecure)
	{
		error = "MQTT broker URL must begin with mqtts://, or mqtt:// when explicitly allowed.";
		return false;
	}
	if (insecure && !allowInsecure)
	{
		error = "Unencrypted mqtt:// is disabled. Use mqtts:// or explicitly allow local-network MQTT.";
		return false;
	}
	if (brokerUrl.size() > 511 || HasControlOrSpace(brokerUrl))
	{
		error = "MQTT broker URL is empty, too long, or contains whitespace.";
		return false;
	}
	const std::size_t authorityStart = secure ? 8 : 7;
	const std::size_t pathStart = brokerUrl.find('/', authorityStart);
	const std::string authority = brokerUrl.substr(authorityStart,
		pathStart == std::string::npos ? std::string::npos : pathStart - authorityStart);
	if (authority.empty() || authority.find('@') != std::string::npos ||
		authority.find('?') != std::string::npos || authority.find('#') != std::string::npos)
	{
		error = "MQTT broker URL must contain a host and must not embed credentials.";
		return false;
	}
	if (pathStart != std::string::npos && brokerUrl.substr(pathStart).find_first_not_of('/') != std::string::npos)
	{
		error = "Keep the MQTT broker URL to scheme, host, and port; enter the topic separately.";
		return false;
	}
	if (topic.empty() || topic.size() > 255 || HasControlOrSpace(topic) ||
		topic[0] == '/' || topic[topic.size() - 1] == '/' ||
		topic.find('#') != std::string::npos || topic.find('+') != std::string::npos)
	{
		error = "MQTT topic must be a non-empty publish topic without spaces, wildcards, or edge slashes.";
		return false;
	}
	return true;
}

std::string BuildMqttPublishUrl(const std::string& brokerUrl, const std::string& topic)
{
	std::string result(brokerUrl);
	while (!result.empty() && result[result.size() - 1] == '/') result.erase(result.size() - 1);
	result += '/';
	static const char hex[] = "0123456789ABCDEF";
	for (std::string::const_iterator character = topic.begin(); character != topic.end(); ++character)
	{
		const unsigned char value = static_cast<unsigned char>(*character);
		if (IsUnreservedTopicByte(value) || value == '/') result += static_cast<char>(value);
		else
		{
			result += '%';
			result += hex[value >> 4];
			result += hex[value & 15];
		}
	}
	return result;
}

} // namespace outputs
} // namespace pdw
