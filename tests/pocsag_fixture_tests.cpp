#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>

#include "headers/pdw.h"
#include "pocsag_test_environment.h"

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
	}
}

int main(int argc, char* argv[])
{
	if (argc != 2) Fail("usage: PDWPocsagFixtureTests <fixture-directory>");
	const std::string fixtureDirectory = argv[1];
	RunFixture(fixtureDirectory, "synthetic-basic-1200", 10);
	RunFixture(fixtureDirectory, "synthetic-numeric-1200", 4);
	RunFixture(fixtureDirectory, "synthetic-tone-1200", 3);
	std::cout << "synthetic POCSAG alpha/numeric/tone fixture regression passed\n";
	return 0;
}
