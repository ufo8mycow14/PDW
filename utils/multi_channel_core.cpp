#include "multi_channel_core.h"

#include <cctype>
#include <iomanip>
#include <sstream>

namespace pdw
{
namespace multichannel
{

namespace
{
	const std::size_t CHANNEL_COUNT = 4;

	bool IsAsciiSpace(char character)
	{
		return character == ' ' || character == '\t' || character == '\r' ||
			character == '\n' || character == '\f' || character == '\v';
	}

	char LowerAscii(char character)
	{
		return character >= 'A' && character <= 'Z' ?
			static_cast<char>(character + ('a' - 'A')) : character;
	}
}

ChannelConfig::ChannelConfig()
	: enabled(false), source(RECEIVER_SOURCE_RTL_TCP), host("127.0.0.1"),
	  port(1234), deviceIndex(0), frequencyHz(148000000)
{
}

ActiveReceiver::ActiveReceiver()
	: active(false), source(RECEIVER_SOURCE_RTL_TCP), port(0), deviceIndex(0)
{
}

std::string NormalizeRtlTcpHost(const std::string& host)
{
	// Endpoint comparison must not depend on DNS, which could block worker
	// validation or change according to the machine's resolver configuration.
	// DNS names are ASCII case-insensitive and a trailing root dot is optional.
	std::size_t first = 0;
	while (first < host.size() && IsAsciiSpace(host[first])) ++first;
	std::size_t last = host.size();
	while (last > first && IsAsciiSpace(host[last - 1])) --last;

	std::string normalized;
	normalized.reserve(last - first);
	for (std::size_t index = first; index < last; ++index)
		normalized.push_back(LowerAscii(host[index]));
	while (!normalized.empty() && normalized[normalized.size() - 1] == '.')
		normalized.erase(normalized.size() - 1);
	if (normalized.size() > 2 && normalized[0] == '[' &&
		normalized[normalized.size() - 1] == ']')
		normalized = normalized.substr(1, normalized.size() - 2);

	if (normalized == "localhost" || normalized == "127.0.0.1" ||
		normalized == "::1" || normalized == "0:0:0:0:0:0:0:1")
		return "127.0.0.1";
	return normalized;
}

bool ValidateChannels(const std::vector<ChannelConfig>& channels,
	const ActiveReceiver* mainReceiver, std::string& error)
{
	error.clear();
	if (channels.size() != CHANNEL_COUNT)
	{
		error = "Exactly four guarded channel slots are required.";
		return false;
	}
	for (std::size_t index = 0; index < channels.size(); ++index)
	{
		const ChannelConfig& channel = channels[index];
		if (channel.source != RECEIVER_SOURCE_RTL_TCP &&
			channel.source != RECEIVER_SOURCE_RTL_SDR)
		{
			error = "Each channel must use rtl_tcp or a direct RTL-SDR receiver.";
			return false;
		}
		if (channel.label.size() > 80 || channel.host.size() > 253)
		{
			error = "Channel labels or receiver hosts are too long.";
			return false;
		}
		if (!channel.enabled) continue;
		if (channel.frequencyHz < 100000 || channel.frequencyHz > 2000000000U)
		{
			error = "Enabled channel frequencies must be between 100 kHz and 2 GHz.";
			return false;
		}
		const std::string normalizedHost = channel.source == RECEIVER_SOURCE_RTL_TCP ?
			NormalizeRtlTcpHost(channel.host) : std::string();
		if (channel.source == RECEIVER_SOURCE_RTL_TCP &&
			(normalizedHost.empty() || channel.port < 1 || channel.port > 65535))
		{
			error = "Enabled rtl_tcp channels need a host and valid port.";
			return false;
		}
		if (mainReceiver && mainReceiver->active && channel.source == mainReceiver->source)
		{
			if (channel.source == RECEIVER_SOURCE_RTL_SDR &&
				channel.deviceIndex == mainReceiver->deviceIndex)
			{
				error = "A worker cannot reuse the direct RTL-SDR device selected by the main PDW window.";
				return false;
			}
			if (channel.source == RECEIVER_SOURCE_RTL_TCP &&
				normalizedHost == NormalizeRtlTcpHost(mainReceiver->host) &&
				channel.port == mainReceiver->port)
			{
				error = "A worker cannot reuse the rtl_tcp endpoint selected by the main PDW window.";
				return false;
			}
		}
		for (std::size_t previous = 0; previous < index; ++previous)
		{
			const ChannelConfig& other = channels[previous];
			if (!other.enabled || other.source != channel.source) continue;
			if (channel.source == RECEIVER_SOURCE_RTL_SDR &&
				other.deviceIndex == channel.deviceIndex)
			{
				error = "Direct RTL-SDR worker channels must use different device indexes.";
				return false;
			}
			if (channel.source == RECEIVER_SOURCE_RTL_TCP &&
				NormalizeRtlTcpHost(other.host) == normalizedHost &&
				other.port == channel.port)
			{
				error = "rtl_tcp worker channels must use different receiver endpoints.";
				return false;
			}
		}
	}
	return true;
}

std::string BuildInstanceMutexName(const std::string& applicationPath,
	unsigned int workerSlot)
{
	// Kernel object names may not contain path separators beyond the Local\
	// namespace prefix. Hash a case-normalized path so portable PDW folders
	// remain isolated without embedding an invalid filesystem path.
	unsigned long long hash = 1469598103934665603ULL;
	for (std::string::const_iterator character = applicationPath.begin();
		character != applicationPath.end(); ++character)
	{
		const unsigned char normalized = static_cast<unsigned char>(
			std::tolower(static_cast<unsigned char>(*character)));
		hash ^= normalized;
		hash *= 1099511628211ULL;
	}
	std::ostringstream name;
	name << "Local\\PDW-" << std::hex << std::setw(16) << std::setfill('0') << hash;
	if (workerSlot) name << "-channel-" << std::dec << workerSlot;
	else name << "-main";
	return name.str();
}

} // namespace multichannel
} // namespace pdw
