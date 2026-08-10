#ifndef PDW_MESSAGE_ARCHIVE_MANAGER_H
#define PDW_MESSAGE_ARCHIVE_MANAGER_H

#include <ostream>
#include <string>
#include <vector>

#include "../utils/message_archive.h"

void MessageArchiveManagerInitialize(void);
void MessageArchiveManagerShutdown(void);
void MessageArchiveSettingsChanged(void);
void MessageArchivePublishEvent(const pdw::publishing::PublishEvent& event);
bool MessageArchiveAnnotateEvent(pdw::publishing::PublishEvent& event);

bool MessageArchiveListCapcodes(const std::string& search,
	std::vector<pdw::archive::CapcodeEntry>& entries, std::string& error);
bool MessageArchiveUpsertCapcode(const pdw::archive::CapcodeEntry& entry,
	std::string& error);
bool MessageArchiveDeleteCapcode(const std::string& protocol,
	const std::string& address, std::string& error);
bool MessageArchiveQueryHistory(const pdw::archive::HistoryQuery& query,
	std::vector<pdw::archive::HistoryRow>& rows, int& total, std::string& error);
bool MessageArchiveExportHistoryCsv(const pdw::archive::HistoryQuery& query,
	std::ostream& output, int& exported, std::string& error);
bool MessageArchivePurgeHistory(unsigned int retentionDays, int& removed,
	std::string& error);

std::string MessageArchiveBuildMessagesJson(int limit, int offset,
	const std::string& search, const std::string& protocol, bool filteredOnly);
std::string MessageArchiveBuildCapcodesJson(const std::string& search);
std::string MessageArchiveStatusText(void);

#ifdef PDW_MESSAGE_ARCHIVE_MANAGER_TEST_HOOKS
enum MessageArchiveManagerTestHookStage
{
	MESSAGE_ARCHIVE_TEST_BEFORE_OPERATION_LOCK = 1,
	MESSAGE_ARCHIVE_TEST_AFTER_ARCHIVE_OPEN = 2
};

typedef void (*MessageArchiveManagerTestHook)(MessageArchiveManagerTestHookStage stage,
	const char* resolvedPath, void* context);
void MessageArchiveManagerSetTestHook(MessageArchiveManagerTestHook hook, void* context);
#endif

#endif
