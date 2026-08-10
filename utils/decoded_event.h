#ifndef PDW_DECODED_EVENT_H
#define PDW_DECODED_EVENT_H

#include <string>

#include "publishing_core.h"

struct DecodedMessageNotificationContext;

namespace pdw
{
namespace events
{

std::string PdwTextToUtf8(const char* source);
std::string CurrentUtcIso8601();
std::string CreateEventId();
pdw::publishing::PublishEvent BuildDecodedEvent(const DecodedMessageNotificationContext& context);
pdw::publishing::PublishEvent BuildTestEvent(const std::string& targetName);

} // namespace events
} // namespace pdw

#endif
