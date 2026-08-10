#ifndef PDW_NOTIFICATION_CORE_H
#define PDW_NOTIFICATION_CORE_H

#include <stddef.h>
#include <string>
#include <vector>

enum NotificationSeverity
{
	NOTIFICATION_SEVERITY_INFORMATION = 0,
	NOTIFICATION_SEVERITY_SUCCESS,
	NOTIFICATION_SEVERITY_WARNING,
	NOTIFICATION_SEVERITY_ERROR
};

struct NotificationMetadata
{
	std::string name;
	std::string value;
};

struct NotificationEvent
{
	std::string identifier;
	std::string title;
	std::string message;
	NotificationSeverity severity;
	std::string timestamp;
	std::string actionUrl;
	std::vector<NotificationMetadata> metadata;
};

struct NotificationRouteDecision
{
	bool email;
	bool push;
};

NotificationRouteDecision NotificationDecideRoutes(bool emailEnabled,
	bool pushEnabled, bool filteredMessage);
const char *NotificationAppriseType(NotificationSeverity severity);
std::string NotificationBuildFilteredBody(const std::string &filterLabel,
	const std::string &address, const std::string &mode,
	const std::string &messageType, const std::string &decodedMessage,
	bool includeDecodedMessage, size_t maximumBytes);
std::string NotificationBuildApprisePayload(const NotificationEvent &event,
	const std::string &destinationUrls);
bool NotificationHttpStatusIsTransient(long httpStatus);
std::string NotificationSanitizedHttpStatus(long httpStatus);

#endif
