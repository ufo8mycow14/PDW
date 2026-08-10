#ifndef PDW_MULTI_CHANNEL_MANAGER_H
#define PDW_MULTI_CHANNEL_MANAGER_H

#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>

#include <string>
#include <vector>

#include "multi_channel_core.h"

namespace pdw
{
namespace multichannel
{

std::vector<ChannelConfig> LoadChannels();
bool SaveChannels(const std::vector<ChannelConfig>& channels, std::string& error);
bool LaunchEnabledChannels(const std::vector<ChannelConfig>& channels, std::string& error);
void StopAllChannels(std::string& status);
std::string ChannelStatus(unsigned int index);

bool ConfigureWorkerFromCommandLine(const char* commandLine);
bool WorkerCommandRequested();
bool WorkerActive();
unsigned int WorkerIndex();
std::string WorkerMutexName(const char* baseName);

} // namespace multichannel
} // namespace pdw

#endif
