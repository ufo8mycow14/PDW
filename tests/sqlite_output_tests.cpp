#include "sqlite_output.h"

#include <windows.h>
#include <winsqlite/winsqlite3.h>

#include <iostream>
#include <string>

int main()
{
	pdw::outputs::SqliteOutput output;
	std::string error;
	char temporaryFolder[MAX_PATH] = {};
	char databasePath[MAX_PATH] = {};
	if (!GetTempPathA(_countof(temporaryFolder), temporaryFolder) ||
		!GetTempFileNameA(temporaryFolder, "pdw", 0, databasePath))
	{
		std::cerr << "Could not create a temporary SQLite test path.\n";
		return 1;
	}
	DeleteFileA(databasePath);
	if (!output.Open(databasePath, "pdw_messages", error))
	{
		std::cerr << "Could not open temporary SQLite output: " << error << '\n';
		return 1;
	}
	pdw::publishing::PublishEvent event;
	event.id = "test-event-1";
	event.timestamp = "2026-08-10T01:02:03.004Z";
	event.source = "PDW test";
	event.address = "1234567";
	event.time = "10:32:00";
	event.date = "10-08-26";
	event.mode = "FLEX";
	event.messageType = "ALPHA";
	event.bitrate = "1600";
	event.message = "SQLite integration test";
	event.filterLabel = "Test filter";
	event.filterMatched = true;
	event.filtered = true;
	event.cycle = 3;
	event.frame = 42;
	if (!output.Write(event, error) || !output.Write(event, error))
	{
		std::cerr << "Could not write an idempotent SQLite event: " << error << '\n';
		return 1;
	}

	output.Close();
	sqlite3* verification = NULL;
	if (sqlite3_open_v2(databasePath, &verification, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK)
	{
		std::cerr << "Could not reopen the SQLite test database.\n";
		return 1;
	}
	sqlite3_stmt* count = NULL;
	const char* query = "SELECT COUNT(*), MAX(message), MAX(cycle), MAX(frame) FROM pdw_messages;";
	const bool prepared = sqlite3_prepare_v2(verification, query, -1, &count, NULL) == SQLITE_OK;
	const bool row = prepared && sqlite3_step(count) == SQLITE_ROW;
	const bool correct = row && sqlite3_column_int(count, 0) == 1 &&
		std::string(reinterpret_cast<const char*>(sqlite3_column_text(count, 1))) == event.message &&
		sqlite3_column_int(count, 2) == event.cycle && sqlite3_column_int(count, 3) == event.frame;
	if (count) sqlite3_finalize(count);
	sqlite3_close(verification);
	DeleteFileA(databasePath);
	DeleteFileA((std::string(databasePath) + "-wal").c_str());
	DeleteFileA((std::string(databasePath) + "-shm").c_str());
	if (!correct)
	{
		std::cerr << "SQLite schema, metadata, or idempotent event count was incorrect.\n";
		return 1;
	}
	std::cout << "SQLite output integration test passed.\n";
	return 0;
}
