#ifndef PDW_MESSAGE_ARCHIVE_H
#define PDW_MESSAGE_ARCHIVE_H

#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>

#include <istream>
#include <ostream>
#include <string>
#include <vector>

#include "publishing_core.h"

struct sqlite3;

namespace pdw
{
namespace archive
{

struct CapcodeEntry
{
	long long id;
	std::string protocol;
	std::string address;
	std::string displayName;
	std::string agency;
	std::string notes;
	std::string matchText;
	std::string filterLabel;
	std::string separateFile1;
	std::string separateFile2;
	std::string separateFile3;
	unsigned long color;
	int filterType;
	int waveNumber;
	int labelColor;
	unsigned int hitCounter;
	std::string lastHitDate;
	std::string lastHitTime;
	bool enabled;
	bool reject;
	bool matchExactMessage;
	bool showFilterLabel;
	bool commandEnabled;
	bool monitorOnly;
	bool emailEnabled;
	bool separateFileEnabled;

	CapcodeEntry() : id(0), color(RGB(0, 102, 204)), filterType(0),
		waveNumber(0), labelColor(0), hitCounter(0), enabled(true), reject(false),
		matchExactMessage(false), showFilterLabel(true), commandEnabled(false),
		monitorOnly(false), emailEnabled(false), separateFileEnabled(false) {}
};

struct HistoryQuery
{
	std::string search;
	std::string protocol;
	bool filteredOnly;
	int limit;
	int offset;

	HistoryQuery() : filteredOnly(false), limit(200), offset(0) {}
};

struct HistoryRow
{
	pdw::publishing::PublishEvent event;
	std::string displayName;
	std::string agency;
	unsigned long color;

	HistoryRow() : color(RGB(0, 102, 204)) {}
};

bool IsValidCapcode(const std::string& address);
bool IsValidProtocolName(const std::string& protocol);
std::string CsvEscape(const std::string& value);
bool ParseCsvLine(const std::string& line, std::vector<std::string>& fields);
enum CsvRecordReadResult
{
	CSV_RECORD_END = 0,
	CSV_RECORD_COMPLETE = 1,
	CSV_RECORD_MALFORMED = 2
};
CsvRecordReadResult ReadCsvRecord(std::istream& input, std::string& record);
bool WriteCapcodeDirectoryCsv(const std::vector<CapcodeEntry>& entries,
	std::ostream& output, std::string& error);
bool ReadCapcodeDirectoryCsv(std::istream& input,
	std::vector<CapcodeEntry>& entries, int& rejected, std::string& error);
bool ExportHistoryCsv(const std::string& utf8Path, const HistoryQuery& query,
	std::ostream& output, int& exported, std::string& error);

class MessageArchive
{
public:
	MessageArchive();
	~MessageArchive();

	bool Open(const std::string& utf8Path, std::string& error);
	void Close();
	bool IsOpen() const;

	bool StoreEvent(const pdw::publishing::PublishEvent& event,
		bool includeMessage, std::string& error);
	bool UpsertCapcode(const CapcodeEntry& entry, std::string& error);
	bool ReplaceCapcodes(const std::vector<CapcodeEntry>& entries, std::string& error);
	bool UpdateCapcodeRuntimeState(long long id, unsigned int hitCounter,
		const std::string& lastHitDate, const std::string& lastHitTime,
		std::string& error);
	bool DeleteCapcode(long long id, std::string& error);
	bool DeleteCapcode(const std::string& protocol, const std::string& address,
		std::string& error);
	bool LookupCapcode(const std::string& mode, const std::string& address,
		CapcodeEntry& entry, std::string& error);
	bool ListCapcodes(const std::string& search, std::vector<CapcodeEntry>& entries,
		std::string& error);
	bool QueryHistory(const HistoryQuery& query, std::vector<HistoryRow>& rows,
		int& total, std::string& error);
	bool PurgeHistory(unsigned int retentionDays, int& removed, std::string& error);

private:
	MessageArchive(const MessageArchive&);
	MessageArchive& operator=(const MessageArchive&);

	bool Execute(const std::string& sql, std::string& error);
	bool LookupCapcodeUnlocked(const std::string& mode, const std::string& address,
		CapcodeEntry& entry, std::string& error);

	mutable CRITICAL_SECTION lock_;
	sqlite3* database_;
	std::string path_;
};

} // namespace archive
} // namespace pdw

#endif
