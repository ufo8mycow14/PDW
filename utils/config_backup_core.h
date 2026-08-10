#ifndef PDW_CONFIG_BACKUP_CORE_H
#define PDW_CONFIG_BACKUP_CORE_H

#include <string>
#include <vector>

namespace pdw
{
namespace backup
{

struct BackupCredential
{
	std::string name;
	std::string username;
	std::vector<unsigned char> secret;
};

struct BackupContents
{
	std::string settings;
	std::string filters;
	std::vector<BackupCredential> credentials;
};

bool IsAllowedCredentialName(const std::string& name);
bool CreateEncryptedBackup(const BackupContents& contents, const std::string& password,
	std::vector<unsigned char>& output, std::string& error);
bool OpenEncryptedBackup(const std::vector<unsigned char>& input, const std::string& password,
	BackupContents& contents, std::string& error);
void WipeBackupContents(BackupContents& contents);

} // namespace backup
} // namespace pdw

#endif
