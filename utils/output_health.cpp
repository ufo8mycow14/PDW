#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "headers\output_health.h"
#include "headers\pdw.h"
#include "headers\initapp.h"
#include "headers\resource.h"
#include "decoded_event.h"
#include "output_health_core.h"

namespace
{
	CRITICAL_SECTION g_healthLock;
	bool g_healthInitialized = false;
	volatile LONG g_healthAccepting = 0;
	HWND g_alertWindow = NULL;
	pdw::health::Registry g_health(200);

	bool ValidDestination(int destination)
	{
		return destination >= OUTPUT_HEALTH_SMTP && destination < OUTPUT_HEALTH_DESTINATION_COUNT;
	}

	pdw::health::Destination CoreDestination(int destination)
	{
		return static_cast<pdw::health::Destination>(destination);
	}

	pdw::health::Outcome CoreOutcome(int outcome)
	{
		if (outcome < OUTPUT_HEALTH_INFO || outcome > OUTPUT_HEALTH_DROPPED)
			return pdw::health::OUTCOME_INFO;
		return static_cast<pdw::health::Outcome>(outcome);
	}

	std::string SanitizeSummary(const char* summary)
	{
		std::string result(summary ? summary : "");
		if (result.size() > 240) result.resize(240);
		for (std::string::iterator character = result.begin(); character != result.end(); ++character)
		{
			const unsigned char value = static_cast<unsigned char>(*character);
			if (value < 0x20 || value == 0x7f) *character = ' ';
		}
		while (!result.empty() && result[result.size() - 1] == ' ') result.erase(result.size() - 1);
		return result.empty() ? "No detail supplied." : result;
	}

	void CopyText(char* destination, std::size_t capacity, const std::string& value)
	{
		if (!destination || !capacity) return;
		strncpy(destination, value.c_str(), capacity - 1);
		destination[capacity - 1] = '\0';
	}

	const char* StateName(const pdw::health::DestinationState& state)
	{
		if (!state.enabled) return "Disabled";
		if (state.alertPending) return "Attention";
		if (state.lastOutcome == pdw::health::OUTCOME_FAILURE ||
			state.lastOutcome == pdw::health::OUTCOME_DROPPED) return "Warning";
		if (state.lastOutcome == pdw::health::OUTCOME_SUCCESS) return "Healthy";
		return "Ready";
	}

	void ConfigureHealthList(HWND list)
	{
		ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
		struct Column { const char* title; int width; };
		static const Column columns[] = {
			{"Output", 118}, {"State", 72}, {"OK", 44}, {"Fail", 44}, {"Drop", 44},
			{"Last activity (UTC)", 132}, {"Detail", 240}
		};
		for (int index = 0; index < static_cast<int>(_countof(columns)); ++index)
		{
			LVCOLUMNA column = {};
			column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
			column.pszText = const_cast<char*>(columns[index].title);
			column.cx = columns[index].width;
			column.iSubItem = index;
			ListView_InsertColumn(list, index, &column);
		}
	}

	void RefreshHealthDialog(HWND dialog)
	{
		OUTPUT_HEALTH_SNAPSHOT snapshots[OUTPUT_HEALTH_DESTINATION_COUNT] = {};
		const size_t snapshotCount = OutputHealthGetSnapshots(snapshots, _countof(snapshots));
		HWND list = GetDlgItem(dialog, IDC_HEALTH_LIST);
		ListView_DeleteAllItems(list);
		for (size_t index = 0; index < snapshotCount; ++index)
		{
			LVITEMA item = {};
			item.mask = LVIF_TEXT;
			item.iItem = static_cast<int>(index);
			item.pszText = snapshots[index].name;
			const int row = ListView_InsertItem(list, &item);
			char successes[24], failures[24], dropped[24];
			_snprintf_s(successes, sizeof(successes), _TRUNCATE, "%lu", snapshots[index].successes);
			_snprintf_s(failures, sizeof(failures), _TRUNCATE, "%lu", snapshots[index].failures);
			_snprintf_s(dropped, sizeof(dropped), _TRUNCATE, "%lu", snapshots[index].dropped);
			ListView_SetItemText(list, row, 1, snapshots[index].state);
			ListView_SetItemText(list, row, 2, successes);
			ListView_SetItemText(list, row, 3, failures);
			ListView_SetItemText(list, row, 4, dropped);
			ListView_SetItemText(list, row, 5, snapshots[index].lastUtc);
			ListView_SetItemText(list, row, 6, snapshots[index].summary);
		}

		OUTPUT_HEALTH_EVENT events[100] = {};
		const size_t eventCount = OutputHealthGetHistory(events, _countof(events));
		HWND history = GetDlgItem(dialog, IDC_HEALTH_HISTORY);
		SendMessageA(history, LB_RESETCONTENT, 0, 0);
		for (size_t index = 0; index < eventCount; ++index)
		{
			char line[420];
			_snprintf_s(line, sizeof(line), _TRUNCATE, "%s  %-20s  %-8s  %s",
				events[index].utc, events[index].destinationName,
				events[index].outcomeName, events[index].summary);
			SendMessageA(history, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line));
		}
		SendMessageA(history, LB_SETHORIZONTALEXTENT, 1600, 0);

		const unsigned int alerts = OutputHealthGetPendingAlertCount();
		char summary[256];
		if (alerts)
			_snprintf_s(summary, sizeof(summary), _TRUNCATE,
				"%u output%s need attention. Review the warning rows, then acknowledge when handled.",
				alerts, alerts == 1 ? "" : "s");
		else
			_snprintf_s(summary, sizeof(summary), _TRUNCATE,
				"No output alerts are pending. Counters cover this PDW run only.");
		SetDlgItemTextA(dialog, IDC_HEALTH_SUMMARY, summary);
	}

	bool ApplyAlertPolicy(HWND dialog)
	{
		BOOL translated = FALSE;
		const UINT threshold = GetDlgItemInt(dialog, IDC_HEALTH_THRESHOLD, &translated, FALSE);
		if (!translated || threshold < 1 || threshold > 20)
		{
			MessageBoxA(dialog, "Consecutive failure threshold must be between 1 and 20.",
				"PDW Delivery Health", MB_ICONWARNING);
			return false;
		}
		Profile.outputHealthAlertsEnabled = IsDlgButtonChecked(dialog, IDC_HEALTH_ALERTS) == BST_CHECKED;
		Profile.outputHealthFailureThreshold = threshold;
		OutputHealthConfigure(Profile.outputHealthAlertsEnabled, Profile.outputHealthFailureThreshold);
		WriteSettings();
		RefreshHealthDialog(dialog);
		return true;
	}
}

void OutputHealthInitialize(HWND alertWindow)
{
	if (!g_healthInitialized)
	{
		InitializeCriticalSection(&g_healthLock);
		g_healthInitialized = true;
	}
	EnterCriticalSection(&g_healthLock);
	g_alertWindow = alertWindow;
	InterlockedExchange(&g_healthAccepting, 1);
	LeaveCriticalSection(&g_healthLock);
}

void OutputHealthShutdown(void)
{
	if (!g_healthInitialized) return;
	InterlockedExchange(&g_healthAccepting, 0);
	EnterCriticalSection(&g_healthLock);
	g_alertWindow = NULL;
	LeaveCriticalSection(&g_healthLock);
	// Keep this small process-owned lock valid until process exit. A bounded
	// network shutdown can deliberately leave a stuck worker alive; late calls
	// will now see g_healthAccepting == 0 instead of touching a deleted lock.
}

void OutputHealthConfigure(int alertsEnabled, unsigned int failureThreshold)
{
	if (!g_healthInitialized || !InterlockedCompareExchange(&g_healthAccepting, 0, 0)) return;
	EnterCriticalSection(&g_healthLock);
	if (InterlockedCompareExchange(&g_healthAccepting, 0, 0))
		g_health.ConfigureAlerts(alertsEnabled != 0, failureThreshold);
	LeaveCriticalSection(&g_healthLock);
}

void OutputHealthSetEnabled(int destination, int enabled)
{
	if (!g_healthInitialized || !InterlockedCompareExchange(&g_healthAccepting, 0, 0) ||
		!ValidDestination(destination)) return;
	EnterCriticalSection(&g_healthLock);
	if (InterlockedCompareExchange(&g_healthAccepting, 0, 0))
		g_health.SetEnabled(CoreDestination(destination), enabled != 0,
			pdw::events::CurrentUtcIso8601());
	LeaveCriticalSection(&g_healthLock);
}

void OutputHealthRecord(int destination, int outcome, const char* summary)
{
	if (!g_healthInitialized || !InterlockedCompareExchange(&g_healthAccepting, 0, 0) ||
		!ValidDestination(destination)) return;
	const std::string safeSummary = SanitizeSummary(summary);
	bool alert = false;
	HWND alertWindow = NULL;
	EnterCriticalSection(&g_healthLock);
	if (InterlockedCompareExchange(&g_healthAccepting, 0, 0))
	{
		alert = g_health.Record(CoreDestination(destination), CoreOutcome(outcome),
			pdw::events::CurrentUtcIso8601(), safeSummary);
		alertWindow = g_alertWindow;
	}
	LeaveCriticalSection(&g_healthLock);
	if (alert && alertWindow) PostMessage(alertWindow, OUTPUT_HEALTH_ALERT_MESSAGE,
		static_cast<WPARAM>(destination), 0);
}

size_t OutputHealthGetSnapshots(OUTPUT_HEALTH_SNAPSHOT* snapshots, size_t capacity)
{
	if (!g_healthInitialized || !InterlockedCompareExchange(&g_healthAccepting, 0, 0) ||
		!snapshots || !capacity) return 0;
	const size_t count = (std::min)(capacity, static_cast<size_t>(OUTPUT_HEALTH_DESTINATION_COUNT));
	EnterCriticalSection(&g_healthLock);
	for (size_t index = 0; index < count; ++index)
	{
		const pdw::health::Destination destination = static_cast<pdw::health::Destination>(index);
		const pdw::health::DestinationState& state = g_health.State(destination);
		OUTPUT_HEALTH_SNAPSHOT& snapshot = snapshots[index];
		ZeroMemory(&snapshot, sizeof(snapshot));
		snapshot.destination = static_cast<int>(index);
		snapshot.enabled = state.enabled ? 1 : 0;
		snapshot.successes = state.successes;
		snapshot.failures = state.failures;
		snapshot.dropped = state.dropped;
		snapshot.consecutiveFailures = state.consecutiveFailures;
		snapshot.alertPending = state.alertPending ? 1 : 0;
		CopyText(snapshot.name, sizeof(snapshot.name), pdw::health::DestinationName(destination));
		CopyText(snapshot.state, sizeof(snapshot.state), StateName(state));
		CopyText(snapshot.lastUtc, sizeof(snapshot.lastUtc), state.lastUtc);
		CopyText(snapshot.summary, sizeof(snapshot.summary), state.summary);
	}
	LeaveCriticalSection(&g_healthLock);
	return count;
}

size_t OutputHealthGetHistory(OUTPUT_HEALTH_EVENT* events, size_t capacity)
{
	if (!g_healthInitialized || !InterlockedCompareExchange(&g_healthAccepting, 0, 0) ||
		!events || !capacity) return 0;
	EnterCriticalSection(&g_healthLock);
	const std::vector<pdw::health::Event> history = g_health.History();
	const size_t count = (std::min)(capacity, history.size());
	for (size_t index = 0; index < count; ++index)
	{
		OUTPUT_HEALTH_EVENT& event = events[index];
		ZeroMemory(&event, sizeof(event));
		event.destination = static_cast<int>(history[index].destination);
		event.outcome = static_cast<int>(history[index].outcome);
		CopyText(event.destinationName, sizeof(event.destinationName),
			pdw::health::DestinationName(history[index].destination));
		CopyText(event.outcomeName, sizeof(event.outcomeName), pdw::health::OutcomeName(history[index].outcome));
		CopyText(event.utc, sizeof(event.utc), history[index].utc);
		CopyText(event.summary, sizeof(event.summary), history[index].summary);
	}
	LeaveCriticalSection(&g_healthLock);
	return count;
}

unsigned int OutputHealthGetPendingAlertCount(void)
{
	if (!g_healthInitialized || !InterlockedCompareExchange(&g_healthAccepting, 0, 0)) return 0;
	EnterCriticalSection(&g_healthLock);
	const unsigned int count = g_health.PendingAlertCount();
	LeaveCriticalSection(&g_healthLock);
	return count;
}

void OutputHealthAcknowledgeAll(void)
{
	if (!g_healthInitialized || !InterlockedCompareExchange(&g_healthAccepting, 0, 0)) return;
	EnterCriticalSection(&g_healthLock);
	g_health.AcknowledgeAll();
	LeaveCriticalSection(&g_healthLock);
}

BOOL FAR PASCAL OutputHealthDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM)
{
	const UINT_PTR HEALTH_TIMER = 14;
	switch (uMsg)
	{
		case WM_INITDIALOG:
			CenterWindow(hDlg);
			ConfigureHealthList(GetDlgItem(hDlg, IDC_HEALTH_LIST));
			CheckDlgButton(hDlg, IDC_HEALTH_ALERTS,
				Profile.outputHealthAlertsEnabled ? BST_CHECKED : BST_UNCHECKED);
			SetDlgItemInt(hDlg, IDC_HEALTH_THRESHOLD, Profile.outputHealthFailureThreshold, FALSE);
			RefreshHealthDialog(hDlg);
			SetTimer(hDlg, HEALTH_TIMER, 1000, NULL);
			return TRUE;
		case WM_TIMER:
			if (wParam == HEALTH_TIMER) { RefreshHealthDialog(hDlg); return TRUE; }
			break;
		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
				case IDC_HEALTH_APPLY: ApplyAlertPolicy(hDlg); return TRUE;
				case IDC_HEALTH_ACKNOWLEDGE:
					OutputHealthAcknowledgeAll(); RefreshHealthDialog(hDlg); return TRUE;
				case IDC_HEALTH_REFRESH: RefreshHealthDialog(hDlg); return TRUE;
				case IDOK:
					KillTimer(hDlg, HEALTH_TIMER); EndDialog(hDlg, TRUE); return TRUE;
				case IDCANCEL:
					KillTimer(hDlg, HEALTH_TIMER); EndDialog(hDlg, FALSE); return TRUE;
			}
			break;
		case WM_CLOSE:
			KillTimer(hDlg, HEALTH_TIMER);
			EndDialog(hDlg, FALSE);
			return TRUE;
	}
	return FALSE;
}
