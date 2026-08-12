#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace
{
typedef std::pair<std::string, std::string> IniKey;
typedef std::map<IniKey, std::string> IniValues;

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

IniValues ReadIni(const char* path)
{
	std::ifstream input(path, std::ios::binary);
	Require(input.good(), std::string("could not open packaged profile: ") + path);
	IniValues values;
	std::set<std::string> sections;
	std::set<IniKey> keys;
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
			Require(!section.empty(), std::string("empty INI section in ") + path);
			Require(sections.insert(section).second,
				std::string("duplicate INI section [") + section + "] in " + path);
			continue;
		}
		const std::string::size_type equals = trimmed.find('=');
		if (equals == std::string::npos) continue;
		const IniKey key(section, Lower(Trim(trimmed.substr(0, equals))));
		Require(!key.first.empty() && !key.second.empty(),
			std::string("setting outside a named section or with an empty key in ") + path);
		Require(keys.insert(key).second, std::string("duplicate INI key [") +
			key.first + "] " + key.second + " in " + path);
		values[key] = Trim(trimmed.substr(equals + 1));
	}
	return values;
}

std::string Value(const IniValues& values, const char* sectionName, const char* keyName)
{
	const IniValues::const_iterator found =
		values.find(IniKey(Lower(sectionName), Lower(keyName)));
	Require(found != values.end(), std::string("missing packaged setting [") +
		sectionName + "] " + keyName);
	return found->second;
}

void RequireZero(const IniValues& values, const char* sectionName, const char* keyName)
{
	Require(Value(values, sectionName, keyName) == "0",
		std::string("packaged setting must default off: [") + sectionName + "] " + keyName);
}

void RequireSafeOutputs(const IniValues& values)
{
	RequireZero(values, "SMTP", "Enable");
	RequireZero(values, "Apprise", "Enable");
	RequireZero(values, "FTP", "Enable");
	RequireZero(values, "Publishing", "Enable");
	RequireZero(values, "Publishing", "PermissionAcknowledged");
	RequireZero(values, "DataOutputs", "Enable");
	RequireZero(values, "DataOutputs", "PermissionAcknowledged");
	RequireZero(values, "MQTT", "Enable");
	RequireZero(values, "MQTT", "AllowInsecure");
	RequireZero(values, "SQLiteOutput", "Enable");
	RequireZero(values, "MySQLOdbc", "Enable");
	RequireZero(values, "TelnetOutput", "Enable");
	RequireZero(values, "TelnetOutput", "AllowRemote");
	RequireZero(values, "WindowsToast", "Enable");
	RequireZero(values, "PDW", "FlexFragmentReassembly");
	RequireZero(values, "PDW", "RtlSignalConditioner");
	RequireZero(values, "MessageArchive", "EnableHistory");
	RequireZero(values, "MessageArchive", "IncludeMessage");
	RequireZero(values, "LiveDashboard", "Enable");
	RequireZero(values, "GatewayOutbox", "Enable");
	Require(Value(values, "GatewayOutbox", "ReceiverId").empty(),
		"gateway receiver identity must be operator-approved, not packaged");

	Require(Value(values, "Publishing", "FilteredOnly") == "1", "publishing must default to filtered messages");
	Require(Value(values, "DataOutputs", "FilteredOnly") == "1", "data outputs must default to filtered messages");
	Require(Value(values, "DataOutputs", "MaskAddress") == "1", "data outputs must mask addresses by default");
	Require(Value(values, "DataOutputs", "IncludeMessage") == "0", "data outputs must omit message text by default");
	Require(Value(values, "TelnetOutput", "BindAddress") == "127.0.0.1", "Telnet must bind to loopback by default");
	Require(Value(values, "SMTP", "Password").empty(), "packaged SMTP password must be empty");
	Require(Value(values, "Publishing", "WebhookUrl").empty(), "packaged webhook URL must be empty");
	Require(Value(values, "FTP", "Server").empty(), "packaged FTP server must be empty");
	Require(Value(values, "MQTT", "Username").empty(), "packaged MQTT username must be empty");
	Require(Value(values, "MySQLOdbc", "Username").empty(), "packaged ODBC username must be empty");
}
}

int main(int argc, char** argv)
{
	Require(argc == 3, "expected Standard and Adelaide FLEX packaged profile paths");
	const IniValues standard = ReadIni(argv[1]);
	const IniValues adelaide = ReadIni(argv[2]);
	RequireSafeOutputs(standard);
	RequireSafeOutputs(adelaide);

	Require(Value(standard, "PDW", "AudioEnabled") == "1",
		"the Standard profile must retain established local-audio startup behavior");
	Require(Value(standard, "InputProfile", "PresetId").empty(),
		"the general clean-install profile must not silently select a radio preset");
	Require(Value(standard, "InputProfile", "PresetName").empty() &&
		Value(standard, "InputProfile", "DeviceEndpointId").empty() &&
		Value(standard, "InputProfile", "DeviceFriendlyName").empty(),
		"the Standard profile must not carry a machine-specific audio identity");
	Require(Value(standard, "InputProfile", "IdentityInvalid") == "0" &&
		Value(adelaide, "InputProfile", "IdentityInvalid") == "0",
		"packaged input profiles must start with a valid identity state");

	Require(Value(adelaide, "InputProfile", "PresetId") ==
		"sdrsharp-vbcable-adelaide-flex-v1", "Adelaide preset ID mismatch");
	Require(Value(adelaide, "InputProfile", "PresetName") ==
		"SDR# + VB-Audio Cable (Adelaide FLEX)", "Adelaide preset name mismatch");
	Require(Value(adelaide, "InputProfile", "DeviceFriendlyName") ==
		"CABLE Output (VB-Audio Virtual Cable)", "Adelaide capture endpoint name mismatch");
	Require(Value(adelaide, "InputProfile", "DeviceEndpointId").empty(),
		"a machine-specific Windows endpoint ID must not be packaged");

	const char* allowedDifferences[][4] = {
		{ "PDW", "AudioConfiguration", "5", "0" },
		{ "PDW", "BTSYNC", "52428", "13107" },
		{ "PDW", "Flex3200", "1", "0" },
		{ "PDW", "Flex6400", "1", "0" },
		{ "PDW", "InvertData", "0", "1" },
		{ "PDW", "MINMSG", "5", "15" },
		{ "PDW", "Percent", "65", "69" },
		{ "PDW", "PocsagShowBoth", "0", "1" },
		{ "PDW", "ShowCFS", "0", "1" },
		{ "PDW", "Threshold1600", "0", "2" },
		{ "InputProfile", "PresetId", "", "sdrsharp-vbcable-adelaide-flex-v1" },
		{ "InputProfile", "PresetName", "", "SDR# + VB-Audio Cable (Adelaide FLEX)" },
		{ "InputProfile", "DeviceFriendlyName", "", "CABLE Output (VB-Audio Virtual Cable)" }
	};
	for (std::size_t index = 0;
		index < sizeof(allowedDifferences) / sizeof(allowedDifferences[0]); ++index)
	{
		Require(Value(standard, allowedDifferences[index][0], allowedDifferences[index][1]) ==
			allowedDifferences[index][2],
			std::string("unexpected Standard value for ") + allowedDifferences[index][1]);
		Require(Value(adelaide, allowedDifferences[index][0], allowedDifferences[index][1]) ==
			allowedDifferences[index][3],
			std::string("unexpected Adelaide value for ") + allowedDifferences[index][1]);
	}
	Require(standard.size() == adelaide.size(),
		"the named profile must not add or remove unrelated settings");
	for (IniValues::const_iterator entry = standard.begin(); entry != standard.end(); ++entry)
	{
		const IniValues::const_iterator named = adelaide.find(entry->first);
		Require(named != adelaide.end(), "the named profile is missing a Standard setting");
		if (entry->second == named->second) continue;
		bool allowed = false;
		for (std::size_t index = 0;
			index < sizeof(allowedDifferences) / sizeof(allowedDifferences[0]); ++index)
		{
			const IniKey key(Lower(allowedDifferences[index][0]),
				Lower(allowedDifferences[index][1]));
			if (entry->first != key) continue;
			Require(entry->second == allowedDifferences[index][2] &&
				named->second == allowedDifferences[index][3],
				std::string("unexpected Adelaide value for ") + allowedDifferences[index][1]);
			allowed = true;
			break;
		}
		Require(allowed, std::string("unapproved Adelaide profile difference: [") +
			entry->first.first + "] " + entry->first.second);
	}

	std::cout << "release defaults tests passed\n";
	return 0;
}
