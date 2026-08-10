#ifndef PDW_PUBLISHING_CORE_H
#define PDW_PUBLISHING_CORE_H

#include <string>
#include <vector>

namespace pdw
{
namespace publishing
{

struct PublishEvent
{
	std::string id;
	std::string timestamp;
	std::string source;
	std::string address;
	std::string time;
	std::string date;
	std::string mode;
	std::string messageType;
	std::string bitrate;
	std::string message;
	std::string filterLabel;
	bool filtered;
	PublishEvent() : filtered(false) {}
};

struct TransformOptions
{
	std::string sourceAlias;
	bool maskAddress;
	bool includeMessage;
	TransformOptions() : maskAddress(false), includeMessage(true) {}
};

PublishEvent ApplyTransform(const PublishEvent& source, const TransformOptions& options);
std::string BuildJsonObject(const PublishEvent& event);
std::string BuildJsonFeed(const std::vector<PublishEvent>& events);
std::string BuildJsonLines(const std::vector<PublishEvent>& events);
std::string BuildRssFeed(const std::vector<PublishEvent>& events);
std::string BuildAtomFeed(const std::vector<PublishEvent>& events);
std::string BuildHtmlFeed(const std::vector<PublishEvent>& events);

} // namespace publishing
} // namespace pdw

#endif
