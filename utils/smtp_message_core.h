#ifndef PDW_SMTP_MESSAGE_CORE_H
#define PDW_SMTP_MESSAGE_CORE_H

#include <cstddef>
#include <string>

namespace pdw {

struct SmtpMessageParts
{
	std::string subject;
	std::string body;
};

// Appends one selected legacy SMTP field while keeping the completed message
// within maxLength bytes. Null and empty fields are ignored.
bool SmtpAppendField(
	std::string& message,
	const char* value,
	std::size_t maxLength,
	const char* prefix = "",
	const char* suffix = " ");

// PDW uses a display separator byte inside multi-line decoded messages. The
// historical SMTP subject replaces it with " - ", while the body uses LF.
SmtpMessageParts SmtpSplitLegacyMessage(
	const char* message,
	unsigned char separator,
	std::size_t maxPartLength);

// Header values must not contain CR/LF. Other content is left unchanged so
// legacy character sets and message text remain intact.
std::string SmtpSanitizeHeader(const std::string& value);

// RFC 5321 section 4.5.2 transparency: duplicate a dot at the beginning of
// the DATA body and after every LF. The caller still sends the final terminator.
std::string SmtpDotStuff(const std::string& body);

} // namespace pdw

#endif
