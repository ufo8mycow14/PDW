#include "message_archive.h"
#include "filter_match_core.h"

#include <winsqlite/winsqlite3.h>

#include <algorithm>
#include <cctype>
#include <climits>
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
	const ULONGLONG EXPORT_LIMIT_MS = 5ULL * 60ULL * 1000ULL;
	const int PDW_ARCHIVE_APPLICATION_ID = 0x50445731; // ASCII "PDW1"
	const int PDW_ARCHIVE_SCHEMA_VERSION = 3;

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
			if (schemaVersion >= 1 && schemaVersion <= PDW_ARCHIVE_SCHEMA_VERSION) return true;
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

	bool VerifyExactArchiveOwnership(sqlite3* database, std::string& error)
	{
		int applicationId = 0;
		int schemaVersion = 0;
		if (!ReadSingleInteger(database, "PRAGMA application_id;", applicationId, error) ||
			!ReadSingleInteger(database, "PRAGMA user_version;", schemaVersion, error)) return false;
		if (applicationId != PDW_ARCHIVE_APPLICATION_ID)
		{
			error = "The selected SQLite file is not a PDW message archive.";
			return false;
		}
		if (schemaVersion < 1 || schemaVersion > PDW_ARCHIVE_SCHEMA_VERSION)
		{
			error = "The PDW message archive schema version is not supported.";
			return false;
		}
		return true;
	}

	std::string Lowercase(const std::string& value)
	{
		std::string result(value);
		for (std::size_t index = 0; index < result.size(); ++index)
			result[index] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[index])));
		return result;
	}

	bool CsvBoolean(const std::string& value)
	{
		return value != "0" && _stricmp(value.c_str(), "false") != 0;
	}

	bool ParseCsvUnsigned(const std::string& value, unsigned long maximum,
		unsigned long& parsed)
	{
		if (value.empty()) return false;
		unsigned long result = 0;
		for (std::string::const_iterator digit = value.begin(); digit != value.end(); ++digit)
		{
			if (*digit < '0' || *digit > '9') return false;
			const unsigned long next = static_cast<unsigned long>(*digit - '0');
			if (result > (maximum - next) / 10UL) return false;
			result = result * 10UL + next;
		}
		parsed = result;
		return true;
	}

	const char* HistoryFilterWhereSql()
	{
		return
			" WHERE (?='' OR lower(h.address) LIKE ? OR lower(h.mode) LIKE ? "
			"OR lower(h.message_type) LIKE ? OR lower(h.message) LIKE ? OR lower(h.filter_label) LIKE ? "
			"OR EXISTS(SELECT 1 FROM capcode_directory searched WHERE searched.address=h.address "
			"AND searched.enabled=1 AND (searched.protocol='' OR h.mode LIKE searched.protocol || '%') AND "
			"(lower(searched.display_name) LIKE ? OR lower(searched.agency) LIKE ?))) "
			"AND (?='' OR h.mode LIKE ? || '%') AND (?=0 OR h.filtered=1)";
	}

	bool BindHistoryFilter(sqlite3_stmt* statement, const HistoryQuery& query)
	{
		const std::string pattern = "%" + Lowercase(query.search) + "%";
		bool bound = BindText(statement, 1, query.search);
		for (int index = 2; index <= 8; ++index)
			bound = BindText(statement, index, pattern) && bound;
		bound = BindText(statement, 9, query.protocol) && bound;
		bound = BindText(statement, 10, query.protocol) && bound;
		bound = sqlite3_bind_int(statement, 11, query.filteredOnly ? 1 : 0) == SQLITE_OK && bound;
		return bound;
	}

	bool ReadCapcodeRow(sqlite3_stmt* statement, CapcodeEntry& entry)
	{
		entry.id = static_cast<long long>(sqlite3_column_int64(statement, 0));
		entry.protocol = ColumnText(statement, 1);
		entry.address = ColumnText(statement, 2);
		entry.displayName = ColumnText(statement, 3);
		entry.agency = ColumnText(statement, 4);
		entry.color = static_cast<unsigned long>(sqlite3_column_int64(statement, 5));
		entry.notes = ColumnText(statement, 6);
		entry.enabled = sqlite3_column_int(statement, 7) != 0;
		entry.filterType = sqlite3_column_int(statement, 8);
		entry.matchText = ColumnText(statement, 9);
		entry.filterLabel = ColumnText(statement, 10);
		entry.reject = sqlite3_column_int(statement, 11) != 0;
		entry.matchExactMessage = sqlite3_column_int(statement, 12) != 0;
		entry.showFilterLabel = sqlite3_column_int(statement, 13) != 0;
		entry.commandEnabled = sqlite3_column_int(statement, 14) != 0;
		entry.monitorOnly = sqlite3_column_int(statement, 15) != 0;
		entry.emailEnabled = sqlite3_column_int(statement, 16) != 0;
		entry.separateFileEnabled = sqlite3_column_int(statement, 17) != 0;
		entry.separateFile1 = ColumnText(statement, 18);
		entry.separateFile2 = ColumnText(statement, 19);
		entry.separateFile3 = ColumnText(statement, 20);
		entry.waveNumber = sqlite3_column_int(statement, 21);
		entry.labelColor = sqlite3_column_int(statement, 22);
		entry.hitCounter = static_cast<unsigned int>(sqlite3_column_int64(statement, 23));
		entry.lastHitDate = ColumnText(statement, 24);
		entry.lastHitTime = ColumnText(statement, 25);
		entry.filterEnabled = sqlite3_column_int(statement, 26) != 0;
		entry.outputRoutingConfigured = sqlite3_column_int(statement, 27) != 0;
		entry.outputRoutes = static_cast<unsigned int>(sqlite3_column_int64(statement, 28));
		entry.agencyLabelPosition = sqlite3_column_int(statement, 29);
		if (entry.displayName.empty()) entry.displayName = entry.filterLabel;
		entry.filterLabel = entry.displayName;
		if (entry.outputRoutingConfigured)
			entry.emailEnabled = (entry.outputRoutes & PDW_OUTPUT_ROUTE_EMAIL) != 0;
		return true;
	}

	const char* CapcodeSelectColumns()
	{
		return "id,protocol,address,display_name,agency,color,notes,enabled,filter_type,match_text,"
			"filter_label,reject,match_exact,show_label,command_enabled,monitor_only,email_enabled,"
			"separate_file_enabled,separate_file_1,separate_file_2,separate_file_3,wave_number,"
			"label_color,hit_counter,last_hit_date,last_hit_time,filter_enabled,"
			"output_routing_configured,output_routes,agency_label_position";
	}

	bool MigrateArchiveSchema(sqlite3* database, std::string& error)
	{
		int schemaVersion = 0;
		if (!ReadSingleInteger(database, "PRAGMA user_version;", schemaVersion, error)) return false;
		if (schemaVersion < 1 || schemaVersion > PDW_ARCHIVE_SCHEMA_VERSION)
		{
			error = "The PDW message archive schema version is not supported.";
			return false;
		}
		if (schemaVersion == 1)
		{
			const char migration[] =
			"BEGIN IMMEDIATE;"
			"ALTER TABLE capcode_directory RENAME TO capcode_directory_v1;"
			"CREATE TABLE capcode_directory("
			"id INTEGER PRIMARY KEY,protocol TEXT NOT NULL DEFAULT '',address TEXT NOT NULL DEFAULT '',"
			"display_name TEXT NOT NULL DEFAULT '',agency TEXT NOT NULL DEFAULT '',"
			"color INTEGER NOT NULL DEFAULT 13369344,notes TEXT NOT NULL DEFAULT '',enabled INTEGER NOT NULL DEFAULT 1,"
			"filter_type INTEGER NOT NULL DEFAULT 0,match_text TEXT NOT NULL DEFAULT '',filter_label TEXT NOT NULL DEFAULT '',"
			"reject INTEGER NOT NULL DEFAULT 0,match_exact INTEGER NOT NULL DEFAULT 0,show_label INTEGER NOT NULL DEFAULT 1,"
			"command_enabled INTEGER NOT NULL DEFAULT 0,monitor_only INTEGER NOT NULL DEFAULT 0,email_enabled INTEGER NOT NULL DEFAULT 0,"
			"separate_file_enabled INTEGER NOT NULL DEFAULT 0,separate_file_1 TEXT NOT NULL DEFAULT '',"
			"separate_file_2 TEXT NOT NULL DEFAULT '',separate_file_3 TEXT NOT NULL DEFAULT '',"
			"wave_number INTEGER NOT NULL DEFAULT 0,label_color INTEGER NOT NULL DEFAULT 0,hit_counter INTEGER NOT NULL DEFAULT 0,"
			"last_hit_date TEXT NOT NULL DEFAULT '',last_hit_time TEXT NOT NULL DEFAULT '',updated_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);"
			"INSERT INTO capcode_directory(protocol,address,display_name,agency,color,notes,enabled,filter_type,filter_label,show_label,updated_utc) "
			"SELECT protocol,address,display_name,agency,color,notes,enabled,"
			"CASE upper(protocol) WHEN 'FLEX' THEN 1 WHEN 'POCSAG' THEN 2 WHEN 'ERMES' THEN 4 WHEN 'ACARS' THEN 5 WHEN 'MOBITEX' THEN 6 ELSE 0 END,"
			"display_name,1,updated_utc FROM capcode_directory_v1;"
			"DROP TABLE capcode_directory_v1;"
			"PRAGMA user_version=2;COMMIT;";
			char* sqliteError = NULL;
			if (sqlite3_exec(database, migration, NULL, NULL, &sqliteError) != SQLITE_OK)
			{
				error = sqliteError ? sqliteError : "PDW could not upgrade the Capcode Directory.";
				if (sqliteError) sqlite3_free(sqliteError);
				sqlite3_exec(database, "ROLLBACK;", NULL, NULL, NULL);
				return false;
			}
			schemaVersion = 2;
		}
		if (schemaVersion == 2)
		{
			const char migration[] =
				"BEGIN IMMEDIATE;"
				"ALTER TABLE capcode_directory ADD COLUMN filter_enabled INTEGER NOT NULL DEFAULT 1;"
				"ALTER TABLE capcode_directory ADD COLUMN output_routing_configured INTEGER NOT NULL DEFAULT 0;"
				"ALTER TABLE capcode_directory ADD COLUMN output_routes INTEGER NOT NULL DEFAULT 0;"
				"ALTER TABLE capcode_directory ADD COLUMN agency_label_position INTEGER NOT NULL DEFAULT 0;"
				"UPDATE capcode_directory SET display_name=filter_label "
				"WHERE display_name='' AND filter_label<>'';"
				"UPDATE capcode_directory SET filter_enabled=CASE WHEN enabled=1 AND monitor_only=0 THEN 1 ELSE 0 END,"
				"output_routing_configured=0,"
				"output_routes=CASE WHEN email_enabled=1 THEN 1 ELSE 0 END,"
				"filter_label=display_name,command_enabled=1;"
				"PRAGMA user_version=3;COMMIT;";
			char* sqliteError = NULL;
			if (sqlite3_exec(database, migration, NULL, NULL, &sqliteError) != SQLITE_OK)
			{
				error = sqliteError ? sqliteError : "PDW could not add per-filter output routing.";
				if (sqliteError) sqlite3_free(sqliteError);
				sqlite3_exec(database, "ROLLBACK;", NULL, NULL, NULL);
				return false;
			}
		}
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

namespace
{
	bool ExecuteDatabaseSql(sqlite3* database, const char* sql, std::string& error)
	{
		char* sqliteError = NULL;
		const int result = sqlite3_exec(database, sql, NULL, NULL, &sqliteError);
		if (result == SQLITE_OK) return true;
		error = sqliteError ? sqliteError : sqlite3_errmsg(database);
		if (sqliteError) sqlite3_free(sqliteError);
		return false;
	}

	bool OpenReadOnlyArchive(const std::string& utf8Path, sqlite3*& database,
		std::string& error)
	{
		database = NULL;
		if (utf8Path.empty())
		{
			error = "Choose a message archive database file.";
			return false;
		}
		if (sqlite3_libversion_number() < 3031000)
		{
			error = "Message archive requires current Windows SQLite security support.";
			return false;
		}
		if (sqlite3_open_v2(utf8Path.c_str(), &database,
			SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX |
			SQLITE_OPEN_PRIVATECACHE | SQLITE_OPEN_NOFOLLOW, NULL) != SQLITE_OK)
		{
			error = database ? sqlite3_errmsg(database) :
				"SQLite could not open the message archive for export.";
			if (database) sqlite3_close(database);
			database = NULL;
			return false;
		}
		sqlite3_busy_timeout(database, 5000);
		if (!SetDatabaseConfig(database, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0,
			"untrusted-schema mode", error) ||
			!SetDatabaseConfig(database, SQLITE_DBCONFIG_DEFENSIVE, 1,
				"defensive mode", error) ||
			!SetDatabaseConfig(database, SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION, 0,
				"disabled extension loading", error) ||
			!SetDatabaseConfig(database, SQLITE_DBCONFIG_ENABLE_TRIGGER, 0,
				"disabled triggers", error) ||
			!SetDatabaseConfig(database, SQLITE_DBCONFIG_ENABLE_VIEW, 0,
				"disabled views", error))
		{
			sqlite3_close(database);
			database = NULL;
			return false;
		}
		// Keep quick_check as the first SQL prepared against this separately
		// opened, operator-selected archive. Export must never create, repair,
		// claim, or otherwise mutate the selected database.
		if (!QuickCheckDatabase(database, error) ||
			!ExecuteDatabaseSql(database, "PRAGMA query_only=ON;", error) ||
			!QueryPragmaInteger(database, "PRAGMA query_only;", 1, error) ||
			!ExecuteDatabaseSql(database, "PRAGMA cell_size_check=ON;", error) ||
			!QueryPragmaInteger(database, "PRAGMA cell_size_check;", 1, error) ||
			!ExecuteDatabaseSql(database, "PRAGMA mmap_size=0;", error) ||
			!QueryPragmaInteger(database, "PRAGMA mmap_size;", 0, error) ||
			!VerifyExactArchiveOwnership(database, error))
		{
			sqlite3_close(database);
			database = NULL;
			return false;
		}
		return true;
	}

	bool ReadCsvColumn(sqlite3_stmt* statement, int column,
		std::string& value, std::string& error)
	{
		const unsigned char* raw = sqlite3_column_text(statement, column);
		if (!raw)
		{
			value.clear();
			return true;
		}
		const int byteCount = (std::max)(0, sqlite3_column_bytes(statement, column));
		const char* first = reinterpret_cast<const char*>(raw);
		const char* last = first + byteCount;
		if (std::find(first, last, '\0') != last)
		{
			error = "A message-history value contains an embedded NUL byte and cannot be represented safely in CSV.";
			return false;
		}
		value.assign(first, last);
		return true;
	}

	std::string SpreadsheetCsvEscape(const std::string& value)
	{
		std::size_t firstMeaningful = 0;
		bool hasLeadingControl = false;
		while (firstMeaningful < value.size() &&
			static_cast<unsigned char>(value[firstMeaningful]) <= 0x20)
		{
			if (static_cast<unsigned char>(value[firstMeaningful]) < 0x20)
				hasLeadingControl = true;
			++firstMeaningful;
		}
		std::string safe(value);
		bool requiresTextPrefix = hasLeadingControl;
		if (firstMeaningful < value.size())
		{
			const char first = value[firstMeaningful];
			if (first == '=' || first == '+' || first == '-' || first == '@')
				requiresTextPrefix = true;
		}
		if (requiresTextPrefix) safe.insert(safe.begin(), '\'');
		return CsvEscape(safe);
	}

	bool ExportHistoryCsvFromDatabase(sqlite3* database, const HistoryQuery& query,
		std::ostream& output, int& exported, std::string& error)
	{
		const std::string selectSql = std::string(
			"SELECT h.received_utc,h.mode,h.address,COALESCE(current_alias.display_name,''),"
			"COALESCE(current_alias.agency,''),h.message_type,h.message,h.filter_label "
			"FROM message_history h LEFT JOIN capcode_directory current_alias "
			"ON current_alias.id=(SELECT candidate.id FROM capcode_directory candidate "
			"WHERE candidate.address=h.address AND candidate.enabled=1 AND "
			"(candidate.protocol='' OR h.mode LIKE candidate.protocol || '%') "
			"ORDER BY CASE WHEN candidate.protocol='' THEN 1 ELSE 0 END,candidate.id LIMIT 1)") +
			HistoryFilterWhereSql() + " ORDER BY h.received_utc DESC,h.id DESC;";
		ProgressDeadline deadline(EXPORT_LIMIT_MS);
		ProgressGuard progress(database, &deadline);
		sqlite3_stmt* statement = NULL;
		if (sqlite3_prepare_v2(database, selectSql.c_str(), -1, &statement, NULL) != SQLITE_OK ||
			!BindHistoryFilter(statement, query))
		{
			error = deadline.expired ? "Message archive CSV export exceeded five minutes." :
				sqlite3_errmsg(database);
			if (statement) sqlite3_finalize(statement);
			return false;
		}

		try
		{
			output << "\xEF\xBB\xBFReceived,Protocol,Capcode,Name,Agency,Type,Message,Filter\r\n";
		}
		catch (...)
		{
			sqlite3_finalize(statement);
			error = "The message-history CSV file could not be written completely.";
			return false;
		}
		if (!output.good())
		{
			sqlite3_finalize(statement);
			error = "The message-history CSV file could not be written completely.";
			return false;
		}

		int result = sqlite3_step(statement);
		int completed = 0;
		while (result == SQLITE_ROW)
		{
			if (completed == INT_MAX)
			{
				sqlite3_finalize(statement);
				error = "The message-history CSV export contains too many rows.";
				return false;
			}
			try
			{
				for (int column = 0; column < 8; ++column)
				{
					if (column) output << ',';
					std::string value;
					if (!ReadCsvColumn(statement, column, value, error))
					{
						sqlite3_finalize(statement);
						return false;
					}
					output << SpreadsheetCsvEscape(value);
				}
				output << "\r\n";
			}
			catch (...)
			{
				sqlite3_finalize(statement);
				error = "The message-history CSV file could not be written completely.";
				return false;
			}
			if (!output.good())
			{
				sqlite3_finalize(statement);
				error = "The message-history CSV file could not be written completely.";
				return false;
			}
			++completed;
			result = sqlite3_step(statement);
		}
		if (result != SQLITE_DONE)
			error = deadline.expired ? "Message archive CSV export exceeded five minutes." :
				sqlite3_errmsg(database);
		const int finalization = sqlite3_finalize(statement);
		if (result == SQLITE_DONE && finalization != SQLITE_OK)
			error = sqlite3_errmsg(database);
		if (result != SQLITE_DONE || finalization != SQLITE_OK) return false;
		exported = completed;
		return true;
	}
}

bool ExportHistoryCsv(const std::string& utf8Path, const HistoryQuery& query,
	std::ostream& output, int& exported, std::string& error)
{
	exported = 0;
	error.clear();
	sqlite3* database = NULL;
	if (!OpenReadOnlyArchive(utf8Path, database, error)) return false;
	const bool success = ExportHistoryCsvFromDatabase(database, query, output,
		exported, error);
	const int closeResult = sqlite3_close(database);
	if (success && closeResult != SQLITE_OK)
	{
		exported = 0;
		error = "The message archive CSV export could not close its database snapshot.";
		return false;
	}
	if (!success) exported = 0;
	return success;
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
	if (!MigrateArchiveSchema(database_, error))
	{
		sqlite3_close(database_);
		database_ = NULL;
		return false;
	}
	const std::string schema =
		"PRAGMA journal_mode=WAL;PRAGMA synchronous=NORMAL;PRAGMA foreign_keys=ON;"
		"CREATE TABLE IF NOT EXISTS capcode_directory("
		"id INTEGER PRIMARY KEY,protocol TEXT NOT NULL DEFAULT '',address TEXT NOT NULL DEFAULT '',"
		"display_name TEXT NOT NULL DEFAULT '',agency TEXT NOT NULL DEFAULT '',"
		"color INTEGER NOT NULL DEFAULT 13369344,notes TEXT NOT NULL DEFAULT '',enabled INTEGER NOT NULL DEFAULT 1,"
		"filter_type INTEGER NOT NULL DEFAULT 0,match_text TEXT NOT NULL DEFAULT '',filter_label TEXT NOT NULL DEFAULT '',"
		"reject INTEGER NOT NULL DEFAULT 0,match_exact INTEGER NOT NULL DEFAULT 0,show_label INTEGER NOT NULL DEFAULT 1,"
		"command_enabled INTEGER NOT NULL DEFAULT 0,monitor_only INTEGER NOT NULL DEFAULT 0,email_enabled INTEGER NOT NULL DEFAULT 0,"
		"separate_file_enabled INTEGER NOT NULL DEFAULT 0,separate_file_1 TEXT NOT NULL DEFAULT '',"
		"separate_file_2 TEXT NOT NULL DEFAULT '',separate_file_3 TEXT NOT NULL DEFAULT '',"
		"wave_number INTEGER NOT NULL DEFAULT 0,label_color INTEGER NOT NULL DEFAULT 0,hit_counter INTEGER NOT NULL DEFAULT 0,"
		"last_hit_date TEXT NOT NULL DEFAULT '',last_hit_time TEXT NOT NULL DEFAULT '',"
		"filter_enabled INTEGER NOT NULL DEFAULT 1,output_routing_configured INTEGER NOT NULL DEFAULT 1,"
		"output_routes INTEGER NOT NULL DEFAULT 0,agency_label_position INTEGER NOT NULL DEFAULT 0,"
		"updated_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);"
		"CREATE INDEX IF NOT EXISTS idx_capcode_directory_address ON capcode_directory(address);"
		"CREATE INDEX IF NOT EXISTS idx_capcode_directory_name ON capcode_directory(display_name);"
		"CREATE UNIQUE INDEX IF NOT EXISTS idx_capcode_directory_rule ON capcode_directory(protocol,address,filter_type,match_text);"
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
	bool validAddress = entry.filterType == 3 && entry.address.empty();
	if (!validAddress && !entry.address.empty() && entry.address.size() <= 18)
	{
		validAddress = true;
		for (std::string::const_iterator character = entry.address.begin(); character != entry.address.end(); ++character)
		{
			const unsigned char value = static_cast<unsigned char>(*character);
			if (!std::isalnum(value) && value != '?' && value != '-') { validAddress = false; break; }
		}
	}
	if (!validAddress) { error = "Enter a capcode/filter pattern of 1 to 18 letters, digits, ? or -, or leave it blank for a Text filter."; return false; }
	if (!IsValidProtocolName(entry.protocol)) { error = "Choose a supported protocol or Any protocol."; return false; }
	if (entry.filterType == 3 && entry.matchText.empty()) { error = "A Text filter requires at least one keyword."; return false; }
	if (entry.matchExactMessage && entry.matchText.find('+') != std::string::npos)
	{
		error = "Use either + between required keywords or Enable exact message match, not both.";
		return false;
	}
	if (!pdw::filters::IsValidRequiredTermsExpression(entry.matchText.c_str(), 10))
	{
		error = "Use 2 to 10 non-empty keywords separated by +, or enter one keyword.";
		return false;
	}
	if (entry.filterEnabled && entry.monitorOnly)
	{
		error = "Choose either Filter or Monitor only; a message cannot be sent to both panes.";
		return false;
	}
	if (entry.matchText.size() > 40 || entry.filterLabel.size() > 256 || entry.displayName.size() > 256 ||
		entry.separateFile1.size() > 128 || entry.separateFile2.size() > 128 || entry.separateFile3.size() > 128)
	{
		error = "One or more filter fields exceeds the established PDW legacy field length.";
		return false;
	}
	if (entry.color > 0x00ffffffUL) { error = "Capcode colours must be a valid Windows RGB value."; return false; }
	if (entry.filterType < 0 || entry.filterType > 6) { error = "Choose a supported filter type."; return false; }
	if (entry.waveNumber < 0 || entry.waveNumber > 10 || entry.labelColor < 0 || entry.labelColor > 16)
	{
		error = "The selected audio or label colour is invalid.";
		return false;
	}
	if ((entry.outputRoutes & ~PDW_OUTPUT_ROUTE_ALL) != 0)
	{
		error = "One or more selected output routes is invalid.";
		return false;
	}
	if (entry.agencyLabelPosition < PDW_AGENCY_LABEL_HIDDEN ||
		entry.agencyLabelPosition > PDW_AGENCY_LABEL_AFTER)
	{
		error = "Choose whether Agency / service is hidden, before, or after the display name.";
		return false;
	}
	const char* insertSql =
		"INSERT INTO capcode_directory(protocol,address,display_name,agency,color,notes,enabled,filter_type,match_text,filter_label,"
		"reject,match_exact,show_label,command_enabled,monitor_only,email_enabled,separate_file_enabled,separate_file_1,separate_file_2,"
		"separate_file_3,wave_number,label_color,hit_counter,last_hit_date,last_hit_time,filter_enabled,output_routing_configured,"
		"output_routes,agency_label_position,updated_utc)"
		" VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,CURRENT_TIMESTAMP)"
		" ON CONFLICT(protocol,address,filter_type,match_text) DO UPDATE SET display_name=excluded.display_name,agency=excluded.agency,"
		"color=excluded.color,notes=excluded.notes,enabled=excluded.enabled,filter_label=excluded.filter_label,reject=excluded.reject,"
		"match_exact=excluded.match_exact,show_label=excluded.show_label,command_enabled=excluded.command_enabled,"
		"monitor_only=excluded.monitor_only,email_enabled=excluded.email_enabled,separate_file_enabled=excluded.separate_file_enabled,"
		"separate_file_1=excluded.separate_file_1,separate_file_2=excluded.separate_file_2,separate_file_3=excluded.separate_file_3,"
		"wave_number=excluded.wave_number,label_color=excluded.label_color,hit_counter=excluded.hit_counter,"
		"last_hit_date=excluded.last_hit_date,last_hit_time=excluded.last_hit_time,filter_enabled=excluded.filter_enabled,"
		"output_routing_configured=excluded.output_routing_configured,output_routes=excluded.output_routes,"
		"agency_label_position=excluded.agency_label_position,updated_utc=CURRENT_TIMESTAMP;";
	const char* updateSql =
		"UPDATE capcode_directory SET protocol=?,address=?,display_name=?,agency=?,color=?,notes=?,enabled=?,filter_type=?,match_text=?,"
		"filter_label=?,reject=?,match_exact=?,show_label=?,command_enabled=?,monitor_only=?,email_enabled=?,separate_file_enabled=?,"
		"separate_file_1=?,separate_file_2=?,separate_file_3=?,wave_number=?,label_color=?,hit_counter=?,last_hit_date=?,last_hit_time=?,"
		"filter_enabled=?,output_routing_configured=?,output_routes=?,agency_label_position=?,"
		"updated_utc=CURRENT_TIMESTAMP WHERE id=?;";
	sqlite3_stmt* statement = NULL;
	if (sqlite3_prepare_v2(database_, entry.id > 0 ? updateSql : insertSql, -1, &statement, NULL) != SQLITE_OK)
	{
		error = sqlite3_errmsg(database_);
		return false;
	}
	bool bound = BindText(statement, 1, entry.protocol) && BindText(statement, 2, entry.address) &&
		BindText(statement, 3, entry.displayName) && BindText(statement, 4, entry.agency) &&
		sqlite3_bind_int64(statement, 5, static_cast<sqlite3_int64>(entry.color)) == SQLITE_OK &&
		BindText(statement, 6, entry.notes) && sqlite3_bind_int(statement, 7, entry.enabled ? 1 : 0) == SQLITE_OK &&
		sqlite3_bind_int(statement, 8, entry.filterType) == SQLITE_OK && BindText(statement, 9, entry.matchText) &&
		BindText(statement, 10, entry.filterLabel) && sqlite3_bind_int(statement, 11, entry.reject ? 1 : 0) == SQLITE_OK &&
		sqlite3_bind_int(statement, 12, entry.matchExactMessage ? 1 : 0) == SQLITE_OK &&
		sqlite3_bind_int(statement, 13, entry.showFilterLabel ? 1 : 0) == SQLITE_OK &&
		sqlite3_bind_int(statement, 14, entry.commandEnabled ? 1 : 0) == SQLITE_OK &&
		sqlite3_bind_int(statement, 15, entry.monitorOnly ? 1 : 0) == SQLITE_OK &&
		sqlite3_bind_int(statement, 16, entry.emailEnabled ? 1 : 0) == SQLITE_OK &&
		sqlite3_bind_int(statement, 17, entry.separateFileEnabled ? 1 : 0) == SQLITE_OK &&
		BindText(statement, 18, entry.separateFile1) && BindText(statement, 19, entry.separateFile2) &&
		BindText(statement, 20, entry.separateFile3) && sqlite3_bind_int(statement, 21, entry.waveNumber) == SQLITE_OK &&
		sqlite3_bind_int(statement, 22, entry.labelColor) == SQLITE_OK &&
		sqlite3_bind_int64(statement, 23, static_cast<sqlite3_int64>(entry.hitCounter)) == SQLITE_OK &&
		BindText(statement, 24, entry.lastHitDate) && BindText(statement, 25, entry.lastHitTime) &&
		sqlite3_bind_int(statement, 26, entry.filterEnabled ? 1 : 0) == SQLITE_OK &&
		sqlite3_bind_int(statement, 27, entry.outputRoutingConfigured ? 1 : 0) == SQLITE_OK &&
		sqlite3_bind_int64(statement, 28, static_cast<sqlite3_int64>(entry.outputRoutes)) == SQLITE_OK &&
		sqlite3_bind_int(statement, 29, entry.agencyLabelPosition) == SQLITE_OK;
	if (entry.id > 0)
		bound = sqlite3_bind_int64(statement, 30, static_cast<sqlite3_int64>(entry.id)) == SQLITE_OK && bound;
	const int result = bound ? sqlite3_step(statement) : SQLITE_ERROR;
	const int changed = result == SQLITE_DONE ? sqlite3_changes(database_) : 0;
	if (result != SQLITE_DONE || (entry.id > 0 && changed != 1))
		error = result != SQLITE_DONE ? sqlite3_errmsg(database_) : "The selected Capcode Directory entry no longer exists.";
	sqlite3_finalize(statement);
	return result == SQLITE_DONE && (entry.id <= 0 || changed == 1);
}

bool MessageArchive::ReplaceCapcodes(const std::vector<CapcodeEntry>& entries,
	std::string& error)
{
	LockGuard guard(&lock_);
	error.clear();
	if (!database_) { error = "Message archive is not open."; return false; }
	if (!Execute("BEGIN IMMEDIATE;", error)) return false;
	if (!Execute("DELETE FROM capcode_directory;", error))
	{
		Execute("ROLLBACK;", error);
		return false;
	}
	for (std::vector<CapcodeEntry>::const_iterator entry = entries.begin();
		entry != entries.end(); ++entry)
	{
		CapcodeEntry insert(*entry);
		insert.id = 0;
		if (!UpsertCapcode(insert, error))
		{
			std::string ignored;
			Execute("ROLLBACK;", ignored);
			return false;
		}
	}
	if (!Execute("COMMIT;", error))
	{
		std::string ignored;
		Execute("ROLLBACK;", ignored);
		return false;
	}
	return true;
}

bool WriteCapcodeDirectoryCsv(const std::vector<CapcodeEntry>& entries,
	std::ostream& output, std::string& error)
{
	error.clear();
	output << "protocol,address,display_name,agency,color,notes,enabled,filter_type,match_text,filter_label,"
		"reject,match_exact,show_label,command_enabled,monitor_only,email_enabled,separate_file_enabled,"
		"separate_file_1,separate_file_2,separate_file_3,wave_number,label_color,hit_counter,last_hit_date,last_hit_time,"
		"filter_enabled,output_routing_configured,output_routes,agency_label_position\r\n";
	for (std::vector<CapcodeEntry>::const_iterator item = entries.begin(); item != entries.end(); ++item)
	{
		output << CsvEscape(item->protocol.empty() ? "Any" : item->protocol) << ','
			<< CsvEscape(item->address) << ',' << CsvEscape(item->displayName) << ','
			<< CsvEscape(item->agency) << ',' << item->color << ',' << CsvEscape(item->notes) << ','
			<< (item->enabled ? 1 : 0) << ',' << item->filterType << ',' << CsvEscape(item->matchText) << ','
			<< CsvEscape(item->displayName) << ',' << (item->reject ? 1 : 0) << ','
			<< (item->matchExactMessage ? 1 : 0) << ',' << (item->showFilterLabel ? 1 : 0) << ','
			<< (item->commandEnabled ? 1 : 0) << ',' << (item->monitorOnly ? 1 : 0) << ','
			<< (item->emailEnabled ? 1 : 0) << ',' << (item->separateFileEnabled ? 1 : 0) << ','
			<< CsvEscape(item->separateFile1) << ',' << CsvEscape(item->separateFile2) << ','
			<< CsvEscape(item->separateFile3) << ',' << item->waveNumber << ',' << item->labelColor << ','
			<< item->hitCounter << ',' << CsvEscape(item->lastHitDate) << ','
			<< CsvEscape(item->lastHitTime) << ',' << (item->filterEnabled ? 1 : 0) << ','
			<< (item->outputRoutingConfigured ? 1 : 0) << ',' << item->outputRoutes << ','
			<< item->agencyLabelPosition << "\r\n";
		if (!output.good()) { error = "The Capcode Directory CSV could not be written."; return false; }
	}
	return output.good();
}

bool ReadCapcodeDirectoryCsv(std::istream& input,
	std::vector<CapcodeEntry>& entries, int& rejected, std::string& error)
{
	entries.clear();
	rejected = 0;
	error.clear();
	std::string record;
	int row = 0;
	for (;;)
	{
		const CsvRecordReadResult readResult = ReadCsvRecord(input, record);
		if (readResult == CSV_RECORD_END) break;
		++row;
		if (readResult == CSV_RECORD_MALFORMED)
		{
			error = "The Capcode Directory CSV contains an unterminated quoted field.";
			++rejected;
			return false;
		}
		if (record.size() >= 3 && static_cast<unsigned char>(record[0]) == 0xEF &&
			static_cast<unsigned char>(record[1]) == 0xBB && static_cast<unsigned char>(record[2]) == 0xBF)
			record.erase(0, 3);
		std::vector<std::string> fields;
		if (!ParseCsvLine(record, fields) || fields.size() < 2) { ++rejected; continue; }
		if (row == 1 && _stricmp(fields[0].c_str(), "protocol") == 0) continue;
		CapcodeEntry entry;
		entry.protocol = _stricmp(fields[0].c_str(), "Any") == 0 ? "" : fields[0];
		entry.address = fields[1];
		if (fields.size() > 2) entry.displayName = fields[2];
		if (fields.size() > 3) entry.agency = fields[3];
		unsigned long parsed = 0;
		if (fields.size() > 4 && !fields[4].empty())
		{
			if (!ParseCsvUnsigned(fields[4], 0x00ffffffUL, parsed)) { ++rejected; continue; }
			entry.color = parsed;
		}
		if (fields.size() > 5) entry.notes = fields[5];
		if (fields.size() > 6) entry.enabled = CsvBoolean(fields[6]);
		if (fields.size() > 7 && !fields[7].empty())
		{
			if (!ParseCsvUnsigned(fields[7], 6, parsed)) { ++rejected; continue; }
			entry.filterType = static_cast<int>(parsed);
		}
		if (fields.size() > 8) entry.matchText = fields[8];
		if (fields.size() > 9) entry.filterLabel = fields[9];
		if (fields.size() > 10) entry.reject = CsvBoolean(fields[10]);
		if (fields.size() > 11) entry.matchExactMessage = CsvBoolean(fields[11]);
		if (fields.size() > 12) entry.showFilterLabel = CsvBoolean(fields[12]);
		if (fields.size() > 13) entry.commandEnabled = CsvBoolean(fields[13]);
		if (fields.size() > 14) entry.monitorOnly = CsvBoolean(fields[14]);
		if (fields.size() > 15) entry.emailEnabled = CsvBoolean(fields[15]);
		if (fields.size() > 16) entry.separateFileEnabled = CsvBoolean(fields[16]);
		if (fields.size() > 17) entry.separateFile1 = fields[17];
		if (fields.size() > 18) entry.separateFile2 = fields[18];
		if (fields.size() > 19) entry.separateFile3 = fields[19];
		if (fields.size() > 20 && !fields[20].empty())
		{
			if (!ParseCsvUnsigned(fields[20], 10, parsed)) { ++rejected; continue; }
			entry.waveNumber = static_cast<int>(parsed);
		}
		if (fields.size() > 21 && !fields[21].empty())
		{
			if (!ParseCsvUnsigned(fields[21], 16, parsed)) { ++rejected; continue; }
			entry.labelColor = static_cast<int>(parsed);
		}
		if (fields.size() > 22 && !fields[22].empty())
		{
			if (!ParseCsvUnsigned(fields[22], UINT_MAX, parsed)) { ++rejected; continue; }
			entry.hitCounter = static_cast<unsigned int>(parsed);
		}
		if (fields.size() > 23) entry.lastHitDate = fields[23];
		if (fields.size() > 24) entry.lastHitTime = fields[24];
		if (fields.size() > 25) entry.filterEnabled = CsvBoolean(fields[25]);
		else entry.filterEnabled = entry.enabled && !entry.monitorOnly;
		if (fields.size() > 26)
		{
			entry.outputRoutingConfigured = CsvBoolean(fields[26]);
			if (fields.size() > 27 && !fields[27].empty())
			{
				if (!ParseCsvUnsigned(fields[27], PDW_OUTPUT_ROUTE_ALL, parsed)) { ++rejected; continue; }
				entry.outputRoutes = static_cast<unsigned int>(parsed);
			}
			else entry.outputRoutes = 0;
		}
		else
		{
			entry.outputRoutingConfigured = false;
			entry.outputRoutes = entry.emailEnabled ? PDW_OUTPUT_ROUTE_EMAIL : 0;
		}
		if (fields.size() > 28 && !fields[28].empty())
		{
			if (!ParseCsvUnsigned(fields[28], PDW_AGENCY_LABEL_AFTER, parsed)) { ++rejected; continue; }
			entry.agencyLabelPosition = static_cast<int>(parsed);
		}
		if (entry.displayName.empty()) entry.displayName = entry.filterLabel;
		entry.filterLabel = entry.displayName;
		entry.commandEnabled = true;
		if (entry.outputRoutingConfigured)
			entry.emailEnabled = (entry.outputRoutes & PDW_OUTPUT_ROUTE_EMAIL) != 0;
		entries.push_back(entry);
	}
	if (input.bad()) { error = "The Capcode Directory CSV could not be read."; return false; }
	return true;
}

bool MessageArchive::UpdateCapcodeRuntimeState(long long id, unsigned int hitCounter,
	const std::string& lastHitDate, const std::string& lastHitTime, std::string& error)
{
	LockGuard guard(&lock_);
	error.clear();
	if (!database_) { error = "Message archive is not open."; return false; }
	if (id <= 0) return true;
	sqlite3_stmt* statement = NULL;
	if (sqlite3_prepare_v2(database_,
		"UPDATE capcode_directory SET hit_counter=?,last_hit_date=?,last_hit_time=?,updated_utc=CURRENT_TIMESTAMP WHERE id=?;",
		-1, &statement, NULL) != SQLITE_OK)
	{
		error = sqlite3_errmsg(database_);
		return false;
	}
	const bool bound = sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(hitCounter)) == SQLITE_OK &&
		BindText(statement, 2, lastHitDate) && BindText(statement, 3, lastHitTime) &&
		sqlite3_bind_int64(statement, 4, static_cast<sqlite3_int64>(id)) == SQLITE_OK;
	const int result = bound ? sqlite3_step(statement) : SQLITE_ERROR;
	if (result != SQLITE_DONE) error = sqlite3_errmsg(database_);
	sqlite3_finalize(statement);
	return result == SQLITE_DONE;
}

bool MessageArchive::DeleteCapcode(long long id, std::string& error)
{
	LockGuard guard(&lock_);
	error.clear();
	if (!database_) { error = "Message archive is not open."; return false; }
	sqlite3_stmt* statement = NULL;
	if (sqlite3_prepare_v2(database_, "DELETE FROM capcode_directory WHERE id=?;", -1, &statement, NULL) != SQLITE_OK)
	{
		error = sqlite3_errmsg(database_);
		return false;
	}
	const bool bound = sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(id)) == SQLITE_OK;
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
	const std::string sql = std::string("SELECT ") + CapcodeSelectColumns() +
		" FROM capcode_directory WHERE address=? AND enabled=1 AND (protocol='' OR ? LIKE protocol || '%') "
		"ORDER BY CASE WHEN protocol='' THEN 1 ELSE 0 END,id LIMIT 1;";
	sqlite3_stmt* statement = NULL;
	if (sqlite3_prepare_v2(database_, sql.c_str(), -1, &statement, NULL) != SQLITE_OK)
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
	const std::string sql = std::string("SELECT ") + CapcodeSelectColumns() + " FROM capcode_directory "
		"WHERE ?='' OR lower(protocol) LIKE ? OR lower(address) LIKE ? OR lower(display_name) LIKE ? "
		"OR lower(agency) LIKE ? OR lower(notes) LIKE ? OR lower(match_text) LIKE ? OR lower(filter_label) LIKE ? "
		"ORDER BY display_name,address,protocol,id;";
	sqlite3_stmt* statement = NULL;
	if (sqlite3_prepare_v2(database_, sql.c_str(), -1, &statement, NULL) != SQLITE_OK)
	{
		error = sqlite3_errmsg(database_);
		return false;
	}
	bool bound = BindText(statement, 1, search) && BindText(statement, 2, pattern) &&
		BindText(statement, 3, pattern) && BindText(statement, 4, pattern) &&
		BindText(statement, 5, pattern) && BindText(statement, 6, pattern) &&
		BindText(statement, 7, pattern) && BindText(statement, 8, pattern);
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
	const std::string where = " FROM message_history h" +
		std::string(HistoryFilterWhereSql());
	const std::string countSql = "SELECT COUNT(*)" + where + ";";
	const std::string selectSql =
		"SELECT h.id,h.received_utc,h.source,h.address,h.local_time,h.local_date,h.mode,h.message_type,"
		"h.bitrate,h.message,h.filter_label,h.filtered,h.rejected,h.blocked_duplicate,h.fragmented,h.assembled" +
		where + " ORDER BY h.received_utc DESC,h.id DESC LIMIT ? OFFSET ?;";

	sqlite3_stmt* countStatement = NULL;
	if (sqlite3_prepare_v2(database_, countSql.c_str(), -1, &countStatement, NULL) != SQLITE_OK ||
		!BindHistoryFilter(countStatement, query) || sqlite3_step(countStatement) != SQLITE_ROW)
	{
		error = deadline.expired ? "Message archive query exceeded three seconds." :
			sqlite3_errmsg(database_);
		if (countStatement) sqlite3_finalize(countStatement);
		return false;
	}
	total = sqlite3_column_int(countStatement, 0);
	sqlite3_finalize(countStatement);

	sqlite3_stmt* statement = NULL;
	if (sqlite3_prepare_v2(database_, selectSql.c_str(), -1, &statement, NULL) != SQLITE_OK ||
		!BindHistoryFilter(statement, query) ||
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
