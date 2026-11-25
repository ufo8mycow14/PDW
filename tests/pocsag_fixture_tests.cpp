#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>

#include "headers/pdw.h"
#include "pocsag_test_environment.h"
#include "utils/decoder_audio_adapter.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	void Fail(const std::string& message)
	{
		std::cerr << "FAILED: " << message << '\n';
		std::exit(1);
	}

	void Expect(bool condition, const std::string& message)
	{
		if (!condition) Fail(message);
	}

	std::string JoinPath(const std::string& directory, const char* fileName)
	{
		if (directory.empty()) return fileName;
		const char last = directory[directory.size() - 1];
		return directory + ((last == '\\' || last == '/') ? "" : "\\") + fileName;
	}

	std::string ReadText(const std::string& path)
	{
		std::ifstream input(path.c_str(), std::ios::binary);
		if (!input) Fail("unable to open " + path);
		return std::string((std::istreambuf_iterator<char>(input)),
			std::istreambuf_iterator<char>());
	}

	std::vector<int> ReadBits(const std::string& path)
	{
		std::ifstream input(path.c_str());
		if (!input) Fail("unable to open " + path);
		std::vector<int> bits;
		std::string line;
		while (std::getline(input, line))
		{
			const std::string::size_type comment = line.find('#');
			if (comment != std::string::npos) line.erase(comment);
			for (std::string::const_iterator value = line.begin(); value != line.end(); ++value)
			{
				if (*value == '0' || *value == '1') bits.push_back(*value - '0');
				else if (!std::isspace(static_cast<unsigned char>(*value)))
					Fail("invalid character in " + path);
			}
		}
		return bits;
	}

	pdw_test::CapturedPocsagMessage ReadExpected(const std::string& path)
	{
		std::ifstream input(path.c_str());
		if (!input) Fail("unable to open " + path);
		std::string line;
		while (std::getline(input, line))
		{
			if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
			if (line.empty() || line[0] == '#') continue;
			std::vector<std::string> fields;
			std::string::size_type start = 0;
			for (;;)
			{
				const std::string::size_type separator = line.find('|', start);
				fields.push_back(line.substr(start, separator == std::string::npos ?
					std::string::npos : separator - start));
				if (separator == std::string::npos) break;
				start = separator + 1;
			}
			if (fields.size() != 5) Fail("expected five fields in " + path);
			pdw_test::CapturedPocsagMessage expected;
			expected.address = fields[0];
			expected.mode = fields[1];
			expected.messageType = fields[2];
			expected.bitrate = fields[3];
			expected.payload = fields[4];
			return expected;
		}
		Fail("no expected message in " + path);
		return pdw_test::CapturedPocsagMessage();
	}

	void ExpectEqual(const std::string& actual, const std::string& expected,
		const char* field)
	{
		if (actual != expected)
			Fail(std::string(field) + " mismatch: expected [" + expected +
				"] but received [" + actual + "]");
	}

	std::vector<int> DamageMessageWords(std::vector<int> bits)
	{
		for (std::size_t word = 2; word + 1 < bits.size() / 32; ++word)
		{
			bits[word * 32 + 2] ^= 1;
			bits[word * 32 + 12] ^= 1;
		}
		return bits;
	}

	void RunFixture(const std::string& fixtureDirectory, const char* fixtureName,
		std::size_t expectedWordCount)
	{
		const std::string baseName = fixtureName;
		const std::string metadata = ReadText(JoinPath(fixtureDirectory,
			(baseName + ".meta").c_str()));
		Expect(metadata.find("origin=synthetic") != std::string::npos,
			baseName + " provenance must be synthetic");
		Expect(metadata.find("captured_traffic=false") != std::string::npos,
			baseName + " must explicitly exclude captured traffic");

		const std::vector<int> bits = ReadBits(JoinPath(fixtureDirectory,
			(baseName + ".bits").c_str()));
		Expect(bits.size() == expectedWordCount * 32,
			baseName + " contains an unexpected number of words");
		const pdw_test::CapturedPocsagMessage expected = ReadExpected(
			JoinPath(fixtureDirectory, (baseName + ".expected").c_str()));

		pdw_test::ResetPocsagEnvironment();
		POCSAG decoder;
		decoder.frame(-1);
		for (std::vector<int>::const_iterator bit = bits.begin(); bit != bits.end(); ++bit)
			decoder.frame(*bit);

		Expect(!pdw_test::PocsagFixtureHadInvalidCodeword(),
			baseName + " failed clean BCH/parity validation");
		Expect(!pdw_test::PocsagFixtureChangedPolarity(),
			baseName + " unexpectedly inverted the decoder");
		const std::vector<pdw_test::CapturedPocsagMessage>& messages =
			pdw_test::CapturedPocsagMessages();
		Expect(messages.size() == 1, baseName + " must decode exactly one message");
		ExpectEqual(messages[0].address, expected.address, "address");
		ExpectEqual(messages[0].mode, expected.mode, "mode/function");
		ExpectEqual(messages[0].messageType, expected.messageType, "message type");
		ExpectEqual(messages[0].bitrate, expected.bitrate, "bitrate");
		ExpectEqual(messages[0].payload, expected.payload, "payload");

		const std::vector<int>& errors = pdw_test::PocsagBitErrorObservations();
		// Sync and idle are recognized by frame(), while every address/message
		// word enters ecd(). show_addr() adds one final address-quality result.
		Expect(errors.size() == expectedWordCount - 1,
			baseName + " reported an unexpected number of quality observations");
		for (std::vector<int>::const_iterator error = errors.begin(); error != errors.end(); ++error)
			Expect(*error == 0, baseName + " clean fixture reported a bit error");

		// frame() owns legacy static sync/framing state. Reset at both fixture
		// boundaries so one batch cannot influence the next case.
		decoder.frame(-1);
		// Sample the same synthetic codewords at every supported reference case.
		for (const unsigned rate : {8000u, 44100u, 48000u, 96000u, 192000u})
		{
			std::vector<float> audio((bits.size() * static_cast<std::uint64_t>(rate) + 1199) / 1200);
			for (std::size_t i = 0; i < audio.size(); ++i)
				audio[i] = bits[(std::min)(bits.size() - 1, static_cast<std::size_t>(i * 1200ULL / rate))] ? 0.75f : -0.75f;
			pdw::signal::DecoderAudioAdapter adapter;
			std::vector<float> reference, part;
			bool reset = false;
			for (std::size_t i = 0; i < audio.size(); i += 113)
			{
				Expect(adapter.Process(audio.data() + i, (std::min)(std::size_t(113), audio.size() - i), rate, false, part, reset), "synthetic rate accepted");
				reference.insert(reference.end(), part.begin(), part.end());
			}
			adapter.Flush(part); reference.insert(reference.end(), part.begin(), part.end());
			pdw_test::ResetPocsagEnvironment(); decoder.frame(-1);
			for (std::size_t i = 0; i < bits.size(); ++i)
			{
				const std::size_t centre = static_cast<std::size_t>((i + 0.5) * 44100 / 1200);
				Expect(centre < reference.size(), "finite audio includes final codeword");
				decoder.frame(reference[centre] > 0 ? 1 : 0);
			}
			const auto& adapted = pdw_test::CapturedPocsagMessages();
			Expect(adapted.size() == 1 && adapted[0].payload == expected.payload && adapted[0].address == expected.address,
				"rate-adapted synthetic recording retains exact decoded identity and text");
			decoder.frame(-1);
		}
		for (unsigned baud : {512u, 1200u, 2400u}) for (bool reversed : {false, true})
		{
			pdw_test::RunPocsagAudio(bits, baud, reversed ? 44100u : 48000u, reversed ? 997u : 32768u, reversed);
			const auto& rows = pdw_test::CapturedPocsagMessages();
			const std::string scenario = baseName + " at " + std::to_string(baud) + (reversed ? " reversed" : " normal");
			Expect(rows.size() == 1, scenario + " must produce exactly one row through IQ and the paging slicer");
			Expect(rows[0].payload == expected.payload && rows[0].address == expected.address &&
				rows[0].messageType == expected.messageType && rows[0].mode == expected.mode &&
				std::stoi(rows[0].bitrate) == static_cast<int>(baud), scenario + " must reproduce the exact expected row");
			const auto& checks = pdw_test::PocsagBitErrorObservations();
			Expect(checks.size() == expectedWordCount - 1 && std::all_of(checks.begin(), checks.end(),
				[](int errors) { return errors == 0; }), scenario + " clean IQ must require no repairs");
		}
		if (baseName == "synthetic-basic-1200")
		{
			const auto damaged = DamageMessageWords(bits);
			pdw_test::RunPocsagAudio(damaged, 1200, 48000, 997, false);
			const auto& rows = pdw_test::CapturedPocsagMessages();
			const auto& checks = pdw_test::PocsagBitErrorObservations();
			Expect(rows.size() == 1 && rows[0].payload == expected.payload && rows[0].address == expected.address,
				"Seven consecutive correctable two-bit words must retain lock and exact POCSAG text");
			Expect(!pdw_test::PocsagFixtureHadInvalidCodeword() && std::count(checks.begin(), checks.end(), 2) == 7,
				"All seven injected two-bit checks must be reported, not suppressed");
		}
	}
}

int main(int argc, char* argv[])
{
	if (argc == 4 && (std::string(argv[1]) == "--append-native-iq" || std::string(argv[1]) == "--append-truncated-iq"))
	{
		auto bits = DamageMessageWords(ReadBits(JoinPath(argv[2], "synthetic-basic-1200.bits")));
		const std::vector<int> idleWord(bits.end() - 32, bits.end());
		if (std::string(argv[1]) == "--append-native-iq")
			while (bits.size() < 17 * 32) bits.insert(bits.end(), idleWord.begin(), idleWord.end());
		const auto iq = pdw_test::SyntheticPocsagIq(bits, 1200, false);
		std::ofstream output(argv[3], std::ios::binary | std::ios::app);
		output.write(reinterpret_cast<const char*>(iq.data()), static_cast<std::streamsize>(iq.size()));
		output.close();
		return output ? 0 : 1;
	}
	if (argc != 2) Fail("usage: PDWPocsagFixtureTests <fixture-directory>");
	const std::string fixtureDirectory = argv[1];
	RunFixture(fixtureDirectory, "synthetic-basic-1200", 10);
	RunFixture(fixtureDirectory, "synthetic-numeric-1200", 4);
	RunFixture(fixtureDirectory, "synthetic-tone-1200", 3);
	std::cout << "18 clean 12500 Hz IQ-to-row cases and one seven-word BCH recovery passed (POCSAG alpha/numeric/tone).\n";
	return 0;
}
