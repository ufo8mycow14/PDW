#include "config_backup_core.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	int g_failures = 0;

	void Expect(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAILED: " << message << '\n';
			++g_failures;
		}
	}

	pdw::backup::BackupContents RepresentativeConfiguration()
	{
		pdw::backup::BackupContents contents;
		contents.settings =
			"[General]\r\nLanguage=English\r\n"
			"[RTL-SDR]\r\nFrequency=148337500\r\nPPM=-12\r\n"
			"[SMTP]\r\nUsername=dispatch@example.test\r\nPassword=smtp-secret-91\r\n"
			"[Publishing]\r\nWebhook=https://example.test/pager\r\n";
		contents.filters = "[Filter 1]\r\nName=Emergency\r\nEnabled=1\r\n";

		pdw::backup::BackupCredential credential;
		credential.name = "PDW File Transfer";
		credential.username = "ftp-operator";
		credential.secret.assign({'f', 't', 'p', '-', 's', 'e', 'c', 'r', 'e', 't'});
		contents.credentials.push_back(credential);

		credential.name = "PDW Apprise Endpoint 1";
		credential.username = "pager-user";
		credential.secret.assign({'a', 'p', 'p', 'r', 'i', 's', 'e', '-', 't', 'o', 'k', 'e', 'n'});
		contents.credentials.push_back(credential);

		credential.name = "PDW Publishing Bearer";
		credential.username.clear();
		credential.secret.assign({'b', 'e', 'a', 'r', 'e', 'r', '-', 's', 'e', 'c', 'r', 'e', 't'});
		contents.credentials.push_back(credential);

		credential.name = "PDW Data Output MQTT";
		credential.username = "mqtt-user";
		credential.secret.assign({'m', 'q', 't', 't', '-', 's', 'e', 'c', 'r', 'e', 't'});
		contents.credentials.push_back(credential);
		return contents;
	}

	bool ContainsText(const std::vector<unsigned char>& bytes, const std::string& text)
	{
		return std::search(bytes.begin(), bytes.end(), text.begin(), text.end()) != bytes.end();
	}
}

int main()
{
	const std::string password = "correct horse battery staple";
	const pdw::backup::BackupContents original = RepresentativeConfiguration();
	std::vector<unsigned char> encrypted;
	std::string error;
	Expect(pdw::backup::CreateEncryptedBackup(original, password, encrypted, error),
		"representative configuration encrypts");
	Expect(!encrypted.empty(), "encrypted backup is not empty");
	Expect(!ContainsText(encrypted, "smtp-secret-91"), "INI password is not visible");
	Expect(!ContainsText(encrypted, "ftp-operator"), "credential username is not visible");
	Expect(!ContainsText(encrypted, "148337500"), "frequency is not visible");

	pdw::backup::BackupContents restored;
	Expect(pdw::backup::OpenEncryptedBackup(encrypted, password, restored, error),
		"encrypted backup decrypts");
	Expect(restored.settings == original.settings, "settings round trip exactly");
	Expect(restored.filters == original.filters, "filters round trip exactly");
	Expect(restored.credentials.size() == original.credentials.size(),
		"all credentials round trip");
	if (restored.credentials.size() == original.credentials.size())
	{
		for (std::size_t index = 0; index < original.credentials.size(); ++index)
		{
			Expect(restored.credentials[index].name == original.credentials[index].name,
				"credential name round trips");
			Expect(restored.credentials[index].username == original.credentials[index].username,
				"credential username round trips");
			Expect(restored.credentials[index].secret == original.credentials[index].secret,
				"credential secret round trips");
		}
	}
	pdw::backup::WipeBackupContents(restored);

	Expect(!pdw::backup::OpenEncryptedBackup(encrypted, "wrong password", restored, error),
		"wrong password is rejected");
	Expect(restored.settings.empty() && restored.credentials.empty(),
		"failed decrypt leaves no restored content");

	std::vector<unsigned char> changed = encrypted;
	if (!changed.empty()) changed[changed.size() - 1] ^= 0x40;
	Expect(!pdw::backup::OpenEncryptedBackup(changed, password, restored, error),
		"tampered backup is rejected");

	std::vector<unsigned char> truncated(encrypted.begin(), encrypted.begin() + encrypted.size() / 2);
	Expect(!pdw::backup::OpenEncryptedBackup(truncated, password, restored, error),
		"truncated backup is rejected");

	std::vector<unsigned char> shortPasswordOutput;
	Expect(!pdw::backup::CreateEncryptedBackup(original, "short", shortPasswordOutput, error),
		"short password is rejected");

	pdw::backup::BackupContents invalid = RepresentativeConfiguration();
	invalid.credentials[0].name = "Unrelated Application Password";
	Expect(!pdw::backup::CreateEncryptedBackup(invalid, password, shortPasswordOutput, error),
		"unrecognised credential target is rejected");

	pdw::backup::BackupContents duplicate = RepresentativeConfiguration();
	duplicate.credentials.push_back(duplicate.credentials[0]);
	Expect(!pdw::backup::CreateEncryptedBackup(duplicate, password, shortPasswordOutput, error),
		"duplicate credential target is rejected");

	pdw::backup::WipeBackupContents(invalid);
	pdw::backup::WipeBackupContents(duplicate);
	std::cout << "Configuration backup core tests completed with " << g_failures
		<< " failure(s).\n";
	return g_failures ? 1 : 0;
}
