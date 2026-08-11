#include "multipart_message_reassembly_core.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	void Expect(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAILED: " << message << "\n";
			std::exit(1);
		}
	}

	pdw::multipart::MultipartResult Observe(
		pdw::multipart::MultipartReassembler& reassembler,
		const char* address,
		const char* protocol,
		const char* type,
		const std::string& text,
		std::uint64_t now,
		std::uint8_t color = 7)
	{
		std::vector<std::uint8_t> colors(text.size(), color);
		pdw::multipart::MultipartObservation observation;
		observation.address = address;
		observation.protocol = protocol;
		observation.messageType = type;
		observation.observedAtMs = now;
		observation.text = reinterpret_cast<const unsigned char*>(text.data());
		observation.colors = colors.empty() ? NULL : &colors[0];
		observation.length = text.size();
		return reassembler.Observe(observation);
	}
}

int main()
{
	pdw::multipart::MultipartReassembler reassembler(4, 100, 128, 8);

	pdw::multipart::MultipartResult result = Observe(reassembler,
		"1234567", "POCSAG", "ALPHA", "ordinary message", 1);
	Expect(result.status == pdw::multipart::MULTIPART_NOT_RECOGNIZED,
		"ordinary messages are not buffered");

	result = Observe(reassembler, "1234567", "POCSAG", "ALPHA",
		"Part 1 of 2: Road closed", 2, 3);
	Expect(result.status == pdw::multipart::MULTIPART_BUFFERED,
		"first text part is buffered");
	Expect(reassembler.ActiveCount() == 1, "one multipart chain is active");
	result = Observe(reassembler, "1234567", "POCSAG", "ALPHA",
		"Traffic diverted [Part 2 of 2]", 3, 4);
	Expect(result.status == pdw::multipart::MULTIPART_ASSEMBLED && result.assembled,
		"complete text chain assembles");
	Expect(result.text == "Road closed Traffic diverted",
		"part markers are removed and payload follows part order");
	Expect(result.totalParts == 2, "assembled result reports total parts");
	Expect(result.colors.size() == result.text.size(),
		"assembled text retains aligned display colours");

	result = Observe(reassembler, "7654321", "FLEX", "ALPHA",
		"PART #3 OF 3 - final", 10);
	Expect(result.status == pdw::multipart::MULTIPART_BUFFERED,
		"out-of-order final part is buffered");
	Observe(reassembler, "7654321", "FLEX", "ALPHA", "Part 1 of 3 first", 11);
	result = Observe(reassembler, "7654321", "FLEX", "ALPHA",
		"second - part 2 of 3", 12);
	Expect(result.status == pdw::multipart::MULTIPART_ASSEMBLED,
		"out-of-order explicit parts assemble when the gap arrives");
	Expect(result.text == "first second final", "out-of-order payload is ordered");

	result = Observe(reassembler, "1111111", "POCSAG", "ALPHA",
		"Part 1 of 2 hello", 20);
	Expect(result.status == pdw::multipart::MULTIPART_BUFFERED, "duplicate fixture starts");
	result = Observe(reassembler, "1111111", "POCSAG", "ALPHA",
		"Part 1 of 2 hello", 21);
	Expect(result.status == pdw::multipart::MULTIPART_DUPLICATE,
		"identical repeated part is ignored");
	result = Observe(reassembler, "1111111", "POCSAG", "NUMERIC",
		"Part 2 of 2 separate", 22);
	Expect(result.status == pdw::multipart::MULTIPART_BUFFERED,
		"message type isolates chains for one capcode");

	result = Observe(reassembler, "2222222", "POCSAG", "ALPHA",
		"Part 2 of 2 first version", 30);
	Expect(result.status == pdw::multipart::MULTIPART_BUFFERED, "conflict fixture starts");
	result = Observe(reassembler, "2222222", "POCSAG", "ALPHA",
		"Part 2 of 2 changed version", 31);
	Expect(result.status == pdw::multipart::MULTIPART_CONFLICT,
		"conflicting same-number content is never guessed");

	pdw::multipart::MultipartReassembler expiry(2, 10, 128, 8);
	Observe(expiry, "3333333", "POCSAG", "ALPHA", "Part 1 of 2 old", 1);
	result = Observe(expiry, "3333333", "POCSAG", "ALPHA", "Part 2 of 2 new", 12);
	Expect(result.status == pdw::multipart::MULTIPART_BUFFERED,
		"expired content is not mixed into a later part");
	result = Observe(expiry, "3333333", "POCSAG", "ALPHA", "Part 1 of 2 fresh", 13);
	Expect(result.status == pdw::multipart::MULTIPART_ASSEMBLED &&
		result.text == "fresh new", "a fresh chain can complete after expiry");

	pdw::multipart::MultipartReassembler bounded(1, 100, 128, 8);
	Observe(bounded, "4444444", "POCSAG", "ALPHA", "Part 1 of 2 one", 1);
	result = Observe(bounded, "5555555", "POCSAG", "ALPHA", "Part 1 of 2 two", 2);
	Expect(result.status == pdw::multipart::MULTIPART_CAPACITY_REACHED,
		"bounded cache fails closed when full");

	pdw::multipart::MultipartReassembler shortText(2, 100, 5, 8);
	Observe(shortText, "6666666", "POCSAG", "ALPHA", "Part 1 of 2 abc", 1);
	result = Observe(shortText, "6666666", "POCSAG", "ALPHA", "Part 2 of 2 def", 2);
	Expect(result.status == pdw::multipart::MULTIPART_ASSEMBLED && result.truncated,
		"assembled text respects its byte bound");
	Expect(result.text == "abc d", "bounded assembly truncates deterministically");

	result = Observe(reassembler, "7777777", "POCSAG", "ALPHA", "Part 0 of 2 bad", 40);
	Expect(result.status == pdw::multipart::MULTIPART_INVALID,
		"invalid part ranges are rejected without guessing");
	result = Observe(reassembler, "7777777", "POCSAG", "ALPHA", "Part 1 of 99 bad", 41);
	Expect(result.status == pdw::multipart::MULTIPART_INVALID,
		"unbounded part counts are rejected");

	reassembler.Reset();
	Expect(reassembler.ActiveCount() == 0, "reset removes pending multipart state");
	std::cout << "Multipart message reassembly core tests passed\n";
	return 0;
}
