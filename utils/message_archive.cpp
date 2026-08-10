#include "message_archive.h"

#include <winsqlite/winsqlite3.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace pdw
{
namespace archive
{
namespace
{
	const ULONGLONG INTEGRITY_CHECK_LIMIT_MS = 5000;
	const ULONGLONG QUERY_LIMIT_MS = 3000;
	const int PDW_ARCHIVE_APPLICATION_ID = 0x50445731; // ASCII "PDW1"
	const int PDW_ARCHIVE_SCHEMA_VERSION = 1;

	struct ProgressDeadline
	{
		ULONGLONG deadline;
		bool expired;

		explicit ProgressDeadline(ULONGLONG limitMilliseconds) :
			deadline(GetTickCount64() + limitMilliseconds),
			expired(false) {}
	};

	int CALLBACK AbortAfterDeadline(void* context)
	{
		ProgressDeadline* deadline = static_cast<ProgressDeadline*>(context);
		if (GetTickCount64() < deadline->deadline) return 0;
		deadline->expired = true;
		return 1;
	}

	class ProgressGuard
	{
	public:
		ProgressGuard(sqlite3* database, ProgressDeadline* deadline) : database_(database)
		{
			sqlite3_progress_handler(database_, 1000, AbortAfterDeadline, deadline);
		}
		~ProgressGuard() { sqlite3_progress_handler(database_, 0, NULL, NULL); }
	private:
		sqlite3* database_;
	};

	class LockGuard
	{
	public:
		explicit LockGuard(CRITICAL_SECTION* lock) : lock_(lock) { EnterCriticalSection(lock_); }
		~LockGuard() { LeaveCriticalSection(lock_); }
	private:
		CRITICAL_SECTION* lock_;
	};

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

	bool QuickCheckDatabase(sqlite3* database, std::string& error)
	{
		ProgressDeadline deadline(INTEGRITY_CHECK_LIMIT_MS);
		ProgressGuard progress(database, &deadline);
		sqlite3_stmt* statement = NULL;
		if (sqlite3_prepare_v2(database, "PRAGMA quick_check(1);", -1,
			&statement, NULL) != SQLITE_OK)
		{
			error = "Message archive integrity check could not start.";
			return false;
		}
		const int result = sqlite3_step(statement);
		const bool oneOkRow = result == SQLITE_ROW && ColumnText(statement, 0) == "ok";
		const int completion = oneOkRow ? sqlite3_step(statement) : result;
		const int finalization = sqlite3_finalize(statement);
		const bool valid = oneOkRow && completion == SQLITE_DONE && finalization == SQLITE_OK;
		if (!valid)
			error = deadline.expired ?
				"Message archive integrity check exceeded five seconds." :
				"Message archive integrity check failed.";
		return valid;
	}

	bool SetDatabaseConfig(sqlite3* database, int option, int value,
		const char* description, std::string& error)
	{
		int effective = -1;
		if (sqlite3_db_config(database, option, value, &effective) == SQLITE_OK &&
			effective == value) return true;
		error = std::string("Message archive could not enforce ") + description + ".";
		return false;
	}

	bool QueryPragmaInteger(sqlite3* database, const char* sql,
		int expected, std::string& error)
	{
		sqlite3_stmt* statement = NULL;
		if (sqlite3_prepare_v2(database, sql, -1, &statement, NULL) != SQLITE_OK)
		{
			error = "Message archive security settings are unavailable.";
			return false;
		}
		const int result = sqlite3_step(statement);
		const bool expectedValue = result == SQLITE_ROW &&
			sqlite3_column_int(statement, 0) == expected;
		const int completion = expectedValue ? sqlite3_step(statement) : result;
		const int finalization = sqlite3_finalize(statement);
		if (!expectedValue || completion != SQLITE_DONE || finalization != SQLITE_OK)
		{
			error = "Message archive security settings are unavailable.";
			return false;
		}
		return true;
	}

	bool ReadSingleInteger(sqlite3* database, const char* sql, int& value,
		std::string& error)
	{
		sqlite3_stmt* statement = NULL;
		if (sqlite3_prepare_v2(database, sql, -1, &statement, NULL) != SQLITE_OK)
		{
			error = "Message archive ownership could not be verified.";
			return false;
		}
		const int result = sqlite3_step(statement);
		const bool oneRow = result == SQLITE_ROW;
		if (oneRow) value = sqlite3_column_int(statement, 0);
		const int completion = oneRow ? sqlite3_step(statement) : result;
		const int finalization = sqlite3_finalize(statement);
		if (!oneRow || completion != SQLITE_DONE || finalization != SQLITE_OK)
		{
			error = "Message archive ownership could not be verified.";
			return false;
		}
		return true;
	}

	bool VerifyOrClaimArchiveOwnership(sqlite3* database, std::string& error)
	{
		int applicationId = 0;
		int schemaVersion = 0;
		int objectCount = 0;
		if (!ReadSingleInteger(database, "PRAGMA application_id;", applicationId, error) ||
			!ReadSingleInteger(database, "PRAGMA user_version;", schemaVersion, error) ||
			!ReadSingleInteger(database,
				"SELECT COUNT(*) FROM sqlite_master WHERE name NOT GLOB 'sqlite_*';",
				objectCount, error)) return false;

		if (applicationId == PDW_ARCHIVE_APPLICATION_ID)
		{
			if (schemaVersion == PDW_ARCHIVE_SCHEMA_VERSION) return true;
			error = "The PDW message archive schema version is not supported.";
			return false;
		}
		if (applicationId != 0 || schemaVersion != 0 || objectCount != 0)
		{
			error = "The selected SQLite file is not a PDW message archive.";
			return false;
		}

		char claimSql[128] = {};
		std::snprintf(claimSql, sizeof(claimSql),
			"PRAGMA application_id=%d;PRAGMA user_version=%d;",
			PDW_ARCHIVE_APPLICATION_ID, PDW_ARCHIVE_SCHEMA_VERSION);
		char* sqliteError = NULL;
		const int result = sqlite3_exec(database, claimSql, NULL, NULL, &sqliteError);
		if (result != SQLITE_OK)
		{
			error = sqliteError ? sqliteError : "PDW could not claim the new message archive.";
			if (sqliteError) sqlite3_free(sqliteError);
			return false;
		}
		return QueryPragmaInteger(database, "PRAGMA application_id;",
			PDW_ARCHIVE_APPLICATION_ID, error) &&
			QueryPragmaInteger(database, "PRAGMA user_version;",
				PDW_ARCHIVE_SCHEMA_VERSION, error);
	}

	std::string Lowercase(const std::string& value)
	{
		std::string result(value);
		for (std::size_t index = 0; index < result.size(); ++index)
			result[index] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[index])));
		return result;
	}

	bool ReadCapcodeRow(sqlite3_stmt* statement, CapcodeEntry& entry)
	{
		entry.protocol = ColumnText(statement, 0);
		entry.address = ColumnText(statement, 1);
		entry.displayName = ColumnText(statement, 2);
		entry.agency = ColumnText(statement, 3);
		entry.color = static_cast<unsigned long>(sqlite3_column_int64(statement, 4));
		entry.notes = ColumnText(statement, 5);
		entry.enabled = sqlite3_column_int(statement, 6) != 0;
		return true;
	}
}

bool IsValidCapcode(const std::string& address)
{
	if (address.empty() || address.size() > 18) return false;
	for (std::string::const_iterator character = address.begin(); character != address.end(); ++character)
		if (!std::isdigit(static_cast<unsigned char>(*character))) return false;
	return true;
}

bool IsValidProtocolName(const std::string& protocol)
{
	if (protocol.empty()) return true;
	static const char* const protocols[] = { "POCSAG", "FLEX", "ERMES", "ACARS", "MOBITEX" };
	for (std::size_t index = 0; index < sizeof(protocols) / sizeof(protocols[0]); ++index)
		if (_stricmp(protocol.c_str(), protocols[index]) == 0) return true;
	return false;
}

std::string CsvEscape(const std::string& value)
{
	if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
	std::string result("\"");
	for (std::string::const_iterator character = value.begin(); character != value.end(); ++character)
	{
		if (*character == '"') result += "\"\"";
		else result += *character;
	}
	result += '"';
	return result;
}

bool ParseCsvLine(const std::string& line, std::vector<std::string>& fields)
{
	fields.clear();
	std::string field;
	bool quoted = false;
	for (std::size_t index = 0; index < line.size(); ++index)
	{
		const char character = line[index];
		if (quoted)
		{
			if (character == '"')
			{
				if (index + 1 < line.size() && line[index + 1] == '"')
				{
					field += '"';
					++index;
				}
				else quoted = false;
			}
			else field += character;
		}
		else if (character == ',' )
		{
			fields.push_back(field);
			field.clear();
		}
		else if (character == '"' && field.empty()) quoted = true;
		else field += character;
	}
	if (quoted) return false;
	if (!field.empty() && field[field.size() - 1] == '\r') field.resize(field.size() - 1);
	fields.push_back(field);
	return true;
}

CsvRecordReadResult ReadCsvRecord(std::istream& input, std::string& record)
{
	const std::size_t MAX_CSV_RECORD_BYTES = 1024 * 1024;
	record.clear();
	std::string physicalLine;
	while (std::getline(input, physicalLine))
	{
		if (!record.empty()) record += '\n';
		record += physicalLine;
		if (record.size() > MAX_CSV_RECORD_BYTES) return CSV_RECORD_MALFORMED;
		std::vector<std::string> fields;
		if (ParseCsvLine(record, fields)) return CSV_RECORD_COMPLETE;
	}
	return record.empty() ? CSV_RECORD_END : CSV_RECORD_MALFORMED;
}

MessageArchive::MessageArchive() : database_(NULL)
{
	InitializeCriticalSection(&lock_);
}

MessageArchive::~MessageArchive()
{
	Close();
	DeleteCriticalSection(&lock_);
}

bool MessageArchive::Execute(const std::string& sql, std::string& error)
{
	char* sqliteError = NULL;
	const int result = sqlite3_exec(database_, sql.c_str(), NULL, NULL, &sqliteError);
	if (result == SQLITE_OK) return true;
	error = sqliteError ? sqliteError : sqlite3_errmsg(database_);
	if (sqliteError) sqlite3_free(sqliteError);
	return false;
}

bool MessageArchive::Open(const std::string& utf8Path, std::string& error)
{
	LockGuard guard(&lock_);
	error.clear();
	if (utf8Path.empty())
	{
		error = "Choose a message archive database file.";
		return false;
	}
	if (database_ && path_ == utf8Path) return true;
	if (database_) sqlite3_close(database_);
	database_ = NULL;
	path_.clear();
	if (sqlite3_libversion_number() < 3031000)
	{
		error = "Message archive requires current Windows SQLite security support.";
		return false;
	}
	if (sqlite3_open_v2(utf8Path.c_str(), &database_,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX |
		SQLITE_OPEN_PRIVATECACHE | SQLITE_OPEN_NOFOLLOW, NULL) != SQLITE_OK)
	{
		error = database_ ? sqlite3_errmsg(database_) : "SQLite could not open the message archive.";
		if (database_) sqlite3_close(database_);
		database_ = NULL;
		return false;
	}
	sqlite3_busy_timeout(database_, 5000);
	if (!SetDatabaseConfig(database_, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0,
		"untrusted-schema mode", error) ||
		!SetDatabaseConfig(database_, SQLITE_DBCONFIG_DEFENSIVE, 1,
			"defensive mode", error) ||
		!SetDatabaseConfig(database_, SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION, 0,
			"disabled extension loading", error) ||
		!SetDatabaseConfig(database_, SQLITE_DBCONFIG_ENABLE_TRIGGER, 0,
			"disabled triggers", error) ||
		!SetDatabaseConfig(database_, SQLITE_DBCONFIG_ENABLE_VIEW, 0,
			"disabled views", error))
	{
		sqlite3_close(database_);
		database_ = NULL;
		return false;
	}
	// SQLite's integrity check must be the first SQL prepared against an
	// operator-selected archive. Do not create, repair, or replace a file that
	// fails this bounded check.
	if (!QuickCheckDatabase(database_, error) ||
		!Execute("PRAGMA cell_size_check=ON;", error) ||
		!QueryPragmaInteger(database_, "PRAGMA cell_size_check;", 1, error) ||
		!Execute("PRAGMA mmap_size=0;", error) ||
		!QueryPragmaInteger(database_, "PRAGMA mmap_size;", 0, error) ||
		!VerifyOrClaimArchiveOwnership(database_, error))
	{
		sqlite3_close(database_);
		database_ = NULL;
		return false;
	}
	const std::string schema =
		"PRAGMA journal_mode=WAL;PRAGMA synchronous=NORMAL;PRAGMA foreign_keys=ON;"
		"CREATE TABLE IF NOT EXISTS capcode_directory("
		"protocol TEXT NOT NULL DEFAULT '',address TEXT NOT NULL,display_name TEXT NOT NULL DEFAULT '',"
		"agency TEXT NOT NULL DEFAULT '',color INTEGER NOT NULL DEFAULT 13369344,notes TEXT NOT NULL DEFAULT '',"
		"enabled INTEGER NOT NULL DEFAULT 1,updated_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
		"PRIMARY KEY(protocol,address));"
		"CREATE INDEX IF NOT EXISTS idx_capcode_directory_address ON capcode_directory(address);"
		"CREATE INDEX IF NOT EXISTS idx_capcode_directory_name ON capcode_directory(display_name);"
		"CREATE TABLE IF NOT EXISTS message_history("
		"id TEXT PRIMARY KEY NOT NULL,received_utc TEXT NOT NULL,source TEXT NOT NULL DEFAULT '',"
		"address TEXT NOT NULL DEFAULT '',local_time TEXT NOT NULL DEFAULT '',local_date TEXT NOT NULL DEFAULT '',"
		"mode TEXT NOT NULL DEFAULT '',message_type TEXT NOT NULL DEFAULT '',bitrate TEXT NOT NULL DEFAULT '',"
		"message TEXT NOT NULL DEFAULT '',filter_label TEXT NOT NULL DEFAULT '',filtered INTEGER NOT NULL DEFAULT 0,"
		"rejected INTEGER NOT NULL DEFAULT 0,blocked_duplicate INTEGER NOT NULL DEFAULT 0,"
		"fragmented INTEGER NOT NULL DEFAULT 0,assembled INTEGER NOT NULL DEFAULT 0);"
		"CREATE INDEX IF NOT EXISTS idx_message_history_received ON message_history(received_utc DESC);"
		"CREATE INDEX IF NOT EXISTS idx_message_history_address ON message_history(address);"
		"CREATE INDEX IF NOT EXISTS idx_message_history_mode ON message_history(mode);"
		"CREATE INDEX IF NOT EXISTS idx_message_history_filtered ON message_history(filtered);";
	if (!Execute(schema, error))
	{
		sqlite3_close(database_);
		database_ = NULL;
		return false;
	}
	path_ = utf8Path;
	return true;
}

void MessageArchive::Close()
{
	LockGuard guard(&lock_);
	if (database_) sqlite3_close(database_);
	database_ = NULL;
	path_.clear();
}

bool MessageArchive::IsOpen() const
{
	LockGuard guard(&lock_);
	return database_ != NULL;
}

bool MessageArchive::StoreEvent(const pdw::publishing::PublishEvent& event,
	bool includeMessage, std::string& error)
{
	LockGuard guard(&lock_);
	error.clear();
	if (!database_)
	{
		error = "Message archive is not open.";
		return false;
	}
	static const char sql[] =
		"INSERT OR IGNORE INTO message_history(id,received_utc,source,address,local_time,local_date,mode,"
		"message_type,bitrate,message,filter_label,filtered,rejected,blocked_duplicate,fragmented,assembled)"
		" VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
	sqlite3_stmt* statement = NULL;
	if (sqlite3_prepare_v2(database_, sql, -1, &statement, NULL) != SQLITE_OK)
	{
		error = sqlite3_errmsg(database_);
		return false;
	}
	bool bound = BindText(statement, 1, event.id) && BindText(statement, 2, event.timestamp) &&
		BindText(statement, 3, event.source) && BindText(statement, 4, event.address) &&
		BindText(statement, 5, event.time) && BindText(statement, 6, event.date) &&
		BindText(statement, 7, event.mode) && BindText(statement, 8, event.messageType) &&
		BindText(statement, 9, event.bitrate) && BindText(statement, 10, includeMessage ? event.message : std::string()) &&
		BindText(statement, 11, event.filterLabel);
	bound = bound && sqlite3_bind_int(statement, 12, event.filtered ? 1 : 0) == SQLITE_OK;
	bound = bound && sqlite3_bind_int(statement, 13, event.rejected ? 1 : 0) == SQLITE_OK;
	bound = bound && sqlite3_bind_int(statement, 14, event.blockedDuplicate ? 1 : 0) == SQLITE_OK;
	bound = bound && sqlite3_bind_int(statement, 15, event.fragmented ? 1 : 0) == SQLITE_OK;
	bound = bound && sqlite3_bind_int(statement, 16, event.assembled ? 1 : 0) == SQLITE_OK;
	const int result = bound ? sqlite3_step(statement) : SQLITE_ERROR;
	if (result != SQLITE_DONE) error = sqlite3_errmsg(database_);
	sqlite3_finalize(statement);
	return result == SQLITE_DONE;
}

bool MessageArchive::UpsertCapcode(const CapcodeEntry& entry, std::string& error)
{
	LockGuard guard(&lock_);
	error.clear();
	if (!database_) { error = "Message archive is not open."; return false; }
	if (!IsValidCapcode(entry.address)) { error = "Capcodes must contain 1 to 18 digits."; return false; }
	if (!IsValidProtocolName(entry.protocol)) { error = "Choose a supported protocol or Any protocol."; return false; }
	if (entry.color > 0x00ffffffUL) { error = "Capcode colours must be a valid Windows RGB value."; return false; }
	static const char sql[] =
		"INSERT INTO capcode_directory(protocol,address,display_name,agency,color,notes,enabled,updated_utc)"
		" VALUES(?,?,?,?,?,?,?,CURRENT_TIMESTAMP) ON CONFLICT(protocol,address) DO UPDATE SET "
		"display_name=excluded.display_name,agency=excluded.agency,color=excluded.color,notes=excluded.notes,"
		"enabled=excluded.enabled,updated_utc=CURRENT_TIMESTAMP;";
	sqlite3_stmt* statement = NULL;
	if (sqlite3_prepare_v2(database_, sql, -1, &statement, NULL) != SQLITE_OK)
	{
		error = sqlite3_errmsg(database_);
		return false;
	}
	bool bound = BindText(statement, 1, entry.protocol) && BindText(statement, 2, entry.address) &&
		BindText(statement, 3, entry.displayName) && BindText(statement, 4, entry.agency) &&
		sqlite3_bind_int64(statement, 5, static_cast<sqlite3_int64>(entry.color)) == SQLITE_OK &&
		BindText(statement, 6, entry.notes) && sqlite3_bind_int(statement, 7, entry.enabled ? 1 : 0) == SQLITE_OK;
	const int result = bound ? sqlite3_step(statement) : SQLITE_ERROR;
	if (result != SQLITE_DONE) error = sqlite3_errmsg(database_);
	sqlite3_finalize(statement);
	return result == SQLITE_DONE;
}

bool MessageArchive::DeleteCapcode(const std::string& protocol, const std::string& address,
	std::string& error)
{
	LockGuard guard(&lock_);
	error.clear();
	if (!database_) { error = "Message archive is not open."; return false; }
	sqlite3_stmt* statement = NULL;
	if (sqlite3_prepare_v2(database_,
		"DELETE FROM capcode_directory WHERE protocol=? AND address=?;", -1, &statement, NULL) != SQLITE_OK)
	{
		error = sqlite3_errmsg(database_);
		return false;
	}
	const bool bound = BindText(statement, 1, protocol) && BindText(statement, 2, address);
	const int result = bound ? sqlite3_step(statement) : SQLITE_ERROR;
	if (result != SQLITE_DONE) error = sqlite3_errmsg(database_);
	sqlite3_finalize(statement);
	return result == SQLITE_DONE;
}

bool MessageArchive::LookupCapcodeUnlocked(const std::string& mode, const std::string& address,
	CapcodeEntry& entry, std::string& error)
{
	error.clear();
	if (!database_) { error = "Message archive is not open."; return false; }
	static const char sql[] =
		"SELECT protocol,address,display_name,agency,color,notes,enabled FROM capcode_directory "
		"WHERE address=? AND enabled=1 AND (protocol='' OR ? LIKE protocol || '%') "
		"ORDER BY CASE WHEN protocol='' THEN 1 ELSE 0 END LIMIT 1;";
	sqlite3_stmt* statement = NULL;
	if (sqlite3_prepare_v2(database_, sql, -1, &statement, NULL) != SQLITE_OK)
	{
		error = sqlite3_errmsg(database_);
		return false;
	}
	if (!BindText(statement, 1, address) || !BindText(statement, 2, mode))
	{
		error = sqlite3_errmsg(database_);
		sqlite3_finalize(statement);
		return false;
	}
	const int result = sqlite3_step(statement);
	if (result == SQLITE_ROW) ReadCapcodeRow(statement, entry);
	else if (result != SQLITE_DONE) error = sqlite3_errmsg(database_);
	sqlite3_finalize(statement);
	return result == SQLITE_ROW;
}

bool MessageArchive::LookupCapcode(const std::string& mode, const std::string& address,
	CapcodeEntry& entry, std::string& error)
{
	LockGuard guard(&lock_);
	return LookupCapcodeUnlocked(mode, address, entry, error);
}

bool MessageArchive::ListCapcodes(const std::string& search,
	std::vector<CapcodeEntry>& entries, std::string& error)
{
	LockGuard guard(&lock_);
	entries.clear();
	error.clear();
	if (!database_) { error = "Message archive is not open."; return false; }
	ProgressDeadline deadline(QUERY_LIMIT_MS);
	ProgressGuard progress(database_, &deadline);
	const std::string pattern = "%" + Lowercase(search) + "%";
	static const char sql[] =
		"SELECT protocol,address,display_name,agency,color,notes,enabled FROM capcode_directory "
		"WHERE ?='' OR lower(protocol) LIKE ? OR lower(address) LIKE ? OR lower(display_name) LIKE ? "
		"OR lower(agency) LIKE ? OR lower(notes) LIKE ? ORDER BY display_name,address,protocol;";
	sqlite3_stmt* statement = NULL;
	if (sqlite3_prepare_v2(database_, sql, -1, &statement, NULL) != SQLITE_OK)
	{
		error = sqlite3_errmsg(database_);
		return false;
	}
	bool bound = BindText(statement, 1, search) && BindText(statement, 2, pattern) &&
		BindText(statement, 3, pattern) && BindText(statement, 4, pattern) &&
		BindText(statement, 5, pattern) && BindText(statement, 6, pattern);
	int result = bound ? sqlite3_step(statement) : SQLITE_ERROR;
	while (result == SQLITE_ROW)
	{
		CapcodeEntry entry;
		ReadCapcodeRow(statement, entry);
		entries.push_back(entry);
		result = sqlite3_step(statement);
	}
	if (result != SQLITE_DONE) error = deadline.expired ?
		"Message archive query exceeded three seconds." : sqlite3_errmsg(database_);
	sqlite3_finalize(statement);
	return result == SQLITE_DONE;
}

bool MessageArchive::QueryHistory(const HistoryQuery& query,
	std::vector<HistoryRow>& rows, int& total, std::string& error)
{
	LockGuard guard(&lock_);
	rows.clear();
	total = 0;
	error.clear();
	if (!database_) { error = "Message archive is not open."; return false; }
	ProgressDeadline deadline(QUERY_LIMIT_MS);
	ProgressGuard progress(database_, &deadline);
	const int limit = (std::max)(1, (std::min)(500, query.limit));
	const int offset = (std::max)(0, query.offset);
	const std::string pattern = "%" + Lowercase(query.search) + "%";
	const std::string where =
		" FROM message_history h WHERE (?='' OR lower(h.address) LIKE ? OR lower(h.mode) LIKE ? "
		"OR lower(h.message_type) LIKE ? OR lower(h.message) LIKE ? OR lower(h.filter_label) LIKE ? "
		"OR EXISTS(SELECT 1 FROM capcode_directory d WHERE d.address=h.address AND d.enabled=1 "
		"AND (d.protocol='' OR h.mode LIKE d.protocol || '%') AND "
		"(lower(d.display_name) LIKE ? OR lower(d.agency) LIKE ?))) "
		"AND (?='' OR h.mode LIKE ? || '%') AND (?=0 OR h.filtered=1)";
	const std::string countSql = "SELECT COUNT(*)" + where + ";";
	const std::string selectSql =
		"SELECT h.id,h.received_utc,h.source,h.address,h.local_time,h.local_date,h.mode,h.message_type,"
		"h.bitrate,h.message,h.filter_label,h.filtered,h.rejected,h.blocked_duplicate,h.fragmented,h.assembled" +
		where + " ORDER BY h.received_utc DESC LIMIT ? OFFSET ?;";

	auto bindFilter = [&](sqlite3_stmt* statement) -> bool
	{
		bool bound = BindText(statement, 1, query.search);
		for (int index = 2; index <= 8; ++index) bound = bound && BindText(statement, index, pattern);
		bound = bound && BindText(statement, 9, query.protocol) && BindText(statement, 10, query.protocol);
		bound = bound && sqlite3_bind_int(statement, 11, query.filteredOnly ? 1 : 0) == SQLITE_OK;
		return bound;
	};

	sqlite3_stmt* countStatement = NULL;
	if (sqlite3_prepare_v2(database_, countSql.c_str(), -1, &countStatement, NULL) != SQLITE_OK ||
		!bindFilter(countStatement) || sqlite3_step(countStatement) != SQLITE_ROW)
	{
		error = deadline.expired ? "Message archive query exceeded three seconds." :
			sqlite3_errmsg(database_);
		if (countStatement) sqlite3_finalize(countStatement);
		return false;
	}
	total = sqlite3_column_int(countStatement, 0);
	sqlite3_finalize(countStatement);

	sqlite3_stmt* statement = NULL;
	if (sqlite3_prepare_v2(database_, selectSql.c_str(), -1, &statement, NULL) != SQLITE_OK || !bindFilter(statement) ||
		sqlite3_bind_int(statement, 12, limit) != SQLITE_OK || sqlite3_bind_int(statement, 13, offset) != SQLITE_OK)
	{
		error = deadline.expired ? "Message archive query exceeded three seconds." :
			sqlite3_errmsg(database_);
		if (statement) sqlite3_finalize(statement);
		return false;
	}
	int result = sqlite3_step(statement);
	while (result == SQLITE_ROW)
	{
		HistoryRow row;
		row.event.id = ColumnText(statement, 0);
		row.event.timestamp = ColumnText(statement, 1);
		row.event.source = ColumnText(statement, 2);
		row.event.address = ColumnText(statement, 3);
		row.event.time = ColumnText(statement, 4);
		row.event.date = ColumnText(statement, 5);
		row.event.mode = ColumnText(statement, 6);
		row.event.messageType = ColumnText(statement, 7);
		row.event.bitrate = ColumnText(statement, 8);
		row.event.message = ColumnText(statement, 9);
		row.event.filterLabel = ColumnText(statement, 10);
		row.event.filtered = sqlite3_column_int(statement, 11) != 0;
		row.event.rejected = sqlite3_column_int(statement, 12) != 0;
		row.event.blockedDuplicate = sqlite3_column_int(statement, 13) != 0;
		row.event.fragmented = sqlite3_column_int(statement, 14) != 0;
		row.event.assembled = sqlite3_column_int(statement, 15) != 0;
		CapcodeEntry alias;
		std::string lookupError;
		if (LookupCapcodeUnlocked(row.event.mode, row.event.address, alias, lookupError))
		{
			row.displayName = alias.displayName;
			row.agency = alias.agency;
			row.color = alias.color;
		}
		rows.push_back(row);
		result = sqlite3_step(statement);
	}
	if (result != SQLITE_DONE) error = deadline.expired ?
		"Message archive query exceeded three seconds." : sqlite3_errmsg(database_);
	sqlite3_finalize(statement);
	return result == SQLITE_DONE;
}

bool MessageArchive::PurgeHistory(unsigned int retentionDays, int& removed,
	std::string& error)
{
	LockGuard guard(&lock_);
	removed = 0;
	error.clear();
	if (!database_) { error = "Message archive is not open."; return false; }
	if (retentionDays < 1 || retentionDays > 3650)
	{
		error = "Retention must be between 1 and 3650 days.";
		return false;
	}
	ProgressDeadline deadline(INTEGRITY_CHECK_LIMIT_MS);
	ProgressGuard progress(database_, &deadline);
	sqlite3_stmt* statement = NULL;
	if (sqlite3_prepare_v2(database_,
		"DELETE FROM message_history WHERE julianday(received_utc) < julianday('now', ?);", -1, &statement, NULL) != SQLITE_OK)
	{
		error = sqlite3_errmsg(database_);
		return false;
	}
	std::ostringstream modifier;
	modifier << '-' << retentionDays << " days";
	const bool bound = BindText(statement, 1, modifier.str());
	const int result = bound ? sqlite3_step(statement) : SQLITE_ERROR;
	if (result == SQLITE_DONE) removed = sqlite3_changes(database_);
	else error = deadline.expired ? "Message archive purge exceeded five seconds." :
		sqlite3_errmsg(database_);
	sqlite3_finalize(statement);
	return result == SQLITE_DONE;
}

} // namespace archive
} // namespace pdw
