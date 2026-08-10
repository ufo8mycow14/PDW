#include "headers\message_archive_manager.h"

#include <windows.h>

#include <algorithm>
#include <deque>
#include <sstream>

#include "headers\pdw.h"
#include "decoded_event.h"
#include "local_dashboard_server.h"

extern PROFILE Profile;
extern TCHAR szPath[MAX_PATH];

namespace
{
	const std::size_t MAX_PENDING_EVENTS = 1024;

#ifdef PDW_MESSAGE_ARCHIVE_MANAGER_TEST_HOOKS
	MessageArchiveManagerTestHook g_testHook = NULL;
	void* g_testHookContext = NULL;

	void InvokeTestHook(MessageArchiveManagerTestHookStage stage,
		const std::string& resolvedPath)
	{
		if (g_testHook) g_testHook(stage, resolvedPath.c_str(), g_testHookContext);
	}
#else
	const int MESSAGE_ARCHIVE_TEST_BEFORE_OPERATION_LOCK = 1;
	const int MESSAGE_ARCHIVE_TEST_AFTER_ARCHIVE_OPEN = 2;
	void InvokeTestHook(int, const std::string&) {}
#endif

	class ArchiveOperationLock
	{
	public:
		ArchiveOperationLock() { InitializeCriticalSection(&lock_); }
		~ArchiveOperationLock() { DeleteCriticalSection(&lock_); }
		void Enter() { EnterCriticalSection(&lock_); }
		void Leave() { LeaveCriticalSection(&lock_); }
	private:
		CRITICAL_SECTION lock_;
	} g_archiveOperationLock;

	class ArchiveOperationGuard
	{
	public:
		explicit ArchiveOperationGuard(const std::string& resolvedPath)
		{
			InvokeTestHook(MESSAGE_ARCHIVE_TEST_BEFORE_OPERATION_LOCK, resolvedPath);
			g_archiveOperationLock.Enter();
		}
		~ArchiveOperationGuard() { g_archiveOperationLock.Leave(); }
	private:
		ArchiveOperationGuard(const ArchiveOperationGuard&);
		ArchiveOperationGuard& operator=(const ArchiveOperationGuard&);
	};

	pdw::archive::MessageArchive g_archive;
	pdw::dashboard::LocalDashboardServer g_dashboard;

	class LockedStatus
	{
	public:
		LockedStatus() : text_("Message archive is not open.") { InitializeCriticalSection(&lock_); }
		~LockedStatus() { DeleteCriticalSection(&lock_); }
		void Set(const std::string& value)
		{
			EnterCriticalSection(&lock_); text_ = value; LeaveCriticalSection(&lock_);
		}
		std::string Get()
		{
			EnterCriticalSection(&lock_); const std::string value = text_; LeaveCriticalSection(&lock_); return value;
		}
	private:
		CRITICAL_SECTION lock_;
		std::string text_;
	} g_status;

	struct ArchiveConfig
	{
		bool historyEnabled;
		bool includeMessage;
		unsigned int retentionDays;
		std::string path;
		bool dashboardEnabled;
		unsigned short dashboardPort;

		ArchiveConfig() : historyEnabled(false), includeMessage(false), retentionDays(30),
			dashboardEnabled(false), dashboardPort(8090) {}
	};

	struct QueuedEvent
	{
		pdw::publishing::PublishEvent event;
		bool includeMessage;
		unsigned int retentionDays;
		std::string path;
	};

	class ArchiveState
	{
	public:
		ArchiveState() : workEvent(NULL), stopEvent(NULL), thread(NULL),
			acceptingEvents(false), droppedEvents(0)
		{
			InitializeCriticalSection(&lock);
		}
		~ArchiveState() { DeleteCriticalSection(&lock); }

		CRITICAL_SECTION lock;
		HANDLE workEvent;
		HANDLE stopEvent;
		HANDLE thread;
		std::deque<QueuedEvent> queue;
		std::vector<pdw::archive::CapcodeEntry> aliases;
		ArchiveConfig config;
		bool acceptingEvents;
		unsigned long droppedEvents;
	} g_state;

	ULONGLONG g_lastPurgeAttemptTick = 0;
	ULONGLONG g_lastPurgeSuccessTick = 0;
	std::string g_lastPurgePath;
	unsigned int g_lastPurgeRetentionDays = 0;
	bool g_lastPurgeFailed = false;

	std::string ResolvePath(const std::string& path)
	{
		if (path.empty()) return path;
		if ((path.size() >= 2 && path[1] == ':') ||
			(path.size() >= 2 && path[0] == '\\' && path[1] == '\\')) return path;
		std::string result(szPath);
		if (!result.empty() && result[result.size() - 1] != '\\') result += '\\';
		return result + path;
	}

	ArchiveConfig CurrentConfig()
	{
		EnterCriticalSection(&g_state.lock);
		const ArchiveConfig copy = g_state.config;
		LeaveCriticalSection(&g_state.lock);
		return copy;
	}

	ArchiveConfig UpdateConfigFromProfile()
	{
		ArchiveConfig updated;
		updated.historyEnabled = Profile.messageHistoryEnabled != 0;
		updated.includeMessage = Profile.messageHistoryIncludeMessage != 0;
		updated.retentionDays = Profile.messageHistoryRetentionDays;
		updated.path = Profile.messageArchivePath;
		updated.dashboardEnabled = Profile.liveDashboardEnabled != 0;
		updated.dashboardPort = static_cast<unsigned short>(Profile.liveDashboardPort);
		EnterCriticalSection(&g_state.lock);
		g_state.config = updated;
		LeaveCriticalSection(&g_state.lock);
		return updated;
	}

	std::string ResolveArchivePath(const std::string& configuredPath)
	{
		return pdw::events::PdwTextToUtf8(
			ResolvePath(configuredPath).c_str());
	}

	bool EnsureOpen(const std::string& resolvedPath, std::string& error)
	{
		if (g_archive.Open(resolvedPath, error))
		{
			InvokeTestHook(MESSAGE_ARCHIVE_TEST_AFTER_ARCHIVE_OPEN, resolvedPath);
			return true;
		}
		g_status.Set("Message archive: " + error);
		return false;
	}

	void ReplaceAliasCache(const std::string& configuredPath,
		const std::vector<pdw::archive::CapcodeEntry>& entries)
	{
		EnterCriticalSection(&g_state.lock);
		if (g_state.config.path == configuredPath) g_state.aliases = entries;
		LeaveCriticalSection(&g_state.lock);
	}

	void UpsertAliasCache(const std::string& configuredPath,
		const pdw::archive::CapcodeEntry& updated)
	{
		EnterCriticalSection(&g_state.lock);
		if (g_state.config.path == configuredPath)
		{
			bool replaced = false;
			for (std::vector<pdw::archive::CapcodeEntry>::iterator entry = g_state.aliases.begin();
				entry != g_state.aliases.end(); ++entry)
			{
				if (entry->protocol == updated.protocol && entry->address == updated.address)
				{
					*entry = updated;
					replaced = true;
					break;
				}
			}
			if (!replaced) g_state.aliases.push_back(updated);
		}
		LeaveCriticalSection(&g_state.lock);
	}

	void DeleteFromAliasCache(const std::string& configuredPath,
		const std::string& protocol, const std::string& address)
	{
		EnterCriticalSection(&g_state.lock);
		if (g_state.config.path == configuredPath)
		{
			for (std::vector<pdw::archive::CapcodeEntry>::iterator entry = g_state.aliases.begin();
				entry != g_state.aliases.end();)
			{
				if (entry->protocol == protocol && entry->address == address)
					entry = g_state.aliases.erase(entry);
				else ++entry;
			}
		}
		LeaveCriticalSection(&g_state.lock);
	}

	bool RefreshAliasCache(const std::string& configuredPath, std::string& error)
	{
		const std::string resolvedPath = ResolveArchivePath(configuredPath);
		std::vector<pdw::archive::CapcodeEntry> entries;
		{
			ArchiveOperationGuard operation(resolvedPath);
			if (!EnsureOpen(resolvedPath, error) ||
				!g_archive.ListCapcodes(std::string(), entries, error)) return false;
			ReplaceAliasCache(configuredPath, entries);
		}
		return true;
	}

	bool PopQueuedEvent(QueuedEvent& queued)
	{
		EnterCriticalSection(&g_state.lock);
		if (g_state.queue.empty())
		{
			LeaveCriticalSection(&g_state.lock);
			return false;
		}
		queued = g_state.queue.front();
		g_state.queue.pop_front();
		LeaveCriticalSection(&g_state.lock);
		return true;
	}

	void RecordDroppedEvent()
	{
		EnterCriticalSection(&g_state.lock);
		++g_state.droppedEvents;
		LeaveCriticalSection(&g_state.lock);
	}

	DWORD WINAPI ArchiveWriterThread(LPVOID)
	{
		HANDLE waits[2] = { g_state.stopEvent, g_state.workEvent };
		for (;;)
		{
			const DWORD waitResult = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
			const bool stopping = waitResult == WAIT_OBJECT_0;
			QueuedEvent queued;
			while (PopQueuedEvent(queued))
			{
				std::string error;
				{
					const std::string resolvedPath = ResolveArchivePath(queued.path);
					ArchiveOperationGuard operation(resolvedPath);
					if (!EnsureOpen(resolvedPath, error))
					{
						RecordDroppedEvent();
						continue;
					}
					if (!g_archive.StoreEvent(queued.event, queued.includeMessage, error))
					{
						RecordDroppedEvent();
						g_status.Set("Message history: " + error);
						continue;
					}
					if (g_lastPurgePath != resolvedPath ||
						g_lastPurgeRetentionDays != queued.retentionDays)
					{
						g_lastPurgePath = resolvedPath;
						g_lastPurgeRetentionDays = queued.retentionDays;
						g_lastPurgeAttemptTick = 0;
						g_lastPurgeSuccessTick = 0;
						g_lastPurgeFailed = false;
					}
					const ULONGLONG now = GetTickCount64();
					const bool purgeDue = !g_lastPurgeSuccessTick ||
						now - g_lastPurgeSuccessTick >= 24ULL * 60ULL * 60ULL * 1000ULL;
					const bool retryDue = !g_lastPurgeAttemptTick ||
						now - g_lastPurgeAttemptTick >= 5ULL * 60ULL * 1000ULL;
					if (purgeDue && retryDue)
					{
						int removed = 0;
						g_lastPurgeAttemptTick = now;
						if (!g_archive.PurgeHistory(queued.retentionDays, removed, error))
						{
							g_lastPurgeFailed = true;
							g_status.Set("Message history stored an event, but retention purge failed: " + error);
							continue;
						}
						g_lastPurgeSuccessTick = now;
						g_lastPurgeFailed = false;
					}
				}
				if (!g_lastPurgeFailed)
					g_status.Set("Message history stored the latest queued event.");
			}
			if (stopping) break;
		}
		return 0;
	}

	bool StartArchiveWriter()
	{
		if (g_state.thread) return true;
		g_state.stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		g_state.workEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		if (!g_state.stopEvent || !g_state.workEvent)
		{
			if (g_state.stopEvent) CloseHandle(g_state.stopEvent);
			if (g_state.workEvent) CloseHandle(g_state.workEvent);
			g_state.stopEvent = NULL;
			g_state.workEvent = NULL;
			g_status.Set("Message history background queue could not start.");
			return false;
		}
		g_state.thread = CreateThread(NULL, 0, ArchiveWriterThread, NULL, 0, NULL);
		if (!g_state.thread)
		{
			CloseHandle(g_state.stopEvent);
			CloseHandle(g_state.workEvent);
			g_state.stopEvent = NULL;
			g_state.workEvent = NULL;
			g_status.Set("Message history background queue could not start.");
			return false;
		}
		EnterCriticalSection(&g_state.lock);
		g_state.acceptingEvents = true;
		LeaveCriticalSection(&g_state.lock);
		return true;
	}

	void StopArchiveWriter()
	{
		// History is best-effort and must never turn application shutdown into a
		// many-minute queue drain when another process holds the database lock.
		// Stop accepting work and discard the queued tail; at most the one event
		// already executing can consume SQLite's bounded timeout.
		EnterCriticalSection(&g_state.lock);
		g_state.acceptingEvents = false;
		g_state.droppedEvents += static_cast<unsigned long>(g_state.queue.size());
		g_state.queue.clear();
		LeaveCriticalSection(&g_state.lock);
		if (g_state.stopEvent) SetEvent(g_state.stopEvent);
		if (g_state.workEvent) SetEvent(g_state.workEvent);
		if (g_state.thread)
		{
			WaitForSingleObject(g_state.thread, INFINITE);
			CloseHandle(g_state.thread);
		}
		if (g_state.stopEvent) CloseHandle(g_state.stopEvent);
		if (g_state.workEvent) CloseHandle(g_state.workEvent);
		g_state.thread = NULL;
		g_state.stopEvent = NULL;
		g_state.workEvent = NULL;
	}

	std::string JsonEscape(const std::string& value)
	{
		std::ostringstream output;
		for (std::string::const_iterator character = value.begin(); character != value.end(); ++character)
		{
			const unsigned char byte = static_cast<unsigned char>(*character);
			switch (byte)
			{
				case '"': output << "\\\""; break;
				case '\\': output << "\\\\"; break;
				case '\b': output << "\\b"; break;
				case '\f': output << "\\f"; break;
				case '\n': output << "\\n"; break;
				case '\r': output << "\\r"; break;
				case '\t': output << "\\t"; break;
				default:
					if (byte < 0x20)
					{
						static const char hex[] = "0123456789abcdef";
						output << "\\u00" << hex[byte >> 4] << hex[byte & 15];
					}
					else output << *character;
			}
		}
		return output.str();
	}
}

#ifdef PDW_MESSAGE_ARCHIVE_MANAGER_TEST_HOOKS
void MessageArchiveManagerSetTestHook(MessageArchiveManagerTestHook hook, void* context)
{
	g_testHookContext = context;
	g_testHook = hook;
}
#endif

void MessageArchiveManagerInitialize(void)
{
	UpdateConfigFromProfile();
	StartArchiveWriter();
	MessageArchiveSettingsChanged();
}

void MessageArchiveManagerShutdown(void)
{
	// Stop accepting dashboard work before clearing the writer queue. Joining
	// the dashboard first could leave it waiting behind every queued archive
	// operation, while stopping the writer first could admit new HTTP work.
	g_dashboard.RequestStop();
	StopArchiveWriter();
	g_dashboard.Stop();
	{
		const std::string noPath;
		ArchiveOperationGuard operation(noPath);
		g_archive.Close();
	}
	EnterCriticalSection(&g_state.lock);
	g_state.queue.clear();
	g_state.aliases.clear();
	LeaveCriticalSection(&g_state.lock);
	g_status.Set("Message archive is stopped.");
}

void MessageArchiveSettingsChanged(void)
{
	const ArchiveConfig config = UpdateConfigFromProfile();
	std::string error;
	const std::string resolved = ResolvePath(config.path);
	const bool databaseExists = !resolved.empty() &&
		GetFileAttributesA(resolved.c_str()) != INVALID_FILE_ATTRIBUTES;
	if (config.historyEnabled || config.dashboardEnabled || databaseExists)
	{
		if (!RefreshAliasCache(config.path, error))
		{
			g_dashboard.Stop();
			return;
		}
	}
	if (config.dashboardEnabled)
	{
		if (!config.historyEnabled)
		{
			g_dashboard.Stop();
			g_status.Set("Enable local message history before starting the dashboard.");
		}
		else if (!g_dashboard.Start(config.dashboardPort, error))
			g_status.Set("Local dashboard: " + error);
		else g_status.Set(g_dashboard.Status());
	}
	else g_dashboard.Stop();
}

bool MessageArchiveAnnotateEvent(pdw::publishing::PublishEvent& event)
{
	bool found = false;
	pdw::archive::CapcodeEntry selectedAlias;
	EnterCriticalSection(&g_state.lock);
	for (int pass = 0; pass < 2 && !found; ++pass)
	{
		for (std::vector<pdw::archive::CapcodeEntry>::const_iterator entry = g_state.aliases.begin();
			entry != g_state.aliases.end(); ++entry)
		{
			if (!entry->enabled || entry->address != event.address) continue;
			const bool anyProtocol = entry->protocol.empty();
			if ((pass == 0 && anyProtocol) || (pass == 1 && !anyProtocol)) continue;
			if (!anyProtocol && (event.mode.size() < entry->protocol.size() ||
				_strnicmp(event.mode.c_str(), entry->protocol.c_str(), entry->protocol.size()) != 0)) continue;
			selectedAlias = *entry;
			found = true;
			break;
		}
	}
	LeaveCriticalSection(&g_state.lock);
	if (!found) return false;
	event.addressName = selectedAlias.displayName;
	event.agency = selectedAlias.agency;
	event.aliasColor = selectedAlias.color;
	return true;
}

void MessageArchivePublishEvent(const pdw::publishing::PublishEvent& event)
{
	bool queueFull = false;
	EnterCriticalSection(&g_state.lock);
	if (g_state.config.historyEnabled && g_state.thread && g_state.acceptingEvents)
	{
		if (g_state.queue.size() >= MAX_PENDING_EVENTS)
		{
			++g_state.droppedEvents;
			queueFull = true;
		}
		else
		{
			QueuedEvent queued;
			queued.event = event;
			queued.includeMessage = g_state.config.includeMessage;
			queued.retentionDays = g_state.config.retentionDays;
			queued.path = g_state.config.path;
			g_state.queue.push_back(queued);
			SetEvent(g_state.workEvent);
		}
	}
	LeaveCriticalSection(&g_state.lock);
	if (queueFull) g_status.Set("Message history queue is full; the newest event was not stored.");
}

bool MessageArchiveListCapcodes(const std::string& search,
	std::vector<pdw::archive::CapcodeEntry>& entries, std::string& error)
{
	const ArchiveConfig config = CurrentConfig();
	const std::string resolvedPath = ResolveArchivePath(config.path);
	{
		ArchiveOperationGuard operation(resolvedPath);
		if (!EnsureOpen(resolvedPath, error) ||
			!g_archive.ListCapcodes(search, entries, error)) return false;
		if (search.empty()) ReplaceAliasCache(config.path, entries);
	}
	return true;
}

bool MessageArchiveUpsertCapcode(const pdw::archive::CapcodeEntry& entry,
	std::string& error)
{
	const ArchiveConfig config = CurrentConfig();
	const std::string resolvedPath = ResolveArchivePath(config.path);
	{
		ArchiveOperationGuard operation(resolvedPath);
		if (!EnsureOpen(resolvedPath, error) ||
			!g_archive.UpsertCapcode(entry, error)) return false;
		// Keep cache mutation ordered with the database commit and every full
		// snapshot replacement under the same operation lock.
		UpsertAliasCache(config.path, entry);
	}
	// The committed mutation is authoritative. Update the in-memory decoder
	// snapshot directly so a later full-list timeout cannot turn a successful
	// save into a false failure or make CSV import reload the table per row.
	return true;
}

bool MessageArchiveDeleteCapcode(const std::string& protocol,
	const std::string& address, std::string& error)
{
	const ArchiveConfig config = CurrentConfig();
	const std::string resolvedPath = ResolveArchivePath(config.path);
	{
		ArchiveOperationGuard operation(resolvedPath);
		if (!EnsureOpen(resolvedPath, error) ||
			!g_archive.DeleteCapcode(protocol, address, error)) return false;
		DeleteFromAliasCache(config.path, protocol, address);
	}
	return true;
}

bool MessageArchiveQueryHistory(const pdw::archive::HistoryQuery& query,
	std::vector<pdw::archive::HistoryRow>& rows, int& total, std::string& error)
{
	const ArchiveConfig config = CurrentConfig();
	const std::string resolvedPath = ResolveArchivePath(config.path);
	ArchiveOperationGuard operation(resolvedPath);
	return EnsureOpen(resolvedPath, error) &&
		g_archive.QueryHistory(query, rows, total, error);
}

bool MessageArchiveExportHistoryCsv(const pdw::archive::HistoryQuery& query,
	std::ostream& output, int& exported, std::string& error)
{
	// Capture the configured path once. The exporter owns a separate hardened
	// read-only SQLite connection, so path changes and background writes cannot
	// retarget or serialize the streaming snapshot through g_archive.
	const ArchiveConfig config = CurrentConfig();
	const std::string resolvedPath = ResolveArchivePath(config.path);
	return pdw::archive::ExportHistoryCsv(resolvedPath, query, output, exported, error);
}

bool MessageArchivePurgeHistory(unsigned int retentionDays, int& removed,
	std::string& error)
{
	const ArchiveConfig config = CurrentConfig();
	const std::string resolvedPath = ResolveArchivePath(config.path);
	ArchiveOperationGuard operation(resolvedPath);
	return EnsureOpen(resolvedPath, error) &&
		g_archive.PurgeHistory(retentionDays, removed, error);
}

std::string MessageArchiveBuildMessagesJson(int limit, int offset,
	const std::string& search, const std::string& protocol, bool filteredOnly)
{
	pdw::archive::HistoryQuery query;
	query.limit = limit;
	query.offset = offset;
	query.search = search;
	query.protocol = protocol;
	query.filteredOnly = filteredOnly;
	std::vector<pdw::archive::HistoryRow> rows;
	int total = 0;
	std::string error;
	std::ostringstream output;
	if (!MessageArchiveQueryHistory(query, rows, total, error))
	{
		output << "{\"version\":1,\"total\":0,\"error\":\"" << JsonEscape(error)
			<< "\",\"events\":[]}\n";
		return output.str();
	}
	output << "{\"version\":1,\"total\":" << total << ",\"offset\":" << offset << ",\"events\":[";
	for (std::size_t index = 0; index < rows.size(); ++index)
	{
		if (index) output << ',';
		const pdw::publishing::PublishEvent& event = rows[index].event;
		// History deliberately stores a reduced, privacy-oriented event schema.
		// Emit only persisted fields so omitted live-only metadata is never
		// misrepresented as false, zero, or null after a database round trip.
		output << "{\"id\":\"" << JsonEscape(event.id)
			<< "\",\"timestamp\":\"" << JsonEscape(event.timestamp)
			<< "\",\"source\":\"" << JsonEscape(event.source)
			<< "\",\"address\":\"" << JsonEscape(event.address)
			<< "\",\"address_name\":\"" << JsonEscape(rows[index].displayName)
			<< "\",\"agency\":\"" << JsonEscape(rows[index].agency)
			<< "\",\"alias_color\":" << rows[index].color
			<< ",\"time\":\"" << JsonEscape(event.time)
			<< "\",\"date\":\"" << JsonEscape(event.date)
			<< "\",\"mode\":\"" << JsonEscape(event.mode)
			<< "\",\"message_type\":\"" << JsonEscape(event.messageType)
			<< "\",\"bitrate\":\"" << JsonEscape(event.bitrate)
			<< "\",\"message\":\"" << JsonEscape(event.message)
			<< "\",\"filter_label\":\"" << JsonEscape(event.filterLabel)
			<< "\",\"filtered\":" << (event.filtered ? "true" : "false")
			<< ",\"rejected\":" << (event.rejected ? "true" : "false")
			<< ",\"blocked_duplicate\":" << (event.blockedDuplicate ? "true" : "false")
			<< ",\"fragmented\":" << (event.fragmented ? "true" : "false")
			<< ",\"assembled\":" << (event.assembled ? "true" : "false") << '}';
	}
	output << "]}\n";
	return output.str();
}

std::string MessageArchiveBuildCapcodesJson(const std::string& search)
{
	std::vector<pdw::archive::CapcodeEntry> entries;
	std::string error;
	std::ostringstream output;
	if (!MessageArchiveListCapcodes(search, entries, error))
	{
		output << "{\"version\":1,\"error\":\"" << JsonEscape(error) << "\",\"capcodes\":[]}\n";
		return output.str();
	}
	output << "{\"version\":1,\"capcodes\":[";
	for (std::size_t index = 0; index < entries.size(); ++index)
	{
		if (index) output << ',';
		output << "{\"protocol\":\"" << JsonEscape(entries[index].protocol)
			<< "\",\"address\":\"" << JsonEscape(entries[index].address)
			<< "\",\"display_name\":\"" << JsonEscape(entries[index].displayName)
			<< "\",\"agency\":\"" << JsonEscape(entries[index].agency)
			<< "\",\"color\":" << entries[index].color
			<< ",\"notes\":\"" << JsonEscape(entries[index].notes)
			<< "\",\"enabled\":" << (entries[index].enabled ? "true" : "false") << '}';
	}
	output << "]}\n";
	return output.str();
}

std::string MessageArchiveStatusText(void)
{
	return g_status.Get();
}
