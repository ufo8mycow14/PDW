#include "gateway_outbox_core.h"

#ifndef STRICT
#define STRICT 1
#endif
#include <windows.h>
#include <bcrypt.h>
#include <winsqlite/winsqlite3.h>

#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <set>
#include <sstream>
#include <vector>

namespace pdw
{
namespace gateway
{
namespace
{
	bool BindText(sqlite3_stmt* statement, int index, const std::string& value)
	{
		return sqlite3_bind_text(statement, index, value.c_str(),
			static_cast<int>(value.size()), SQLITE_TRANSIENT) == SQLITE_OK;
	}

	std::string ColumnText(sqlite3_stmt* statement, int index)
	{
		const unsigned char* value = sqlite3_column_text(statement, index);
		return value ? reinterpret_cast<const char*>(value) : std::string();
	}

	bool ReadInteger(sqlite3* database, const char* sql, long long& value,
		std::string& error)
	{
		sqlite3_stmt* statement = NULL;
		if (sqlite3_prepare_v2(database, sql, -1, &statement, NULL) != SQLITE_OK)
		{
			error = sqlite3_errmsg(database);
			return false;
		}
		const int step = sqlite3_step(statement);
		if (step == SQLITE_ROW) value = sqlite3_column_int64(statement, 0);
		const int finalization = sqlite3_finalize(statement);
		if (step != SQLITE_ROW || finalization != SQLITE_OK)
		{
			error = "Gateway outbox metadata could not be read.";
			return false;
		}
		return true;
	}

	bool ReadText(sqlite3* database, const char* sql, std::string& value,
		std::string& error)
	{
		sqlite3_stmt* statement = NULL;
		if (sqlite3_prepare_v2(database, sql, -1, &statement, NULL) != SQLITE_OK)
		{
			error = sqlite3_errmsg(database);
			return false;
		}
		const int step = sqlite3_step(statement);
		if (step == SQLITE_ROW) value = ColumnText(statement, 0);
		const int finalization = sqlite3_finalize(statement);
		if (step != SQLITE_ROW || finalization != SQLITE_OK)
		{
			error = "Gateway outbox metadata text could not be read.";
			return false;
		}
		return true;
	}

	bool DatabaseIsEmpty(sqlite3* database, bool& empty, std::string& error)
	{
		long long count = 0;
		if (!ReadInteger(database,
			"SELECT COUNT(*) FROM sqlite_master WHERE name NOT GLOB 'sqlite_*';",
			count, error)) return false;
		empty = count == 0;
		return true;
	}

	std::string JsonEscape(const std::string& value)
	{
		std::ostringstream output;
		for (std::string::const_iterator character = value.begin();
			character != value.end(); ++character)
		{
			const unsigned char byte = static_cast<unsigned char>(*character);
			switch (byte)
			{
				case '"': output << "\\\""; break;
				case '\\': output << "\\\\"; break;
				case '\b': output << "\\b"; break;
				case '\f': output << "\\f"; break;
				case '\n': output << "\\n"; break;
				case '\r': output << "\\r"; break;
				case '\t': output << "\\t"; break;
				default:
					if (byte < 0x20)
					{
						static const char hex[] = "0123456789abcdef";
						output << "\\u00" << hex[byte >> 4] << hex[byte & 15];
					}
					else output << *character;
			}
		}
		return output.str();
	}

	std::string Sha256Hex(const std::string& value)
	{
		BCRYPT_ALG_HANDLE algorithm = NULL;
		BCRYPT_HASH_HANDLE hash = NULL;
		DWORD objectBytes = 0;
		DWORD digestBytes = 0;
		DWORD returned = 0;
		std::vector<unsigned char> object;
		std::vector<unsigned char> digest;
		if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
			NULL, 0) != 0) return std::string();
		if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
			reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes), &returned, 0) != 0 ||
			BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
			reinterpret_cast<PUCHAR>(&digestBytes), sizeof(digestBytes), &returned, 0) != 0)
		{
			BCryptCloseAlgorithmProvider(algorithm, 0);
			return std::string();
		}
		object.resize(objectBytes);
		digest.resize(digestBytes);
		if (BCryptCreateHash(algorithm, &hash, &object[0], objectBytes,
			NULL, 0, 0) != 0 ||
			BCryptHashData(hash,
				reinterpret_cast<PUCHAR>(const_cast<char*>(value.data())),
				static_cast<ULONG>(value.size()), 0) != 0 ||
			BCryptFinishHash(hash, &digest[0], digestBytes, 0) != 0)
		{
			if (hash) BCryptDestroyHash(hash);
			BCryptCloseAlgorithmProvider(algorithm, 0);
			return std::string();
		}
		BCryptDestroyHash(hash);
		BCryptCloseAlgorithmProvider(algorithm, 0);
		static const char hex[] = "0123456789abcdef";
		std::string result;
		result.reserve(digest.size() * 2);
		for (std::vector<unsigned char>::const_iterator byte = digest.begin();
			byte != digest.end(); ++byte)
		{
			result.push_back(hex[*byte >> 4]);
			result.push_back(hex[*byte & 15]);
		}
		return result;
	}

	unsigned long long FileSize(const std::string& path)
	{
		WIN32_FILE_ATTRIBUTE_DATA data = {};
		if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data)) return 0;
		return (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) |
			data.nFileSizeLow;
	}
}

GatewayEvent::GatewayEvent() : receiverSequence(0), schemaVersion(GATEWAY_EVENT_SCHEMA_VERSION),
	frequencyHz(0), filterMatched(false), monitorOnly(false), filtered(false),
	rejected(false), blockedDuplicate(false), groupCall(false), groupFinal(false),
	fragmented(false), assembled(false), groupBit(-1), cycle(-1), frame(-1),
	synthetic(false)
{
}

std::string GatewayEventCanonicalPayload(const GatewayEvent& event)
{
	std::ostringstream output;
	output << "{\"schema_version\":" << event.schemaVersion
		<< ",\"receiver_sequence\":" << event.receiverSequence
		<< ",\"receiver_id\":\"" << JsonEscape(event.receiverId)
		<< "\",\"decoder_finalized_utc\":\"" << JsonEscape(event.decoderFinalizedUtc)
		<< "\",\"timestamp_method\":\"" << JsonEscape(event.timestampMethod)
		<< "\",\"source\":\"" << JsonEscape(event.source) << "\"";
	if (event.frequencyHz > 0) output << ",\"frequency_hz\":" << event.frequencyHz;
	else output << ",\"frequency_hz\":null";
	output << ",\"frequency_provenance\":\"" << JsonEscape(event.frequencyProvenance)
		<< "\",\"frequency_effective_utc\":\"" << JsonEscape(event.frequencyEffectiveUtc)
		<< "\",\"protocol\":\"" << JsonEscape(event.protocol)
		<< "\",\"protocol_metadata\":" << event.protocolMetadataJson
		<< ",\"capcode\":\"" << JsonEscape(event.capcode)
		<< "\",\"message_type\":\"" << JsonEscape(event.messageType)
		<< "\",\"bitrate\":\"" << JsonEscape(event.bitrate)
		<< "\",\"message\":\"" << JsonEscape(event.message)
		<< "\",\"pdw_version\":\"" << JsonEscape(event.pdwVersion)
		<< "\",\"filter_label\":\"" << JsonEscape(event.filterLabel)
		<< "\",\"filter_matched\":" << (event.filterMatched ? "true" : "false")
		<< ",\"monitor_only\":" << (event.monitorOnly ? "true" : "false")
		<< ",\"filtered\":" << (event.filtered ? "true" : "false")
		<< ",\"rejected\":" << (event.rejected ? "true" : "false")
		<< ",\"blocked_duplicate\":" << (event.blockedDuplicate ? "true" : "false")
		<< ",\"group_call\":" << (event.groupCall ? "true" : "false")
		<< ",\"group_final\":" << (event.groupFinal ? "true" : "false")
		<< ",\"fragmented\":" << (event.fragmented ? "true" : "false")
		<< ",\"assembled\":" << (event.assembled ? "true" : "false")
		<< ",\"group_bit\":" << event.groupBit
		<< ",\"cycle\":" << event.cycle
		<< ",\"frame\":" << event.frame
		<< ",\"synthetic\":" << (event.synthetic ? "true" : "false") << "}";
	return output.str();
}

std::string GatewayEventContentHash(const GatewayEvent& event)
{
	return Sha256Hex(GatewayEventCanonicalPayload(event));
}

bool ValidateGatewayEvent(const GatewayEvent& event, std::string& error)
{
	if (event.schemaVersion != GATEWAY_EVENT_SCHEMA_VERSION)
		error = "Gateway event schema version is unsupported.";
	else if (event.receiverSequence <= 0) error = "Gateway receiver sequence is invalid.";
	else if (event.eventId.empty()) error = "Gateway event ID is missing.";
	else if (event.receiverId.empty()) error = "Gateway receiver ID is missing.";
	else if (event.decoderFinalizedUtc.empty()) error = "Gateway finalized timestamp is missing.";
	else if (event.timestampMethod.empty()) error = "Gateway timestamp method is missing.";
	else if (event.frequencyProvenance.empty()) error = "Gateway frequency provenance is missing.";
	else if (event.protocol.empty()) error = "Gateway protocol is missing.";
	else if (event.protocolMetadataJson.size() < 2 ||
		event.protocolMetadataJson[0] != '{' ||
		event.protocolMetadataJson[event.protocolMetadataJson.size() - 1] != '}')
		error = "Gateway protocol metadata is invalid.";
	else if (event.pdwVersion.empty()) error = "Gateway PDW version is missing.";
	else if (event.contentHash.size() != 64 ||
		event.contentHash != GatewayEventContentHash(event))
		error = "Gateway event content hash is invalid.";
	else return true;
	return false;
}

GatewayOutboxStore::GatewayOutboxStore() : database_(NULL) {}
GatewayOutboxStore::~GatewayOutboxStore() { Close(); }

bool GatewayOutboxStore::Execute(const char* sql, std::string& error)
{
	char* sqliteError = NULL;
	const int result = sqlite3_exec(database_, sql, NULL, NULL, &sqliteError);
	if (result == SQLITE_OK) return true;
	error = sqliteError ? sqliteError : sqlite3_errmsg(database_);
	if (sqliteError) sqlite3_free(sqliteError);
	return false;
}

bool GatewayOutboxStore::Migrate(std::string& error)
{
	long long applicationId = 0;
	long long version = 0;
	bool empty = false;
	if (!ReadInteger(database_, "PRAGMA application_id;", applicationId, error) ||
		!ReadInteger(database_, "PRAGMA user_version;", version, error) ||
		!DatabaseIsEmpty(database_, empty, error)) return false;
	if (applicationId == 0 && version == 0 && empty)
	{
		if (!Execute("PRAGMA application_id=1346651991;", error)) return false;
		applicationId = GATEWAY_APPLICATION_ID;
	}
	if (applicationId != GATEWAY_APPLICATION_ID)
	{
		error = "The selected SQLite file is not a PDW gateway outbox.";
		return false;
	}
	if (version < 0 || version > GATEWAY_DATABASE_SCHEMA_VERSION)
	{
		error = "The PDW gateway outbox schema version is unsupported.";
		return false;
	}
	if (version == 0)
	{
		if (!Execute(
			"PRAGMA auto_vacuum=INCREMENTAL;"
			"BEGIN IMMEDIATE;"
			"CREATE TABLE gateway_outbox_state(singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
			"highest_assigned_sequence INTEGER NOT NULL);"
			"INSERT INTO gateway_outbox_state(singleton,highest_assigned_sequence) VALUES(1,0);"
			"CREATE TABLE gateway_events("
			"receiver_sequence INTEGER PRIMARY KEY,event_id TEXT NOT NULL UNIQUE,"
			"event_schema_version INTEGER NOT NULL,receiver_id TEXT NOT NULL,"
			"decoder_finalized_utc TEXT NOT NULL,timestamp_method TEXT NOT NULL,"
			"source TEXT NOT NULL,frequency_hz INTEGER,frequency_provenance TEXT NOT NULL,"
			"protocol TEXT NOT NULL,protocol_metadata_json TEXT NOT NULL,capcode TEXT NOT NULL,"
			"message_type TEXT NOT NULL,bitrate TEXT NOT NULL,message TEXT NOT NULL,"
			"content_hash TEXT NOT NULL,pdw_version TEXT NOT NULL,filter_label TEXT NOT NULL,"
			"filter_matched INTEGER NOT NULL,monitor_only INTEGER NOT NULL,filtered INTEGER NOT NULL,"
			"rejected INTEGER NOT NULL,blocked_duplicate INTEGER NOT NULL,group_call INTEGER NOT NULL,"
			"group_final INTEGER NOT NULL,fragmented INTEGER NOT NULL,assembled INTEGER NOT NULL,"
			"group_bit INTEGER NOT NULL,cycle INTEGER NOT NULL,frame INTEGER NOT NULL,"
			"synthetic INTEGER NOT NULL,committed_utc TEXT NOT NULL DEFAULT(strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
			"CREATE INDEX gateway_events_finalized_idx ON gateway_events(decoder_finalized_utc,receiver_sequence);"
			"CREATE TRIGGER gateway_events_immutable_update BEFORE UPDATE ON gateway_events "
			"BEGIN SELECT RAISE(ABORT,'gateway events are immutable'); END;"
			"PRAGMA user_version=1;COMMIT;", error)) return false;
		version = 1;
	}
	if (version == 1)
	{
		// Version 2 adds frequency-effective provenance and makes size retention
		// reclaim pages for databases created by the version-1 preview schema.
		if (!Execute("PRAGMA auto_vacuum=INCREMENTAL;VACUUM;", error)) return false;
		if (!Execute(
			"BEGIN IMMEDIATE;"
			"ALTER TABLE gateway_events ADD COLUMN frequency_effective_utc TEXT NOT NULL DEFAULT '';"
			"PRAGMA user_version=2;COMMIT;", error)) return false;
	}
	return true;
}

bool GatewayOutboxStore::ValidateSchema(std::string& error)
{
	long long applicationId = 0;
	long long version = 0;
	if (!ReadInteger(database_, "PRAGMA application_id;", applicationId, error) ||
		!ReadInteger(database_, "PRAGMA user_version;", version, error)) return false;
	if (applicationId != GATEWAY_APPLICATION_ID ||
		version != GATEWAY_DATABASE_SCHEMA_VERSION)
	{
		error = "The PDW gateway outbox ownership or schema is invalid.";
		return false;
	}
	static const char* required[] = {
		"receiver_sequence", "event_id", "event_schema_version", "receiver_id",
		"decoder_finalized_utc", "timestamp_method", "source", "frequency_hz",
		"frequency_provenance", "frequency_effective_utc", "protocol",
		"protocol_metadata_json", "capcode", "message_type", "bitrate", "message",
		"content_hash", "pdw_version", "filter_label", "filter_matched", "monitor_only",
		"filtered", "rejected", "blocked_duplicate", "group_call", "group_final",
		"fragmented", "assembled", "group_bit", "cycle", "frame", "synthetic",
		"committed_utc"
	};
	std::set<std::string> columns;
	sqlite3_stmt* statement = NULL;
	if (sqlite3_prepare_v2(database_, "PRAGMA table_info(gateway_events);", -1,
		&statement, NULL) != SQLITE_OK)
	{
		error = sqlite3_errmsg(database_);
		return false;
	}
	while (sqlite3_step(statement) == SQLITE_ROW) columns.insert(ColumnText(statement, 1));
	sqlite3_finalize(statement);
	for (std::size_t index = 0; index < sizeof(required) / sizeof(required[0]); ++index)
	{
		if (columns.find(required[index]) == columns.end())
		{
			error = std::string("Gateway outbox schema is missing ") + required[index] + ".";
			return false;
		}
	}
	long long stateColumns = 0;
	if (!ReadInteger(database_,
		"SELECT COUNT(*) FROM pragma_table_info('gateway_outbox_state') "
		"WHERE name IN ('singleton','highest_assigned_sequence');",
		stateColumns, error) || stateColumns != 2)
	{
		if (error.empty()) error = "Gateway outbox sequence state schema is invalid.";
		return false;
	}
	return true;
}

bool GatewayOutboxStore::Open(const std::string& utf8Path, std::string& error)
{
	if (database_ && path_ == utf8Path) return true;
	Close();
	if (utf8Path.empty())
	{
		error = "Gateway outbox path is empty.";
		return false;
	}
	if (sqlite3_open_v2(utf8Path.c_str(), &database_,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL) != SQLITE_OK)
	{
		error = database_ ? sqlite3_errmsg(database_) : "Gateway outbox could not open.";
		Close();
		return false;
	}
	path_ = utf8Path;
	sqlite3_busy_timeout(database_, 1000);
	int effective = 0;
	std::string journalMode;
	long long synchronous = 0;
	if (sqlite3_db_config(database_, SQLITE_DBCONFIG_DEFENSIVE, 1, &effective) != SQLITE_OK ||
		effective != 1 || !Execute("PRAGMA trusted_schema=OFF;PRAGMA foreign_keys=ON;", error) ||
		!Migrate(error) || !Execute("PRAGMA journal_mode=WAL;PRAGMA synchronous=FULL;", error) ||
		!ReadText(database_, "PRAGMA journal_mode;", journalMode, error) ||
		_stricmp(journalMode.c_str(), "wal") != 0 ||
		!ReadInteger(database_, "PRAGMA synchronous;", synchronous, error) || synchronous != 2 ||
		!ValidateSchema(error))
	{
		if (error.empty()) error = "Gateway outbox could not enforce WAL and full synchronization.";
		Close();
		return false;
	}
	return true;
}

void GatewayOutboxStore::Close()
{
	if (database_) sqlite3_close_v2(database_);
	database_ = NULL;
	path_.clear();
}

bool GatewayOutboxStore::IsOpen() const { return database_ != NULL; }
const std::string& GatewayOutboxStore::Path() const { return path_; }

bool GatewayOutboxStore::Append(const GatewayEvent& event, std::string& error)
{
	if (!database_)
	{
		error = "Gateway outbox is not open.";
		return false;
	}
	if (!ValidateGatewayEvent(event, error)) return false;
	static const char sql[] =
		"INSERT INTO gateway_events(receiver_sequence,event_id,event_schema_version,receiver_id,"
		"decoder_finalized_utc,timestamp_method,source,frequency_hz,frequency_provenance,"
		"protocol,protocol_metadata_json,capcode,message_type,bitrate,message,content_hash,pdw_version,"
		"filter_label,filter_matched,monitor_only,filtered,rejected,blocked_duplicate,group_call,"
		"group_final,fragmented,assembled,group_bit,cycle,frame,synthetic,frequency_effective_utc)"
		" VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
	sqlite3_stmt* statement = NULL;
	if (sqlite3_prepare_v2(database_, sql, -1, &statement, NULL) != SQLITE_OK)
	{
		error = sqlite3_errmsg(database_);
		return false;
	}
	int index = 1;
	bool bound = sqlite3_bind_int64(statement, index++, event.receiverSequence) == SQLITE_OK;
	bound = BindText(statement, index++, event.eventId) && bound;
	bound = sqlite3_bind_int(statement, index++, event.schemaVersion) == SQLITE_OK && bound;
	bound = BindText(statement, index++, event.receiverId) && bound;
	bound = BindText(statement, index++, event.decoderFinalizedUtc) && bound;
	bound = BindText(statement, index++, event.timestampMethod) && bound;
	bound = BindText(statement, index++, event.source) && bound;
	bound = (event.frequencyHz > 0 ? sqlite3_bind_int64(statement, index++, event.frequencyHz) :
		sqlite3_bind_null(statement, index++)) == SQLITE_OK && bound;
	bound = BindText(statement, index++, event.frequencyProvenance) && bound;
	bound = BindText(statement, index++, event.protocol) && bound;
	bound = BindText(statement, index++, event.protocolMetadataJson) && bound;
	bound = BindText(statement, index++, event.capcode) && bound;
	bound = BindText(statement, index++, event.messageType) && bound;
	bound = BindText(statement, index++, event.bitrate) && bound;
	bound = BindText(statement, index++, event.message) && bound;
	bound = BindText(statement, index++, event.contentHash) && bound;
	bound = BindText(statement, index++, event.pdwVersion) && bound;
	bound = BindText(statement, index++, event.filterLabel) && bound;
	bound = sqlite3_bind_int(statement, index++, event.filterMatched ? 1 : 0) == SQLITE_OK && bound;
	bound = sqlite3_bind_int(statement, index++, event.monitorOnly ? 1 : 0) == SQLITE_OK && bound;
	bound = sqlite3_bind_int(statement, index++, event.filtered ? 1 : 0) == SQLITE_OK && bound;
	bound = sqlite3_bind_int(statement, index++, event.rejected ? 1 : 0) == SQLITE_OK && bound;
	bound = sqlite3_bind_int(statement, index++, event.blockedDuplicate ? 1 : 0) == SQLITE_OK && bound;
	bound = sqlite3_bind_int(statement, index++, event.groupCall ? 1 : 0) == SQLITE_OK && bound;
	bound = sqlite3_bind_int(statement, index++, event.groupFinal ? 1 : 0) == SQLITE_OK && bound;
	bound = sqlite3_bind_int(statement, index++, event.fragmented ? 1 : 0) == SQLITE_OK && bound;
	bound = sqlite3_bind_int(statement, index++, event.assembled ? 1 : 0) == SQLITE_OK && bound;
	bound = sqlite3_bind_int(statement, index++, event.groupBit) == SQLITE_OK && bound;
	bound = sqlite3_bind_int(statement, index++, event.cycle) == SQLITE_OK && bound;
	bound = sqlite3_bind_int(statement, index++, event.frame) == SQLITE_OK && bound;
	bound = sqlite3_bind_int(statement, index++, event.synthetic ? 1 : 0) == SQLITE_OK && bound;
	bound = BindText(statement, index++, event.frequencyEffectiveUtc) && bound;
	const int step = bound ? sqlite3_step(statement) : SQLITE_MISUSE;
	const int finalization = sqlite3_finalize(statement);
	if (!bound || step != SQLITE_DONE || finalization != SQLITE_OK)
	{
		error = sqlite3_errmsg(database_);
		return false;
	}
	return true;
}

bool GatewayOutboxStore::RecordHighestAssignedSequence(long long sequence,
	std::string& error)
{
	if (!database_) { error = "Gateway outbox is not open."; return false; }
	sqlite3_stmt* statement = NULL;
	if (sqlite3_prepare_v2(database_,
		"UPDATE gateway_outbox_state SET highest_assigned_sequence=max(highest_assigned_sequence,?) "
		"WHERE singleton=1;", -1, &statement, NULL) != SQLITE_OK)
	{
		error = sqlite3_errmsg(database_);
		return false;
	}
	sqlite3_bind_int64(statement, 1, sequence);
	const int step = sqlite3_step(statement);
	const int finalization = sqlite3_finalize(statement);
	if (step != SQLITE_DONE || finalization != SQLITE_OK)
	{
		error = sqlite3_errmsg(database_);
		return false;
	}
	return true;
}

bool GatewayOutboxStore::GetHighestAssignedSequence(long long& sequence,
	std::string& error)
{
	if (!database_) { error = "Gateway outbox is not open."; return false; }
	return ReadInteger(database_,
		"SELECT highest_assigned_sequence FROM gateway_outbox_state WHERE singleton=1;",
		sequence, error);
}

bool GatewayOutboxStore::Checkpoint(std::string& error)
{
	if (!database_) { error = "Gateway outbox is not open."; return false; }
	int logFrames = 0;
	int checkpointed = 0;
	if (sqlite3_wal_checkpoint_v2(database_, NULL, SQLITE_CHECKPOINT_TRUNCATE,
		&logFrames, &checkpointed) != SQLITE_OK)
	{
		error = sqlite3_errmsg(database_);
		return false;
	}
	return true;
}

bool GatewayOutboxStore::RefreshFileStatistics(StoreStatistics& statistics)
{
	statistics.databaseBytes = FileSize(path_) + FileSize(path_ + "-wal") + FileSize(path_ + "-shm");
	ULARGE_INTEGER available = {}, total = {}, freeTotal = {};
	std::string root = path_;
	if (root.size() >= 3 && root[1] == ':') root = root.substr(0, 3);
	else root = ".";
	if (GetDiskFreeSpaceExA(root.c_str(), &available, &total, &freeTotal))
		statistics.availableDiskBytes = available.QuadPart;
	return true;
}

bool GatewayOutboxStore::GetStatistics(StoreStatistics& statistics, std::string& error)
{
	if (!database_) { error = "Gateway outbox is not open."; return false; }
	long long last = 0, oldest = 0, count = 0;
	if (!ReadInteger(database_, "SELECT COALESCE(MAX(receiver_sequence),0) FROM gateway_events;", last, error) ||
		!ReadInteger(database_, "SELECT COALESCE(MIN(receiver_sequence),0) FROM gateway_events;", oldest, error) ||
		!ReadInteger(database_, "SELECT COUNT(*) FROM gateway_events;", count, error)) return false;
	statistics.lastCommittedSequence = last;
	statistics.oldestRetainedSequence = oldest;
	statistics.retainedRecords = count;
	RefreshFileStatistics(statistics);
	return true;
}

bool GatewayOutboxStore::EnforceRetention(const RetentionPolicy& policy,
	int& removed, std::string& error)
{
	removed = 0;
	if (!database_) { error = "Gateway outbox is not open."; return false; }
	if (policy.retentionDays < 1 || policy.maximumMegabytes < 1)
	{
		error = "Gateway retention policy is invalid.";
		return false;
	}
	sqlite3_stmt* ageDelete = NULL;
	if (sqlite3_prepare_v2(database_,
		"DELETE FROM gateway_events WHERE decoder_finalized_utc < "
		"strftime('%Y-%m-%dT%H:%M:%fZ','now',printf('-%d days',?));", -1,
		&ageDelete, NULL) != SQLITE_OK)
	{
		error = sqlite3_errmsg(database_);
		return false;
	}
	sqlite3_bind_int(ageDelete, 1, static_cast<int>(policy.retentionDays));
	if (sqlite3_step(ageDelete) != SQLITE_DONE)
	{
		error = sqlite3_errmsg(database_);
		sqlite3_finalize(ageDelete);
		return false;
	}
	removed += sqlite3_changes(database_);
	sqlite3_finalize(ageDelete);
	const unsigned long long maximum = static_cast<unsigned long long>(policy.maximumMegabytes) * 1024ULL * 1024ULL;
	for (int pass = 0; pass < 128; ++pass)
	{
		StoreStatistics statistics;
		RefreshFileStatistics(statistics);
		if (statistics.databaseBytes <= maximum) break;
		if (!Execute("DELETE FROM gateway_events WHERE receiver_sequence IN "
			"(SELECT receiver_sequence FROM gateway_events ORDER BY receiver_sequence LIMIT 8192);", error))
			return false;
		const int changed = sqlite3_changes(database_);
		removed += changed;
		if (!changed) break;
		if (!Execute("PRAGMA incremental_vacuum(1024);", error)) return false;
		if (!Checkpoint(error)) return false;
	}
	StoreStatistics finalStatistics;
	RefreshFileStatistics(finalStatistics);
	if (finalStatistics.databaseBytes > maximum)
	{
		error = "Gateway outbox remains above its configured maximum size; writing is paused.";
		return false;
	}
	return true;
}

bool VerifyGatewayReadOnlyAccess(const std::string& utf8Path,
	long long& lastSequence, std::string& error)
{
	lastSequence = 0;
	sqlite3* database = NULL;
	const std::string uri = "file:" + utf8Path + "?mode=ro";
	if (sqlite3_open_v2(uri.c_str(), &database,
		SQLITE_OPEN_READONLY | SQLITE_OPEN_URI | SQLITE_OPEN_NOMUTEX, NULL) != SQLITE_OK)
	{
		error = database ? sqlite3_errmsg(database) : "Gateway reader could not open the outbox.";
		if (database) sqlite3_close_v2(database);
		return false;
	}
	char* sqliteError = NULL;
	if (sqlite3_exec(database, "PRAGMA query_only=ON;", NULL, NULL, &sqliteError) != SQLITE_OK)
	{
		error = sqliteError ? sqliteError : sqlite3_errmsg(database);
		if (sqliteError) sqlite3_free(sqliteError);
		sqlite3_close_v2(database);
		return false;
	}
	long long applicationId = 0;
	long long version = 0;
	const bool valid = ReadInteger(database, "PRAGMA application_id;", applicationId, error) &&
		ReadInteger(database, "PRAGMA user_version;", version, error) &&
		ReadInteger(database, "SELECT COALESCE(MAX(receiver_sequence),0) FROM gateway_events;",
			lastSequence, error) && applicationId == GATEWAY_APPLICATION_ID &&
		version == GATEWAY_DATABASE_SCHEMA_VERSION;
	if (!valid && error.empty()) error = "Gateway reader found an incompatible outbox.";
	sqlite3_close_v2(database);
	return valid;
}

} // namespace gateway
} // namespace pdw
