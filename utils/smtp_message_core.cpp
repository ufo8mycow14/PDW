#include "smtp_message_core.h"

#include <algorithm>
#include <cstring>

namespace pdw {
namespace {

void AppendBounded(std::string& target, const char* value, std::size_t maxLength)
{
	if (!value || target.size() >= maxLength)
		return;

	const std::size_t available = maxLength - target.size();
	target.append(value, std::min(available, std::strlen(value)));
}

void AppendBoundedChar(std::string& target, char value, std::size_t maxLength)
{
	if (target.size() < maxLength)
		target.push_back(value);
}

} // namespace

bool SmtpAppendField(
	std::string& message,
	const char* value,
	std::size_t maxLength,
	const char* prefix,
	const char* suffix)
{
	if (!value || !value[0] || message.size() >= maxLength)
		return false;

	AppendBounded(message, prefix, maxLength);
	AppendBounded(message, value, maxLength);
	AppendBounded(message, suffix, maxLength);
	return true;
}

SmtpMessageParts SmtpSplitLegacyMessage(
	const char* message,
	unsigned char separator,
	std::size_t maxPartLength)
{
	SmtpMessageParts parts;
	if (!message)
		return parts;

	for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(message);
		*cursor;
		++cursor)
	{
		if (*cursor == separator)
		{
			AppendBounded(parts.subject, " - ", maxPartLength);
			AppendBoundedChar(parts.body, '\n', maxPartLength);
		}
		else
		{
			AppendBoundedChar(parts.subject, static_cast<char>(*cursor), maxPartLength);
			AppendBoundedChar(parts.body, static_cast<char>(*cursor), maxPartLength);
		}
	}

	return parts;
}

std::string SmtpSanitizeHeader(const std::string& value)
{
	std::string safe(value);
	std::replace(safe.begin(), safe.end(), '\r', ' ');
	std::replace(safe.begin(), safe.end(), '\n', ' ');
	return safe;
}

std::string SmtpDotStuff(const std::string& body)
{
	std::string escaped;
	escaped.reserve(body.size() + (body.size() / 32) + 1);

	bool atLineStart = true;
	for (const char value : body)
	{
		if (atLineStart && value == '.')
			escaped.push_back('.');

		escaped.push_back(value);
		atLineStart = value == '\n';
	}

	return escaped;
}

} // namespace pdw
