// Persistent, modeless navigation shell for PDW settings.

#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include "headers\resource.h"
#include "headers\pdw.h"
#include "headers\initapp.h"
#include "headers\live_signal_meter.h"
#include "headers\settings_center.h"
#include "headers\startup.h"
#include "headers\ui_theme.h"
#include "headers\version.h"

namespace
{
	const char kSettingsClass[] = "PDWSettingsCenterWindow";

	enum ControlId
	{
		IDC_SC_NAV = 2100,
		IDC_SC_TITLE,
		IDC_SC_SUBTITLE,
		IDC_SC_SEARCH,
		IDC_SC_REVERT,
		IDC_SC_APPLY,
		IDC_SC_STARTUP,
		IDC_SC_STARTUP_STATUS,
		IDC_SC_THEME,
		IDC_SC_THEME_STATUS,
		IDC_SC_SIGNAL_PREVIEW,
		IDC_SC_CARD_FIRST = 2200
	};

	struct PageDefinition
	{
		const char* title;
		const char* subtitle;
	};

	struct SettingAction
	{
		int page;
		UINT command;
		const char* title;
		const char* description;
	};

	const PageDefinition kPages[PDW_SETTINGS_PAGE_COUNT] =
	{
		{ "General", "Startup and everyday PDW behaviour." },
		{ "Appearance", "Choose how PDW follows Windows and presents text." },
		{ "Display", "Monitor columns, scrollback and background behaviour." },
		{ "Decoder", "Protocols, message handling and decoder behaviour." },
		{ "Signal & radio", "Select, test and diagnose the live signal source." },
		{ "Filters", "Control which decoded messages are highlighted or routed." },
		{ "Data outputs", "Configure email, notifications and approved decoded-output destinations." },
		{ "Health & diagnostics", "Check decoder statistics, diagnostics and delivery health." },
		{ "About me", "PDW version, credits and help." }
	};

	const SettingAction kActions[] =
	{
		{ PDW_SETTINGS_GENERAL, IDM_GENERAL, "General behaviour", "Exit confirmation, duplicate handling and common decoder behaviour." },
		{ PDW_SETTINGS_GENERAL, IDM_CONFIG_BACKUP, "Backup / Restore", "Export or restore every setting, filter, frequency, username and saved credentials." },
		{ PDW_SETTINGS_APPEARANCE, IDM_COLOR, "Text and colours", "Adjust monitor colours while retaining the current theme." },
		{ PDW_SETTINGS_APPEARANCE, IDM_FONT, "Monitor font", "Choose the fixed-width font used for decoded messages." },
		{ PDW_SETTINGS_DISPLAY, IDM_SCREENOPTIONS, "View and columns", "Choose the decoded fields shown in each monitor pane." },
		{ PDW_SETTINGS_DISPLAY, IDM_SCROLLBACK, "Scrollback", "Set pane sizes, scrolling speed and retained history." },
		{ PDW_SETTINGS_DISPLAY, IDM_SYSTEMTRAY, "System tray", "Control minimise-to-tray and restore behaviour." },
		{ PDW_SETTINGS_DECODER, IDM_OPTIONS, "Decoder options", "Configure POCSAG, FLEX and message-processing options." },
		{ PDW_SETTINGS_SIGNAL, IDM_SIGNAL_SOURCES, "Signal source, receiver and replay", "Choose and test local audio, serial, network or USB input; record or replay WAV/SigMF signals." },
		{ PDW_SETTINGS_SIGNAL, IDM_INTERFACE, "Legacy input setup", "Configure WinMM audio, serial input and slicer settings." },
		{ PDW_SETTINGS_SIGNAL, IDM_VOLUME, "Windows input volume", "Open the Windows mixer for the selected input." },
		{ PDW_SETTINGS_FILTERS, IDM_FILTERS, "Capcode Directory and filters", "Add, edit and configure matching, display and filter-file behaviour in one place." },
		{ PDW_SETTINGS_DATA_OUTPUTS, IDM_MAIL, "Email", "Configure filtered-message email delivery." },
		{ PDW_SETTINGS_DATA_OUTPUTS, IDM_APPRISE, "Push and Windows notifications", "Route matching messages to Apprise or Windows notifications." },
		{ PDW_SETTINGS_DATA_OUTPUTS, IDM_FTP, "File transfer", "Continuously upload selected files using FTP, FTPS or SFTP." },
		{ PDW_SETTINGS_DATA_OUTPUTS, IDM_PUBLISHING, "Publish to web", "Maintain approved feeds, web files and HTTPS webhooks." },
		{ PDW_SETTINGS_DATA_OUTPUTS, IDM_DATA_OUTPUTS, "Data outputs", "Configure privacy-aware MQTT, database and Telnet destinations." },
		{ PDW_SETTINGS_DATA_OUTPUTS, IDM_GATEWAY_OUTBOX, "Local Gateway Outbox", "Configure the disabled-by-default append-only local gateway handoff." },
		{ PDW_SETTINGS_HEALTH, IDM_OUTPUT_HEALTH, "Delivery health", "Review destination state, failures and pending alerts." },
		{ PDW_SETTINGS_HEALTH, IDM_MONSTAT, "Decoder statistics", "Review message counts, errors and current decoder activity." },
		{ PDW_SETTINGS_HEALTH, IDM_DEBUG, "Diagnostics", "Open PDW's diagnostic information and troubleshooting view." },
		{ PDW_SETTINGS_ABOUT, IDM_ABOUT, "About PDW", "View the PDW version, architecture, capabilities, credits and project links." }
	};

	HWND g_window = NULL;
	HWND g_nav = NULL;
	HWND g_title = NULL;
	HWND g_subtitle = NULL;
	HWND g_search = NULL;
	HWND g_revert = NULL;
	HWND g_apply = NULL;
	HWND g_startup = NULL;
	HWND g_startupStatus = NULL;
	HWND g_theme = NULL;
	HWND g_themeStatus = NULL;
	HWND g_signalPreview = NULL;
	HFONT g_titleFont = NULL;
	HFONT g_bodyFont = NULL;
	HFONT g_smallFont = NULL;
	HFONT g_navFont = NULL;
	HFONT g_navHeadingFont = NULL;
	HFONT g_cardTitleFont = NULL;
	HBRUSH g_backgroundControlBrush = NULL;
	HBRUSH g_surfaceControlBrush = NULL;
	HBRUSH g_headerControlBrush = NULL;
	std::vector<HWND> g_cards;
	std::vector<int> g_visibleActions;
	int g_selectedPage = PDW_SETTINGS_GENERAL;
	int g_draftTheme = PDW_THEME_SYSTEM;
	int g_appliedTheme = PDW_THEME_SYSTEM;
	BOOL g_draftStartup = FALSE;
	BOOL g_appliedStartup = FALSE;
	bool g_dirty = false;
	int g_dpi = 96;
	RECT g_directCard = { 0, 0, 0, 0 };

	int Scale(int value)
	{
		return MulDiv(value, g_dpi, 96);
	}

	int ReadWindowDpi(HWND window)
	{
		typedef UINT (WINAPI* GetDpiForWindowProc)(HWND);
		HMODULE user32 = GetModuleHandleA("user32.dll");
		GetDpiForWindowProc getDpi = user32 ? reinterpret_cast<GetDpiForWindowProc>(
			GetProcAddress(user32, "GetDpiForWindow")) : NULL;
		return getDpi ? static_cast<int>(getDpi(window)) : 96;
	}

	void DeleteFonts()
	{
		if (g_titleFont) DeleteObject(g_titleFont);
		if (g_bodyFont) DeleteObject(g_bodyFont);
		if (g_smallFont) DeleteObject(g_smallFont);
		if (g_navFont) DeleteObject(g_navFont);
		if (g_navHeadingFont) DeleteObject(g_navHeadingFont);
		if (g_cardTitleFont) DeleteObject(g_cardTitleFont);
		g_titleFont = NULL;
		g_bodyFont = NULL;
		g_smallFont = NULL;
		g_navFont = NULL;
		g_navHeadingFont = NULL;
		g_cardTitleFont = NULL;
	}

	HFONT MakeFont(int pointSize, int weight)
	{
		return CreateFontW(-MulDiv(pointSize, g_dpi, 72), 0, 0, 0, weight,
			FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
			CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
			L"Segoe UI");
	}

	void RefreshFonts()
	{
		DeleteFonts();
		// The approved design specifies a 24-pixel, medium-weight heading. At
		// 96 DPI that is 18 points; 24 points was 32 pixels and crowded the
		// subtitle/control bounds during resize.
		g_titleFont = MakeFont(18, FW_MEDIUM);
		g_bodyFont = MakeFont(11, FW_NORMAL);
		g_smallFont = MakeFont(9, FW_NORMAL);
		g_navFont = MakeFont(11, FW_NORMAL);
		g_navHeadingFont = MakeFont(14, FW_SEMIBOLD);
		g_cardTitleFont = MakeFont(11, FW_SEMIBOLD);
		if (g_title) SendMessage(g_title, WM_SETFONT, reinterpret_cast<WPARAM>(g_titleFont), TRUE);
		if (g_nav) SendMessage(g_nav, WM_SETFONT, reinterpret_cast<WPARAM>(g_navFont), TRUE);
		HWND controls[] = { g_subtitle, g_search, g_revert, g_apply, g_startup,
			g_startupStatus, g_theme, g_themeStatus };
		for (int index = 0; index < static_cast<int>(_countof(controls)); ++index)
			if (controls[index]) SendMessage(controls[index], WM_SETFONT,
				reinterpret_cast<WPARAM>(g_bodyFont), TRUE);
		for (std::size_t index = 0; index < g_cards.size(); ++index)
			SendMessage(g_cards[index], WM_SETFONT,
				reinterpret_cast<WPARAM>(g_bodyFont), TRUE);
	}

	void RefreshControlBrushes()
	{
		if (g_backgroundControlBrush) DeleteObject(g_backgroundControlBrush);
		if (g_surfaceControlBrush) DeleteObject(g_surfaceControlBrush);
		if (g_headerControlBrush) DeleteObject(g_headerControlBrush);
		g_backgroundControlBrush = CreateSolidBrush(PdwThemeBackgroundColor());
		g_surfaceControlBrush = CreateSolidBrush(PdwThemeSurfaceColor());
		g_headerControlBrush = CreateSolidBrush(PdwThemeHeaderColor());
	}

	COLORREF Blend(COLORREF first, COLORREF second, int secondPercent)
	{
		const int firstPercent = 100 - secondPercent;
		return RGB((GetRValue(first) * firstPercent + GetRValue(second) * secondPercent) / 100,
			(GetGValue(first) * firstPercent + GetGValue(second) * secondPercent) / 100,
			(GetBValue(first) * firstPercent + GetBValue(second) * secondPercent) / 100);
	}

	std::string Lower(const char* text)
	{
		std::string result = text ? text : "";
		std::transform(result.begin(), result.end(), result.begin(),
			[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
		return result;
	}

	bool ContainsText(const SettingAction& action, const std::string& query)
	{
		return Lower(action.title).find(query) != std::string::npos ||
			Lower(action.description).find(query) != std::string::npos ||
			Lower(kPages[action.page].title).find(query) != std::string::npos;
	}

	void ApplyChildThemes()
	{
		const wchar_t* theme = PdwThemeIsDark() && !PdwThemeIsHighContrast() ?
			L"DarkMode_Explorer" : L"Explorer";
		HWND controls[] = { g_nav, g_search, g_revert, g_apply, g_startup, g_theme };
		for (int index = 0; index < static_cast<int>(_countof(controls)); ++index)
			if (controls[index])
			{
				if (controls[index] == g_startup) SetWindowTheme(controls[index], L"", L"");
				else if (PdwThemeIsDark() && !PdwThemeIsHighContrast() &&
					(controls[index] == g_search || controls[index] == g_theme))
					SetWindowTheme(controls[index], L"DarkMode_CFD", NULL);
				else SetWindowTheme(controls[index], theme, NULL);
			}
		for (std::size_t index = 0; index < g_cards.size(); ++index)
			SetWindowTheme(g_cards[index], theme, NULL);
	}

	void SetDirty(bool dirty)
	{
		g_dirty = dirty;
		if (g_apply) EnableWindow(g_apply, dirty ? TRUE : FALSE);
		if (g_revert) EnableWindow(g_revert, dirty ? TRUE : FALSE);
	}

	void RefreshDraftControls()
	{
		if (g_startup)
		{
			SendMessage(g_startup, BM_SETCHECK, g_draftStartup ? BST_CHECKED : BST_UNCHECKED, 0);
			InvalidateRect(g_startup, NULL, TRUE);
			SetWindowTextA(g_startupStatus, g_draftStartup ?
				"PDW will start 5 seconds after you sign in." :
				"PDW will only start when you open it.");
		}
		if (g_theme)
		{
			SendMessage(g_theme, CB_SETCURSEL, g_draftTheme, 0);
			SetWindowTextA(g_themeStatus, g_draftTheme == PDW_THEME_SYSTEM ?
				"PDW will change automatically with Windows." :
				"PDW will keep this appearance until you change it.");
		}
	}

	bool ApplyDraft(HWND owner)
	{
		if (g_draftStartup != g_appliedStartup)
		{
			char error[384] = { 0 };
			if (!SetStartWithWindowsEnabled(g_draftStartup, error, sizeof(error)))
			{
				MessageBoxA(owner, error, "Start with Windows", MB_OK | MB_ICONERROR);
				return false;
			}
			g_appliedStartup = g_draftStartup;
		}
		if (g_draftTheme != g_appliedTheme)
		{
			if (!PdwThemeSetMode(g_draftTheme, owner))
			{
				RefreshDraftControls();
				SetDirty(g_draftStartup != g_appliedStartup ||
					g_draftTheme != g_appliedTheme);
				return false;
			}
			g_appliedTheme = g_draftTheme;
		}
		SetDirty(false);
		SettingsCenterNotifyThemeChanged();
		return true;
	}

	void RevertDraft()
	{
		g_draftStartup = g_appliedStartup;
		g_draftTheme = g_appliedTheme;
		RefreshDraftControls();
		SetDirty(false);
	}

	void DestroyDynamicControls()
	{
		for (std::size_t index = 0; index < g_cards.size(); ++index)
			DestroyWindow(g_cards[index]);
		g_cards.clear();
		g_visibleActions.clear();
		HWND controls[] = { g_startup, g_startupStatus, g_theme, g_themeStatus };
		for (int index = 0; index < static_cast<int>(_countof(controls)); ++index)
			if (controls[index]) DestroyWindow(controls[index]);
		g_startup = NULL;
		g_startupStatus = NULL;
		g_theme = NULL;
		g_themeStatus = NULL;
		if (g_signalPreview) DestroyWindow(g_signalPreview);
		g_signalPreview = NULL;
		SetRectEmpty(&g_directCard);
	}

	void AddCard(int actionIndex)
	{
		const int visibleIndex = static_cast<int>(g_visibleActions.size());
		const int controlId = IDC_SC_CARD_FIRST + visibleIndex;
		g_visibleActions.push_back(actionIndex);
		std::string cardText;
		cardText += kActions[actionIndex].title;
		cardText += "\r\n";
		cardText += kActions[actionIndex].description;
		HWND card = CreateWindowExA(0, "BUTTON", cardText.c_str(), WS_CHILD | WS_VISIBLE |
			WS_TABSTOP | BS_OWNERDRAW,
			0, 0, 0, 0, g_window,
			reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)), ghInstance, NULL);
		if (!card)
		{
			g_visibleActions.pop_back();
			return;
		}
		SendMessage(card, WM_SETFONT, reinterpret_cast<WPARAM>(g_bodyFont), TRUE);
		g_cards.push_back(card);
		InvalidateRect(card, NULL, TRUE);
	}

	void CreateDirectControls()
	{
		if (g_selectedPage == PDW_SETTINGS_GENERAL)
		{
			g_startup = CreateWindowExA(0, "BUTTON", "Start PDW with Windows",
				WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
				0, 0, 0, 0, g_window,
				reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SC_STARTUP)), ghInstance, NULL);
			g_startupStatus = CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE,
				0, 0, 0, 0, g_window,
				reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SC_STARTUP_STATUS)), ghInstance, NULL);
		}
		else if (g_selectedPage == PDW_SETTINGS_APPEARANCE)
		{
			g_theme = CreateWindowExA(0, "COMBOBOX", "",
				WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
				0, 0, 0, 0, g_window,
				reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SC_THEME)), ghInstance, NULL);
			SendMessageA(g_theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Follow Windows"));
			SendMessageA(g_theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Light"));
			SendMessageA(g_theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Dark"));
			g_themeStatus = CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE,
				0, 0, 0, 0, g_window,
				reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SC_THEME_STATUS)), ghInstance, NULL);
		}
		else if (g_selectedPage == PDW_SETTINGS_SIGNAL)
		{
			g_signalPreview = LiveSignalMeterCreate(g_window, IDC_SC_SIGNAL_PREVIEW);
		}
		RefreshDraftControls();
	}

	void LayoutControls()
	{
		if (!g_window) return;
		RECT client;
		GetClientRect(g_window, &client);
		const int width = client.right;
		const int height = client.bottom;
		const bool narrow = width < Scale(860);
		const int navWidth = narrow ? Scale(196) : Scale(244);
		const int margin = Scale(26);
		const int contentLeft = navWidth + margin;
		const int contentWidth = (std::max)(Scale(300), width - contentLeft - margin);
		const int navTop = Scale(108);
		const int availableNavHeight = (std::max)(Scale(380), height - navTop);
		const int navRowHeight = (std::min)(Scale(42),
			(std::max)(Scale(38), availableNavHeight / PDW_SETTINGS_PAGE_COUNT));

		MoveWindow(g_search, Scale(12), Scale(64), navWidth - Scale(24), Scale(34), TRUE);
		MoveWindow(g_nav, 0, navTop, navWidth, height - navTop, TRUE);
		SendMessage(g_nav, LB_SETITEMHEIGHT, 0, navRowHeight);
		MoveWindow(g_title, contentLeft, Scale(22),
			narrow ? contentWidth : contentWidth - Scale(232), Scale(30), TRUE);
		MoveWindow(g_subtitle, contentLeft, Scale(53), contentWidth, Scale(24), TRUE);
		if (narrow)
		{
			MoveWindow(g_revert, width - margin - Scale(200), Scale(94),
				Scale(80), Scale(36), TRUE);
			MoveWindow(g_apply, width - margin - Scale(112), Scale(94),
				Scale(112), Scale(36), TRUE);
		}
		else
		{
			MoveWindow(g_revert, width - margin - Scale(200), Scale(22), Scale(80), Scale(34), TRUE);
			MoveWindow(g_apply, width - margin - Scale(112), Scale(22), Scale(112), Scale(34), TRUE);
		}

		int y = narrow ? Scale(168) : Scale(124);
		if (g_startup || g_theme)
		{
			SetRect(&g_directCard, contentLeft, y, contentLeft + contentWidth, y + Scale(80));
			if (g_startup)
			{
				MoveWindow(g_startup, contentLeft + Scale(16), y + Scale(12), Scale(260), Scale(26), TRUE);
				MoveWindow(g_startupStatus, contentLeft + Scale(16), y + Scale(43),
					contentWidth - Scale(32), Scale(24), TRUE);
			}
			if (g_theme)
			{
				MoveWindow(g_theme, contentLeft + Scale(16), y + Scale(14), Scale(190), Scale(220), TRUE);
				MoveWindow(g_themeStatus, contentLeft + Scale(224), y + Scale(18),
					contentWidth - Scale(240), Scale(24), TRUE);
			}
			y += Scale(88);
		}
		if (g_signalPreview)
		{
			MoveWindow(g_signalPreview, contentLeft, y, contentWidth, Scale(74), TRUE);
			y += Scale(80);
		}

		const int cardHeight = Scale(64);
		const int cardGap = Scale(6);
		for (std::size_t index = 0; index < g_cards.size(); ++index)
		{
			MoveWindow(g_cards[index], contentLeft, y, contentWidth, cardHeight, TRUE);
			RedrawWindow(g_cards[index], NULL, NULL,
				RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
			y += cardHeight + cardGap;
		}
	}

	void RenderPage()
	{
		DestroyDynamicControls();
		char queryText[160] = { 0 };
		GetWindowTextA(g_search, queryText, sizeof(queryText));
		const std::string query = Lower(queryText);

		if (!query.empty())
		{
			SetWindowTextA(g_title, "Search results");
			std::string subtitle = "Settings matching \"" + std::string(queryText) + "\"";
			SetWindowTextA(g_subtitle, subtitle.c_str());
			RECT client;
			GetClientRect(g_window, &client);
			const int contentTop = client.right < Scale(860) ? Scale(168) : Scale(124);
			const int maxCards = (std::max)(1,
				(static_cast<int>(client.bottom) - contentTop) / (std::max)(1, Scale(70)));
			for (int index = 0; index < static_cast<int>(_countof(kActions)); ++index)
			{
				if (ContainsText(kActions[index], query) &&
					static_cast<int>(g_visibleActions.size()) < maxCards)
					AddCard(index);
			}
		}
		else
		{
			SetWindowTextA(g_title, kPages[g_selectedPage].title);
			SetWindowTextA(g_subtitle, kPages[g_selectedPage].subtitle);
			CreateDirectControls();
			for (int index = 0; index < static_cast<int>(_countof(kActions)); ++index)
				if (kActions[index].page == g_selectedPage) AddCard(index);
		}
		RefreshFonts();
		ApplyChildThemes();
		LayoutControls();
		InvalidateRect(g_window, NULL, TRUE);
	}

	void SelectPage(int page)
	{
		if (page < 0 || page >= PDW_SETTINGS_PAGE_COUNT) page = PDW_SETTINGS_GENERAL;
		g_selectedPage = page;
		SetWindowTextA(g_search, "");
		SendMessage(g_nav, LB_SETCURSEL, page, 0);
		RenderPage();
	}

	void LaunchAction(int visibleIndex)
	{
		if (visibleIndex < 0 || visibleIndex >= static_cast<int>(g_visibleActions.size())) return;
		const SettingAction& action = kActions[g_visibleActions[visibleIndex]];
		char queryText[2] = { 0 };
		GetWindowTextA(g_search, queryText, sizeof(queryText));
		if (queryText[0])
		{
			SelectPage(action.page);
			return;
		}
		EnableWindow(g_window, FALSE);
		SendMessage(ghWnd, WM_COMMAND, MAKEWPARAM(action.command, 0), 0);
		if (g_window)
		{
			EnableWindow(g_window, TRUE);
			SetForegroundWindow(g_window);
		}
	}

	void DrawNavigationIcon(HDC dc, int page, const RECT& row, COLORREF colour)
	{
		const int left = row.left + Scale(13);
		const int top = row.top + ((row.bottom - row.top) - Scale(18)) / 2;
		HPEN pen = CreatePen(PS_SOLID, PdwThemeIsHighContrast() ? 2 : 1, colour);
		HGDIOBJ oldPen = SelectObject(dc, pen);
		HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
		const int s = Scale(18);
		switch (page)
		{
			case PDW_SETTINGS_GENERAL:
				Rectangle(dc, left + Scale(2), top + Scale(3), left + s - Scale(2), top + s - Scale(3));
				MoveToEx(dc, left + Scale(5), top + Scale(7), NULL); LineTo(dc, left + s - Scale(5), top + Scale(7));
				MoveToEx(dc, left + Scale(5), top + Scale(11), NULL); LineTo(dc, left + s - Scale(5), top + Scale(11));
				break;
			case PDW_SETTINGS_APPEARANCE:
				Ellipse(dc, left + Scale(5), top + Scale(5), left + s - Scale(5), top + s - Scale(5));
				MoveToEx(dc, left + Scale(9), top, NULL); LineTo(dc, left + Scale(9), top + Scale(4));
				MoveToEx(dc, left + Scale(9), top + s - Scale(4), NULL); LineTo(dc, left + Scale(9), top + s);
				MoveToEx(dc, left, top + Scale(9), NULL); LineTo(dc, left + Scale(4), top + Scale(9));
				MoveToEx(dc, left + s - Scale(4), top + Scale(9), NULL); LineTo(dc, left + s, top + Scale(9));
				break;
			case PDW_SETTINGS_DISPLAY:
				Rectangle(dc, left + Scale(1), top + Scale(2), left + s - Scale(1), top + s - Scale(4));
				MoveToEx(dc, left + Scale(6), top + s - Scale(1), NULL); LineTo(dc, left + s - Scale(6), top + s - Scale(1));
				MoveToEx(dc, left + Scale(9), top + s - Scale(4), NULL); LineTo(dc, left + Scale(9), top + s - Scale(1));
				break;
			case PDW_SETTINGS_DECODER:
				Rectangle(dc, left + Scale(4), top + Scale(4), left + s - Scale(4), top + s - Scale(4));
				for (int offset = 5; offset <= 13; offset += 4)
				{
					MoveToEx(dc, left, top + Scale(offset), NULL); LineTo(dc, left + Scale(4), top + Scale(offset));
					MoveToEx(dc, left + s - Scale(4), top + Scale(offset), NULL); LineTo(dc, left + s, top + Scale(offset));
				}
				break;
			case PDW_SETTINGS_SIGNAL:
				MoveToEx(dc, left + Scale(9), top + Scale(9), NULL); LineTo(dc, left + Scale(9), top + s);
				MoveToEx(dc, left + Scale(5), top + s - Scale(1), NULL); LineTo(dc, left + Scale(13), top + s - Scale(1));
				Arc(dc, left + Scale(4), top + Scale(4), left + Scale(14), top + Scale(14),
					left + Scale(5), top + Scale(5), left + Scale(5), top + Scale(13));
				Arc(dc, left, top, left + s, top + s, left + Scale(3), top + Scale(2), left + Scale(3), top + Scale(16));
				break;
			case PDW_SETTINGS_FILTERS:
				MoveToEx(dc, left + Scale(1), top + Scale(2), NULL); LineTo(dc, left + s - Scale(1), top + Scale(2));
				LineTo(dc, left + Scale(11), top + Scale(9)); LineTo(dc, left + Scale(11), top + Scale(16));
				LineTo(dc, left + Scale(7), top + Scale(14)); LineTo(dc, left + Scale(7), top + Scale(9));
				LineTo(dc, left + Scale(1), top + Scale(2));
				break;
			case PDW_SETTINGS_DATA_OUTPUTS:
				Ellipse(dc, left + Scale(2), top + Scale(2), left + s - Scale(2), top + Scale(7));
				MoveToEx(dc, left + Scale(2), top + Scale(4), NULL); LineTo(dc, left + Scale(2), top + Scale(14));
				MoveToEx(dc, left + s - Scale(2), top + Scale(4), NULL); LineTo(dc, left + s - Scale(2), top + Scale(14));
				Arc(dc, left + Scale(2), top + Scale(11), left + s - Scale(2), top + Scale(17),
					left + Scale(2), top + Scale(14), left + s - Scale(2), top + Scale(14));
				break;
			case PDW_SETTINGS_ABOUT:
				Ellipse(dc, left + Scale(6), top + Scale(2), left + Scale(12), top + Scale(8));
				Arc(dc, left + Scale(3), top + Scale(8), left + Scale(15), top + Scale(18),
					left + Scale(3), top + Scale(15), left + Scale(15), top + Scale(15));
				break;
			default:
				MoveToEx(dc, left, top + Scale(10), NULL); LineTo(dc, left + Scale(4), top + Scale(10));
				LineTo(dc, left + Scale(7), top + Scale(4)); LineTo(dc, left + Scale(11), top + Scale(15));
				LineTo(dc, left + Scale(14), top + Scale(8)); LineTo(dc, left + s, top + Scale(8));
				break;
		}
		SelectObject(dc, oldBrush);
		SelectObject(dc, oldPen);
		DeleteObject(pen);
	}

	void DrawNavigationItem(const DRAWITEMSTRUCT* item)
	{
		if (!item || item->itemID == static_cast<UINT>(-1) ||
			item->itemID >= PDW_SETTINGS_PAGE_COUNT) return;
		RECT bounds = item->rcItem;
		const bool selected = (item->itemState & ODS_SELECTED) != 0;
		const COLORREF background = selected ?
			(PdwThemeIsDark() ? RGB(18, 58, 75) : RGB(229, 243, 255)) :
			PdwThemeHeaderColor();
		HBRUSH brush = CreateSolidBrush(background);
		FillRect(item->hDC, &bounds, brush);
		DeleteObject(brush);
		if (selected)
		{
			RECT accent = bounds;
			accent.right = accent.left + Scale(3);
			brush = CreateSolidBrush(PdwThemeAccentColor());
			FillRect(item->hDC, &accent, brush);
			DeleteObject(brush);
		}
		SetBkMode(item->hDC, TRANSPARENT);
		SetTextColor(item->hDC, PdwThemeTextColor());
		DrawNavigationIcon(item->hDC, static_cast<int>(item->itemID), bounds,
			selected ? PdwThemeAccentColor() : PdwThemeMutedTextColor());
		SelectObject(item->hDC, g_navFont);
		bounds.left += Scale(43);
		DrawTextA(item->hDC, kPages[item->itemID].title, -1, &bounds,
			DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
		if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &item->rcItem);
	}

	void DrawCard(const DRAWITEMSTRUCT* item)
	{
		const int visibleIndex = static_cast<int>(item->CtlID) - IDC_SC_CARD_FIRST;
		if (visibleIndex < 0 || visibleIndex >= static_cast<int>(g_visibleActions.size())) return;
		const SettingAction& action = kActions[g_visibleActions[visibleIndex]];
		RECT bounds = item->rcItem;
		const bool pressed = (item->itemState & ODS_SELECTED) != 0;
		COLORREF background = PdwThemeSurfaceColor();
		if (pressed) background = Blend(background, PdwThemeAccentColor(), PdwThemeIsDark() ? 18 : 9);
		HBRUSH brush = CreateSolidBrush(background);
		FillRect(item->hDC, &bounds, brush);
		DeleteObject(brush);

		HPEN border = CreatePen(PS_SOLID, 1, pressed ? PdwThemeAccentColor() : PdwThemeBorderColor());
		HGDIOBJ oldPen = SelectObject(item->hDC, border);
		HGDIOBJ oldBrush = SelectObject(item->hDC, GetStockObject(NULL_BRUSH));
		RoundRect(item->hDC, bounds.left, bounds.top, bounds.right, bounds.bottom,
			Scale(8), Scale(8));
		SelectObject(item->hDC, oldBrush);
		SelectObject(item->hDC, oldPen);
		DeleteObject(border);

		SetBkMode(item->hDC, TRANSPARENT);
		SetTextColor(item->hDC, PdwThemeTextColor());
		SelectObject(item->hDC, g_cardTitleFont);
		RECT openButton = { bounds.right - Scale(80), bounds.top + Scale(15),
			bounds.right - Scale(14), bounds.bottom - Scale(15) };
		RECT title = bounds;
		title.left += Scale(16);
		title.top += Scale(7);
		title.right = openButton.left - Scale(12);
		title.bottom = title.top + Scale(20);
		DrawTextA(item->hDC, action.title, -1, &title,
			DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

		SetTextColor(item->hDC, PdwThemeMutedTextColor());
		SelectObject(item->hDC, g_bodyFont);
		RECT description = bounds;
		description.left += Scale(16);
		description.top += Scale(27);
		description.right = openButton.left - Scale(12);
		description.bottom -= Scale(5);
		DrawTextA(item->hDC, action.description, -1, &description,
			DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);

		HBRUSH openBrush = CreateSolidBrush(PdwThemeHeaderColor());
		HPEN openPen = CreatePen(PS_SOLID, 1, PdwThemeBorderColor());
		oldBrush = SelectObject(item->hDC, openBrush);
		oldPen = SelectObject(item->hDC, openPen);
		RoundRect(item->hDC, openButton.left, openButton.top, openButton.right,
			openButton.bottom, Scale(5), Scale(5));
		SelectObject(item->hDC, oldPen);
		SelectObject(item->hDC, oldBrush);
		DeleteObject(openBrush);
		DeleteObject(openPen);
		SetTextColor(item->hDC, PdwThemeTextColor());
		SelectObject(item->hDC, g_bodyFont);
		DrawTextA(item->hDC, "Open", -1, &openButton,
			DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
		if (item->itemState & ODS_FOCUS)
		{
			InflateRect(&bounds, -Scale(3), -Scale(3));
			DrawFocusRect(item->hDC, &bounds);
		}
	}

	void DrawStartupButton(const DRAWITEMSTRUCT* item)
	{
		if (!item) return;
		RECT bounds = item->rcItem;
		HBRUSH background = CreateSolidBrush(PdwThemeSurfaceColor());
		FillRect(item->hDC, &bounds, background);
		DeleteObject(background);
		RECT box = { bounds.left + Scale(1), bounds.top + Scale(3),
			bounds.left + Scale(17), bounds.top + Scale(19) };
		HPEN border = CreatePen(PS_SOLID, 1, g_draftStartup ?
			PdwThemeAccentColor() : PdwThemeBorderColor());
		HGDIOBJ oldPen = SelectObject(item->hDC, border);
		HBRUSH boxBrush = CreateSolidBrush(g_draftStartup ?
			PdwThemeAccentColor() : PdwThemeSurfaceColor());
		HGDIOBJ oldBrush = SelectObject(item->hDC, boxBrush);
		Rectangle(item->hDC, box.left, box.top, box.right, box.bottom);
		SelectObject(item->hDC, oldBrush);
		DeleteObject(boxBrush);
		if (g_draftStartup)
		{
			HPEN check = CreatePen(PS_SOLID, 2, PdwThemeIsDark() ? RGB(20, 20, 20) : RGB(255, 255, 255));
			SelectObject(item->hDC, check);
			MoveToEx(item->hDC, box.left + Scale(3), box.top + Scale(8), NULL);
			LineTo(item->hDC, box.left + Scale(7), box.top + Scale(12));
			LineTo(item->hDC, box.left + Scale(14), box.top + Scale(4));
			SelectObject(item->hDC, border);
			DeleteObject(check);
		}
		SelectObject(item->hDC, oldPen);
		DeleteObject(border);
		SetBkMode(item->hDC, TRANSPARENT);
		SetTextColor(item->hDC, PdwThemeTextColor());
		SelectObject(item->hDC, PdwThemeUiFont());
		RECT textBounds = bounds;
		textBounds.left += Scale(25);
		DrawTextA(item->hDC, "Start PDW with Windows", -1, &textBounds,
			DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
		if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &bounds);
	}

	void PaintWindow(HWND window)
	{
		PAINTSTRUCT paint;
		HDC dc = BeginPaint(window, &paint);
		RECT client;
		GetClientRect(window, &client);
		HDC memory = CreateCompatibleDC(dc);
		HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
		HGDIOBJ oldBitmap = SelectObject(memory, bitmap);

		HBRUSH background = CreateSolidBrush(PdwThemeBackgroundColor());
		FillRect(memory, &client, background);
		DeleteObject(background);

		const bool narrow = client.right < Scale(860);
		const int navWidth = narrow ? Scale(196) : Scale(244);
		RECT navArea = { 0, 0, navWidth, client.bottom };
		HBRUSH navBrush = CreateSolidBrush(PdwThemeHeaderColor());
		FillRect(memory, &navArea, navBrush);
		DeleteObject(navBrush);
		HPEN divider = CreatePen(PS_SOLID, 1, PdwThemeBorderColor());
		HGDIOBJ oldPen = SelectObject(memory, divider);
		MoveToEx(memory, navWidth - 1, 0, NULL);
		LineTo(memory, navWidth - 1, client.bottom);
		SelectObject(memory, oldPen);
		DeleteObject(divider);

		SetBkMode(memory, TRANSPARENT);
		SetTextColor(memory, PdwThemeTextColor());
		SelectObject(memory, g_navHeadingFont);
		RECT brand = { Scale(20), Scale(10), navWidth - Scale(14), Scale(38) };
		DrawTextA(memory, "Settings", -1, &brand, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
		SetTextColor(memory, PdwThemeMutedTextColor());
		SelectObject(memory, g_smallFont);
		RECT version = { Scale(20), Scale(38), navWidth - Scale(14), Scale(58) };
		DrawTextA(memory, PDW_DISPLAY_VERSION, -1, &version,
			DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

		const int contentLeft = navWidth + Scale(26);
		const int headerBottom = narrow ? Scale(136) : Scale(88);
		HPEN headerDivider = CreatePen(PS_SOLID, 1, PdwThemeBorderColor());
		oldPen = SelectObject(memory, headerDivider);
		MoveToEx(memory, contentLeft, headerBottom, NULL);
		LineTo(memory, client.right - Scale(26), headerBottom);
		SelectObject(memory, oldPen);
		DeleteObject(headerDivider);
		SetTextColor(memory, PdwThemeMutedTextColor());
		SelectObject(memory, g_smallFont);
		RECT draft = { contentLeft + Scale(23), headerBottom + Scale(7),
			client.right - Scale(26), headerBottom + Scale(31) };
		DrawTextA(memory, "Draft changes are kept while you move between pages.", -1,
			&draft, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
		HPEN infoPen = CreatePen(PS_SOLID, 1, PdwThemeMutedTextColor());
		oldPen = SelectObject(memory, infoPen);
		HGDIOBJ infoOldBrush = SelectObject(memory, GetStockObject(NULL_BRUSH));
		Ellipse(memory, contentLeft + Scale(2), headerBottom + Scale(10),
			contentLeft + Scale(16), headerBottom + Scale(24));
		SelectObject(memory, infoOldBrush);
		SelectObject(memory, oldPen);
		DeleteObject(infoPen);
		RECT infoText = { contentLeft + Scale(2), headerBottom + Scale(9),
			contentLeft + Scale(16), headerBottom + Scale(25) };
		DrawTextA(memory, "i", -1, &infoText, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

		if (!IsRectEmpty(&g_directCard))
		{
			HBRUSH card = CreateSolidBrush(PdwThemeSurfaceColor());
			FillRect(memory, &g_directCard, card);
			DeleteObject(card);
			HPEN cardBorder = CreatePen(PS_SOLID, 1, PdwThemeBorderColor());
			oldPen = SelectObject(memory, cardBorder);
			HGDIOBJ oldBrush = SelectObject(memory, GetStockObject(NULL_BRUSH));
			RoundRect(memory, g_directCard.left, g_directCard.top, g_directCard.right,
				g_directCard.bottom, Scale(8), Scale(8));
			SelectObject(memory, oldBrush);
			SelectObject(memory, oldPen);
			DeleteObject(cardBorder);
		}

		BitBlt(dc, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
		SelectObject(memory, oldBitmap);
		DeleteObject(bitmap);
		DeleteDC(memory);
		EndPaint(window, &paint);
	}

	bool ConfirmClose(HWND window)
	{
		if (!g_dirty) return true;
		const int choice = MessageBoxA(window,
			"Apply your pending Settings changes before closing?\n\n"
			"Yes applies them, No discards them, and Cancel keeps Settings open.",
			"PDW Settings", MB_YESNOCANCEL | MB_ICONQUESTION);
		if (choice == IDCANCEL) return false;
		if (choice == IDYES) return ApplyDraft(window);
		RevertDraft();
		return true;
	}

	LRESULT CALLBACK SettingsWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
			case WM_CREATE:
			{
				g_window = window;
				g_dpi = ReadWindowDpi(window);
				g_appliedTheme = g_draftTheme = Profile.uiTheme;
				g_appliedStartup = g_draftStartup = IsStartWithWindowsEnabled();
				PdwThemeApplyToWindow(window);
				RefreshControlBrushes();

				g_nav = CreateWindowExA(0, "LISTBOX", "", WS_CHILD | WS_VISIBLE |
					WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT,
					0, 0, 0, 0, window,
					reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SC_NAV)), ghInstance, NULL);
				for (int index = 0; index < PDW_SETTINGS_PAGE_COUNT; ++index)
					SendMessageA(g_nav, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kPages[index].title));

				g_title = CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE |
					SS_LEFT | SS_NOPREFIX,
					0, 0, 0, 0, window,
					reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SC_TITLE)), ghInstance, NULL);
				g_subtitle = CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE |
					SS_LEFT | SS_NOPREFIX,
					0, 0, 0, 0, window,
					reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SC_SUBTITLE)), ghInstance, NULL);
				g_search = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
					WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
					0, 0, 0, 0, window,
					reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SC_SEARCH)), ghInstance, NULL);
				SendMessageW(g_search, EM_SETCUEBANNER, TRUE,
					reinterpret_cast<LPARAM>(L"Search settings"));
				g_revert = CreateWindowExA(0, "BUTTON", "Revert", WS_CHILD | WS_VISIBLE |
					WS_TABSTOP, 0, 0, 0, 0, window,
					reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SC_REVERT)), ghInstance, NULL);
				g_apply = CreateWindowExA(0, "BUTTON", "Apply changes", WS_CHILD | WS_VISIBLE |
					WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, window,
					reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SC_APPLY)), ghInstance, NULL);
				RefreshFonts();
				ApplyChildThemes();
				SetDirty(false);
				SelectPage(g_selectedPage);
				return 0;
			}

			case WM_SIZE:
				LayoutControls();
				// Child windows move substantially when crossing the compact-width
				// breakpoint. Repaint both the vacated parent pixels and every child
				// so interactive resizing cannot leave text/button trails behind.
				RedrawWindow(window, NULL, NULL,
					RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
				return 0;

			case WM_DPICHANGED:
				g_dpi = HIWORD(wParam);
				if (lParam)
				{
					RECT* suggested = reinterpret_cast<RECT*>(lParam);
					SetWindowPos(window, NULL, suggested->left, suggested->top,
						suggested->right - suggested->left, suggested->bottom - suggested->top,
						SWP_NOZORDER | SWP_NOACTIVATE);
				}
				RefreshFonts();
				RenderPage();
				return 0;

			case WM_GETMINMAXINFO:
			{
				MINMAXINFO* limits = reinterpret_cast<MINMAXINFO*>(lParam);
				limits->ptMinTrackSize.x = Scale(720);
				limits->ptMinTrackSize.y = Scale(560);
				return 0;
			}

			case WM_MEASUREITEM:
			{
				MEASUREITEMSTRUCT* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
				if (measure && measure->CtlID == IDC_SC_NAV)
				{
					measure->itemHeight = Scale(42);
					return TRUE;
				}
				break;
			}

			case WM_DRAWITEM:
			{
				DRAWITEMSTRUCT* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
				if (!item) break;
				if (item->CtlID == IDC_SC_NAV) { DrawNavigationItem(item); return TRUE; }
				if (item->CtlID == IDC_SC_STARTUP) { DrawStartupButton(item); return TRUE; }
				if (item->CtlID >= IDC_SC_CARD_FIRST &&
					item->CtlID < IDC_SC_CARD_FIRST + static_cast<UINT>(g_cards.size()))
				{
					DrawCard(item);
					return TRUE;
				}
				break;
			}

			case WM_COMMAND:
			{
				const UINT id = LOWORD(wParam);
				const UINT notification = HIWORD(wParam);
				if (id == IDC_SC_NAV && notification == LBN_SELCHANGE)
				{
					const int page = static_cast<int>(SendMessage(g_nav, LB_GETCURSEL, 0, 0));
					if (page >= 0) SelectPage(page);
					return 0;
				}
				if (id == IDC_SC_SEARCH && notification == EN_CHANGE)
				{
					RenderPage();
					return 0;
				}
				if (id == IDC_SC_STARTUP && notification == BN_CLICKED)
				{
					g_draftStartup = !g_draftStartup;
					RefreshDraftControls();
					SetDirty(g_draftStartup != g_appliedStartup || g_draftTheme != g_appliedTheme);
					return 0;
				}
				if (id == IDC_SC_THEME && notification == CBN_SELCHANGE)
				{
					g_draftTheme = static_cast<int>(SendMessage(g_theme, CB_GETCURSEL, 0, 0));
					RefreshDraftControls();
					SetDirty(g_draftStartup != g_appliedStartup || g_draftTheme != g_appliedTheme);
					return 0;
				}
				if (id == IDC_SC_APPLY) { ApplyDraft(window); return 0; }
				if (id == IDC_SC_REVERT) { RevertDraft(); return 0; }
				if (id >= IDC_SC_CARD_FIRST && id < IDC_SC_CARD_FIRST + 32)
				{
					LaunchAction(static_cast<int>(id - IDC_SC_CARD_FIRST));
					return 0;
				}
				break;
			}

			case WM_KEYDOWN:
				if (wParam == VK_ESCAPE) { SendMessage(window, WM_CLOSE, 0, 0); return 0; }
				if (wParam == 'F' && (GetKeyState(VK_CONTROL) & 0x8000))
				{
					SetFocus(g_search);
					SendMessage(g_search, EM_SETSEL, 0, -1);
					return 0;
				}
				break;

			case WM_CTLCOLORSTATIC:
			{
				HDC dc = reinterpret_cast<HDC>(wParam);
				SetTextColor(dc, PdwThemeTextColor());
				const HWND control = reinterpret_cast<HWND>(lParam);
				const bool onSurface = control == g_startupStatus || control == g_themeStatus;
				SetBkMode(dc, OPAQUE);
				SetBkColor(dc, onSurface ? PdwThemeSurfaceColor() : PdwThemeBackgroundColor());
				return reinterpret_cast<LRESULT>(onSurface ?
					g_surfaceControlBrush : g_backgroundControlBrush);
			}

			case WM_CTLCOLORBTN:
			{
				HDC dc = reinterpret_cast<HDC>(wParam);
				SetTextColor(dc, PdwThemeTextColor());
				SetBkMode(dc, TRANSPARENT);
				return reinterpret_cast<LRESULT>(g_backgroundControlBrush);
			}

			case WM_CTLCOLOREDIT:
			{
				HDC dc = reinterpret_cast<HDC>(wParam);
				SetTextColor(dc, PdwThemeTextColor());
				SetBkColor(dc, PdwThemeSurfaceColor());
				return reinterpret_cast<LRESULT>(g_surfaceControlBrush);
			}

			case WM_CTLCOLORLISTBOX:
			{
				HDC dc = reinterpret_cast<HDC>(wParam);
				const bool navigation = reinterpret_cast<HWND>(lParam) == g_nav;
				SetTextColor(dc, PdwThemeTextColor());
				SetBkColor(dc, navigation ? PdwThemeHeaderColor() : PdwThemeSurfaceColor());
				return reinterpret_cast<LRESULT>(navigation ?
					g_headerControlBrush : g_surfaceControlBrush);
			}

			case WM_ERASEBKGND:
				return TRUE;

			case WM_PAINT:
				PaintWindow(window);
				return 0;

			case WM_SETTINGCHANGE:
				SettingsCenterNotifyThemeChanged();
				break;

			case WM_CLOSE:
				if (ConfirmClose(window)) DestroyWindow(window);
				return 0;

			case WM_DESTROY:
				DestroyDynamicControls();
				DeleteFonts();
				if (g_backgroundControlBrush) DeleteObject(g_backgroundControlBrush);
				if (g_surfaceControlBrush) DeleteObject(g_surfaceControlBrush);
				if (g_headerControlBrush) DeleteObject(g_headerControlBrush);
				g_backgroundControlBrush = NULL;
				g_surfaceControlBrush = NULL;
				g_headerControlBrush = NULL;
				g_window = NULL;
				g_nav = NULL;
				g_title = NULL;
				g_subtitle = NULL;
				g_search = NULL;
				g_revert = NULL;
				g_apply = NULL;
				return 0;
		}
		return DefWindowProc(window, message, wParam, lParam);
	}

	bool RegisterSettingsClass()
	{
		WNDCLASSEXA windowClass;
		ZeroMemory(&windowClass, sizeof(windowClass));
		windowClass.cbSize = sizeof(windowClass);
		windowClass.style = CS_DBLCLKS;
		windowClass.lpfnWndProc = SettingsWindowProc;
		windowClass.hInstance = ghInstance;
		windowClass.hIcon = LoadIcon(ghInstance, MAKEINTRESOURCE(PDWICON));
		windowClass.hIconSm = windowClass.hIcon;
		windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
		windowClass.lpszClassName = kSettingsClass;
		if (RegisterClassExA(&windowClass)) return true;
		return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
	}
}

void ShowSettingsCenter(HWND owner, int page)
{
	if (page < 0 || page >= PDW_SETTINGS_PAGE_COUNT) page = g_selectedPage;
	if (g_window)
	{
		if (IsIconic(g_window)) ShowWindow(g_window, SW_RESTORE);
		SelectPage(page);
		ShowWindow(g_window, SW_SHOW);
		SetForegroundWindow(g_window);
		return;
	}
	if (!RegisterSettingsClass()) return;

	RECT workArea;
	SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
	const int dpi = ReadWindowDpi(owner);
	const int workWidth = static_cast<int>(workArea.right - workArea.left);
	const int workHeight = static_cast<int>(workArea.bottom - workArea.top);
	const int width = (std::min)(MulDiv(1000, dpi, 96), (std::max)(680, workWidth - 24));
	const int height = (std::min)(MulDiv(720, dpi, 96), (std::max)(540, workHeight - 24));
	RECT ownerRect = { 0, 0, 0, 0 };
	GetWindowRect(owner, &ownerRect);
	int x = static_cast<int>(ownerRect.left) +
		(static_cast<int>(ownerRect.right - ownerRect.left) - width) / 2;
	int y = static_cast<int>(ownerRect.top) +
		(static_cast<int>(ownerRect.bottom - ownerRect.top) - height) / 2;
	x = (std::max)(static_cast<int>(workArea.left),
		(std::min)(x, static_cast<int>(workArea.right) - width));
	y = (std::max)(static_cast<int>(workArea.top),
		(std::min)(y, static_cast<int>(workArea.bottom) - height));
	g_selectedPage = page;
	g_window = CreateWindowExA(WS_EX_APPWINDOW, kSettingsClass, "PDW Settings",
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX,
		x, y, width, height, owner, NULL, ghInstance, NULL);
	if (!g_window) return;
	ShowWindow(g_window, SW_SHOW);
	UpdateWindow(g_window);
}

BOOL SettingsCenterIsDialogMessage(MSG* message)
{
	if (!g_window || !message) return FALSE;
	if (message->message == WM_KEYDOWN && message->wParam == 'F' &&
		(GetKeyState(VK_CONTROL) & 0x8000))
	{
		SetFocus(g_search);
		SendMessage(g_search, EM_SETSEL, 0, -1);
		return TRUE;
	}
	return IsDialogMessage(g_window, message);
}

void SettingsCenterNotifyThemeChanged(void)
{
	if (!g_window) return;
	if (!g_dirty)
	{
		g_appliedTheme = g_draftTheme = Profile.uiTheme;
		g_appliedStartup = g_draftStartup = IsStartWithWindowsEnabled();
		RefreshDraftControls();
	}
	RefreshControlBrushes();
	PdwThemeApplyToWindow(g_window);
	LiveSignalMeterRefreshTheme(g_signalPreview);
	ApplyChildThemes();
	RefreshFonts();
	RedrawWindow(g_window, NULL, NULL,
		RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

void SettingsCenterClose(void)
{
	if (g_window) SendMessage(g_window, WM_CLOSE, 0, 0);
}

HWND SettingsCenterWindow(void)
{
	return g_window;
}
