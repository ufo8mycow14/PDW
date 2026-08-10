#ifndef PDW_MULTI_CHANNEL_CORE_H
#define PDW_MULTI_CHANNEL_CORE_H

#include <string>
#include <vector>

namespace pdw
{
namespace multichannel
{

enum ReceiverSource
{
	RECEIVER_SOURCE_RTL_TCP = 1,
	RECEIVER_SOURCE_RTL_SDR = 2
};

struct ChannelConfig
{
	bool enabled;
	int source;
	std::string label;
	std::string host;
	unsigned int port;
	unsigned int deviceIndex;
	unsigned int frequencyHz;

	ChannelConfig();
};

struct ActiveReceiver
{
	bool active;
	int source;
	std::string host;
	unsigned int port;
	unsigned int deviceIndex;

	ActiveReceiver();
};

bool ValidateChannels(const std::vector<ChannelConfig>& channels,
	const ActiveReceiver* mainReceiver, std::string& error);
std::string NormalizeRtlTcpHost(const std::string& host);
std::string BuildInstanceMutexName(const std::string& applicationPath,
	unsigned int workerSlot);

} // namespace multichannel
} // namespace pdw

#endif
