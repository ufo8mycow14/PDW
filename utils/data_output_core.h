#ifndef PDW_DATA_OUTPUT_CORE_H
#define PDW_DATA_OUTPUT_CORE_H

#include <string>

namespace pdw
{
namespace outputs
{

bool IsSafeSqlIdentifier(const std::string& value);
bool IsLoopbackBindAddress(const std::string& value);
bool ValidateTelnetEndpoint(const std::string& bindAddress, int port,
	bool allowRemote, std::string& error);
bool ValidateMqttSettings(const std::string& brokerUrl, const std::string& topic,
	bool allowInsecure, std::string& error);
std::string BuildMqttPublishUrl(const std::string& brokerUrl, const std::string& topic);

} // namespace outputs
} // namespace pdw

#endif
