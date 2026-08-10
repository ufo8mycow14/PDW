#include "smtp_message_core.h"

#include <cassert>
#include <string>

int main()
{
	std::string message;
	assert(!pdw::SmtpAppendField(message, nullptr, 32));
	assert(pdw::SmtpAppendField(message, "12345", 32));
	assert(pdw::SmtpAppendField(message, "Fire", 32, "- "));
	assert(message == "12345 - Fire ");

	std::string bounded;
	assert(pdw::SmtpAppendField(bounded, "123456789", 5));
	assert(bounded == "12345");
	assert(!pdw::SmtpAppendField(bounded, "ignored", 5));

	const unsigned char separator = 0xBB;
	const char legacy[] = { 'O', 'n', 'e', static_cast<char>(separator), 'T', 'w', 'o', 0 };
	const pdw::SmtpMessageParts parts =
		pdw::SmtpSplitLegacyMessage(legacy, separator, 64);
	assert(parts.subject == "One - Two");
	assert(parts.body == "One\nTwo");

	const pdw::SmtpMessageParts truncated =
		pdw::SmtpSplitLegacyMessage("abcdefgh", separator, 4);
	assert(truncated.subject == "abcd");
	assert(truncated.body == "abcd");

	assert(pdw::SmtpSanitizeHeader("first\r\nBcc: injected") ==
		"first  Bcc: injected");
	assert(pdw::SmtpDotStuff(".first\r\nnormal\n..third") ==
		"..first\r\nnormal\n...third");
	assert(pdw::SmtpDotStuff("plain") == "plain");

	return 0;
}
