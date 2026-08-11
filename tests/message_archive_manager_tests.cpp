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
	const bool aliasInserted = MessageArchiveUpsertCapcode(alias, error);
	if (!aliasInserted) std::cerr << "archive manager error: " << error << '\n';
	Expect(aliasInserted, "capcode cache source inserted");

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
	std::strcpy(generated.capcode, "1708068");
	std::strcpy(generated.label, "SAAS Unit Goolwa");
	std::strcpy(generated.text, "SAAS Unit Goolwa");
	generated.label_enabled = 1;
	FILTERLIST legacyFilters;
	legacyFilters.push_back(generated);
	pdw::archive::CapcodeEntry healthyDirectory;
	healthyDirectory.protocol = "POCSAG";
	healthyDirectory.address = "1708068";
	healthyDirectory.displayName = "Healthy Directory Name";
	healthyDirectory.agency = "SAAS";
	healthyDirectory.filterType = POCSAG_FILTER;
	Expect(MessageArchiveUpsertCapcode(healthyDirectory, error) &&
		MessageArchiveMergeLegacyFilters(legacyFilters, error),
		"legacy migration merges with an existing healthy directory");
	std::vector<pdw::archive::CapcodeEntry> mergedRows;
	Expect(MessageArchiveListCapcodes("1708068", mergedRows, error) && mergedRows.size() == 1 &&
		mergedRows[0].displayName == "Healthy Directory Name" && mergedRows[0].agency == "SAAS" &&
		mergedRows[0].matchText.empty() && mergedRows[0].filterLabel == "SAAS Unit Goolwa",
		"merge removes duplicate rules while preserving directory metadata and legacy filter behavior");
	Expect(MessageArchiveReplaceLegacyFilters(legacyFilters, error),
		"legacy filters migrate transactionally into the directory");
	Expect(Profile.filters.size() == 1 && Profile.filters[0].directory_id > 0 &&
		Profile.filters[0].type == POCSAG_FILTER &&
		std::string(Profile.filters[0].capcode) == "1708068" &&
		std::string(Profile.filters[0].label) == "SAAS Unit Goolwa" &&
		std::string(Profile.filters[0].text).empty(),
		"generator label duplication is repaired without losing the capcode label");
	Profile.filters[0].hitcounter = 9;
	std::strcpy(Profile.filters[0].lasthit_date, "11-08-26");
	std::strcpy(Profile.filters[0].lasthit_time, "20:30:00");
	Expect(MessageArchivePersistRuntimeFilterState(error),
		"runtime hit state persists to the directory database");
	std::string directoryCsv;
	Expect(MessageArchiveExportCapcodesCsv(directoryCsv, error) &&
		directoryCsv.find("SAAS Unit Goolwa") != std::string::npos,
		"configuration backup can export the expanded directory CSV");
	int rejected = -1;
	Expect(MessageArchiveReplaceCapcodesCsv(directoryCsv, rejected, error) && rejected == 0 &&
		Profile.filters.size() == 1 && Profile.filters[0].hitcounter == 9,
		"configuration restore replaces and reloads directory rules immediately");

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
