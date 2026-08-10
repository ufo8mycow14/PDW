#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <utility>

namespace
{
typedef std::pair<std::string, std::string> IniKey;

std::string Trim(const std::string& value)
{
	std::string::size_type first = 0;
	while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
	std::string::size_type last = value.size();
	while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
	return value.substr(first, last - first);
}

std::string Lower(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(),
		[](unsigned char character) { return static_cast<char>(std::tolower(character)); });
	return value;
}

void Require(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		std::exit(1);
	}
}
}

int main(int argc, char** argv)
{
	Require(argc == 2, "expected the packaged PDW.INI path");
	std::ifstream input(argv[1], std::ios::binary);
	Require(input.good(), "could not open the packaged PDW.INI");

	std::map<IniKey, std::string> values;
	std::string section;
	std::string line;
	while (std::getline(input, line))
	{
		if (!line.empty() && line[line.size() - 1] == '\r') line.resize(line.size() - 1);
		const std::string trimmed = Trim(line);
		if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') continue;
		if (trimmed.size() >= 2 && trimmed[0] == '[' && trimmed[trimmed.size() - 1] == ']')
		{
			section = Lower(Trim(trimmed.substr(1, trimmed.size() - 2)));
			continue;
		}
		const std::string::size_type equals = trimmed.find('=');
		if (equals == std::string::npos) continue;
		values[IniKey(section, Lower(Trim(trimmed.substr(0, equals))))] = Trim(trimmed.substr(equals + 1));
	}

	auto Value = [&values](const char* sectionName, const char* keyName) -> std::string {
		const std::map<IniKey, std::string>::const_iterator found =
			values.find(IniKey(Lower(sectionName), Lower(keyName)));
		Require(found != values.end(), std::string("missing packaged setting [") + sectionName + "] " + keyName);
		return found->second;
	};
	auto RequireZero = [&Value](const char* sectionName, const char* keyName) {
		Require(Value(sectionName, keyName) == "0",
			std::string("packaged setting must default off: [") + sectionName + "] " + keyName);
	};

	RequireZero("SMTP", "Enable");
	RequireZero("Apprise", "Enable");
	RequireZero("FTP", "Enable");
	RequireZero("Publishing", "Enable");
	RequireZero("Publishing", "PermissionAcknowledged");
	RequireZero("DataOutputs", "Enable");
	RequireZero("DataOutputs", "PermissionAcknowledged");
	RequireZero("MQTT", "Enable");
	RequireZero("MQTT", "AllowInsecure");
	RequireZero("SQLiteOutput", "Enable");
	RequireZero("MySQLOdbc", "Enable");
	RequireZero("TelnetOutput", "Enable");
	RequireZero("TelnetOutput", "AllowRemote");
	RequireZero("WindowsToast", "Enable");
	RequireZero("PDW", "FlexFragmentReassembly");

	Require(Value("Publishing", "FilteredOnly") == "1", "publishing must default to filtered messages");
	Require(Value("DataOutputs", "FilteredOnly") == "1", "data outputs must default to filtered messages");
	Require(Value("DataOutputs", "MaskAddress") == "1", "data outputs must mask addresses by default");
	Require(Value("DataOutputs", "IncludeMessage") == "0", "data outputs must omit message text by default");
	Require(Value("TelnetOutput", "BindAddress") == "127.0.0.1", "Telnet must bind to loopback by default");
	Require(Value("SMTP", "Password").empty(), "packaged SMTP password must be empty");
	Require(Value("Publishing", "WebhookUrl").empty(), "packaged webhook URL must be empty");
	Require(Value("FTP", "Server").empty(), "packaged FTP server must be empty");
	Require(Value("MQTT", "Username").empty(), "packaged MQTT username must be empty");
	Require(Value("MySQLOdbc", "Username").empty(), "packaged ODBC username must be empty");

	std::cout << "release defaults tests passed\n";
	return 0;
}
