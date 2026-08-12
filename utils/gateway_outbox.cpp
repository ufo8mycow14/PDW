#include "headers\gateway_outbox.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <sstream>

#include "headers\pdw.h"
#include "headers\resource.h"
#include "headers\version.h"
#include "decoded_event.h"
#include "gateway_outbox_core.h"

extern PROFILE Profile;
extern TCHAR szPath[MAX_PATH];

namespace
{
	const unsigned long long DISK_WARNING_BYTES = 512ULL * 1024ULL * 1024ULL;

	struct OutboxConfig
	{
		bool enabled;
		std::string path;
		std::string receiverId;
		unsigned int retentionDays;
		unsigned int maximumMegabytes;
		unsigned int queueCapacity;

		OutboxConfig() : enabled(false), retentionDays(30), maximumMegabytes(512),
			queueCapacity(1024) {}
	};

	struct OutboxState
	{
		OutboxState() : workEvent(NULL), stopEvent(NULL), readyEvent(NULL), thread(NULL),
			accepting(false), startupSucceeded(false), nextSequence(0),
			reservationCeiling(0),
			queueHighWaterMark(0), droppedEvents(0), writeFailures(0),
			lastCommittedSequence(0), oldestRetainedSequence(0), retainedRecords(0),
			databaseBytes(0), availableDiskBytes(0), diskWarning(false)
		{
			InitializeCriticalSection(&lock);
		}
		~OutboxState() { DeleteCriticalSection(&lock); }

		CRITICAL_SECTION lock;
		HANDLE workEvent;
		HANDLE stopEvent;
		HANDLE readyEvent;
		HANDLE thread;
		std::deque<pdw::gateway::GatewayEvent> queue;
		OutboxConfig config;
		bool accepting;
		bool startupSucceeded;
		long long nextSequence;
		long long reservationCeiling;
		unsigned long queueHighWaterMark;
		unsigned long long droppedEvents;
		unsigned long long writeFailures;
		long long lastCommittedSequence;
		long long oldestRetainedSequence;
		long long retainedRecords;
		unsigned long long databaseBytes;
		unsigned long long availableDiskBytes;
		bool diskWarning;
		std::string lastError;
	} g_state;

#ifdef PDW_GATEWAY_OUTBOX_TEST_HOOKS
	volatile LONG g_workerDelayMs = 0;
#endif

	std::string ResolvePath(const std::string& configured)
	{
		if (configured.empty()) return configured;
		if ((configured.size() >= 2 && configured[1] == ':') ||
			(configured.size() >= 2 && configured[0] == '\\' && configured[1] == '\\'))
			return configured;
		std::string result(szPath);
		if (!result.empty() && result[result.size() - 1] != '\\') result += '\\';
		return result + configured;
	}

	OutboxConfig ConfigFromProfile()
	{
		OutboxConfig config;
		config.enabled = Profile.gatewayOutboxEnabled != 0;
		config.path = pdw::events::PdwTextToUtf8(Profile.gatewayOutboxPath);
		config.receiverId = pdw::events::PdwTextToUtf8(Profile.gatewayReceiverId);
		config.retentionDays = Profile.gatewayOutboxRetentionDays;
		config.maximumMegabytes = Profile.gatewayOutboxMaximumMegabytes;
		config.queueCapacity = Profile.gatewayOutboxQueueCapacity;
		return config;
	}

	void SetLastError(const std::string& error)
	{
		EnterCriticalSection(&g_state.lock);
		g_state.lastError = error;
		LeaveCriticalSection(&g_state.lock);
	}

	std::string ProtocolName(const std::string& mode)
	{
		if (_strnicmp(mode.c_str(), "POCSAG", 6) == 0) return "POCSAG";
		if (_strnicmp(mode.c_str(), "FLEX", 4) == 0) return "FLEX";
		if (_strnicmp(mode.c_str(), "ERMES", 5) == 0) return "ERMES";
		if (_strnicmp(mode.c_str(), "ACARS", 5) == 0) return "ACARS";
		if (_strnicmp(mode.c_str(), "MOBITEX", 7) == 0) return "MOBITEX";
		return mode.empty() ? "UNKNOWN" : mode;
	}

	std::string ProtocolMetadata(const pdw::publishing::PublishEvent& decoded)
	{
		std::ostringstream json;
		json << "{\"mode\":\"";
		for (std::string::const_iterator c = decoded.mode.begin(); c != decoded.mode.end(); ++c)
		{
			if (*c == '"' || *c == '\\') json << '\\';
			json << *c;
		}
		json << "\",\"cycle\":" << decoded.cycle
			<< ",\"frame\":" << decoded.frame
			<< ",\"group_bit\":" << decoded.groupBit << "}";
		return json.str();
	}

	pdw::gateway::GatewayEvent BuildEvent(
		const DecodedMessageNotificationContext& context, bool synthetic)
	{
		const pdw::publishing::PublishEvent decoded = pdw::events::BuildDecodedEvent(context);
		pdw::gateway::GatewayEvent event;
		event.eventId = decoded.id;
		event.receiverId = pdw::events::PdwTextToUtf8(Profile.gatewayReceiverId);
		event.decoderFinalizedUtc = decoded.timestamp;
		event.timestampMethod = synthetic ? "synthetic_control_system_utc" :
			"decoder_finalized_system_utc";
		event.source = "PDW local decoder";
		if (synthetic)
		{
			event.frequencyHz = 148337500;
			event.frequencyProvenance = "synthetic_fixture";
			event.frequencyEffectiveUtc = decoded.timestamp;
		}
		else if (Profile.audioSource == AUDIO_SOURCE_RTL_TCP ||
			Profile.audioSource == AUDIO_SOURCE_RTL_SDR)
		{
			event.frequencyHz = Profile.rtlFrequencyHz;
			event.frequencyProvenance = "pdw_receiver_configuration";
			event.frequencyEffectiveUtc = decoded.timestamp;
		}
		else event.frequencyProvenance = "not_available_for_selected_input";
		event.protocol = ProtocolName(decoded.mode);
		event.protocolMetadataJson = ProtocolMetadata(decoded);
		event.capcode = decoded.address;
		event.messageType = decoded.messageType;
		event.bitrate = decoded.bitrate;
		event.message = decoded.message;
		event.pdwVersion = PDW_VERSION_STRING;
		event.filterLabel = decoded.filterLabel;
		event.filterMatched = decoded.filterMatched;
		event.monitorOnly = decoded.monitorOnly;
		event.filtered = decoded.filtered;
		event.rejected = decoded.rejected;
		event.blockedDuplicate = decoded.blockedDuplicate;
		event.groupCall = decoded.groupCall;
		event.groupFinal = decoded.groupFinal;
		event.fragmented = decoded.fragmented;
		event.assembled = decoded.assembled;
		event.groupBit = decoded.groupBit;
		event.cycle = decoded.cycle;
		event.frame = decoded.frame;
		event.synthetic = synthetic;
		return event;
	}

	bool QueueEvent(pdw::gateway::GatewayEvent event, std::string* error)
	{
		bool queued = false;
		EnterCriticalSection(&g_state.lock);
		if (!g_state.config.enabled || !g_state.accepting || !g_state.thread)
		{
			if (g_state.config.enabled) ++g_state.droppedEvents;
			if (error) *error = "Local Gateway Outbox is disabled or unavailable.";
		}
		else
		{
			if (g_state.queue.size() >= g_state.config.queueCapacity)
			{
				++g_state.droppedEvents;
				if (error) *error = "Local Gateway Outbox queue is full; the event was dropped.";
			}
			else if (g_state.nextSequence >= g_state.reservationCeiling)
			{
				++g_state.droppedEvents;
				if (error) *error = "Local Gateway Outbox sequence reservation is temporarily unavailable.";
			}
			else
			{
				event.receiverSequence = ++g_state.nextSequence;
				event.contentHash = pdw::gateway::GatewayEventContentHash(event);
				g_state.queue.push_back(event);
				g_state.queueHighWaterMark = (std::max)(g_state.queueHighWaterMark,
					static_cast<unsigned long>(g_state.queue.size()));
				queued = true;
			}
			SetEvent(g_state.workEvent);
		}
		LeaveCriticalSection(&g_state.lock);
		return queued;
	}

	bool PopEvent(pdw::gateway::GatewayEvent& event)
	{
		EnterCriticalSection(&g_state.lock);
		if (g_state.queue.empty())
		{
			LeaveCriticalSection(&g_state.lock);
			return false;
		}
		event = g_state.queue.front();
		g_state.queue.pop_front();
		LeaveCriticalSection(&g_state.lock);
		return true;
	}

	void UpdateStatistics(const pdw::gateway::StoreStatistics& statistics)
	{
		EnterCriticalSection(&g_state.lock);
		g_state.lastCommittedSequence = statistics.lastCommittedSequence;
		g_state.oldestRetainedSequence = statistics.oldestRetainedSequence;
		g_state.retainedRecords = statistics.retainedRecords;
		g_state.databaseBytes = statistics.databaseBytes;
		g_state.availableDiskBytes = statistics.availableDiskBytes;
		g_state.diskWarning = statistics.availableDiskBytes > 0 &&
			statistics.availableDiskBytes < DISK_WARNING_BYTES;
		LeaveCriticalSection(&g_state.lock);
	}

	DWORD WINAPI WriterThread(LPVOID)
	{
		OutboxConfig config;
		EnterCriticalSection(&g_state.lock);
		config = g_state.config;
		LeaveCriticalSection(&g_state.lock);

		pdw::gateway::GatewayOutboxStore store;
		std::string error;
		const std::string resolvedPath = ResolvePath(config.path);
		long long highestAssigned = 0;
		pdw::gateway::StoreStatistics statistics;
		pdw::gateway::RetentionPolicy startupPolicy;
		startupPolicy.retentionDays = config.retentionDays;
		startupPolicy.maximumMegabytes = config.maximumMegabytes;
		int startupRemoved = 0;
		const long long reservationSize = (std::max)(2048LL,
			static_cast<long long>(config.queueCapacity) * 2LL);
		long long reservationCeiling = 0;
		const bool opened = store.Open(resolvedPath, error) &&
			store.EnforceRetention(startupPolicy, startupRemoved, error) &&
			store.GetHighestAssignedSequence(highestAssigned, error) &&
			store.GetStatistics(statistics, error) &&
			((reservationCeiling = (std::max)(highestAssigned,
				statistics.lastCommittedSequence) + reservationSize) > 0) &&
			store.RecordHighestAssignedSequence(reservationCeiling, error);
		EnterCriticalSection(&g_state.lock);
		g_state.startupSucceeded = opened;
		if (opened)
		{
			g_state.nextSequence = (std::max)(highestAssigned, statistics.lastCommittedSequence);
			g_state.reservationCeiling = reservationCeiling;
			g_state.lastCommittedSequence = statistics.lastCommittedSequence;
			g_state.oldestRetainedSequence = statistics.oldestRetainedSequence;
			g_state.retainedRecords = statistics.retainedRecords;
			g_state.databaseBytes = statistics.databaseBytes;
			g_state.availableDiskBytes = statistics.availableDiskBytes;
			g_state.diskWarning = statistics.availableDiskBytes > 0 &&
				statistics.availableDiskBytes < DISK_WARNING_BYTES;
		}
		else g_state.lastError = error;
		LeaveCriticalSection(&g_state.lock);
		SetEvent(g_state.readyEvent);
		if (!opened) return 0;

		HANDLE waits[2] = { g_state.stopEvent, g_state.workEvent };
		unsigned int writesSinceRetention = 0;
		for (;;)
		{
			const DWORD waitResult = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
			const bool stopping = waitResult == WAIT_OBJECT_0;
			pdw::gateway::GatewayEvent event;
			while (PopEvent(event))
			{
#ifdef PDW_GATEWAY_OUTBOX_TEST_HOOKS
				const LONG delay = InterlockedCompareExchange(&g_workerDelayMs, 0, 0);
				if (delay > 0) Sleep(static_cast<DWORD>(delay));
#endif
				if (!store.Append(event, error) ||
					!store.RecordHighestAssignedSequence(event.receiverSequence, error))
				{
					EnterCriticalSection(&g_state.lock);
					++g_state.writeFailures;
					++g_state.droppedEvents;
					g_state.lastError = error;
					LeaveCriticalSection(&g_state.lock);
					continue;
				}
				long long currentReservation = 0;
				EnterCriticalSection(&g_state.lock);
				currentReservation = g_state.reservationCeiling;
				LeaveCriticalSection(&g_state.lock);
				if (event.receiverSequence + static_cast<long long>(config.queueCapacity) >=
					currentReservation)
				{
					const long long extended = currentReservation + reservationSize;
					if (store.RecordHighestAssignedSequence(extended, error))
					{
						EnterCriticalSection(&g_state.lock);
						g_state.reservationCeiling = extended;
						LeaveCriticalSection(&g_state.lock);
					}
					else SetLastError(error);
				}
				++writesSinceRetention;
				if (writesSinceRetention >= 100)
				{
					pdw::gateway::RetentionPolicy policy;
					policy.retentionDays = config.retentionDays;
					policy.maximumMegabytes = config.maximumMegabytes;
					int removed = 0;
					if (!store.EnforceRetention(policy, removed, error))
					{
						SetLastError(error);
						EnterCriticalSection(&g_state.lock);
						g_state.accepting = false;
						LeaveCriticalSection(&g_state.lock);
					}
					writesSinceRetention = 0;
				}
				if (store.GetStatistics(statistics, error)) UpdateStatistics(statistics);
				else SetLastError(error);
			}
			long long assigned = 0;
			EnterCriticalSection(&g_state.lock);
			assigned = g_state.nextSequence;
			LeaveCriticalSection(&g_state.lock);
			if (!store.RecordHighestAssignedSequence(assigned, error)) SetLastError(error);
			if (stopping) break;
		}
		pdw::gateway::RetentionPolicy policy;
		policy.retentionDays = config.retentionDays;
		policy.maximumMegabytes = config.maximumMegabytes;
		int removed = 0;
		if (!store.EnforceRetention(policy, removed, error)) SetLastError(error);
		store.Checkpoint(error);
		if (store.GetStatistics(statistics, error)) UpdateStatistics(statistics);
		store.Close();
		return 0;
	}

	void StopWorker()
	{
		EnterCriticalSection(&g_state.lock);
		g_state.accepting = false;
		// Shutdown is bounded by at most the operation already executing. The
		// queued tail becomes explicit sequence gaps instead of delaying decoder
		// shutdown behind database contention.
		g_state.droppedEvents += g_state.queue.size();
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
		if (g_state.readyEvent) CloseHandle(g_state.readyEvent);
		EnterCriticalSection(&g_state.lock);
		g_state.thread = NULL;
		g_state.stopEvent = NULL;
		g_state.workEvent = NULL;
		g_state.readyEvent = NULL;
		g_state.startupSucceeded = false;
		LeaveCriticalSection(&g_state.lock);
	}

	bool StartWorker()
	{
		g_state.stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		g_state.workEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		g_state.readyEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (!g_state.stopEvent || !g_state.workEvent || !g_state.readyEvent)
		{
			SetLastError("Local Gateway Outbox worker events could not be created.");
			StopWorker();
			return false;
		}
		g_state.thread = CreateThread(NULL, 0, WriterThread, NULL, 0, NULL);
		if (!g_state.thread)
		{
			SetLastError("Local Gateway Outbox worker could not start.");
			StopWorker();
			return false;
		}
		if (WaitForSingleObject(g_state.readyEvent, 3000) != WAIT_OBJECT_0)
		{
			SetLastError("Local Gateway Outbox worker startup timed out.");
			StopWorker();
			return false;
		}
		EnterCriticalSection(&g_state.lock);
		const bool success = g_state.startupSucceeded;
		g_state.accepting = success;
		LeaveCriticalSection(&g_state.lock);
		if (!success) StopWorker();
		return success;
	}

	void SetDialogHealth(HWND dialog)
	{
		const GatewayOutboxHealth health = GatewayOutboxGetHealth();
		const std::string text = GatewayOutboxBuildDiagnosticText();
		SetDlgItemTextA(dialog, IDC_GATEWAY_STATUS, text.c_str());
		EnableWindow(GetDlgItem(dialog, IDC_GATEWAY_SYNTHETIC_PROTOCOL), health.enabled && health.workerRunning);
		EnableWindow(GetDlgItem(dialog, IDC_GATEWAY_GENERATE_SYNTHETIC), health.enabled && health.workerRunning);
	}

	bool ReadUnsigned(HWND dialog, int id, unsigned int minimum,
		unsigned int maximum, unsigned int& value)
	{
		BOOL translated = FALSE;
		const UINT parsed = GetDlgItemInt(dialog, id, &translated, FALSE);
		if (!translated || parsed < minimum || parsed > maximum) return false;
		value = parsed;
		return true;
	}
}

GatewayOutboxHealth::GatewayOutboxHealth() : enabled(false), workerRunning(false),
	queueDepth(0), queueHighWaterMark(0), droppedEvents(0), writeFailures(0),
	highestAssignedSequence(0), lastCommittedSequence(0), oldestRetainedSequence(0),
	retainedRecords(0), databaseBytes(0), availableDiskBytes(0), diskWarning(false)
{
}

void GatewayOutboxInitialize(void) { GatewayOutboxSettingsChanged(); }

void GatewayOutboxShutdown(void) { StopWorker(); }

void GatewayOutboxSettingsChanged(void)
{
	StopWorker();
	const OutboxConfig config = ConfigFromProfile();
	EnterCriticalSection(&g_state.lock);
	g_state.config = config;
	g_state.lastError.clear();
	LeaveCriticalSection(&g_state.lock);
	if (config.enabled) StartWorker();
}

void GatewayOutboxPublishDecodedMessage(const DecodedMessageNotificationContext& context)
{
	// This authoritative local feed intentionally ignores per-capcode output
	// destinations. Match, reject and duplicate outcomes remain event metadata.
	QueueEvent(BuildEvent(context, false), NULL);
}

bool GatewayOutboxGenerateSynthetic(const char* protocol, std::string& error)
{
	const bool flex = protocol && _stricmp(protocol, "FLEX") == 0;
	DecodedMessageNotificationContext context;
	context.address = flex ? "123456789" : "1234567";
	context.time = "12:34:56";
	context.date = "01-01-30";
	context.mode = flex ? "FLEX" : "POCSAG-1200";
	context.messageType = "ALPHA";
	context.bitrate = flex ? "1600" : "1200";
	context.message = flex ? "SYNTHETIC PDW GATEWAY TEST - FLEX" :
		"SYNTHETIC PDW GATEWAY TEST - POCSAG";
	context.cycle = flex ? 7 : -1;
	context.frame = flex ? 42 : -1;
	return QueueEvent(BuildEvent(context, true), &error);
}

GatewayOutboxHealth GatewayOutboxGetHealth(void)
{
	GatewayOutboxHealth health;
	EnterCriticalSection(&g_state.lock);
	health.enabled = g_state.config.enabled;
	health.workerRunning = g_state.thread != NULL && g_state.accepting;
	health.queueDepth = static_cast<unsigned long>(g_state.queue.size());
	health.queueHighWaterMark = g_state.queueHighWaterMark;
	health.droppedEvents = g_state.droppedEvents;
	health.writeFailures = g_state.writeFailures;
	health.highestAssignedSequence = g_state.nextSequence;
	health.lastCommittedSequence = g_state.lastCommittedSequence;
	health.oldestRetainedSequence = g_state.oldestRetainedSequence;
	health.retainedRecords = g_state.retainedRecords;
	health.databaseBytes = g_state.databaseBytes;
	health.availableDiskBytes = g_state.availableDiskBytes;
	health.diskWarning = g_state.diskWarning;
	health.path = ResolvePath(g_state.config.path);
	health.lastError = g_state.lastError;
	LeaveCriticalSection(&g_state.lock);
	return health;
}

std::string GatewayOutboxBuildDiagnosticText(void)
{
	const GatewayOutboxHealth health = GatewayOutboxGetHealth();
	std::ostringstream text;
	text << (health.enabled ? (health.workerRunning ? "Enabled" : "Enabled - unavailable") : "Disabled")
		<< " | queue " << health.queueDepth << " | high-water " << health.queueHighWaterMark
		<< " | assigned " << health.highestAssignedSequence
		<< " | committed " << health.lastCommittedSequence
		<< " | oldest " << health.oldestRetainedSequence
		<< " | drops/gaps " << health.droppedEvents
		<< " | write failures " << health.writeFailures
		<< "\r\nRecords " << health.retainedRecords
		<< " | database " << (health.databaseBytes / (1024ULL * 1024ULL)) << " MB"
		<< " | free disk " << (health.availableDiskBytes / (1024ULL * 1024ULL)) << " MB";
	if (health.diskWarning) text << " | LOW DISK";
	text << "\r\n" << health.path;
	if (!health.lastError.empty()) text << "\r\nLast error: " << health.lastError;
	return text.str();
}

BOOL FAR PASCAL GatewayOutboxDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM)
{
	switch (uMsg)
	{
		case WM_INITDIALOG:
			CheckDlgButton(hDlg, IDC_GATEWAY_ENABLE,
				Profile.gatewayOutboxEnabled ? BST_CHECKED : BST_UNCHECKED);
			SetDlgItemTextA(hDlg, IDC_GATEWAY_PATH, Profile.gatewayOutboxPath);
			SetDlgItemTextA(hDlg, IDC_GATEWAY_RECEIVER_ID, Profile.gatewayReceiverId);
			SetDlgItemInt(hDlg, IDC_GATEWAY_RETENTION_DAYS,
				Profile.gatewayOutboxRetentionDays, FALSE);
			SetDlgItemInt(hDlg, IDC_GATEWAY_MAX_MB,
				Profile.gatewayOutboxMaximumMegabytes, FALSE);
			SetDlgItemInt(hDlg, IDC_GATEWAY_QUEUE_CAPACITY,
				Profile.gatewayOutboxQueueCapacity, FALSE);
			SendDlgItemMessageA(hDlg, IDC_GATEWAY_SYNTHETIC_PROTOCOL, CB_ADDSTRING, 0,
				reinterpret_cast<LPARAM>("POCSAG"));
			SendDlgItemMessageA(hDlg, IDC_GATEWAY_SYNTHETIC_PROTOCOL, CB_ADDSTRING, 0,
				reinterpret_cast<LPARAM>("FLEX"));
			SendDlgItemMessage(hDlg, IDC_GATEWAY_SYNTHETIC_PROTOCOL, CB_SETCURSEL, 0, 0);
			SetDialogHealth(hDlg);
			SetTimer(hDlg, 1, 1000, NULL);
			return TRUE;

		case WM_TIMER:
			SetDialogHealth(hDlg);
			return TRUE;

		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
				case IDC_GATEWAY_GENERATE_SYNTHETIC:
				{
					const int selectedProtocol = static_cast<int>(SendDlgItemMessage(hDlg,
						IDC_GATEWAY_SYNTHETIC_PROTOCOL, CB_GETCURSEL, 0, 0));
					std::string error;
					if (!GatewayOutboxGenerateSynthetic(selectedProtocol == 1 ? "FLEX" : "POCSAG", error))
						MessageBoxA(hDlg, error.c_str(), "Local Gateway Outbox", MB_OK | MB_ICONWARNING);
					SetDialogHealth(hDlg);
					return TRUE;
				}

				case IDOK:
				{
					char path[GATEWAY_OUTBOX_PATH_LEN + 1] = {};
					char receiver[GATEWAY_RECEIVER_ID_LEN + 1] = {};
					GetDlgItemTextA(hDlg, IDC_GATEWAY_PATH, path, sizeof(path));
					GetDlgItemTextA(hDlg, IDC_GATEWAY_RECEIVER_ID, receiver, sizeof(receiver));
					unsigned int retention = 0, maximum = 0, capacity = 0;
					const bool enabling = IsDlgButtonChecked(hDlg, IDC_GATEWAY_ENABLE) == BST_CHECKED;
					if (!path[0] || (enabling && !receiver[0]) ||
						!ReadUnsigned(hDlg, IDC_GATEWAY_RETENTION_DAYS, 1, 3650, retention) ||
						!ReadUnsigned(hDlg, IDC_GATEWAY_MAX_MB, 16, 102400, maximum) ||
						!ReadUnsigned(hDlg, IDC_GATEWAY_QUEUE_CAPACITY, 16, 65536, capacity))
					{
						MessageBoxA(hDlg, "Enter a path, an operator-approved receiver ID when enabling, retention of 1-3650 days, "
							"maximum size of 16-102400 MB, and queue capacity of 16-65536.",
							"Local Gateway Outbox", MB_OK | MB_ICONWARNING);
						return TRUE;
					}
					Profile.gatewayOutboxEnabled = enabling ? 1 : 0;
					strncpy(Profile.gatewayOutboxPath, path, GATEWAY_OUTBOX_PATH_LEN);
					Profile.gatewayOutboxPath[GATEWAY_OUTBOX_PATH_LEN] = '\0';
					strncpy(Profile.gatewayReceiverId, receiver, GATEWAY_RECEIVER_ID_LEN);
					Profile.gatewayReceiverId[GATEWAY_RECEIVER_ID_LEN] = '\0';
					Profile.gatewayOutboxRetentionDays = retention;
					Profile.gatewayOutboxMaximumMegabytes = maximum;
					Profile.gatewayOutboxQueueCapacity = capacity;
					WriteSettings();
					GatewayOutboxSettingsChanged();
					KillTimer(hDlg, 1);
					EndDialog(hDlg, TRUE);
					return TRUE;
				}

				case IDCANCEL:
					KillTimer(hDlg, 1);
					EndDialog(hDlg, FALSE);
					return TRUE;
			}
			break;
	}
	return FALSE;
}

#ifdef PDW_GATEWAY_OUTBOX_TEST_HOOKS
void GatewayOutboxSetWorkerDelayForTest(unsigned long milliseconds)
{
	InterlockedExchange(&g_workerDelayMs, static_cast<LONG>(milliseconds));
}
#endif
