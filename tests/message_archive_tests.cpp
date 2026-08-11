#include "message_archive.h"

#include <windows.h>
#include <winsqlite/winsqlite3.h>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <streambuf>
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

	class FailingStreamBuffer : public std::streambuf
	{
	protected:
		virtual std::streamsize xsputn(const char*, std::streamsize) { return 0; }
		virtual int_type overflow(int_type = traits_type::eof()) { return traits_type::eof(); }
	};
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
	pdw::archive::HistoryQuery unrelatedQuery;
	std::ostringstream unrelatedOutput;
	int unrelatedExported = -1;
	Expect(!pdw::archive::ExportHistoryCsv(databasePath, unrelatedQuery,
		unrelatedOutput, unrelatedExported, ownershipError) && unrelatedExported == 0,
		"CSV export rejects an unrelated SQLite database without claiming it");
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

	sqlite3* versionOne = NULL;
	Expect(sqlite3_open(databasePath, &versionOne) == SQLITE_OK,
		"version-one Capcode Directory fixture opens");
	Expect(sqlite3_exec(versionOne,
		"PRAGMA application_id=1346656049;PRAGMA user_version=1;"
		"CREATE TABLE capcode_directory(protocol TEXT NOT NULL,address TEXT NOT NULL,display_name TEXT NOT NULL,"
		"agency TEXT NOT NULL,color INTEGER NOT NULL,notes TEXT NOT NULL,enabled INTEGER NOT NULL,updated_utc TEXT NOT NULL);"
		"INSERT INTO capcode_directory VALUES('POCSAG','1708068','SAAS Unit Goolwa','SAAS',13369344,'legacy mapping',1,'2026-08-11');",
		NULL, NULL, NULL) == SQLITE_OK, "version-one directory fixture is populated");
	sqlite3_close(versionOne);
	pdw::archive::MessageArchive migratedArchive;
	std::string migrationError;
	Expect(migratedArchive.Open(databasePath, migrationError),
		"version-one Capcode Directory migrates in place");
	std::vector<pdw::archive::CapcodeEntry> migratedRows;
	Expect(migratedArchive.ListCapcodes(std::string(), migratedRows, migrationError) &&
		migratedRows.size() == 1 && migratedRows[0].displayName == "SAAS Unit Goolwa" &&
		migratedRows[0].filterLabel == "SAAS Unit Goolwa" && migratedRows[0].matchText.empty(),
		"existing directory mappings become capcode-only filter rules without a false text requirement");
	migratedArchive.Close();
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
	capcode.filterType = 2;
	capcode.matchText = "PR1+Traffic";
	capcode.filterLabel = "Station Alpha Dispatch";
	capcode.reject = true;
	capcode.matchExactMessage = true;
	capcode.showFilterLabel = true;
	capcode.commandEnabled = true;
	capcode.monitorOnly = true;
	capcode.emailEnabled = true;
	capcode.separateFileEnabled = true;
	capcode.separateFile1 = "alpha.csv";
	capcode.separateFile2 = "backup.csv";
	capcode.waveNumber = 4;
	capcode.labelColor = 7;
	capcode.hitCounter = 12;
	capcode.lastHitDate = "10-08-26";
	capcode.lastHitTime = "12:34:56";
	capcode.enabled = true;
	Expect(archive.UpsertCapcode(capcode, error), "capcode directory upsert succeeds");
	pdw::archive::CapcodeEntry invalidColor(capcode);
	invalidColor.color = 0x01000000UL;
	Expect(!archive.UpsertCapcode(invalidColor, error), "out-of-range capcode colour rejected");
	pdw::archive::CapcodeEntry emptyTextRule;
	emptyTextRule.filterType = 3;
	Expect(!archive.UpsertCapcode(emptyTextRule, error),
		"blank Text filter cannot accidentally match every decoded message");
	pdw::archive::CapcodeEntry found;
	Expect(archive.LookupCapcode("POCSAG-1200", "1234567", found, error),
		"protocol-specific alias matches decoded mode");
	Expect(found.displayName == "Station Alpha" && found.filterType == 2 &&
		found.matchText == "PR1+Traffic" && found.filterLabel == "Station Alpha Dispatch" &&
		found.reject && found.matchExactMessage && found.commandEnabled && found.monitorOnly &&
		found.emailEnabled && found.separateFileEnabled && found.separateFile1 == "alpha.csv" &&
		found.separateFile2 == "backup.csv" && found.waveNumber == 4 && found.labelColor == 7 &&
		found.hitCounter == 12 && found.lastHitDate == "10-08-26" && found.lastHitTime == "12:34:56",
		"all directory-backed legacy filter fields round trip");
	Expect(archive.UpdateCapcodeRuntimeState(found.id, 13, "11-08-26", "01:02:03", error),
		"directory hit-counter state updates");

	pdw::archive::CapcodeEntry secondRule(capcode);
	secondRule.id = 0;
	secondRule.matchText = "Medical";
	secondRule.filterLabel = "Station Alpha Medical";
	secondRule.reject = false;
	Expect(archive.UpsertCapcode(secondRule, error),
		"multiple text rules can share one protocol and capcode");
	std::vector<pdw::archive::CapcodeEntry> directoryRows;
	Expect(archive.ListCapcodes("Station Alpha", directoryRows, error) && directoryRows.size() == 2,
		"directory lists both same-capcode rules without collapsing them");
	std::ostringstream directoryCsv;
	Expect(pdw::archive::WriteCapcodeDirectoryCsv(directoryRows, directoryCsv, error),
		"expanded directory exports as CSV");
	std::vector<pdw::archive::CapcodeEntry> parsedDirectory;
	int rejectedDirectoryRows = 0;
	std::istringstream directoryInput(directoryCsv.str());
	Expect(pdw::archive::ReadCapcodeDirectoryCsv(directoryInput, parsedDirectory,
		rejectedDirectoryRows, error) && rejectedDirectoryRows == 0 && parsedDirectory.size() == 2 &&
		parsedDirectory[0].filterType == 2 && !parsedDirectory[0].filterLabel.empty(),
		"expanded directory CSV round trips all rule columns");
	std::istringstream invalidDirectory(
		"protocol,address,display_name,agency,color,notes,enabled,filter_type\r\nPOCSAG,123,Name,,999999999999999999999,,1,2\r\n");
	Expect(pdw::archive::ReadCapcodeDirectoryCsv(invalidDirectory, parsedDirectory,
		rejectedDirectoryRows, error) && rejectedDirectoryRows == 1 && parsedDirectory.empty(),
		"overflowing directory numeric fields are rejected safely");

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

	pdw::archive::CapcodeEntry exportAlias;
	exportAlias.protocol = "FLEX";
	exportAlias.address = "1705428";
	exportAlias.displayName = " \t=Formula Name";
	exportAlias.agency = std::string("Agency, \"West\" ") + "\xE2\x82\xAC";
	Expect(archive.UpsertCapcode(exportAlias, error),
		"CSV export alias upsert succeeds");

	for (int index = 0; index < 501; ++index)
	{
		char id[64] = {};
		char message[64] = {};
		std::snprintf(id, sizeof(id), "export-bulk-%04d", index);
		std::snprintf(message, sizeof(message), "bulk export row %04d", index);
		pdw::publishing::PublishEvent bulk;
		bulk.id = id;
		bulk.timestamp = "2026-08-09T00:00:00.000Z";
		bulk.source = "PDW archive export test";
		bulk.address = "1705428";
		bulk.time = "09:00:00";
		bulk.date = "2026-08-09";
		bulk.mode = "FLEX-A";
		bulk.messageType = "ALPHA";
		bulk.bitrate = "1600";
		bulk.message = message;
		bulk.filterLabel = "export filter";
		bulk.filtered = true;
		Expect(archive.StoreEvent(bulk, true, error),
			"bulk CSV export event stores");
	}

	pdw::publishing::PublishEvent edge;
	edge.id = "export-edge";
	edge.timestamp = "2026-08-10T00:00:00.000Z";
	edge.source = "PDW archive export test";
	edge.address = "1705428";
	edge.mode = "FLEX-A";
	edge.messageType = " +ALPHA";
	edge.message = " \t=\"bulk export, quoted\"\r\nsecond line";
	edge.filterLabel = "\t@filter";
	edge.filtered = true;
	Expect(archive.StoreEvent(edge, true, error), "edge CSV export event stores");

	pdw::publishing::PublishEvent orderA(edge);
	orderA.id = "export-order-a";
	orderA.timestamp = "2026-08-11T00:00:00.000Z";
	orderA.messageType = "ALPHA";
	orderA.message = "bulk export order A";
	orderA.filterLabel = "\tplain filter";
	Expect(archive.StoreEvent(orderA, true, error), "first tied CSV export event stores");
	pdw::publishing::PublishEvent orderB(orderA);
	orderB.id = "export-order-b";
	orderB.message = "bulk export order B";
	Expect(archive.StoreEvent(orderB, true, error), "second tied CSV export event stores");
	pdw::archive::HistoryQuery tiedOrderQuery;
	tiedOrderQuery.search = "bulk export order";
	tiedOrderQuery.protocol = "FLEX";
	tiedOrderQuery.filteredOnly = true;
	std::vector<pdw::archive::HistoryRow> tiedOrderRows;
	int tiedOrderTotal = 0;
	Expect(archive.QueryHistory(tiedOrderQuery, tiedOrderRows, tiedOrderTotal, error) &&
		tiedOrderTotal == 2 && tiedOrderRows.size() == 2 &&
		tiedOrderRows[0].event.id == "export-order-b" &&
		tiedOrderRows[1].event.id == "export-order-a",
		"interactive history and CSV export use the same stable tie ordering");

	pdw::publishing::PublishEvent excluded(edge);
	excluded.id = "export-excluded-protocol";
	excluded.mode = "POCSAG-1200";
	excluded.message = "bulk export wrong protocol";
	Expect(archive.StoreEvent(excluded, true, error), "protocol-excluded CSV event stores");
	excluded.id = "export-excluded-filtered";
	excluded.mode = "FLEX-A";
	excluded.filtered = false;
	excluded.message = "bulk export unfiltered";
	Expect(archive.StoreEvent(excluded, true, error), "filtered-excluded CSV event stores");
	excluded.id = "export-excluded-search";
	excluded.filtered = true;
	excluded.message = "unrelated message";
	Expect(archive.StoreEvent(excluded, true, error), "search-excluded CSV event stores");

	pdw::archive::HistoryQuery exportQuery;
	exportQuery.search = "bulk export";
	exportQuery.protocol = "FLEX";
	exportQuery.filteredOnly = true;
	// Paging belongs only to the interactive history list. Export must ignore it.
	exportQuery.limit = 1;
	exportQuery.offset = 500;
	std::ostringstream exportOutput;
	int exported = -1;
	Expect(pdw::archive::ExportHistoryCsv(databasePath, exportQuery,
		exportOutput, exported, error), "filtered CSV history export succeeds");
	Expect(exported == 504, "CSV history export includes every matching row beyond one page");

	std::istringstream exportedCsv(exportOutput.str());
	Expect(pdw::archive::ReadCsvRecord(exportedCsv, csvRecord) ==
		pdw::archive::CSV_RECORD_COMPLETE, "CSV history header reads");
	Expect(csvRecord.size() >= 3 &&
		static_cast<unsigned char>(csvRecord[0]) == 0xEF &&
		static_cast<unsigned char>(csvRecord[1]) == 0xBB &&
		static_cast<unsigned char>(csvRecord[2]) == 0xBF,
		"CSV history export includes a UTF-8 BOM");
	csvRecord.erase(0, 3);
	Expect(pdw::archive::ParseCsvLine(csvRecord, fields) && fields.size() == 8 &&
		fields[0] == "Received" && fields[1] == "Protocol" &&
		fields[2] == "Capcode" && fields[3] == "Name" &&
		fields[4] == "Agency" && fields[5] == "Type" &&
		fields[6] == "Message" && fields[7] == "Filter",
		"CSV history export has the eight visible headings in display order");

	int parsedRows = 0;
	bool foundEdge = false;
	while (pdw::archive::ReadCsvRecord(exportedCsv, csvRecord) ==
		pdw::archive::CSV_RECORD_COMPLETE)
	{
		Expect(pdw::archive::ParseCsvLine(csvRecord, fields) && fields.size() == 8,
			"CSV history data row parses with eight complete values");
		if (parsedRows == 0)
			Expect(fields[6] == "bulk export order B" && fields[7] == "'\tplain filter",
				"CSV history uses a stable tie-breaker and protects leading controls");
		else if (parsedRows == 1)
			Expect(fields[6] == "bulk export order A" && fields[7] == "'\tplain filter",
				"CSV history stable tie order is deterministic");
		Expect(fields[3] == "' \t=Formula Name",
			"CSV history neutralizes formulas after leading whitespace in alias names");
		Expect(fields[4] == exportAlias.agency,
			"CSV history preserves quoted UTF-8 alias values");
		if (fields[6].find("bulk export, quoted") != std::string::npos)
		{
			foundEdge = true;
			Expect(fields[5] == "' +ALPHA" &&
				fields[6] == "' \t=\"bulk export, quoted\"\r\nsecond line" &&
				fields[7] == "'\t@filter",
				"CSV history quotes multiline values and neutralizes untrusted formulas");
		}
		++parsedRows;
	}
	Expect(parsedRows == exported && foundEdge,
		"CSV history row count and multiline edge row round trip completely");

	pdw::publishing::PublishEvent embeddedNul(edge);
	embeddedNul.id = "export-embedded-nul";
	embeddedNul.message = "ordinary";
	embeddedNul.message.push_back('\0');
	embeddedNul.message += "hidden";
	embeddedNul.filterLabel = "embedded NUL export sentinel";
	Expect(archive.StoreEvent(embeddedNul, true, error),
		"embedded-NUL CSV safety fixture stores");
	pdw::archive::HistoryQuery embeddedNulQuery;
	embeddedNulQuery.search = "embedded NUL export sentinel";
	std::ostringstream embeddedNulOutput;
	exported = -1;
	Expect(!pdw::archive::ExportHistoryCsv(databasePath, embeddedNulQuery,
		embeddedNulOutput, exported, error) && exported == 0 &&
		error.find("embedded NUL") != std::string::npos,
		"CSV history export rejects embedded NUL bytes instead of writing ambiguous data");

	FailingStreamBuffer failingBuffer;
	std::ostream failingOutput(&failingBuffer);
	exported = -1;
	Expect(!pdw::archive::ExportHistoryCsv(databasePath, exportQuery,
		failingOutput, exported, error) && exported == 0 && !error.empty(),
		"CSV history export fails as a whole when its output stream fails");

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
