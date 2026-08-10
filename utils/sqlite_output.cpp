#include "sqlite_output.h"

#include <winsqlite/winsqlite3.h>

#include "data_output_core.h"

namespace pdw
{
namespace outputs
{
namespace
{
	bool Execute(sqlite3* database, const std::string& sql, std::string& error)
	{
		char* sqliteError = NULL;
		const int result = sqlite3_exec(database, sql.c_str(), NULL, NULL, &sqliteError);
		if (result == SQLITE_OK) return true;
		error = sqliteError ? sqliteError : sqlite3_errmsg(database);
		if (sqliteError) sqlite3_free(sqliteError);
		return false;
	}

	bool BindText(sqlite3_stmt* statement, int index, const std::string& value)
	{
		return sqlite3_bind_text(statement, index, value.c_str(),
			static_cast<int>(value.size()), SQLITE_TRANSIENT) == SQLITE_OK;
	}
}

SqliteOutput::SqliteOutput() : database_(NULL), insert_(NULL) {}

SqliteOutput::~SqliteOutput()
{
	Close();
}

bool SqliteOutput::Open(const std::string& utf8Path, const std::string& table, std::string& error)
{
	error.clear();
	if (!IsSafeSqlIdentifier(table))
	{
		error = "SQLite table name must begin with a letter or underscore and contain only letters, numbers, or underscores.";
		return false;
	}
	if (utf8Path.empty())
	{
		error = "Choose a SQLite database file.";
		return false;
	}
	if (database_ && path_ == utf8Path && table_ == table) return true;
	Close();
	const int result = sqlite3_open_v2(utf8Path.c_str(), &database_,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL);
	if (result != SQLITE_OK)
	{
		error = database_ ? sqlite3_errmsg(database_) : "SQLite could not open the database.";
		Close();
		return false;
	}
	sqlite3_busy_timeout(database_, 5000);
	if (!Execute(database_, "PRAGMA journal_mode=WAL;", error) ||
		!Execute(database_, "PRAGMA synchronous=NORMAL;", error) ||
		!Execute(database_, "PRAGMA foreign_keys=ON;", error))
	{
		Close();
		return false;
	}

	const std::string quoted = "\"" + table + "\"";
	const std::string schema =
		"CREATE TABLE IF NOT EXISTS " + quoted + " ("
		"id TEXT PRIMARY KEY NOT NULL,"
		"received_utc TEXT NOT NULL,"
		"source TEXT NOT NULL DEFAULT '',"
		"address TEXT NOT NULL DEFAULT '',"
		"local_time TEXT NOT NULL DEFAULT '',"
		"local_date TEXT NOT NULL DEFAULT '',"
		"mode TEXT NOT NULL DEFAULT '',"
		"message_type TEXT NOT NULL DEFAULT '',"
		"bitrate TEXT NOT NULL DEFAULT '',"
		"message TEXT NOT NULL DEFAULT '',"
		"filter_label TEXT NOT NULL DEFAULT '',"
		"filter_matched INTEGER NOT NULL DEFAULT 0,"
		"monitor_only INTEGER NOT NULL DEFAULT 0,"
		"filtered INTEGER NOT NULL DEFAULT 0,"
		"rejected INTEGER NOT NULL DEFAULT 0,"
		"blocked_duplicate INTEGER NOT NULL DEFAULT 0,"
		"group_call INTEGER NOT NULL DEFAULT 0,"
		"group_final INTEGER NOT NULL DEFAULT 0,"
		"fragmented INTEGER NOT NULL DEFAULT 0,"
		"assembled INTEGER NOT NULL DEFAULT 0,"
		"filter_index INTEGER NOT NULL DEFAULT -1,"
		"group_bit INTEGER NOT NULL DEFAULT -1,"
		"cycle INTEGER NOT NULL DEFAULT -1,"
		"frame INTEGER NOT NULL DEFAULT -1"
		");"
		"CREATE INDEX IF NOT EXISTS \"idx_" + table + "_received\" ON " + quoted + "(received_utc);"
		"CREATE INDEX IF NOT EXISTS \"idx_" + table + "_address\" ON " + quoted + "(address);"
		"CREATE INDEX IF NOT EXISTS \"idx_" + table + "_filtered\" ON " + quoted + "(filtered);";
	if (!Execute(database_, schema, error))
	{
		Close();
		return false;
	}

	const std::string insertSql =
		"INSERT OR IGNORE INTO " + quoted + " ("
		"id,received_utc,source,address,local_time,local_date,mode,message_type,bitrate,message,filter_label,"
		"filter_matched,monitor_only,filtered,rejected,blocked_duplicate,group_call,group_final,fragmented,assembled,"
		"filter_index,group_bit,cycle,frame) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
	if (sqlite3_prepare_v2(database_, insertSql.c_str(), -1, &insert_, NULL) != SQLITE_OK)
	{
		error = sqlite3_errmsg(database_);
		Close();
		return false;
	}
	path_ = utf8Path;
	table_ = table;
	return true;
}

bool SqliteOutput::Write(const pdw::publishing::PublishEvent& event, std::string& error)
{
	error.clear();
	if (!database_ || !insert_)
	{
		error = "SQLite output is not open.";
		return false;
	}
	sqlite3_reset(insert_);
	sqlite3_clear_bindings(insert_);
	bool bound = true;
	bound = bound && BindText(insert_, 1, event.id);
	bound = bound && BindText(insert_, 2, event.timestamp);
	bound = bound && BindText(insert_, 3, event.source);
	bound = bound && BindText(insert_, 4, event.address);
	bound = bound && BindText(insert_, 5, event.time);
	bound = bound && BindText(insert_, 6, event.date);
	bound = bound && BindText(insert_, 7, event.mode);
	bound = bound && BindText(insert_, 8, event.messageType);
	bound = bound && BindText(insert_, 9, event.bitrate);
	bound = bound && BindText(insert_, 10, event.message);
	bound = bound && BindText(insert_, 11, event.filterLabel);
	bound = bound && sqlite3_bind_int(insert_, 12, event.filterMatched ? 1 : 0) == SQLITE_OK;
	bound = bound && sqlite3_bind_int(insert_, 13, event.monitorOnly ? 1 : 0) == SQLITE_OK;
	bound = bound && sqlite3_bind_int(insert_, 14, event.filtered ? 1 : 0) == SQLITE_OK;
	bound = bound && sqlite3_bind_int(insert_, 15, event.rejected ? 1 : 0) == SQLITE_OK;
	bound = bound && sqlite3_bind_int(insert_, 16, event.blockedDuplicate ? 1 : 0) == SQLITE_OK;
	bound = bound && sqlite3_bind_int(insert_, 17, event.groupCall ? 1 : 0) == SQLITE_OK;
	bound = bound && sqlite3_bind_int(insert_, 18, event.groupFinal ? 1 : 0) == SQLITE_OK;
	bound = bound && sqlite3_bind_int(insert_, 19, event.fragmented ? 1 : 0) == SQLITE_OK;
	bound = bound && sqlite3_bind_int(insert_, 20, event.assembled ? 1 : 0) == SQLITE_OK;
	bound = bound && sqlite3_bind_int(insert_, 21, event.filterIndex) == SQLITE_OK;
	bound = bound && sqlite3_bind_int(insert_, 22, event.groupBit) == SQLITE_OK;
	bound = bound && sqlite3_bind_int(insert_, 23, event.cycle) == SQLITE_OK;
	bound = bound && sqlite3_bind_int(insert_, 24, event.frame) == SQLITE_OK;
	if (!bound)
	{
		error = sqlite3_errmsg(database_);
		return false;
	}
	const int result = sqlite3_step(insert_);
	if (result == SQLITE_DONE) return true;
	error = sqlite3_errmsg(database_);
	return false;
}

void SqliteOutput::Close()
{
	if (insert_) sqlite3_finalize(insert_);
	insert_ = NULL;
	if (database_) sqlite3_close(database_);
	database_ = NULL;
	path_.clear();
	table_.clear();
}

bool SqliteOutput::IsOpen() const
{
	return database_ != NULL && insert_ != NULL;
}

} // namespace outputs
} // namespace pdw
