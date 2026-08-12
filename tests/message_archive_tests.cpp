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

	const pdw::archive::CapcodeEntry* FindCapcode(
		const std::vector<pdw::archive::CapcodeEntry>& entries,
		const std::string& address)
	{
		for (std::vector<pdw::archive::CapcodeEntry>::const_iterator entry =
			entries.begin(); entry != entries.end(); ++entry)
			if (entry->address == address) return &*entry;
		return NULL;
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
		"INSERT INTO capcode_directory VALUES('POCSAG','1110001','Synthetic Legacy Unit','Synthetic Agency',13369344,'legacy mapping',1,'2026-08-11');"
		"INSERT INTO capcode_directory VALUES('POCSAG','1110002','Dormant Legacy Unit','Synthetic Agency',13369344,'disabled legacy mapping',0,'2026-08-11');",
		NULL, NULL, NULL) == SQLITE_OK, "version-one directory fixture is populated");
	sqlite3_close(versionOne);
	pdw::archive::MessageArchive migratedArchive;
	std::string migrationError;
	Expect(migratedArchive.Open(databasePath, migrationError),
		"version-one Capcode Directory migrates in place");
	std::vector<pdw::archive::CapcodeEntry> migratedRows;
	Expect(migratedArchive.ListCapcodes(std::string(), migratedRows, migrationError) &&
		migratedRows.size() == 2, "both active and dormant legacy rows migrate");
	const pdw::archive::CapcodeEntry* migratedActive = NULL;
	const pdw::archive::CapcodeEntry* migratedDormant = NULL;
	for (std::vector<pdw::archive::CapcodeEntry>::const_iterator row = migratedRows.begin();
		row != migratedRows.end(); ++row)
	{
		if (row->address == "1110001") migratedActive = &*row;
		if (row->address == "1110002") migratedDormant = &*row;
	}
	Expect(migratedActive && migratedActive->displayName == "Synthetic Legacy Unit" &&
		migratedActive->filterLabel == "Synthetic Legacy Unit" && migratedActive->matchText.empty() &&
		migratedActive->filterEnabled && !migratedActive->outputRoutingConfigured &&
		migratedActive->outputRoutes == 0,
		"existing directory mappings become capcode-only filter rules without a false text requirement");
	Expect(migratedDormant && !migratedDormant->enabled && !migratedDormant->filterEnabled &&
		!migratedDormant->outputRoutingConfigured && migratedDormant->outputRoutes == 0,
		"a disabled legacy directory row stays dormant after migration");
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
	capcode.filterLabel = "Legacy label that is replaced by Display name";
	capcode.reject = true;
	capcode.matchExactMessage = false;
	capcode.showFilterLabel = true;
	capcode.commandEnabled = true;
	capcode.monitorOnly = false;
	capcode.emailEnabled = true;
	capcode.filterEnabled = true;
	capcode.outputRoutes = PDW_OUTPUT_ROUTE_EMAIL | PDW_OUTPUT_ROUTE_MQTT |
		PDW_OUTPUT_ROUTE_WINDOWS;
	capcode.agencyLabelPosition = PDW_AGENCY_LABEL_AFTER;
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
	pdw::archive::CapcodeEntry conflictingKeywordMode(capcode);
	conflictingKeywordMode.id = 0;
	conflictingKeywordMode.matchExactMessage = true;
	Expect(!archive.UpsertCapcode(conflictingKeywordMode, error),
		"exact whole-message mode cannot silently reinterpret an all-keyword expression");
	pdw::archive::CapcodeEntry invalidKeywordMode(capcode);
	invalidKeywordMode.id = 0;
	invalidKeywordMode.matchText = "PR1++Traffic";
	Expect(!archive.UpsertCapcode(invalidKeywordMode, error),
		"an empty required-keyword term is rejected before the rule is saved");
	pdw::archive::CapcodeEntry conflictingPaneMode(capcode);
	conflictingPaneMode.id = 0;
	conflictingPaneMode.matchText = "Monitor";
	conflictingPaneMode.monitorOnly = true;
	conflictingPaneMode.filterEnabled = true;
	Expect(!archive.UpsertCapcode(conflictingPaneMode, error),
		"Filter and Monitor only cannot be persisted together");
	pdw::archive::CapcodeEntry found;
	Expect(archive.LookupCapcode("POCSAG-1200", "1234567", found, error),
		"protocol-specific alias matches decoded mode");
	Expect(found.displayName == "Station Alpha" && found.filterType == 2 &&
		found.matchText == "PR1+Traffic" && found.filterLabel == "Station Alpha" &&
		found.reject && !found.matchExactMessage && found.commandEnabled && !found.monitorOnly &&
		found.emailEnabled && found.separateFileEnabled && found.separateFile1 == "alpha.csv" &&
		found.separateFile2 == "backup.csv" && found.waveNumber == 4 && found.labelColor == 7 &&
		found.hitCounter == 12 && found.lastHitDate == "10-08-26" && found.lastHitTime == "12:34:56" &&
		found.filterEnabled && found.outputRoutingConfigured &&
		found.outputRoutes == capcode.outputRoutes && found.agencyLabelPosition == PDW_AGENCY_LABEL_AFTER,
		"directory matching, display, and output-routing fields round trip");
	Expect(archive.UpdateCapcodeRuntimeState(found.id, 13, "11-08-26", "01:02:03", error),
		"directory hit-counter state updates");

	pdw::archive::CapcodeEntry secondRule(capcode);
	secondRule.id = 0;
	secondRule.matchText = "Medical";
	secondRule.filterLabel = "Ignored legacy label";
	secondRule.reject = false;
	secondRule.matchExactMessage = true;
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
		parsedDirectory[0].filterType == 2 && !parsedDirectory[0].filterLabel.empty() &&
		parsedDirectory[0].outputRoutingConfigured,
		"expanded directory CSV round trips all rule columns");
	std::vector<pdw::archive::CapcodeEntry> importExisting(directoryRows);
	pdw::archive::CapcodeEntry unrelatedCapcode;
	unrelatedCapcode.id = 30;
	unrelatedCapcode.protocol = "FLEX";
	unrelatedCapcode.address = "8888888";
	unrelatedCapcode.displayName = "Unchanged";
	importExisting.push_back(unrelatedCapcode);
	std::vector<pdw::archive::CapcodeEntry> importRows;
	pdw::archive::CapcodeEntry update(capcode);
	update.id = 0;
	update.protocol = "FLEX";
	update.displayName = "First CSV value";
	importRows.push_back(update);
	update.displayName = "Final CSV value";
	importRows.push_back(update);
	pdw::archive::CapcodeEntry added(update);
	added.address = "9999999";
	added.displayName = "New first value";
	importRows.push_back(added);
	added.displayName = "New final value";
	importRows.push_back(added);
	std::vector<pdw::archive::CapcodeEntry> mergedImport;
	pdw::archive::CapcodeImportStats importStats;
	pdw::archive::MergeCapcodeDirectoryImport(importExisting, importRows,
		mergedImport, importStats);
	const pdw::archive::CapcodeEntry* updatedImport =
		FindCapcode(mergedImport, "1234567");
	const pdw::archive::CapcodeEntry* addedImport =
		FindCapcode(mergedImport, "9999999");
	Expect(mergedImport.size() == 3 && importStats.added == 1 &&
		importStats.updated == 1 && importStats.duplicatesDiscarded == 3,
		"CSV import collapses existing and in-file duplicate capcodes");
	Expect(updatedImport && updatedImport->displayName == "Final CSV value" &&
		updatedImport->protocol == "FLEX" && addedImport &&
		addedImport->displayName == "New final value" &&
		FindCapcode(mergedImport, "8888888") != NULL,
		"CSV import updates matching capcodes, keeps the final CSV row, and preserves unrelated entries");
	std::istringstream legacyDisabledDirectory(
		"protocol,address,display_name,agency,color,notes,enabled,filter_type,match_text,filter_label,"
		"reject,match_exact,show_label,command_enabled,monitor_only,email_enabled,separate_file_enabled,"
		"separate_file_1,separate_file_2,separate_file_3,wave_number,label_color,hit_counter,last_hit_date,last_hit_time\r\n"
		"POCSAG,7654321,,Dormant Agency,13369344,legacy disabled,0,2,,Dormant Label,1,0,1,0,1,1,1,dormant.csv,,,0,0,0,,\r\n");
	Expect(pdw::archive::ReadCapcodeDirectoryCsv(legacyDisabledDirectory, parsedDirectory,
		rejectedDirectoryRows, error) && rejectedDirectoryRows == 0 && parsedDirectory.size() == 1 &&
		!parsedDirectory[0].enabled && !parsedDirectory[0].filterEnabled &&
		!parsedDirectory[0].outputRoutingConfigured && parsedDirectory[0].emailEnabled &&
		parsedDirectory[0].outputRoutes == PDW_OUTPUT_ROUTE_EMAIL && parsedDirectory[0].reject &&
		parsedDirectory[0].monitorOnly && parsedDirectory[0].separateFileEnabled &&
		parsedDirectory[0].displayName == "Dormant Label",
		"legacy CSV keeps disabled actions dormant, preserves Email semantics, and rescues a blank Display name");
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
	exportAlias.address = "1110002";
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
		bulk.address = "1110002";
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
	edge.address = "1110002";
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
