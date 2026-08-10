#include "message_archive.h"

#include <windows.h>
#include <winsqlite/winsqlite3.h>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	void Expect(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAILED: " << message << '\n';
			std::exit(1);
		}
	}
}

int main()
{
	char temporaryFolder[MAX_PATH] = {};
	char databasePath[MAX_PATH] = {};
	Expect(GetTempPathA(_countof(temporaryFolder), temporaryFolder) != 0,
		"temporary folder available");
	Expect(GetTempFileNameA(temporaryFolder, "pda", 0, databasePath) != 0,
		"temporary archive path available");
	DeleteFileA(databasePath);

	// A valid but unrelated SQLite database is operator-owned data. Opening it
	// as an archive must fail without adding PDW tables or changing its content.
	sqlite3* unrelated = NULL;
	Expect(sqlite3_open(databasePath, &unrelated) == SQLITE_OK,
		"unrelated SQLite fixture opens");
	Expect(sqlite3_exec(unrelated,
		"CREATE TABLE sqliteData(value TEXT);INSERT INTO sqliteData VALUES('preserve me');",
		NULL, NULL, NULL) == SQLITE_OK, "unrelated SQLite fixture is populated");
	sqlite3_close(unrelated);
	pdw::archive::MessageArchive ownershipCheck;
	std::string ownershipError;
	Expect(!ownershipCheck.Open(databasePath, ownershipError),
		"unrelated SQLite database is rejected");
	Expect(sqlite3_open(databasePath, &unrelated) == SQLITE_OK,
		"rejected SQLite fixture reopens");
	sqlite3_stmt* preserved = NULL;
	Expect(sqlite3_prepare_v2(unrelated,
		"SELECT value FROM sqliteData;", -1, &preserved, NULL) == SQLITE_OK &&
		sqlite3_step(preserved) == SQLITE_ROW &&
		std::string(reinterpret_cast<const char*>(sqlite3_column_text(preserved, 0))) == "preserve me",
		"unrelated SQLite content remains intact");
	sqlite3_finalize(preserved);
	sqlite3_close(unrelated);
	DeleteFileA(databasePath);

	pdw::archive::MessageArchive archive;
	std::string error;
	const bool archiveOpened = archive.Open(databasePath, error);
	if (!archiveOpened) std::cerr << "archive open error: " << error << '\n';
	Expect(archiveOpened, "archive opens");
	Expect(pdw::archive::IsValidCapcode("1234567"), "seven digit capcode accepted");
	Expect(!pdw::archive::IsValidCapcode("12A4567"), "non-numeric capcode rejected");

	pdw::archive::CapcodeEntry capcode;
	capcode.protocol = "POCSAG";
	capcode.address = "1234567";
	capcode.displayName = "Station Alpha";
	capcode.agency = "Test Agency";
	capcode.notes = "Local test only";
	capcode.enabled = true;
	Expect(archive.UpsertCapcode(capcode, error), "capcode directory upsert succeeds");
	pdw::archive::CapcodeEntry invalidColor(capcode);
	invalidColor.color = 0x01000000UL;
	Expect(!archive.UpsertCapcode(invalidColor, error), "out-of-range capcode colour rejected");
	pdw::archive::CapcodeEntry found;
	Expect(archive.LookupCapcode("POCSAG-1200", "1234567", found, error),
		"protocol-specific alias matches decoded mode");
	Expect(found.displayName == "Station Alpha", "alias name round trips");

	pdw::publishing::PublishEvent event;
	event.id = "archive-test-event";
	event.timestamp = "2026-08-10T01:02:03.004Z";
	event.source = "PDW archive test";
	event.address = "1234567";
	event.mode = "POCSAG-1200";
	event.messageType = "ALPHA";
	event.message = "Local searchable message";
	event.filtered = true;
	Expect(archive.StoreEvent(event, true, error), "history event stores");
	Expect(archive.StoreEvent(event, true, error), "duplicate event insert is idempotent");

	pdw::archive::HistoryQuery query;
	query.search = "Station Alpha";
	query.filteredOnly = true;
	std::vector<pdw::archive::HistoryRow> rows;
	int total = 0;
	Expect(archive.QueryHistory(query, rows, total, error), "history query succeeds");
	Expect(total == 1 && rows.size() == 1, "history returns one idempotent event");
	Expect(rows[0].displayName == "Station Alpha" && rows[0].event.message == event.message,
		"history joins current directory alias and retained message");

	std::vector<std::string> fields;
	Expect(pdw::archive::ParseCsvLine("POCSAG,1234567,\"Station, Alpha\"", fields) &&
		fields.size() == 3 && fields[2] == "Station, Alpha", "CSV quoted value parses");
	std::istringstream multilineCsv(
		"protocol,address,display_name,agency,color,notes,enabled\r\n"
		"POCSAG,1234567,Station Alpha,Test,13369344,\"first line\r\nsecond line\",1\r\n");
	std::string csvRecord;
	Expect(pdw::archive::ReadCsvRecord(multilineCsv, csvRecord) ==
		pdw::archive::CSV_RECORD_COMPLETE, "CSV header logical record reads");
	Expect(pdw::archive::ReadCsvRecord(multilineCsv, csvRecord) ==
		pdw::archive::CSV_RECORD_COMPLETE &&
		pdw::archive::ParseCsvLine(csvRecord, fields) && fields.size() == 7 &&
		fields[5] == "first line\r\nsecond line",
		"exported multiline notes import as one logical CSV record");

	archive.Close();
	DeleteFileA(databasePath);
	DeleteFileA((std::string(databasePath) + "-wal").c_str());
	DeleteFileA((std::string(databasePath) + "-shm").c_str());

	HANDLE corrupt = CreateFileA(databasePath, GENERIC_WRITE, 0, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	Expect(corrupt != INVALID_HANDLE_VALUE, "corrupt archive fixture opens");
	const char invalidDatabase[] = "synthetic non-SQLite archive";
	DWORD written = 0;
	Expect(WriteFile(corrupt, invalidDatabase, sizeof(invalidDatabase) - 1,
		&written, NULL) != FALSE && written == sizeof(invalidDatabase) - 1,
		"corrupt archive fixture writes");
	CloseHandle(corrupt);
	Expect(!archive.Open(databasePath, error), "non-SQLite archive is rejected");
	Expect(!archive.IsOpen(), "rejected archive remains closed");
	DeleteFileA(databasePath);
	std::cout << "Message archive tests passed\n";
	return 0;
}
