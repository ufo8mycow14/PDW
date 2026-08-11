#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "headers\message_archive_manager.h"
#include "headers\pdw.h"

#ifndef PDW_MESSAGE_ARCHIVE_MANAGER_TEST_HOOKS
#error PDWMessageArchiveManagerTests must enable the archive-manager test hooks
#endif

PROFILE Profile = {};
TCHAR szPath[MAX_PATH] = {};

namespace pdw
{
namespace events
{
	std::string PdwTextToUtf8(const char* value)
	{
		return value ? value : std::string();
	}
}
}

namespace
{
	struct ListThreadContext
	{
		bool changePath;
		const char* configuredPath;
		bool success;
		std::string error;
		std::vector<pdw::archive::CapcodeEntry> entries;
		HANDLE done;

		ListThreadContext() : changePath(false), configuredPath(NULL), success(false), done(NULL) {}
	};

	struct ArchiveOperationHookContext
	{
		std::string firstPath;
		std::string secondPath;
		HANDLE firstOpened;
		HANDLE releaseFirst;
		HANDLE secondWaiting;
		volatile LONG waitTimedOut;

		ArchiveOperationHookContext() : firstOpened(NULL), releaseFirst(NULL),
			secondWaiting(NULL), waitTimedOut(0) {}
	};

	void Expect(bool condition, const char* message)
	{
		if (condition) return;
		std::cerr << "FAILED: " << message << '\n';
		std::exit(1);
	}

	std::string TemporaryFolder()
	{
		char root[MAX_PATH] = {};
		char placeholder[MAX_PATH] = {};
		if (!GetTempPathA(sizeof(root), root) ||
			!GetTempFileNameA(root, "PAM", 0, placeholder)) return std::string();
		DeleteFileA(placeholder);
		if (!CreateDirectoryA(placeholder, NULL)) return std::string();
		return placeholder;
	}

	bool ContainsDisplayName(const std::vector<pdw::archive::CapcodeEntry>& entries,
		const std::string& displayName)
	{
		for (std::vector<pdw::archive::CapcodeEntry>::const_iterator entry = entries.begin();
			entry != entries.end(); ++entry)
		{
			if (entry->displayName == displayName) return true;
		}
		return false;
	}

	void ArchiveOperationHook(MessageArchiveManagerTestHookStage stage,
		const char* resolvedPath, void* rawContext)
	{
		ArchiveOperationHookContext* context =
			static_cast<ArchiveOperationHookContext*>(rawContext);
		const std::string path = resolvedPath ? resolvedPath : std::string();
		if (stage == MESSAGE_ARCHIVE_TEST_BEFORE_OPERATION_LOCK &&
			path == context->secondPath)
		{
			SetEvent(context->secondWaiting);
		}
		else if (stage == MESSAGE_ARCHIVE_TEST_AFTER_ARCHIVE_OPEN &&
			path == context->firstPath)
		{
			SetEvent(context->firstOpened);
			if (WaitForSingleObject(context->releaseFirst, 5000) != WAIT_OBJECT_0)
				InterlockedExchange(&context->waitTimedOut, 1);
		}
	}

	DWORD WINAPI ListCapcodesThread(LPVOID rawContext)
	{
		ListThreadContext* context = static_cast<ListThreadContext*>(rawContext);
		if (context->changePath)
		{
			std::strcpy(Profile.messageArchivePath, context->configuredPath);
			MessageArchiveSettingsChanged();
		}
		context->success = MessageArchiveListCapcodes(std::string(),
			context->entries, context->error);
		SetEvent(context->done);
		return 0;
	}
}

int main()
{
	const std::string folder = TemporaryFolder();
	Expect(!folder.empty(), "temporary archive-manager folder created");
	std::strncpy(szPath, folder.c_str(), sizeof(szPath) - 1);
	Profile.messageHistoryEnabled = 1;
	Profile.messageHistoryIncludeMessage = 0;
	Profile.messageHistoryRetentionDays = 30;
	std::strcpy(Profile.messageArchivePath, "manager-test.sqlite3");
	Profile.liveDashboardEnabled = 0;
	Profile.liveDashboardPort = 8090;

	MessageArchiveManagerInitialize();
	std::string error;
	pdw::archive::CapcodeEntry alias;
	alias.protocol = "POCSAG";
	alias.address = "1234567";
	alias.displayName = "Local Station";
	alias.agency = "Local Test";
	alias.filterEnabled = false;
	alias.outputRoutes = PDW_OUTPUT_ROUTE_MQTT | PDW_OUTPUT_ROUTE_WINDOWS;
	alias.agencyLabelPosition = PDW_AGENCY_LABEL_BEFORE;
	const bool aliasInserted = MessageArchiveUpsertCapcode(alias, error);
	if (!aliasInserted) std::cerr << "archive manager error: " << error << '\n';
	Expect(aliasInserted, "capcode cache source inserted");
	Expect(MessageArchiveReloadRuntimeFilters(error) && Profile.filters.size() == 1 &&
		Profile.filters[0].filter_to_pane == 0 &&
		Profile.filters[0].output_routing_configured == 1 &&
		Profile.filters[0].output_routes == alias.outputRoutes &&
		std::string(Profile.filters[0].label) == "Local Test - Local Station",
		"directory rule keeps lower-pane filtering separate from selected enabled outputs");
	pdw::archive::CapcodeEntry monitorAlias;
	monitorAlias.protocol = "POCSAG";
	monitorAlias.address = "7654321";
	monitorAlias.displayName = "Monitor Command Rule";
	monitorAlias.filterEnabled = false;
	monitorAlias.outputRoutes = 0;
	monitorAlias.monitorOnly = true;
	Expect(MessageArchiveUpsertCapcode(monitorAlias, error) &&
		MessageArchiveReloadRuntimeFilters(error) && Profile.filters.size() == 2,
		"an active rule without Filter or outputs still reaches monitor and shared command handling");
	bool monitorRuleLoaded = false;
	for (FILTERLIST::const_iterator filter = Profile.filters.begin();
		filter != Profile.filters.end(); ++filter)
	{
		if (std::string(filter->capcode) == "7654321")
			monitorRuleLoaded = filter->monitor_only != 0 && filter->filter_to_pane == 0;
	}
	Expect(monitorRuleLoaded, "Monitor only remains active without lower-pane or output routing");
	pdw::archive::CapcodeEntry specificAlias(alias);
	specificAlias.displayName = "Zulu Traffic Rule";
	specificAlias.filterType = POCSAG_FILTER;
	specificAlias.matchText = "Traffic";
	specificAlias.filterEnabled = false;
	specificAlias.outputRoutes = PDW_OUTPUT_ROUTE_EMAIL;
	Expect(MessageArchiveUpsertCapcode(specificAlias, error) &&
		MessageArchiveReloadRuntimeFilters(error) && Profile.filters.size() == 3,
		"a keyword-specific rule can coexist with a broad rule for the same capcode");
	int broadRuleIndex = -1;
	int specificRuleIndex = -1;
	for (std::size_t index = 0; index < Profile.filters.size(); ++index)
	{
		if (std::string(Profile.filters[index].capcode) != "1234567") continue;
		if (std::string(Profile.filters[index].text) == "Traffic")
			specificRuleIndex = static_cast<int>(index);
		else if (Profile.filters[index].text[0] == '\0')
			broadRuleIndex = static_cast<int>(index);
	}
	Expect(specificRuleIndex >= 0 && broadRuleIndex >= 0 && specificRuleIndex < broadRuleIndex,
		"keyword-specific output routing is evaluated before a broad capcode-only rule");
	pdw::archive::CapcodeEntry emptyExactAlias(alias);
	emptyExactAlias.address = "2345678";
	emptyExactAlias.displayName = "A Empty Exact Rule";
	emptyExactAlias.matchText.clear();
	emptyExactAlias.matchExactMessage = true;
	pdw::archive::CapcodeEntry emptyExactSpecific(emptyExactAlias);
	emptyExactSpecific.displayName = "Zulu Empty Exact Keyword Rule";
	emptyExactSpecific.matchText = "Traffic";
	emptyExactSpecific.matchExactMessage = false;
	Expect(MessageArchiveUpsertCapcode(emptyExactAlias, error) &&
		MessageArchiveUpsertCapcode(emptyExactSpecific, error) &&
		MessageArchiveReloadRuntimeFilters(error),
		"a legacy empty exact-match flag remains loadable without gaining text specificity");
	int emptyExactRuleIndex = -1;
	specificRuleIndex = -1;
	for (std::size_t index = 0; index < Profile.filters.size(); ++index)
	{
		if (std::string(Profile.filters[index].capcode) != "2345678") continue;
		if (std::string(Profile.filters[index].text) == "Traffic")
			specificRuleIndex = static_cast<int>(index);
		else if (Profile.filters[index].match_exact_msg != 0)
			emptyExactRuleIndex = static_cast<int>(index);
	}
	Expect(specificRuleIndex >= 0 && emptyExactRuleIndex >= 0 &&
		specificRuleIndex < emptyExactRuleIndex,
		"an empty exact-match flag cannot shadow a keyword-specific rule");
	pdw::archive::CapcodeEntry fnuAlias(alias);
	fnuAlias.protocol.clear();
	fnuAlias.address = "1234567-1";
	fnuAlias.displayName = "Any Protocol FNU Rule";
	fnuAlias.outputRoutes = PDW_OUTPUT_ROUTE_EMAIL;
	Expect(MessageArchiveUpsertCapcode(fnuAlias, error) &&
		MessageArchiveReloadRuntimeFilters(error),
		"an Any-protocol POCSAG function-number rule expands into runtime filters");
	int fnuRuleIndex = -1;
	broadRuleIndex = -1;
	for (std::size_t index = 0; index < Profile.filters.size(); ++index)
	{
		if (Profile.filters[index].type != POCSAG_FILTER) continue;
		const std::string address = Profile.filters[index].capcode;
		if (address == "1234567-1") fnuRuleIndex = static_cast<int>(index);
		else if (address == "1234567" && Profile.filters[index].text[0] == '\0')
			broadRuleIndex = static_cast<int>(index);
	}
	Expect(fnuRuleIndex >= 0 && broadRuleIndex >= 0 && fnuRuleIndex < broadRuleIndex,
		"a POCSAG function-number rule is evaluated before the broad capcode rule");
	pdw::archive::CapcodeEntry dormantAlias;
	dormantAlias.protocol = "POCSAG";
	dormantAlias.address = "9990001";
	dormantAlias.displayName = "Dormant Reset Rule";
	dormantAlias.enabled = false;
	dormantAlias.filterEnabled = false;
	dormantAlias.reject = true;
	dormantAlias.hitCounter = 8;
	Expect(MessageArchiveUpsertCapcode(dormantAlias, error),
		"dormant reset fixture is stored without entering the runtime matcher");
	std::vector<pdw::archive::CapcodeEntry> dormantRows;
	Expect(MessageArchiveListCapcodes("Dormant Reset Rule", dormantRows, error) &&
		dormantRows.size() == 1 &&
		MessageArchiveResetCapcodeHitCounter(dormantRows[0].id, error),
		"hit counter reset uses the runtime-state-only path");
	dormantRows.clear();
	Expect(MessageArchiveListCapcodes("Dormant Reset Rule", dormantRows, error) &&
		dormantRows.size() == 1 && !dormantRows[0].enabled && dormantRows[0].reject &&
		dormantRows[0].hitCounter == 0,
		"resetting a dormant row cannot reactivate or rewrite its preserved actions");
	const std::string capcodesJson = MessageArchiveBuildCapcodesJson(std::string());
	Expect(capcodesJson.find("\"filter_enabled\":false") != std::string::npos &&
		capcodesJson.find("\"output_routing_configured\":true") != std::string::npos &&
		capcodesJson.find("\"output_routes\":") != std::string::npos,
		"capcode API exposes the new filter and output-routing state additively");

	pdw::publishing::PublishEvent event;
	event.id = "queued-event-1";
	event.timestamp = "2026-08-10T12:00:00.000Z";
	event.source = "PDW test";
	event.address = "1234567";
	event.mode = "POCSAG-1200";
	event.messageType = "ALPHA";
	event.message = "must remain out of default history";
	Expect(MessageArchiveAnnotateEvent(event) && event.addressName == "Local Station",
		"decoded-thread alias lookup uses the refreshed in-memory cache");
	MessageArchivePublishEvent(event);

	pdw::archive::HistoryQuery query;
	query.limit = 10;
	std::vector<pdw::archive::HistoryRow> rows;
	int total = 0;
	const ULONGLONG deadline = GetTickCount64() + 3000;
	do
	{
		rows.clear();
		total = 0;
		Expect(MessageArchiveQueryHistory(query, rows, total, error),
			"history remains queryable while the background writer runs");
		if (!rows.empty()) break;
		Sleep(10);
	} while (GetTickCount64() < deadline);
	Expect(rows.size() == 1 && total == 1, "bounded background queue stored the event");
	Expect(rows[0].event.message.empty(), "message text remains excluded by default");
	const std::string historyJson = MessageArchiveBuildMessagesJson(10, 0,
		std::string(), std::string(), false);
	Expect(historyJson.find("\"address_name\":\"Local Station\"") != std::string::npos,
		"history JSON includes local alias metadata");
	Expect(historyJson.find("\"filter_matched\"") == std::string::npos &&
		historyJson.find("\"monitor_only\"") == std::string::npos &&
		historyJson.find("\"group_call\"") == std::string::npos,
		"history JSON does not invent live-only event metadata");

	std::strcpy(Profile.messageArchivePath, "manager-other.sqlite3");
	MessageArchiveSettingsChanged();
	alias.displayName = "Other Station";
	Expect(MessageArchiveUpsertCapcode(alias, error),
		"second archive capcode source inserted");
	std::strcpy(Profile.messageArchivePath, "manager-test.sqlite3");
	MessageArchiveSettingsChanged();

	ArchiveOperationHookContext hook;
	hook.firstPath = folder + "\\manager-test.sqlite3";
	hook.secondPath = folder + "\\manager-other.sqlite3";
	hook.firstOpened = CreateEvent(NULL, TRUE, FALSE, NULL);
	hook.releaseFirst = CreateEvent(NULL, TRUE, FALSE, NULL);
	hook.secondWaiting = CreateEvent(NULL, TRUE, FALSE, NULL);
	Expect(hook.firstOpened && hook.releaseFirst && hook.secondWaiting,
		"archive-operation synchronization events created");
	MessageArchiveManagerSetTestHook(ArchiveOperationHook, &hook);

	ListThreadContext firstList;
	firstList.done = CreateEvent(NULL, TRUE, FALSE, NULL);
	Expect(firstList.done != NULL, "first archive-operation completion event created");
	HANDLE firstThread = CreateThread(NULL, 0, ListCapcodesThread, &firstList, 0, NULL);
	Expect(firstThread != NULL, "first archive-operation thread started");
	Expect(WaitForSingleObject(hook.firstOpened, 3000) == WAIT_OBJECT_0,
		"first operation paused after opening its configured archive");

	ListThreadContext secondList;
	secondList.changePath = true;
	secondList.configuredPath = "manager-other.sqlite3";
	secondList.done = CreateEvent(NULL, TRUE, FALSE, NULL);
	Expect(secondList.done != NULL, "second archive-operation completion event created");
	HANDLE secondThread = CreateThread(NULL, 0, ListCapcodesThread, &secondList, 0, NULL);
	Expect(secondThread != NULL, "second archive-operation thread started");
	Expect(WaitForSingleObject(hook.secondWaiting, 3000) == WAIT_OBJECT_0,
		"path-change operation reached the archive-operation lock");
	Expect(WaitForSingleObject(secondList.done, 100) == WAIT_TIMEOUT,
		"path-change operation cannot cross an in-flight archive operation");

	SetEvent(hook.releaseFirst);
	Expect(WaitForSingleObject(firstList.done, 3000) == WAIT_OBJECT_0 &&
		WaitForSingleObject(secondList.done, 3000) == WAIT_OBJECT_0,
		"serialized archive operations complete after the first is released");
	WaitForSingleObject(firstThread, INFINITE);
	WaitForSingleObject(secondThread, INFINITE);
	MessageArchiveManagerSetTestHook(NULL, NULL);
	Expect(InterlockedCompareExchange(&hook.waitTimedOut, 0, 0) == 0,
		"test hook release did not time out");
	Expect(firstList.success && ContainsDisplayName(firstList.entries, "Local Station") &&
		!ContainsDisplayName(firstList.entries, "Other Station"),
		"first operation remains bound to its configured archive");
	Expect(secondList.success && ContainsDisplayName(secondList.entries, "Other Station") &&
		!ContainsDisplayName(secondList.entries, "Local Station"),
		"path-change operation remains bound to the second archive");
	pdw::publishing::PublishEvent secondAliasEvent;
	secondAliasEvent.address = "1234567";
	secondAliasEvent.mode = "POCSAG-1200";
	Expect(MessageArchiveAnnotateEvent(secondAliasEvent) &&
		secondAliasEvent.addressName == "Other Station",
		"late first-path cache refresh cannot replace the current path cache");

	CloseHandle(firstThread);
	CloseHandle(secondThread);
	CloseHandle(firstList.done);
	CloseHandle(secondList.done);
	CloseHandle(hook.firstOpened);
	CloseHandle(hook.releaseFirst);
	CloseHandle(hook.secondWaiting);

	FILTER generated = {};
	generated.type = POCSAG_FILTER;
	std::strcpy(generated.capcode, "1110001");
	std::strcpy(generated.label, "Synthetic Legacy Unit");
	std::strcpy(generated.text, "Synthetic Legacy Unit");
	generated.label_enabled = 1;
	generated.monitor_only = 1;
	generated.smtp = 1;
	FILTERLIST legacyFilters;
	legacyFilters.push_back(generated);
	pdw::archive::CapcodeEntry healthyDirectory;
	healthyDirectory.protocol = "POCSAG";
	healthyDirectory.address = "1110001";
	healthyDirectory.displayName = "Healthy Directory Name";
	healthyDirectory.agency = "Synthetic Agency";
	healthyDirectory.filterType = POCSAG_FILTER;
	Expect(MessageArchiveUpsertCapcode(healthyDirectory, error) &&
		MessageArchiveMergeLegacyFilters(legacyFilters, error),
		"legacy migration merges with an existing healthy directory");
	std::vector<pdw::archive::CapcodeEntry> mergedRows;
	Expect(MessageArchiveListCapcodes("1110001", mergedRows, error) && mergedRows.size() == 1 &&
		mergedRows[0].displayName == "Healthy Directory Name" &&
		mergedRows[0].agency == "Synthetic Agency" &&
		mergedRows[0].matchText.empty() && mergedRows[0].filterLabel == "Healthy Directory Name",
		"merge removes duplicate rules while preserving directory metadata and legacy filter behavior");
	Expect(MessageArchiveReplaceLegacyFilters(legacyFilters, error),
		"legacy filters migrate transactionally into the directory");
	Expect(Profile.filters.size() == 1 && Profile.filters[0].directory_id > 0 &&
		Profile.filters[0].type == POCSAG_FILTER &&
		std::string(Profile.filters[0].capcode) == "1110001" &&
		std::string(Profile.filters[0].label) == "Synthetic Legacy Unit" &&
		std::string(Profile.filters[0].text).empty() &&
		Profile.filters[0].filter_to_pane == 0 &&
		Profile.filters[0].output_routing_configured == 0 &&
		Profile.filters[0].smtp == 1,
		"generator label duplication is repaired without losing the capcode label");
	Profile.filters[0].hitcounter = 9;
	std::strcpy(Profile.filters[0].lasthit_date, "11-08-26");
	std::strcpy(Profile.filters[0].lasthit_time, "20:30:00");
	Expect(MessageArchivePersistRuntimeFilterState(error),
		"runtime hit state persists to the directory database");
	std::string directoryCsv;
	Expect(MessageArchiveExportCapcodesCsv(directoryCsv, error) &&
		directoryCsv.find("Synthetic Legacy Unit") != std::string::npos,
		"configuration backup can export the expanded directory CSV");
	int rejected = -1;
	Expect(MessageArchiveReplaceCapcodesCsv(directoryCsv, rejected, error) && rejected == 0 &&
		Profile.filters.size() == 1 && Profile.filters[0].hitcounter == 9 &&
		Profile.filters[0].output_routing_configured == 0 && Profile.filters[0].smtp == 1,
		"configuration restore preserves legacy routing while reloading directory rules immediately");

	MessageArchiveManagerShutdown();
	const std::string database = folder + "\\manager-test.sqlite3";
	const std::string otherDatabase = folder + "\\manager-other.sqlite3";
	DeleteFileA((database + "-wal").c_str());
	DeleteFileA((database + "-shm").c_str());
	DeleteFileA(database.c_str());
	DeleteFileA((otherDatabase + "-wal").c_str());
	DeleteFileA((otherDatabase + "-shm").c_str());
	DeleteFileA(otherDatabase.c_str());
	RemoveDirectoryA(folder.c_str());
	std::cout << "Message archive manager tests passed\n";
	return 0;
}
