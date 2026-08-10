#include "decoded_event.h"

#include <windows.h>

#include <cstdio>
#include <vector>

#include "headers\notification.h"

namespace pdw
{
namespace events
{
namespace
{
	volatile LONG g_eventCounter = 0;
}

std::string PdwTextToUtf8(const char* source)
{
	if (!source || !source[0]) return std::string();
	const int wideLength = MultiByteToWideChar(1252, 0, source, -1, NULL, 0);
	if (wideLength <= 1) return std::string(source);
	std::vector<wchar_t> wide(static_cast<std::size_t>(wideLength));
	if (!MultiByteToWideChar(1252, 0, source, -1, &wide[0], wideLength)) return std::string(source);
	const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, &wide[0], -1, NULL, 0, NULL, NULL);
	if (utf8Length <= 1) return std::string();
	std::vector<char> result(static_cast<std::size_t>(utf8Length));
	if (!WideCharToMultiByte(CP_UTF8, 0, &wide[0], -1, &result[0], utf8Length, NULL, NULL))
		return std::string(source);
	return std::string(&result[0]);
}

std::string CurrentUtcIso8601()
{
	SYSTEMTIME now;
	GetSystemTime(&now);
	char text[40];
	snprintf(text, sizeof(text), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
		now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
	return text;
}

std::string CreateEventId()
{
	SYSTEMTIME now;
	GetSystemTime(&now);
	char text[80];
	snprintf(text, sizeof(text), "%04u%02u%02uT%02u%02u%02u-%lu-%ld",
		now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
		static_cast<unsigned long>(GetCurrentProcessId()), InterlockedIncrement(&g_eventCounter));
	return text;
}

pdw::publishing::PublishEvent BuildDecodedEvent(const DecodedMessageNotificationContext& context)
{
	pdw::publishing::PublishEvent event;
	event.id = CreateEventId();
	event.timestamp = CurrentUtcIso8601();
	event.source = "PDW";
	event.address = PdwTextToUtf8(context.address);
	event.time = PdwTextToUtf8(context.time);
	event.date = PdwTextToUtf8(context.date);
	event.mode = PdwTextToUtf8(context.mode);
	event.messageType = PdwTextToUtf8(context.messageType);
	event.bitrate = PdwTextToUtf8(context.bitrate);
	event.message = PdwTextToUtf8(context.message);
	event.filterLabel = PdwTextToUtf8(context.filterLabel);
	event.filterMatched = context.filterMatched;
	event.monitorOnly = context.monitorOnly;
	event.filtered = context.filtered;
	event.rejected = context.rejected;
	event.blockedDuplicate = context.blockedDuplicate;
	event.groupCall = context.groupCall;
	event.groupFinal = context.groupFinal;
	event.fragmented = context.fragmented;
	event.assembled = context.assembled;
	event.filterIndex = context.filterIndex;
	event.groupBit = context.groupBit;
	event.cycle = context.cycle;
	event.frame = context.frame;
	return event;
}

pdw::publishing::PublishEvent BuildTestEvent(const std::string& targetName)
{
	pdw::publishing::PublishEvent event;
	event.id = CreateEventId();
	event.timestamp = CurrentUtcIso8601();
	event.source = "PDW";
	event.mode = "TEST";
	event.messageType = targetName;
	event.message = "PDW output configuration test";
	return event;
}

} // namespace events
} // namespace pdw
