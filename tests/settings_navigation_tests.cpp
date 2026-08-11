#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

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

	std::string ReadFile(const char* path)
	{
		std::ifstream input(path, std::ios::binary);
		Expect(input.good(), "settings source file opens");
		std::ostringstream contents;
		contents << input.rdbuf();
		return contents.str();
	}

	std::size_t Count(const std::string& text, const std::string& needle)
	{
		std::size_t count = 0;
		std::size_t offset = 0;
		while ((offset = text.find(needle, offset)) != std::string::npos)
		{
			++count;
			offset += needle.size();
		}
		return count;
	}
}

int main(int argc, char** argv)
{
	Expect(argc == 3, "settings navigation test receives source and header paths");
	const std::string source = ReadFile(argv[1]);
	const std::string header = ReadFile(argv[2]);

	Expect(Count(source, "PDW_SETTINGS_SIGNAL, IDM_SIGNAL_SOURCES") == 1,
		"Signal and radio exposes one consolidated signal-source editor card");
	Expect(source.find("PDW_SETTINGS_FILTERS, IDM_FILTEROPTIONS") == std::string::npos,
		"Filters does not expose a duplicate card for the consolidated editor");
	Expect(Count(source, "PDW_SETTINGS_FILTERS, IDM_FILTERS") == 1,
		"Filters exposes one Capcode Directory card");
	Expect(Count(source, "PDW_SETTINGS_DATA_OUTPUTS, IDM_MAIL") == 1,
		"Email is grouped under Data outputs");
	Expect(Count(source, "PDW_SETTINGS_DATA_OUTPUTS, IDM_APPRISE") == 1,
		"Push and Windows notifications are grouped under Data outputs");
	Expect(source.find("{ \"Notifications\",") == std::string::npos,
		"empty Notifications page is removed");
	Expect(header.find("PDW_SETTINGS_NOTIFICATIONS") == std::string::npos,
		"removed Notifications page is absent from the navigation enum");

	std::cout << "Settings navigation tests passed\n";
	return 0;
}
