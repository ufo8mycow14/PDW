#include "config_backup_core.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>

namespace pdw
{
namespace backup
{
namespace
{
	const unsigned char kFileMagic[8] = { 'P', 'D', 'W', 'B', 'K', 'P', '0', '1' };
	const unsigned char kPayloadMagic[8] = { 'P', 'D', 'W', 'C', 'F', 'G', '0', '1' };
	const std::uint32_t kVersion = 1;
	const std::uint32_t kIterations = 250000;
	const std::size_t kSaltSize = 16;
	const std::size_t kNonceSize = 12;
	const std::size_t kTagSize = 16;
	const std::size_t kHeaderSize = 8 + 4 + 4 + 4 + kSaltSize + kNonceSize + kTagSize;
	const std::size_t kAuthenticatedHeaderSize = kHeaderSize - kTagSize;
	const std::size_t kMaximumTextSize = 16u * 1024u * 1024u;
	const std::size_t kMaximumSecretSize = 64u * 1024u;
	const std::size_t kMaximumCredentialCount = 100;
	const std::size_t kMaximumBackupSize = 40u * 1024u * 1024u;

	void WipeCredential(BackupCredential& credential)
	{
		if (!credential.username.empty())
			OPENSSL_cleanse(&credential.username[0], credential.username.size());
		if (!credential.secret.empty())
			OPENSSL_cleanse(&credential.secret[0], credential.secret.size());
		credential.username.clear();
		credential.secret.clear();
	}

	void AppendUint32(std::vector<unsigned char>& output, std::uint32_t value)
	{
		output.push_back(static_cast<unsigned char>(value & 0xffu));
		output.push_back(static_cast<unsigned char>((value >> 8) & 0xffu));
		output.push_back(static_cast<unsigned char>((value >> 16) & 0xffu));
		output.push_back(static_cast<unsigned char>((value >> 24) & 0xffu));
	}

	bool ReadUint32(const std::vector<unsigned char>& input, std::size_t& position,
		std::uint32_t& value)
	{
		if (position > input.size() || input.size() - position < 4) return false;
		value = static_cast<std::uint32_t>(input[position]) |
			(static_cast<std::uint32_t>(input[position + 1]) << 8) |
			(static_cast<std::uint32_t>(input[position + 2]) << 16) |
			(static_cast<std::uint32_t>(input[position + 3]) << 24);
		position += 4;
		return true;
	}

	bool AppendField(std::vector<unsigned char>& output, const unsigned char* data,
		std::size_t size)
	{
		if (size > static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)()))
			return false;
		AppendUint32(output, static_cast<std::uint32_t>(size));
		if (size) output.insert(output.end(), data, data + size);
		return true;
	}

	bool AppendString(std::vector<unsigned char>& output, const std::string& value)
	{
		return AppendField(output,
			reinterpret_cast<const unsigned char*>(value.data()), value.size());
	}

	bool ReadField(const std::vector<unsigned char>& input, std::size_t& position,
		std::vector<unsigned char>& value, std::size_t maximum)
	{
		std::uint32_t size = 0;
		if (!ReadUint32(input, position, size) || size > maximum ||
			position > input.size() || input.size() - position < size) return false;
		value.assign(input.begin() + position, input.begin() + position + size);
		position += size;
		return true;
	}

	bool ReadString(const std::vector<unsigned char>& input, std::size_t& position,
		std::string& value, std::size_t maximum)
	{
		std::vector<unsigned char> bytes;
		if (!ReadField(input, position, bytes, maximum)) return false;
		value.assign(bytes.begin(), bytes.end());
		if (!bytes.empty()) OPENSSL_cleanse(&bytes[0], bytes.size());
		return value.find('\0') == std::string::npos;
	}

	bool SerializeContents(const BackupContents& contents,
		std::vector<unsigned char>& output, std::string& error)
	{
		output.clear();
		if (contents.settings.empty() || contents.settings.size() > kMaximumTextSize ||
			contents.filters.size() > kMaximumTextSize ||
			contents.credentials.size() > kMaximumCredentialCount)
		{
			error = "The configuration is missing or too large to back up.";
			return false;
		}
		output.insert(output.end(), kPayloadMagic, kPayloadMagic + sizeof(kPayloadMagic));
		AppendUint32(output, static_cast<std::uint32_t>(contents.credentials.size()));
		if (!AppendString(output, contents.settings) || !AppendString(output, contents.filters))
		{
			error = "The configuration is too large to back up.";
			return false;
		}
		for (std::size_t index = 0; index < contents.credentials.size(); ++index)
		{
			const BackupCredential& credential = contents.credentials[index];
			for (std::size_t existing = 0; existing < index; ++existing)
				if (contents.credentials[existing].name == credential.name)
				{
					error = "A saved credential is duplicated in the backup.";
					return false;
				}
			if (!IsAllowedCredentialName(credential.name) || credential.username.size() > 4096 ||
				credential.secret.size() > kMaximumSecretSize ||
				!AppendString(output, credential.name) ||
				!AppendString(output, credential.username) ||
				!AppendField(output, credential.secret.empty() ? NULL : &credential.secret[0],
					credential.secret.size()))
			{
				error = "A saved credential cannot be included in the backup.";
				return false;
			}
		}
		if (output.size() > kMaximumBackupSize)
		{
			error = "The configuration backup is too large.";
			return false;
		}
		return true;
	}

	bool DeserializeContents(const std::vector<unsigned char>& input,
		BackupContents& contents, std::string& error)
	{
		WipeBackupContents(contents);
		if (input.size() < sizeof(kPayloadMagic) + 4 ||
			memcmp(&input[0], kPayloadMagic, sizeof(kPayloadMagic)) != 0)
		{
			error = "The backup payload is not a supported PDW configuration.";
			return false;
		}
		std::size_t position = sizeof(kPayloadMagic);
		std::uint32_t credentialCount = 0;
		if (!ReadUint32(input, position, credentialCount) ||
			credentialCount > kMaximumCredentialCount ||
			!ReadString(input, position, contents.settings, kMaximumTextSize) ||
			!ReadString(input, position, contents.filters, kMaximumTextSize) ||
			contents.settings.empty())
		{
			error = "The backup payload is incomplete or invalid.";
			WipeBackupContents(contents);
			return false;
		}
		for (std::uint32_t index = 0; index < credentialCount; ++index)
		{
			BackupCredential credential;
			if (!ReadString(input, position, credential.name, 128) ||
				!ReadString(input, position, credential.username, 4096) ||
				!ReadField(input, position, credential.secret, kMaximumSecretSize) ||
				!IsAllowedCredentialName(credential.name))
			{
				WipeCredential(credential);
				error = "The backup contains an invalid credential record.";
				WipeBackupContents(contents);
				return false;
			}
			for (std::size_t existing = 0; existing < contents.credentials.size(); ++existing)
				if (contents.credentials[existing].name == credential.name)
				{
					WipeCredential(credential);
					error = "The backup contains a duplicate credential record.";
					WipeBackupContents(contents);
					return false;
				}
			contents.credentials.push_back(credential);
		}
		if (position != input.size())
		{
			error = "The backup payload has unexpected trailing data.";
			WipeBackupContents(contents);
			return false;
		}
		return true;
	}

	bool DeriveKey(const std::string& password, const unsigned char* salt,
		std::uint32_t iterations, unsigned char* key, std::string& error)
	{
		if (password.size() < 8 || password.size() > 1024 || iterations < 100000 ||
			iterations > 2000000)
		{
			error = "The backup password or encryption parameters are invalid.";
			return false;
		}
		if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt,
			static_cast<int>(kSaltSize), static_cast<int>(iterations), EVP_sha256(), 32, key) != 1)
		{
			error = "PDW could not derive the backup encryption key.";
			return false;
		}
		return true;
	}
}

bool IsAllowedCredentialName(const std::string& name)
{
	if (name.empty() || name.size() > 128 || name.find(':') != std::string::npos ||
		name.find('\0') != std::string::npos) return false;
	return name == "PDW File Transfer" || name == "PDW FTP" ||
		name.compare(0, 12, "PDW Apprise ") == 0 ||
		name.compare(0, 15, "PDW Publishing ") == 0 ||
		name.compare(0, 16, "PDW Data Output ") == 0;
}

bool CreateEncryptedBackup(const BackupContents& contents, const std::string& password,
	std::vector<unsigned char>& output, std::string& error)
{
	error.clear();
	output.clear();
	std::vector<unsigned char> plain;
	if (!SerializeContents(contents, plain, error))
	{
		if (!plain.empty()) OPENSSL_cleanse(&plain[0], plain.size());
		return false;
	}

	unsigned char salt[kSaltSize] = {};
	unsigned char nonce[kNonceSize] = {};
	unsigned char key[32] = {};
	unsigned char tag[kTagSize] = {};
	EVP_CIPHER_CTX* context = NULL;
	bool success = false;
	do
	{
		if (RAND_bytes(salt, static_cast<int>(sizeof(salt))) != 1 ||
			RAND_bytes(nonce, static_cast<int>(sizeof(nonce))) != 1)
		{
			error = "PDW could not create secure random backup encryption data.";
			break;
		}
		if (!DeriveKey(password, salt, kIterations, key, error)) break;
		output.insert(output.end(), kFileMagic, kFileMagic + sizeof(kFileMagic));
		AppendUint32(output, kVersion);
		AppendUint32(output, kIterations);
		AppendUint32(output, static_cast<std::uint32_t>(plain.size()));
		output.insert(output.end(), salt, salt + sizeof(salt));
		output.insert(output.end(), nonce, nonce + sizeof(nonce));
		output.insert(output.end(), kTagSize, 0);
		output.resize(kHeaderSize + plain.size());

		context = EVP_CIPHER_CTX_new();
		int produced = 0;
		int finalProduced = 0;
		if (!context || EVP_EncryptInit_ex(context, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1 ||
			EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN,
				static_cast<int>(sizeof(nonce)), NULL) != 1 ||
			EVP_EncryptInit_ex(context, NULL, NULL, key, nonce) != 1 ||
			EVP_EncryptUpdate(context, NULL, &produced, &output[0],
				static_cast<int>(kAuthenticatedHeaderSize)) != 1 ||
			EVP_EncryptUpdate(context, &output[kHeaderSize], &produced, &plain[0],
				static_cast<int>(plain.size())) != 1 ||
			EVP_EncryptFinal_ex(context, &output[kHeaderSize + produced], &finalProduced) != 1 ||
			static_cast<std::size_t>(produced + finalProduced) != plain.size() ||
			EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_GET_TAG,
				static_cast<int>(sizeof(tag)), tag) != 1)
		{
			error = "PDW could not encrypt the configuration backup.";
			break;
		}
		memcpy(&output[kAuthenticatedHeaderSize], tag, sizeof(tag));
		success = true;
	} while (false);

	if (context) EVP_CIPHER_CTX_free(context);
	OPENSSL_cleanse(key, sizeof(key));
	OPENSSL_cleanse(tag, sizeof(tag));
	if (!plain.empty()) OPENSSL_cleanse(&plain[0], plain.size());
	if (!success) output.clear();
	return success;
}

bool OpenEncryptedBackup(const std::vector<unsigned char>& input, const std::string& password,
	BackupContents& contents, std::string& error)
{
	error.clear();
	WipeBackupContents(contents);
	if (input.size() < kHeaderSize || input.size() > kMaximumBackupSize + kHeaderSize ||
		memcmp(&input[0], kFileMagic, sizeof(kFileMagic)) != 0)
	{
		error = "This is not a supported PDW configuration backup.";
		return false;
	}
	std::size_t position = sizeof(kFileMagic);
	std::uint32_t version = 0;
	std::uint32_t iterations = 0;
	std::uint32_t encryptedSize = 0;
	if (!ReadUint32(input, position, version) || !ReadUint32(input, position, iterations) ||
		!ReadUint32(input, position, encryptedSize) || version != kVersion ||
		encryptedSize > kMaximumBackupSize || input.size() != kHeaderSize + encryptedSize)
	{
		error = "The PDW backup version or size is not supported.";
		return false;
	}
	const unsigned char* salt = &input[position];
	position += kSaltSize;
	const unsigned char* nonce = &input[position];
	position += kNonceSize;
	const unsigned char* tag = &input[position];

	unsigned char key[32] = {};
	if (!DeriveKey(password, salt, iterations, key, error)) return false;
	std::vector<unsigned char> plain(encryptedSize);
	EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
	int produced = 0;
	int finalProduced = 0;
	const bool decrypted = context &&
		EVP_DecryptInit_ex(context, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1 &&
		EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN,
			static_cast<int>(kNonceSize), NULL) == 1 &&
		EVP_DecryptInit_ex(context, NULL, NULL, key, nonce) == 1 &&
		EVP_DecryptUpdate(context, NULL, &produced, &input[0],
			static_cast<int>(kAuthenticatedHeaderSize)) == 1 &&
		EVP_DecryptUpdate(context, plain.empty() ? NULL : &plain[0], &produced,
			&input[kHeaderSize], static_cast<int>(encryptedSize)) == 1 &&
		EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_TAG,
			static_cast<int>(kTagSize), const_cast<unsigned char*>(tag)) == 1 &&
		EVP_DecryptFinal_ex(context,
			plain.empty() ? NULL : &plain[produced], &finalProduced) == 1 &&
		static_cast<std::size_t>(produced + finalProduced) == plain.size();
	if (context) EVP_CIPHER_CTX_free(context);
	OPENSSL_cleanse(key, sizeof(key));
	if (!decrypted)
	{
		if (!plain.empty()) OPENSSL_cleanse(&plain[0], plain.size());
		error = "The password is incorrect or the backup has been changed or damaged.";
		return false;
	}
	const bool parsed = DeserializeContents(plain, contents, error);
	if (!plain.empty()) OPENSSL_cleanse(&plain[0], plain.size());
	return parsed;
}

void WipeBackupContents(BackupContents& contents)
{
	if (!contents.settings.empty()) OPENSSL_cleanse(&contents.settings[0], contents.settings.size());
	if (!contents.filters.empty()) OPENSSL_cleanse(&contents.filters[0], contents.filters.size());
	contents.settings.clear();
	contents.filters.clear();
	for (std::size_t index = 0; index < contents.credentials.size(); ++index)
	{
		WipeCredential(contents.credentials[index]);
	}
	contents.credentials.clear();
}

} // namespace backup
} // namespace pdw
