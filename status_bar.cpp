// Quiet, clickable state summary beneath the monitor panes.

#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "headers\pdw.h"
#include "headers\output_health.h"
#include "headers\settings_center.h"
#include "headers\sound_in.h"
#include "headers\status_bar.h"
#include "headers\ui_theme.h"

extern bool bPauseFlag;
extern bool bPlayback;
extern bool bRecording;
extern int nDriverLoaded;

namespace
{
	const char kStatusClass[] = "PDWModernStatusBar";
	const UINT_PTR kRefreshTimer = 1;

	struct StatusState
	{
		char source[80];
		char decoder[80];
		char capture[80];
		char outputs[80];
		int visibleParts;

		StatusState() : visibleParts(4)
		{
			source[0] = decoder[0] = capture[0] = outputs[0] = '\0';
		}
	};

	int WindowDpi(HWND window)
	{
		typedef UINT (WINAPI* GetDpiForWindowProc)(HWND);
		HMODULE user32 = GetModuleHandleA("user32.dll");
		GetDpiForWindowProc getDpi = user32 ? reinterpret_cast<GetDpiForWindowProc>(
			GetProcAddress(user32, "GetDpiForWindow")) : NULL;
		return getDpi ? static_cast<int>(getDpi(window)) : 96;
	}

	const char* SourceName(const PdwLiveSignalSnapshot& snapshot)
	{
		if (snapshot.diagnosticReplay || bPlayback) return "Playback";
		switch (snapshot.sourceKind)
		{
			case 1: return "Windows audio";
			case 2: return "RTL-TCP";
			case 3: return "RTL-SDR";
			default:
				if (snapshot.captureActive) return "Local audio";
				if (nDriverLoaded) return "Slicer / serial";
				if (snapshot.configuredSource == AUDIO_SOURCE_RTL_TCP) return "RTL-TCP";
				if (snapshot.configuredSource == AUDIO_SOURCE_RTL_SDR) return "RTL-SDR";
				return "No input";
		}
	}

	const char* DecoderName()
	{
		if (Profile.monitor_acars) return "ACARS";
		if (Profile.monitor_mobitex) return "MOBITEX";
		if (Profile.monitor_ermes) return "ERMES";
		return "POCSAG / FLEX";
	}

	void UpdateState(HWND window, StatusState& state)
	{
		PdwLiveSignalSnapshot signal;
		SignalDiagnosticsGetLiveSnapshot(&signal);
		snprintf(state.source, sizeof(state.source), "Source: %s", SourceName(signal));
		snprintf(state.decoder, sizeof(state.decoder), "Decoder: %s", DecoderName());
		if (bPauseFlag) strcpy(state.capture, "Monitor: Paused");
		else if (signal.diagnosticRecording || bRecording) strcpy(state.capture, "Capture: Recording");
		else if (signal.diagnosticReplay || bPlayback) strcpy(state.capture, "Capture: Playback");
		else if (signal.configuredSource != AUDIO_SOURCE_LOCAL && !signal.captureActive)
			snprintf(state.capture, sizeof(state.capture), "Receiver: Retrying in %.1fs",
				signal.retryInMs / 1000.0);
		else if (signal.configuredSource == AUDIO_SOURCE_RTL_SDR &&
			signal.lastIqCallbackTick && signal.lastIqAgeMs > 5000)
			strcpy(state.capture, "Receiver: IQ stream stale");
		else if (signal.captureActive) strcpy(state.capture, "Capture: Live");
		else strcpy(state.capture, "Monitor: No input");

		const unsigned int pending = OutputHealthGetPendingAlertCount();
		OUTPUT_HEALTH_SNAPSHOT snapshots[OUTPUT_HEALTH_DESTINATION_COUNT];
		const size_t count = OutputHealthGetSnapshots(snapshots, _countof(snapshots));
		unsigned int enabled = 0;
		unsigned int failing = 0;
		for (size_t index = 0; index < count; ++index)
		{
			if (!snapshots[index].enabled) continue;
			enabled++;
			if (snapshots[index].consecutiveFailures) failing++;
		}
		if (pending)
			snprintf(state.outputs, sizeof(state.outputs), "Outputs: %u alert%s", pending,
				pending == 1 ? "" : "s");
		else if (failing)
			snprintf(state.outputs, sizeof(state.outputs), "Outputs: %u need attention", failing);
		else if (enabled)
			strcpy(state.outputs, "Outputs: Healthy");
		else
			strcpy(state.outputs, "Outputs: Not configured");
		InvalidateRect(window, NULL, FALSE);
	}

	void PaintStatus(HWND window, StatusState& state)
	{
		PAINTSTRUCT paint;
		HDC dc = BeginPaint(window, &paint);
		RECT client;
		GetClientRect(window, &client);
		HBRUSH background = CreateSolidBrush(PdwThemeHeaderColor());
		FillRect(dc, &client, background);
		DeleteObject(background);
		HPEN border = CreatePen(PS_SOLID, 1, PdwThemeBorderColor());
		HGDIOBJ oldPen = SelectObject(dc, border);
		MoveToEx(dc, 0, 0, NULL);
		LineTo(dc, client.right, 0);
		SelectObject(dc, oldPen);
		DeleteObject(border);

		state.visibleParts = client.right < 700 ? 3 : 4;
		const char* labels[4] = { state.source, state.decoder, state.capture, state.outputs };
		const int mappingNarrow[3] = { 0, 1, 3 };
		SetBkMode(dc, TRANSPARENT);
		SetTextColor(dc, PdwThemeMutedTextColor());
		SelectObject(dc, PdwThemeUiFont());
		for (int part = 0; part < state.visibleParts; ++part)
		{
			const int sourceIndex = state.visibleParts == 3 ? mappingNarrow[part] : part;
			RECT area;
			area.left = client.right * part / state.visibleParts;
			area.right = client.right * (part + 1) / state.visibleParts;
			area.top = 1;
			area.bottom = client.bottom;
			if (part)
			{
				HPEN divider = CreatePen(PS_SOLID, 1, PdwThemeBorderColor());
				oldPen = SelectObject(dc, divider);
				MoveToEx(dc, area.left, 5, NULL);
				LineTo(dc, area.left, client.bottom - 5);
				SelectObject(dc, oldPen);
				DeleteObject(divider);
			}
			area.left += 10;
			area.right -= 8;
			DrawTextA(dc, labels[sourceIndex], -1, &area,
				DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
		}
		EndPaint(window, &paint);
	}

	void OpenPart(HWND window, StatusState& state, int x)
	{
		RECT client;
		GetClientRect(window, &client);
		const int clientWidth = static_cast<int>(client.right - client.left);
		const int part = (std::min)(state.visibleParts - 1,
			(std::max)(0, x * state.visibleParts / (std::max)(1, clientWidth)));
		if (state.visibleParts == 3)
		{
			if (part == 0) ShowSettingsCenter(GetParent(window), PDW_SETTINGS_SIGNAL);
			else if (part == 1) ShowSettingsCenter(GetParent(window), PDW_SETTINGS_DECODER);
			else ShowSettingsCenter(GetParent(window), PDW_SETTINGS_HEALTH);
		}
		else
		{
			if (part == 0 || part == 2) ShowSettingsCenter(GetParent(window), PDW_SETTINGS_SIGNAL);
			else if (part == 1) ShowSettingsCenter(GetParent(window), PDW_SETTINGS_DECODER);
			else ShowSettingsCenter(GetParent(window), PDW_SETTINGS_HEALTH);
		}
	}

	LRESULT CALLBACK StatusWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		StatusState* state = reinterpret_cast<StatusState*>(GetWindowLongPtr(window, GWLP_USERDATA));
		switch (message)
		{
			case WM_CREATE:
				state = new StatusState();
				SetWindowLongPtr(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
				SetTimer(window, kRefreshTimer, 750, NULL);
				UpdateState(window, *state);
				return 0;

			case WM_TIMER:
				if (state && wParam == kRefreshTimer) UpdateState(window, *state);
				return 0;

			case WM_PAINT:
				if (state) PaintStatus(window, *state);
				return 0;

			case WM_ERASEBKGND:
				return TRUE;

			case WM_LBUTTONUP:
				if (state) OpenPart(window, *state, static_cast<short>(LOWORD(lParam)));
				return 0;

			case WM_SETCURSOR:
				SetCursor(LoadCursor(NULL, IDC_HAND));
				return TRUE;

			case WM_DESTROY:
				KillTimer(window, kRefreshTimer);
				delete state;
				SetWindowLongPtr(window, GWLP_USERDATA, 0);
				return 0;
		}
		return DefWindowProc(window, message, wParam, lParam);
	}

	bool RegisterStatusClass()
	{
		WNDCLASSEXA windowClass;
		ZeroMemory(&windowClass, sizeof(windowClass));
		windowClass.cbSize = sizeof(windowClass);
		windowClass.style = CS_HREDRAW | CS_VREDRAW;
		windowClass.lpfnWndProc = StatusWindowProc;
		windowClass.hInstance = GetModuleHandle(NULL);
		windowClass.hCursor = LoadCursor(NULL, IDC_HAND);
		windowClass.lpszClassName = kStatusClass;
		if (RegisterClassExA(&windowClass)) return true;
		return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
	}
}

HWND PdwStatusBarCreate(HWND parent, UINT controlId)
{
	if (!parent || !RegisterStatusClass()) return NULL;
	return CreateWindowExA(0, kStatusClass, "PDW status", WS_CHILD | WS_VISIBLE,
		0, 0, 0, 0, parent,
		reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)), GetModuleHandle(NULL), NULL);
}

void PdwStatusBarResize(HWND statusBar, int parentWidth, int parentHeight)
{
	if (!statusBar) return;
	const int height = PdwStatusBarHeight(GetParent(statusBar));
	MoveWindow(statusBar, 0, (std::max)(0, parentHeight - height),
		parentWidth, height, TRUE);
}

void PdwStatusBarRefresh(HWND statusBar)
{
	if (statusBar) InvalidateRect(statusBar, NULL, TRUE);
}

int PdwStatusBarHeight(HWND parent)
{
	return MulDiv(27, WindowDpi(parent), 96);
}
