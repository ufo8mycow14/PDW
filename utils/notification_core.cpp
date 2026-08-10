#include "notification_core.h"

#include <stdio.h>

using namespace std;

namespace
{
	string CleanDisplayText(const string &source)
	{
		string result;
		result.reserve(source.size());
		bool previousWasSpace = true;

		for (size_t index = 0; index < source.size(); index++)
		{
			unsigned char value = (unsigned char) source[index];
			bool isSpace = value == ' ' || value == '\t' || value == '\r' || value == '\n';

			if (isSpace)
			{
				if (!previousWasSpace) result.push_back(' ');
				previousWasSpace = true;
			}
			else if (value >= 0x20)
			{
				result.push_back((char) value);
				previousWasSpace = false;
			}
		}

		while (!result.empty() && result[result.size()-1] == ' ') result.erase(result.size()-1);
		return result;
	}

	string TruncateUtf8(const string &source, size_t maximumBytes)
	{
		if (source.size() <= maximumBytes) return source;
		if (maximumBytes <= 3) return source.substr(0, maximumBytes);

		size_t cut = maximumBytes-3;
		while (cut > 0 && (((unsigned char) source[cut]) & 0xc0) == 0x80) cut--;
		return source.substr(0, cut) + "...";
	}

	void AppendField(string &body, const char *name, const string &value)
	{
		string cleaned = CleanDisplayText(value);
		if (cleaned.empty()) return;
		if (!body.empty()) body += " | ";
		body += name;
		body += cleaned;
	}

	void AppendJsonString(string &output, const string &value)
	{
		static const char hex[] = "0123456789abcdef";
		output.push_back('"');
		for (size_t index = 0; index < value.size(); index++)
		{
			unsigned char character = (unsigned char) value[index];
			switch (character)
			{
				case '"': output += "\\\""; break;
				case '\\': output += "\\\\"; break;
				case '\b': output += "\\b"; break;
				case '\f': output += "\\f"; break;
				case '\n': output += "\\n"; break;
				case '\r': output += "\\r"; break;
				case '\t': output += "\\t"; break;
				default:
					if (character < 0x20)
					{
						output += "\\u00";
						output.push_back(hex[(character >> 4) & 0x0f]);
						output.push_back(hex[character & 0x0f]);
					}
					else output.push_back((char) character);
				break;
			}
		}
		output.push_back('"');
	}
}

NotificationRouteDecision NotificationDecideRoutes(bool emailEnabled,
	bool pushEnabled, bool filteredMessage)
{
	NotificationRouteDecision decision;
	decision.email = emailEnabled;
	decision.push = pushEnabled && filteredMessage;
	return decision;
}

const char *NotificationAppriseType(NotificationSeverity severity)
{
	switch (severity)
	{
		case NOTIFICATION_SEVERITY_SUCCESS: return "success";
		case NOTIFICATION_SEVERITY_WARNING: return "warning";
		case NOTIFICATION_SEVERITY_ERROR: return "failure";
		case NOTIFICATION_SEVERITY_INFORMATION:
		default: return "info";
	}
}

string NotificationBuildFilteredBody(const string &filterLabel,
	const string &address, const string &mode, const string &messageType,
	const string &decodedMessage, bool includeDecodedMessage,
	size_t maximumBytes)
{
	string body = "A filtered message was received.";
	if (includeDecodedMessage)
	{
		body.clear();
		AppendField(body, "Filter: ", filterLabel);
		AppendField(body, "Address: ", address);
		AppendField(body, "Mode: ", mode);
		AppendField(body, "Type: ", messageType);
		AppendField(body, "Message: ", decodedMessage);
	}

	if (body.empty()) body = "A filtered message was received.";
	return TruncateUtf8(body, maximumBytes);
}

string NotificationBuildApprisePayload(const NotificationEvent &event,
	const string &destinationUrls)
{
	string payload = "{";
	if (!destinationUrls.empty())
	{
		payload += "\"urls\":";
		AppendJsonString(payload, destinationUrls);
		payload += ",";
	}

	payload += "\"body\":";
	AppendJsonString(payload, event.message);
	payload += ",\"title\":";
	AppendJsonString(payload, event.title);
	payload += ",\"type\":";
	AppendJsonString(payload, NotificationAppriseType(event.severity));
	payload += ",\"format\":\"text\"}";
	return payload;
}

bool NotificationHttpStatusIsTransient(long httpStatus)
{
	return httpStatus == 408 || httpStatus == 425 || httpStatus == 429 ||
		httpStatus == 500 || httpStatus == 502 || httpStatus == 503 ||
		httpStatus == 504;
}

string NotificationSanitizedHttpStatus(long httpStatus)
{
	char status[96];
	if (httpStatus == 200)
		return "Apprise delivered the notification.";
	if (httpStatus == 204)
		return "Apprise has no destination configured (HTTP 204).";
	if (httpStatus == 401 || httpStatus == 403)
		return "Apprise authentication was rejected.";
	if (httpStatus == 400 || httpStatus == 405 || httpStatus == 431)
		return "Apprise rejected the notification settings.";
	if (httpStatus == 424)
		return "One or more Apprise destinations could not deliver the notification.";

	snprintf(status, sizeof(status), "Apprise returned HTTP %ld.", httpStatus);
	status[sizeof(status)-1] = '\0';
	return status;
}
