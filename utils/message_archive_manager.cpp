#include "headers\message_archive_manager.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <map>
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
				if ((updated.id > 0 && entry->id == updated.id) ||
					(updated.id <= 0 && entry->protocol == updated.protocol &&
					 entry->address == updated.address && entry->filterType == updated.filterType &&
					 entry->matchText == updated.matchText))
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

	void DeleteFromAliasCache(const std::string& configuredPath, long long id)
	{
		EnterCriticalSection(&g_state.lock);
		if (g_state.config.path == configuredPath)
		{
			for (std::vector<pdw::archive::CapcodeEntry>::iterator entry = g_state.aliases.begin();
				entry != g_state.aliases.end();)
			{
				if (entry->id == id) entry = g_state.aliases.erase(entry);
				else ++entry;
			}
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

	FILTER_TYPE InferRuntimeFilterType(const pdw::archive::CapcodeEntry& entry)
	{
		if (entry.filterType >= FLEX_FILTER && entry.filterType <= MOBITEX_FILTER)
			return static_cast<FILTER_TYPE>(entry.filterType);
		if (_stricmp(entry.protocol.c_str(), "FLEX") == 0) return FLEX_FILTER;
		if (_stricmp(entry.protocol.c_str(), "POCSAG") == 0) return POCSAG_FILTER;
		if (_stricmp(entry.protocol.c_str(), "ERMES") == 0) return ERMES_FILTER;
		if (_stricmp(entry.protocol.c_str(), "ACARS") == 0) return ACARS_FILTER;
		if (_stricmp(entry.protocol.c_str(), "MOBITEX") == 0) return MOBITEX_FILTER;
		return UNUSED_FILTER;
	}

	template <std::size_t Size>
	void CopyRuntimeText(char (&destination)[Size], const std::string& source)
	{
		if (!Size) return;
		strncpy(destination, source.c_str(), Size - 1);
		destination[Size - 1] = '\0';
	}

	std::string DirectoryDisplayLabel(const pdw::archive::CapcodeEntry& entry)
	{
		if (entry.agency.empty() || entry.agencyLabelPosition == PDW_AGENCY_LABEL_HIDDEN)
			return entry.displayName;
		if (entry.displayName.empty()) return entry.agency;
		return entry.agencyLabelPosition == PDW_AGENCY_LABEL_BEFORE ?
			entry.agency + " - " + entry.displayName :
			entry.displayName + " - " + entry.agency;
	}

	FILTER MakeRuntimeFilter(const pdw::archive::CapcodeEntry& entry, FILTER_TYPE type)
	{
		FILTER filter = {};
		filter.directory_id = entry.id;
		filter.filter_to_pane = entry.filterEnabled ? 1 : 0;
		filter.output_routing_configured = entry.outputRoutingConfigured ? 1 : 0;
		filter.output_routes = entry.outputRoutes;
		filter.type = type;
		CopyRuntimeText(filter.capcode, entry.address);
		CopyRuntimeText(filter.text, entry.matchText);
		CopyRuntimeText(filter.label, DirectoryDisplayLabel(entry));
		filter.match_exact_msg = entry.matchExactMessage ? 1 : 0;
		filter.cmd_enabled = 1;
		filter.reject = entry.reject ? 1 : 0;
		filter.monitor_only = entry.monitorOnly ? 1 : 0;
		filter.wave_number = entry.waveNumber;
		filter.label_enabled = entry.showFilterLabel ? 1 : 0;
		filter.label_color = entry.labelColor;
		filter.smtp = entry.outputRoutingConfigured ?
			((entry.outputRoutes & PDW_OUTPUT_ROUTE_EMAIL) ? 1 : 0) :
			(entry.emailEnabled ? 1 : 0);
		filter.sep_filterfile_en = entry.separateFileEnabled ? 1 : 0;
		const std::string files[3] = { entry.separateFile1, entry.separateFile2, entry.separateFile3 };
		for (int index = 0; index < 3; ++index)
		{
			CopyRuntimeText(filter.sep_filterfile[index], files[index]);
			if (!files[index].empty()) ++filter.sep_filterfiles;
		}
		filter.hitcounter = entry.hitCounter;
		CopyRuntimeText(filter.lasthit_date, entry.lastHitDate);
		CopyRuntimeText(filter.lasthit_time, entry.lastHitTime);
		return filter;
	}

	unsigned long RuntimeRuleSpecificity(const pdw::archive::CapcodeEntry& entry)
	{
		unsigned long score = 0;
		if (entry.matchExactMessage && !entry.matchText.empty()) score += 1000000UL;
		if (!entry.matchText.empty())
		{
			unsigned long terms = 1;
			for (std::string::const_iterator character = entry.matchText.begin();
				character != entry.matchText.end(); ++character)
				if (*character == '+') ++terms;
			score += 100000UL + terms * 1000UL +
				static_cast<unsigned long>(std::min<std::size_t>(entry.matchText.size(), 999));
		}
		if (!entry.protocol.empty()) score += 10000UL;
		bool wildcard = false;
		for (std::string::const_iterator character = entry.address.begin();
			character != entry.address.end(); ++character)
		{
			if (*character == '?') wildcard = true;
			else score += 10UL;
		}
		if (!entry.address.empty() && !wildcard) score += 5000UL;
		// A POCSAG address may carry a function-number suffix (1234567-1).
		// It is narrower than the base capcode even when the rule applies to Any
		// protocol and is expanded later, so it must be evaluated first.
		if (entry.address.size() == 9 && entry.address[7] == '-' &&
			entry.address[8] >= '0' && entry.address[8] <= '9')
			score += 20000UL;
		return score;
	}

	std::string ProtocolForLegacyType(FILTER_TYPE type)
	{
		switch (type)
		{
			case FLEX_FILTER: return "FLEX";
			case POCSAG_FILTER: return "POCSAG";
			case ERMES_FILTER: return "ERMES";
			case ACARS_FILTER: return "ACARS";
			case MOBITEX_FILTER: return "MOBITEX";
			default: return std::string();
		}
	}

	pdw::archive::CapcodeEntry LegacyFilterEntry(const FILTER& filter)
	{
		pdw::archive::CapcodeEntry entry;
		entry.protocol = ProtocolForLegacyType(filter.type);
		entry.address = filter.capcode;
		entry.displayName = filter.label;
		entry.filterLabel = filter.label;
		entry.filterType = static_cast<int>(filter.type);
		entry.matchText = filter.text;
		// Some third-party generators copied the friendly label into the
		// required message-text field. A capcode-only legacy rule never needed
		// that duplicate condition, so repair it during the one-time migration.
		if (filter.type != TEXT_FILTER && !entry.matchText.empty() &&
			_stricmp(entry.matchText.c_str(), entry.filterLabel.c_str()) == 0)
			entry.matchText.clear();
		entry.enabled = true;
		entry.filterEnabled = filter.monitor_only == 0;
		// Legacy filters had global output selection. Keep that behavior until
		// the operator deliberately saves explicit destinations on the rule.
		entry.outputRoutingConfigured = false;
		entry.outputRoutes = 0;
		entry.reject = filter.reject != 0;
		entry.matchExactMessage = filter.match_exact_msg != 0;
		entry.showFilterLabel = filter.label_enabled != 0;
		entry.commandEnabled = filter.cmd_enabled != 0;
		entry.monitorOnly = filter.monitor_only != 0;
		entry.emailEnabled = filter.smtp != 0;
		if (entry.emailEnabled) entry.outputRoutes |= PDW_OUTPUT_ROUTE_EMAIL;
		entry.separateFileEnabled = filter.sep_filterfile_en != 0;
		entry.separateFile1 = filter.sep_filterfile[0];
		entry.separateFile2 = filter.sep_filterfile[1];
		entry.separateFile3 = filter.sep_filterfile[2];
		entry.waveNumber = filter.wave_number;
		entry.labelColor = filter.label_color;
		entry.hitCounter = filter.hitcounter;
		entry.lastHitDate = filter.lasthit_date;
		entry.lastHitTime = filter.lasthit_time;
		return entry;
	}

	void BuildRuntimeFilters(const std::vector<pdw::archive::CapcodeEntry>& entries,
		FILTERLIST& filters)
	{
		filters.clear();
		std::vector<const pdw::archive::CapcodeEntry*> ordered;
		for (std::vector<pdw::archive::CapcodeEntry>::const_iterator entry = entries.begin();
			entry != entries.end(); ++entry)
			if (entry->enabled) ordered.push_back(&*entry);
		std::stable_sort(ordered.begin(), ordered.end(),
			[](const pdw::archive::CapcodeEntry* left,
				const pdw::archive::CapcodeEntry* right)
			{
				// Reject is an absolute discard action. Evaluate every matching
				// reject rule before display/output rules so a more-specific route
				// cannot allow the message into either pane or any downstream output.
				if (left->reject != right->reject) return left->reject;
				return RuntimeRuleSpecificity(*left) > RuntimeRuleSpecificity(*right);
			});
		for (std::vector<const pdw::archive::CapcodeEntry*>::const_iterator item = ordered.begin();
			item != ordered.end(); ++item)
		{
			const pdw::archive::CapcodeEntry& entry = **item;
			// The legacy enabled field is retained only as a migration sentinel so
			// previously disabled rows stay dormant. Every active directory row must
			// reach the matcher: Filter/output choices are independent, and monitor,
			// label, hit-counter, wave and shared command actions happen after match.
			const FILTER_TYPE inferred = InferRuntimeFilterType(entry);
			if (inferred != UNUSED_FILTER)
			{
				filters.push_back(MakeRuntimeFilter(entry, inferred));
				continue;
			}
			// "Any protocol" address entries remain one directory row, but become
			// protocol-specific runtime filters so the proven legacy matcher stays intact.
			if (entry.address.empty()) continue;
			filters.push_back(MakeRuntimeFilter(entry, FLEX_FILTER));
			filters.push_back(MakeRuntimeFilter(entry, POCSAG_FILTER));
			filters.push_back(MakeRuntimeFilter(entry, ERMES_FILTER));
			filters.push_back(MakeRuntimeFilter(entry, ACARS_FILTER));
			filters.push_back(MakeRuntimeFilter(entry, MOBITEX_FILTER));
		}
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
	std::string filterError;
	if (!MessageArchiveReloadRuntimeFilters(filterError))
		g_status.Set("Capcode Directory: " + filterError);
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

bool MessageArchiveDeleteCapcode(long long id, std::string& error)
{
	const ArchiveConfig config = CurrentConfig();
	const std::string resolvedPath = ResolveArchivePath(config.path);
	{
		ArchiveOperationGuard operation(resolvedPath);
		if (!EnsureOpen(resolvedPath, error) || !g_archive.DeleteCapcode(id, error)) return false;
		DeleteFromAliasCache(config.path, id);
	}
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

bool MessageArchiveResetCapcodeHitCounter(long long id, std::string& error)
{
	if (id <= 0) { error = "Choose a Capcode Directory entry first."; return false; }
	const ArchiveConfig config = CurrentConfig();
	const std::string resolvedPath = ResolveArchivePath(config.path);
	ArchiveOperationGuard operation(resolvedPath);
	return EnsureOpen(resolvedPath, error) &&
		g_archive.UpdateCapcodeRuntimeState(id, 0, std::string(), std::string(), error);
}

bool MessageArchiveReloadRuntimeFilters(std::string& error)
{
	const ArchiveConfig config = CurrentConfig();
	const std::string resolvedPath = ResolveArchivePath(config.path);
	std::vector<pdw::archive::CapcodeEntry> entries;
	{
		ArchiveOperationGuard operation(resolvedPath);
		if (!EnsureOpen(resolvedPath, error) ||
			!g_archive.ListCapcodes(std::string(), entries, error)) return false;
		ReplaceAliasCache(config.path, entries);
	}
	FILTERLIST filters;
	BuildRuntimeFilters(entries, filters);
	Profile.filters.swap(filters);
	return true;
}

bool MessageArchivePersistRuntimeFilterState(std::string& error)
{
	struct RuntimeState
	{
		unsigned int hits;
		std::string date;
		std::string time;
		RuntimeState() : hits(0) {}
	};
	std::map<long long, RuntimeState> states;
	for (FILTERLIST::const_iterator filter = Profile.filters.begin(); filter != Profile.filters.end(); ++filter)
	{
		if (filter->directory_id <= 0) continue;
		RuntimeState& runtimeState = states[filter->directory_id];
		if (filter->hitcounter >= runtimeState.hits)
		{
			runtimeState.hits = filter->hitcounter;
			runtimeState.date = filter->lasthit_date;
			runtimeState.time = filter->lasthit_time;
		}
	}
	const ArchiveConfig config = CurrentConfig();
	const std::string resolvedPath = ResolveArchivePath(config.path);
	ArchiveOperationGuard operation(resolvedPath);
	if (!EnsureOpen(resolvedPath, error)) return false;
	for (std::map<long long, RuntimeState>::const_iterator state = states.begin(); state != states.end(); ++state)
		if (!g_archive.UpdateCapcodeRuntimeState(state->first, state->second.hits,
			state->second.date, state->second.time, error)) return false;
	return true;
}

bool MessageArchiveExportCapcodesCsv(std::string& csv, std::string& error)
{
	std::vector<pdw::archive::CapcodeEntry> entries;
	if (!MessageArchiveListCapcodes(std::string(), entries, error)) return false;
	std::ostringstream output;
	if (!pdw::archive::WriteCapcodeDirectoryCsv(entries, output, error)) return false;
	csv = output.str();
	return true;
}

bool MessageArchiveReplaceCapcodesCsv(const std::string& csv, int& rejected,
	std::string& error)
{
	std::istringstream input(csv);
	std::vector<pdw::archive::CapcodeEntry> entries;
	if (!pdw::archive::ReadCapcodeDirectoryCsv(input, entries, rejected, error)) return false;
	if (rejected)
	{
		error = "The Capcode Directory CSV contains invalid rows; no current entries were replaced.";
		return false;
	}
	const ArchiveConfig config = CurrentConfig();
	const std::string resolvedPath = ResolveArchivePath(config.path);
	{
		ArchiveOperationGuard operation(resolvedPath);
		if (!EnsureOpen(resolvedPath, error) || !g_archive.ReplaceCapcodes(entries, error) ||
			!g_archive.ListCapcodes(std::string(), entries, error)) return false;
		ReplaceAliasCache(config.path, entries);
	}
	FILTERLIST filters;
	BuildRuntimeFilters(entries, filters);
	Profile.filters.swap(filters);
	return true;
}

bool MessageArchiveReplaceLegacyFilters(const FILTERLIST& filters,
	std::string& error)
{
	std::vector<pdw::archive::CapcodeEntry> entries;
	entries.reserve(filters.size());
	for (FILTERLIST::const_iterator filter = filters.begin(); filter != filters.end(); ++filter)
		entries.push_back(LegacyFilterEntry(*filter));
	const ArchiveConfig config = CurrentConfig();
	const std::string resolvedPath = ResolveArchivePath(config.path);
	{
		ArchiveOperationGuard operation(resolvedPath);
		if (!EnsureOpen(resolvedPath, error) || !g_archive.ReplaceCapcodes(entries, error) ||
			!g_archive.ListCapcodes(std::string(), entries, error)) return false;
		ReplaceAliasCache(config.path, entries);
	}
	FILTERLIST runtime;
	BuildRuntimeFilters(entries, runtime);
	Profile.filters.swap(runtime);
	return true;
}

bool MessageArchiveMergeLegacyFilters(const FILTERLIST& filters,
	std::string& error)
{
	std::vector<pdw::archive::CapcodeEntry> entries;
	if (!MessageArchiveListCapcodes(std::string(), entries, error)) return false;
	for (FILTERLIST::const_iterator filter = filters.begin(); filter != filters.end(); ++filter)
	{
		pdw::archive::CapcodeEntry migrated = LegacyFilterEntry(*filter);
		bool merged = false;
		for (std::vector<pdw::archive::CapcodeEntry>::iterator current = entries.begin();
			current != entries.end(); ++current)
		{
			if (_stricmp(current->protocol.c_str(), migrated.protocol.c_str()) != 0 ||
				current->address != migrated.address || current->filterType != migrated.filterType ||
				_stricmp(current->matchText.c_str(), migrated.matchText.c_str()) != 0) continue;
			if (!current->displayName.empty()) migrated.displayName = current->displayName;
			migrated.agency = current->agency;
			migrated.color = current->color;
			migrated.notes = current->notes;
			migrated.enabled = current->enabled;
			*current = migrated;
			merged = true;
			break;
		}
		if (!merged) entries.push_back(migrated);
	}
	const ArchiveConfig config = CurrentConfig();
	const std::string resolvedPath = ResolveArchivePath(config.path);
	{
		ArchiveOperationGuard operation(resolvedPath);
		if (!EnsureOpen(resolvedPath, error) || !g_archive.ReplaceCapcodes(entries, error) ||
			!g_archive.ListCapcodes(std::string(), entries, error)) return false;
		ReplaceAliasCache(config.path, entries);
	}
	FILTERLIST runtime;
	BuildRuntimeFilters(entries, runtime);
	Profile.filters.swap(runtime);
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
			<< "\",\"enabled\":" << (entries[index].enabled ? "true" : "false")
			<< ",\"filter_enabled\":" << (entries[index].filterEnabled ? "true" : "false")
			<< ",\"output_routing_configured\":"
			<< (entries[index].outputRoutingConfigured ? "true" : "false")
			<< ",\"send_to_outputs\":"
			<< (entries[index].outputRoutingConfigured && entries[index].outputRoutes ? "true" : "false")
			<< ",\"output_routes\":" << entries[index].outputRoutes
			<< ",\"agency_label_position\":" << entries[index].agencyLabelPosition << '}';
	}
	output << "]}\n";
	return output.str();
}

std::string MessageArchiveStatusText(void)
{
	return g_status.Get();
}
