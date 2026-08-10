// Clickable signal meter driven by PDW's real normalized input diagnostics.

#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "headers\pdw.h"
#include "headers\live_signal_meter.h"
#include "headers\settings_center.h"
#include "headers\sound_in.h"
#include "headers\ui_theme.h"

extern bool bPauseFlag;
extern bool bPlayback;
extern int nDriverLoaded;
extern HINSTANCE ghInstance;
extern HWND ghWnd;

namespace
{
	const char kMeterClass[] = "PDWLiveSignalMeter";
	const UINT_PTR kMeterTimer = 1;
	volatile LONG g_legacyActivity = 0;
	volatile LONG g_legacyDirection = 0;

	struct MeterState
	{
		PdwLiveSignalSnapshot snapshot;
		unsigned long long lastSampleCount;
		ULONGLONG lastSampleTick;
		ULONGLONG peakTick;
		LONG lastLegacyActivity;
		float displayedLevel;
		float peakHold;
		bool animationsEnabled;
		bool hovered;
		HWND tooltip;
		char tooltipText[320];
		char stateText[32];
		char sourceText[40];

		MeterState()
			: lastSampleCount(0), lastSampleTick(0), peakTick(0),
			  lastLegacyActivity(0), displayedLevel(0.0f), peakHold(0.0f),
			  animationsEnabled(true), hovered(false), tooltip(NULL)
		{
			ZeroMemory(&snapshot, sizeof(snapshot));
			tooltipText[0] = '\0';
			strcpy(stateText, "No input");
			strcpy(sourceText, "No source");
		}
	};

	float Clamp01(float value)
	{
		return (std::max)(0.0f, (std::min)(1.0f, value));
	}

	float DbNormalized(float value)
	{
		const float db = 20.0f * static_cast<float>(std::log10((std::max)(value, 0.001f)));
		return Clamp01((db + 60.0f) / 60.0f);
	}

	float DbValue(float value)
	{
		return 20.0f * static_cast<float>(std::log10((std::max)(value, 0.001f)));
	}

	bool ClientAnimationsEnabled()
	{
		BOOL enabled = TRUE;
#ifndef SPI_GETCLIENTAREAANIMATION
#define SPI_GETCLIENTAREAANIMATION 0x1042
#endif
		if (!SystemParametersInfo(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0)) return true;
		return enabled != FALSE;
	}

	const char* SourceName(const MeterState& state)
	{
		if (state.snapshot.diagnosticReplay || bPlayback) return "Recording playback";
		switch (state.snapshot.sourceKind)
		{
			case 1: return "Windows audio";
			case 2: return "RTL-TCP";
			case 3: return "RTL-SDR";
			default:
				if (state.snapshot.captureActive) return "Local audio";
				if (nDriverLoaded) return "Slicer / serial";
				if (state.snapshot.configuredSource == AUDIO_SOURCE_RTL_TCP) return "RTL-TCP";
				if (state.snapshot.configuredSource == AUDIO_SOURCE_RTL_SDR) return "RTL-SDR";
				return "No source";
		}
	}

	void UpdateTooltip(HWND window, MeterState& state)
	{
		const float db = DbValue(state.snapshot.rmsLevel);
		snprintf(state.tooltipText, sizeof(state.tooltipText),
			"%s - %s, level %.0f dBFS, quality %.0f%%, clipping %.2f%%. %s%s%s"
			"Click to open Signal & radio settings.",
			state.stateText, state.sourceText, db, state.snapshot.signalQuality,
			state.snapshot.clippingPercent,
			state.snapshot.receiverStatus[0] ? state.snapshot.receiverStatus : "",
			state.snapshot.lastReceiverError[0] ? ". Last error: " : ". ",
			state.snapshot.lastReceiverError);
		SetWindowTextA(window, state.tooltipText);
		if (state.tooltip)
		{
			TOOLINFOA info;
			ZeroMemory(&info, sizeof(info));
			info.cbSize = sizeof(info);
			info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
			info.hwnd = window;
			info.uId = reinterpret_cast<UINT_PTR>(window);
			info.lpszText = state.tooltipText;
			SendMessageA(state.tooltip, TTM_UPDATETIPTEXTA, 0,
				reinterpret_cast<LPARAM>(&info));
		}
	}

	void UpdateState(HWND window, MeterState& state)
	{
		SignalDiagnosticsGetLiveSnapshot(&state.snapshot);
		const ULONGLONG now = GetTickCount64();
		const bool samplesAdvanced = state.snapshot.sampleCount != state.lastSampleCount;
		if (samplesAdvanced)
		{
			state.lastSampleCount = state.snapshot.sampleCount;
			state.lastSampleTick = now;
		}
		const LONG legacyActivity = InterlockedCompareExchange(&g_legacyActivity, 0, 0);
		const bool legacyAdvanced = legacyActivity != state.lastLegacyActivity;
		if (legacyAdvanced)
		{
			state.lastLegacyActivity = legacyActivity;
			state.lastSampleTick = now;
		}

		const bool sourceActive = state.snapshot.captureActive ||
			state.snapshot.diagnosticReplay || bPlayback || nDriverLoaded;
		const ULONGLONG sinceSamples = state.lastSampleTick ? now - state.lastSampleTick : ~0ULL;
		const bool configuredReceiver =
			state.snapshot.configuredSource == AUDIO_SOURCE_RTL_TCP ||
			state.snapshot.configuredSource == AUDIO_SOURCE_RTL_SDR;
		const bool rtlSdrIqFresh = state.snapshot.configuredSource != AUDIO_SOURCE_RTL_SDR ||
			(state.snapshot.lastIqCallbackTick && state.snapshot.lastIqAgeMs <= 5000);
		if (bPauseFlag)
			strcpy(state.stateText, "Paused");
		else if (state.snapshot.diagnosticReplay || bPlayback)
			strcpy(state.stateText, "Playback");
		else if (sourceActive && sinceSamples <= 1200 && rtlSdrIqFresh)
			strcpy(state.stateText, "Live input");
		else if (configuredReceiver || (sourceActive && sinceSamples <= 5000))
			strcpy(state.stateText, "Reconnecting");
		else
			strcpy(state.stateText, "No input");
		strncpy(state.sourceText, SourceName(state), sizeof(state.sourceText) - 1);
		state.sourceText[sizeof(state.sourceText) - 1] = '\0';

		float level = DbNormalized(state.snapshot.rmsLevel);
		float peak = DbNormalized(state.snapshot.peakLevel);
		if (legacyAdvanced && !state.snapshot.captureActive)
		{
			level = InterlockedCompareExchange(&g_legacyDirection, 0, 0) ? 0.72f : 0.52f;
			peak = level;
		}
		if (bPauseFlag || strcmp(state.stateText, "No input") == 0)
			level = 0.0f;

		if (!state.animationsEnabled)
			state.displayedLevel = level;
		else
			state.displayedLevel += (level - state.displayedLevel) *
				(level > state.displayedLevel ? 0.42f : 0.13f);
		if (peak >= state.peakHold)
		{
			state.peakHold = peak;
			state.peakTick = now;
		}
		else if (now - state.peakTick > 400)
		{
			state.peakHold = (std::max)(0.0f, state.peakHold -
				(state.animationsEnabled ? 0.025f : 0.08f));
		}
		UpdateTooltip(window, state);
		InvalidateRect(window, NULL, FALSE);
	}

	COLORREF MeterAccent(const MeterState& state)
	{
		if (state.snapshot.clippingPercent >= 1.0f) return RGB(220, 68, 55);
		if (strcmp(state.stateText, "No input") == 0) return PdwThemeMutedTextColor();
		if (strcmp(state.stateText, "Reconnecting") == 0) return RGB(218, 145, 0);
		return PdwThemeAccentColor();
	}

	COLORREF BlendColour(COLORREF first, COLORREF second, int secondPercent)
	{
		const int firstPercent = 100 - secondPercent;
		return RGB((GetRValue(first) * firstPercent + GetRValue(second) * secondPercent) / 100,
			(GetGValue(first) * firstPercent + GetGValue(second) * secondPercent) / 100,
			(GetBValue(first) * firstPercent + GetBValue(second) * secondPercent) / 100);
	}

	const char* MeterLabel(const MeterState& state)
	{
		if (strcmp(state.stateText, "Live input") == 0) return "LIVE INPUT";
		if (strcmp(state.stateText, "Paused") == 0) return "PAUSED";
		if (strcmp(state.stateText, "Playback") == 0) return "PLAYBACK";
		if (strcmp(state.stateText, "Reconnecting") == 0) return "RECONNECTING";
		return "NO INPUT";
	}

	COLORREF MeterStatusColour(const MeterState& state)
	{
		if (state.snapshot.clippingPercent >= 1.0f) return RGB(255, 112, 102);
		if (strcmp(state.stateText, "Live input") == 0) return RGB(108, 203, 95);
		if (strcmp(state.stateText, "Reconnecting") == 0) return RGB(247, 195, 95);
		return PdwThemeMutedTextColor();
	}

	void PaintMeter(HWND window, MeterState& state)
	{
		PAINTSTRUCT paint;
		HDC dc = BeginPaint(window, &paint);
		RECT bounds;
		GetClientRect(window, &bounds);
		if (bounds.right <= 0 || bounds.bottom <= 0)
		{
			EndPaint(window, &paint);
			return;
		}

		HDC memory = CreateCompatibleDC(dc);
		HBITMAP bitmap = CreateCompatibleBitmap(dc, bounds.right, bounds.bottom);
		HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
		const COLORREF panelColour = state.hovered ?
			BlendColour(PdwThemeSurfaceColor(), PdwThemeAccentColor(), 12) :
			PdwThemeSurfaceColor();
		HBRUSH background = CreateSolidBrush(panelColour);
		FillRect(memory, &bounds, background);
		DeleteObject(background);

		HPEN border = CreatePen(PS_SOLID, 1,
			state.hovered ? PdwThemeAccentColor() : PdwThemeBorderColor());
		HGDIOBJ oldPen = SelectObject(memory, border);
		HGDIOBJ oldBrush = SelectObject(memory, GetStockObject(NULL_BRUSH));
		RoundRect(memory, bounds.left, bounds.top, bounds.right, bounds.bottom, 6, 6);
		SelectObject(memory, oldBrush);
		SelectObject(memory, oldPen);
		DeleteObject(border);

		const COLORREF accent = MeterAccent(state);
		const bool compactHeight = bounds.bottom < 32;
		const bool narrowMeter = bounds.right < 220;
		const int textWidth = narrowMeter ? 82 : (compactHeight ? 148 : 174);
		const int waveLeft = textWidth;
		const int qualityWidth = narrowMeter ? 34 : 40;
		const int qualityRight = bounds.right - 18;
		const int qualityLeft = (std::max)(waveLeft + 8, qualityRight - qualityWidth);

		SetBkMode(memory, TRANSPARENT);
		SetTextColor(memory, MeterStatusColour(state));
		SelectObject(memory, PdwThemeUiSemiboldFont());
		char primaryText[96];
		if (compactHeight || narrowMeter)
			snprintf(primaryText, sizeof(primaryText), "%s  %.0f%%",
				MeterLabel(state), state.snapshot.signalQuality);
		else
			strncpy(primaryText, MeterLabel(state), sizeof(primaryText));
		primaryText[sizeof(primaryText) - 1] = '\0';
		RECT stateRect = { 9, compactHeight || narrowMeter ? 1 : 3, textWidth - 3,
			compactHeight || narrowMeter ? bounds.bottom - 1 : 22 };
		DrawTextA(memory, primaryText, -1, &stateRect,
			DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
		if (!compactHeight && !narrowMeter)
		{
			SetTextColor(memory, PdwThemeMutedTextColor());
			SelectObject(memory, PdwThemeUiFont());
			char detail[96];
			if (bounds.right < 220)
				snprintf(detail, sizeof(detail), "%.0f dBFS", DbValue(state.snapshot.rmsLevel));
			else
				snprintf(detail, sizeof(detail), "%s  \xB7  %.0f dBFS", state.sourceText,
					DbValue(state.snapshot.rmsLevel));
			RECT detailRect = { 9, 21, textWidth - 3, bounds.bottom - 4 };
			DrawTextA(memory, detail, -1, &detailRect,
				DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
		}

		RECT waveform = { waveLeft + 8, 6, qualityLeft - 4, bounds.bottom - 6 };
		const int waveWidth = (std::max)(0,
			static_cast<int>(waveform.right - waveform.left));
		const int barCount = waveWidth < 24 ? 0 : (waveWidth < 72 ? 6 : 12);
		const int barGap = 3;
		const int barWidth = barCount ? (std::max)(2,
			(waveWidth - (barCount - 1) * barGap) / barCount) : 0;
		const int waveHeight = (std::max)(4,
			static_cast<int>(waveform.bottom - waveform.top));
		const float sampleScale = 1.0f / (std::max)(0.002f,
			state.snapshot.peakLevel * 1.15f);
		const float visibleLevel = 0.28f + state.displayedLevel * 0.72f;
		HBRUSH barBrush = CreateSolidBrush(accent);
		oldBrush = SelectObject(memory, barBrush);
		oldPen = SelectObject(memory, GetStockObject(NULL_PEN));
		for (int bar = 0; bar < barCount; ++bar)
		{
			float amplitude = 0.06f;
			if (state.snapshot.waveformCount && !bPauseFlag &&
				strcmp(state.stateText, "No input") != 0)
			{
				const unsigned int start = state.snapshot.waveformCount * bar / barCount;
				const unsigned int end = state.snapshot.waveformCount * (bar + 1) / barCount;
				float segmentPeak = 0.0f;
				for (unsigned int sample = start; sample < end; ++sample)
					segmentPeak = (std::max)(segmentPeak,
						static_cast<float>(std::fabs(state.snapshot.waveform[sample])));
				const unsigned int centre = (std::min)(state.snapshot.waveformCount - 1,
					start + (end - start) / 2);
				const float centreSample = static_cast<float>(
					std::fabs(state.snapshot.waveform[centre]));
				amplitude = (std::max)(amplitude,
					(centreSample * 0.72f + segmentPeak * 0.28f) *
						sampleScale * visibleLevel);
			}
			amplitude = Clamp01(amplitude);
			const int barHeight = (std::max)(3,
				static_cast<int>(3.0f + amplitude * (waveHeight - 3)));
			const int x = waveform.left + bar * (barWidth + barGap);
			RECT barRect = { x, waveform.top + (waveHeight - barHeight) / 2,
				x + barWidth, waveform.top + (waveHeight + barHeight) / 2 };
			RoundRect(memory, barRect.left, barRect.top, barRect.right, barRect.bottom, 3, 3);
		}
		SelectObject(memory, oldPen);
		SelectObject(memory, oldBrush);
		DeleteObject(barBrush);

		if (!compactHeight && !narrowMeter)
		{
			SetTextColor(memory, MeterStatusColour(state));
			SelectObject(memory, PdwThemeUiSemiboldFont());
			char quality[16];
			snprintf(quality, sizeof(quality), "%.0f%%", state.snapshot.signalQuality);
			RECT qualityRect = { qualityLeft, 5, qualityRight, bounds.bottom - 5 };
			DrawTextA(memory, quality, -1, &qualityRect,
				DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);
		}

		RECT peakTrack = { bounds.right - 13, 5, bounds.right - 8, bounds.bottom - 5 };
		HBRUSH trackBrush = CreateSolidBrush(PdwThemeBorderColor());
		oldBrush = SelectObject(memory, trackBrush);
		oldPen = SelectObject(memory, GetStockObject(NULL_PEN));
		RoundRect(memory, peakTrack.left, peakTrack.top, peakTrack.right,
			peakTrack.bottom, 4, 4);
		SelectObject(memory, oldBrush);
		DeleteObject(trackBrush);
		const int peakHeight = static_cast<int>((peakTrack.bottom - peakTrack.top) * state.peakHold);
		RECT peakFill = { peakTrack.left, peakTrack.bottom - peakHeight,
			peakTrack.right, peakTrack.bottom };
		HBRUSH peakBrush = CreateSolidBrush(state.snapshot.clippingPercent >= 1.0f ?
			RGB(255, 112, 102) : RGB(108, 203, 95));
		SelectObject(memory, peakBrush);
		RoundRect(memory, peakFill.left, peakFill.top, peakFill.right,
			peakFill.bottom, 4, 4);
		SelectObject(memory, oldBrush);
		SelectObject(memory, oldPen);
		DeleteObject(peakBrush);

		BitBlt(dc, 0, 0, bounds.right, bounds.bottom, memory, 0, 0, SRCCOPY);
		SelectObject(memory, oldBitmap);
		DeleteObject(bitmap);
		DeleteDC(memory);
		EndPaint(window, &paint);
	}

	void OpenSignalSettings(HWND window)
	{
		HWND mainWindow = GetParent(GetParent(window));
		if (!mainWindow) mainWindow = ghWnd;
		ShowSettingsCenter(mainWindow, PDW_SETTINGS_SIGNAL);
	}

	LRESULT CALLBACK MeterWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		MeterState* state = reinterpret_cast<MeterState*>(GetWindowLongPtr(window, GWLP_USERDATA));
		switch (message)
		{
			case WM_CREATE:
			{
				state = new MeterState();
				state->animationsEnabled = ClientAnimationsEnabled();
				SetWindowLongPtr(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
				state->tooltip = CreateWindowExA(WS_EX_TOPMOST, TOOLTIPS_CLASSA, NULL,
					WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
					CW_USEDEFAULT, CW_USEDEFAULT, window, NULL, ghInstance, NULL);
				if (state->tooltip)
				{
					TOOLINFOA info;
					ZeroMemory(&info, sizeof(info));
					info.cbSize = sizeof(info);
					info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
					info.hwnd = window;
					info.uId = reinterpret_cast<UINT_PTR>(window);
					info.lpszText = state->tooltipText;
					SendMessageA(state->tooltip, TTM_ADDTOOLA, 0,
						reinterpret_cast<LPARAM>(&info));
				}
				SetTimer(window, kMeterTimer, state->animationsEnabled ? 40 : 100, NULL);
				UpdateState(window, *state);
				return 0;
			}

			case WM_TIMER:
				if (state && wParam == kMeterTimer) UpdateState(window, *state);
				return 0;

			case WM_PAINT:
				if (state) PaintMeter(window, *state);
				return 0;

			case WM_ERASEBKGND:
				return TRUE;

			case WM_MOUSEMOVE:
				if (state && !state->hovered)
				{
					state->hovered = true;
					TRACKMOUSEEVENT tracking;
					ZeroMemory(&tracking, sizeof(tracking));
					tracking.cbSize = sizeof(tracking);
					tracking.dwFlags = TME_LEAVE;
					tracking.hwndTrack = window;
					TrackMouseEvent(&tracking);
					InvalidateRect(window, NULL, FALSE);
				}
				return 0;

			case WM_MOUSELEAVE:
				if (state)
				{
					state->hovered = false;
					InvalidateRect(window, NULL, FALSE);
				}
				return 0;

			case WM_LBUTTONUP:
				OpenSignalSettings(window);
				return 0;

			case WM_KEYUP:
				if (wParam == VK_RETURN || wParam == VK_SPACE)
				{
					OpenSignalSettings(window);
					return 0;
				}
				break;

			case WM_GETDLGCODE:
				return DLGC_WANTCHARS;

			case WM_SETCURSOR:
				SetCursor(LoadCursor(NULL, IDC_HAND));
				return TRUE;

			case WM_SETTINGCHANGE:
				if (state) state->animationsEnabled = ClientAnimationsEnabled();
				InvalidateRect(window, NULL, TRUE);
				return 0;

			case WM_DESTROY:
				KillTimer(window, kMeterTimer);
				if (state)
				{
					if (state->tooltip) DestroyWindow(state->tooltip);
					delete state;
				}
				SetWindowLongPtr(window, GWLP_USERDATA, 0);
				return 0;
		}
		return DefWindowProc(window, message, wParam, lParam);
	}

	bool RegisterMeterClass()
	{
		WNDCLASSEXA windowClass;
		ZeroMemory(&windowClass, sizeof(windowClass));
		windowClass.cbSize = sizeof(windowClass);
		windowClass.style = CS_HREDRAW | CS_VREDRAW;
		windowClass.lpfnWndProc = MeterWindowProc;
		windowClass.hInstance = ghInstance;
		windowClass.hCursor = LoadCursor(NULL, IDC_HAND);
		windowClass.lpszClassName = kMeterClass;
		if (RegisterClassExA(&windowClass)) return true;
		return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
	}
}

HWND LiveSignalMeterCreate(HWND parent, UINT controlId)
{
	if (!parent || !RegisterMeterClass()) return NULL;
	return CreateWindowExA(0, kMeterClass, "Live signal", WS_CHILD | WS_VISIBLE |
		WS_TABSTOP, 0, 0, 0, 0, parent,
		reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)), ghInstance, NULL);
}

void LiveSignalMeterMove(HWND meter, int x, int y, int width, int height)
{
	if (!meter) return;
	MoveWindow(meter, x, y, (std::max)(0, width), (std::max)(0, height), TRUE);
	ShowWindow(meter, width >= 150 && height >= 34 ? SW_SHOW : SW_HIDE);
}

void LiveSignalMeterRefreshTheme(HWND meter)
{
	if (meter) InvalidateRect(meter, NULL, TRUE);
}

void LiveSignalMeterNoteLegacyActivity(int direction)
{
	InterlockedExchange(&g_legacyDirection, direction ? 1 : 0);
	InterlockedIncrement(&g_legacyActivity);
}

int LiveSignalMeterPreferredWidth(void)
{
	return 292;
}
