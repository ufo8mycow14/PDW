#include "decoded_event.h"

#include <cctype>
#include <iostream>
#include <set>
#include <string>

namespace
{
	int failures = 0;

	void Expect(bool condition, const char* description)
	{
		if (condition) return;
		std::cerr << "FAILED: " << description << '\n';
		++failures;
	}

	bool IsSafeIdentifier(const std::string& value)
	{
		if (value.empty() || value.size() > 128) return false;
		for (std::string::const_iterator character = value.begin(); character != value.end(); ++character)
		{
			const unsigned char byte = static_cast<unsigned char>(*character);
			if (!(std::isalnum(byte) || byte == '-' || byte == '_')) return false;
		}
		return true;
	}
}

int main()
{
	std::set<std::string> identifiers;
	for (int index = 0; index < 2000; ++index)
	{
		const std::string identifier = pdw::events::CreateEventId();
		Expect(IsSafeIdentifier(identifier), "event ID is safe for queue filenames and HTTP headers");
		Expect(identifiers.insert(identifier).second,
			"event ID remains unique across a rapid same-process batch");
	}
	const std::string timestamp = pdw::events::CurrentUtcIso8601();
	Expect(timestamp.size() == 24 && timestamp[4] == '-' && timestamp[7] == '-' &&
		timestamp[10] == 'T' && timestamp[23] == 'Z',
		"UTC timestamp retains the established millisecond ISO-8601 shape");

	if (failures) return 1;
	std::cout << "Decoded-event identity tests passed.\n";
	return 0;
}
