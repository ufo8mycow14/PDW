#ifndef PDW_PUBLISHING_CORE_H
#define PDW_PUBLISHING_CORE_H

#include <string>
#include <vector>

#include "../Headers/output_routes.h"

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
	std::string addressName;
	std::string agency;
	unsigned long aliasColor;
	std::string time;
	std::string date;
	std::string mode;
	std::string messageType;
	std::string bitrate;
	std::string message;
	std::string filterLabel;
	bool filterMatched;
	bool monitorOnly;
	bool filtered;
	bool rejected;
	bool blockedDuplicate;
	bool groupCall;
	bool groupFinal;
	bool fragmented;
	bool assembled;
	bool outputRoutingConfigured;
	unsigned int outputRoutes;
	int filterIndex;
	int groupBit;
	int cycle;
	int frame;
	PublishEvent()
		: aliasColor(0), filterMatched(false), monitorOnly(false), filtered(false), rejected(false),
		  blockedDuplicate(false), groupCall(false), groupFinal(false), fragmented(false),
		  assembled(false), outputRoutingConfigured(false), outputRoutes(0),
		  filterIndex(-1), groupBit(-1), cycle(-1), frame(-1) {}
};

struct TransformOptions
{
	std::string sourceAlias;
	bool maskAddress;
	bool includeMessage;
	TransformOptions() : maskAddress(false), includeMessage(true) {}
};

PublishEvent ApplyTransform(const PublishEvent& source, const TransformOptions& options);
std::string BuildJsonObject(const PublishEvent& event, bool includeLocalAliases = false);
std::string BuildJsonFeed(const std::vector<PublishEvent>& events);
std::string BuildJsonLines(const std::vector<PublishEvent>& events);
std::string BuildRssFeed(const std::vector<PublishEvent>& events);
std::string BuildAtomFeed(const std::vector<PublishEvent>& events);
std::string BuildHtmlFeed(const std::vector<PublishEvent>& events);

} // namespace publishing
} // namespace pdw

#endif
