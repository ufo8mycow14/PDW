#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>

#include "headers\resource.h"
#include "headers\pdw.h"
#include "headers\initapp.h"
#include "headers\menu.h"
#include "headers\settings_center.h"
#include "headers\toolbar.h"
#include "headers\ui_theme.h"
#include "headers\sigind.h"
#include "headers\startup.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace
{
	bool g_initialized = false;
	bool g_dark = false;
	bool g_highContrast = false;
	HBRUSH g_backgroundBrush = NULL;
	HBRUSH g_surfaceBrush = NULL;
	HBRUSH g_headerBrush = NULL;
	HFONT g_uiFont = NULL;
	HFONT g_uiSemiboldFont = NULL;
	HHOOK g_dialogHook = NULL;

	COLORREF g_background = RGB(243, 243, 243);
	COLORREF g_surface = RGB(255, 255, 255);
	COLORREF g_header = RGB(249, 249, 249);
	COLORREF g_text = RGB(32, 32, 32);
	COLORREF g_muted = RGB(92, 92, 92);
	COLORREF g_border = RGB(209, 209, 209);
	COLORREF g_accent = RGB(0, 103, 192);
	const char* kTopMenuLabels[] = { "&File", "&Monitor", "Fi&lters", "&Outputs", "&View", "&Help" };

	enum PreferredAppMode
	{
		PreferredAppModeDefault = 0,
		PreferredAppModeAllowDark = 1,
		PreferredAppModeForceDark = 2,
		PreferredAppModeForceLight = 3
	};
	typedef PreferredAppMode (WINAPI* SetPreferredAppModeProc)(PreferredAppMode mode);
	typedef void (WINAPI* FlushMenuThemesProc)(void);
	typedef BOOL (WINAPI* AllowDarkModeForWindowProc)(HWND window, BOOL allow);

	LRESULT CALLBACK GroupBoxThemeSubclass(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam,
		UINT_PTR subclassId, DWORD_PTR);

	bool ReadHighContrast()
	{
		HIGHCONTRAST highContrast;
		ZeroMemory(&highContrast, sizeof(highContrast));
		highContrast.cbSize = sizeof(highContrast);
		return SystemParametersInfo(SPI_GETHIGHCONTRAST, sizeof(highContrast), &highContrast, 0) &&
			(highContrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
	}

	bool ReadSystemDarkMode()
	{
		HKEY key = NULL;
		DWORD appsUseLightTheme = 1;
		DWORD valueSize = sizeof(appsUseLightTheme);
		DWORD valueType = 0;
		if (RegOpenKeyEx(HKEY_CURRENT_USER,
			"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
			0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS)
		{
			RegQueryValueEx(key, "AppsUseLightTheme", NULL, &valueType,
				reinterpret_cast<LPBYTE>(&appsUseLightTheme), &valueSize);
			RegCloseKey(key);
		}
		return appsUseLightTheme == 0;
	}

	void RefreshNativeAppTheme()
	{
		// Windows 10 1903+ exposes these uxtheme entry points by ordinal. Calling
		// them dynamically keeps older systems safe while allowing native menu
		// bars and popup menus to match the selected Windows application theme.
		HMODULE themeModule = GetModuleHandleW(L"uxtheme.dll");
		if (!themeModule) themeModule = LoadLibraryW(L"uxtheme.dll");
		if (!themeModule) return;
		SetPreferredAppModeProc setMode = reinterpret_cast<SetPreferredAppModeProc>(
			GetProcAddress(themeModule, MAKEINTRESOURCEA(135)));
		FlushMenuThemesProc flushMenus = reinterpret_cast<FlushMenuThemesProc>(
			GetProcAddress(themeModule, MAKEINTRESOURCEA(136)));
		if (setMode)
			setMode(g_highContrast ? PreferredAppModeDefault :
				(g_dark ? PreferredAppModeForceDark : PreferredAppModeForceLight));
		if (flushMenus) flushMenus();
	}

	void DeleteThemeObjects()
	{
		if (g_backgroundBrush) DeleteObject(g_backgroundBrush);
		if (g_surfaceBrush) DeleteObject(g_surfaceBrush);
		if (g_headerBrush) DeleteObject(g_headerBrush);
		if (g_uiFont) DeleteObject(g_uiFont);
		if (g_uiSemiboldFont) DeleteObject(g_uiSemiboldFont);
		g_backgroundBrush = NULL;
		g_surfaceBrush = NULL;
		g_headerBrush = NULL;
		g_uiFont = NULL;
		g_uiSemiboldFont = NULL;
	}

	void RefreshPalette()
	{
		g_highContrast = ReadHighContrast();
		g_dark = Profile.uiTheme == PDW_THEME_DARK ||
			(Profile.uiTheme == PDW_THEME_SYSTEM && ReadSystemDarkMode());

		if (g_highContrast)
		{
			g_background = GetSysColor(COLOR_WINDOW);
			g_surface = GetSysColor(COLOR_WINDOW);
			g_header = GetSysColor(COLOR_BTNFACE);
			g_text = GetSysColor(COLOR_WINDOWTEXT);
			g_muted = GetSysColor(COLOR_GRAYTEXT);
			g_border = GetSysColor(COLOR_WINDOWFRAME);
			g_accent = GetSysColor(COLOR_HIGHLIGHT);
			g_dark = false;
		}
		else if (g_dark)
		{
			// Keep the approved Windows 11 dark palette from the UI mock-up.
			// Legacy controls are softened separately so these colours remain
			// visually consistent without the old white frames and inputs.
			g_background = RGB(32, 32, 32);   // #202020
			g_surface = RGB(43, 43, 43);      // #2b2b2b
			g_header = RGB(40, 40, 40);       // #282828
			g_text = RGB(242, 242, 242);      // #f2f2f2
			g_muted = RGB(181, 181, 181);     // #b5b5b5
			g_border = RGB(73, 73, 73);       // #494949
			g_accent = RGB(96, 205, 255);     // #60cdff
		}
		else
		{
			g_background = RGB(243, 243, 243);
			g_surface = RGB(255, 255, 255);
			g_header = RGB(250, 250, 250);
			g_text = RGB(27, 27, 27);
			g_muted = RGB(98, 98, 98);
			g_border = RGB(209, 209, 209);
			g_accent = RGB(0, 103, 192);
		}
		RefreshNativeAppTheme();

		DeleteThemeObjects();
		g_backgroundBrush = CreateSolidBrush(g_background);
		g_surfaceBrush = CreateSolidBrush(g_surface);
		g_headerBrush = CreateSolidBrush(g_header);
		g_uiFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
			DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
		g_uiSemiboldFont = CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
			DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
	}

	BOOL CALLBACK ApplyChildTheme(HWND hWnd, LPARAM)
	{
		char className[32] = { 0 };
		GetClassNameA(hWnd, className, sizeof(className));
		if (lstrcmpiA(className, "Button") == 0 &&
			(GetWindowLong(hWnd, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX)
		{
			// Classic group boxes use an almost-white etched frame in dark mode.
			// Paint a quieter one-pixel frame while preserving Windows' native
			// high-contrast rendering when that accessibility mode is active.
			if (g_highContrast)
			{
				RemoveWindowSubclass(hWnd, GroupBoxThemeSubclass, 0x50444742);
				SetWindowTheme(hWnd, L"Explorer", NULL);
			}
			else
			{
				SetWindowTheme(hWnd, L"", L"");
				SetWindowSubclass(hWnd, GroupBoxThemeSubclass, 0x50444742, 0);
			}
		}
		else
		{
			const bool inputControl = lstrcmpiA(className, "Edit") == 0 ||
				lstrcmpiA(className, "ComboBox") == 0;
			const wchar_t* theme = g_dark && !g_highContrast ?
				(inputControl ? L"DarkMode_CFD" : L"DarkMode_Explorer") : L"Explorer";
			SetWindowTheme(hWnd, theme, NULL);
		}
		return TRUE;
	}

	void DrawGroupBox(HWND hWnd, HDC hdc)
	{
		RECT bounds;
		GetClientRect(hWnd, &bounds);
		if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) return;

		wchar_t title[256] = { 0 };
		GetWindowTextW(hWnd, title, static_cast<int>(_countof(title)));
		HFONT font = reinterpret_cast<HFONT>(SendMessage(hWnd, WM_GETFONT, 0, 0));
		if (!font) font = g_uiFont;
		HGDIOBJ oldFont = font ? SelectObject(hdc, font) : NULL;

		SIZE titleSize = { 0, 0 };
		const int titleLength = lstrlenW(title);
		if (titleLength) GetTextExtentPoint32W(hdc, title, titleLength, &titleSize);
		const int lineY = (std::max)(4, static_cast<int>(titleSize.cy / 2));
		const int titleLeft = 9;
		const int titleRight = titleLeft + titleSize.cx;

		HPEN borderPen = CreatePen(PS_SOLID, 1, g_border);
		HGDIOBJ oldPen = SelectObject(hdc, borderPen);
		MoveToEx(hdc, bounds.left, lineY, NULL);
		LineTo(hdc, titleLength ? titleLeft - 3 : bounds.right - 1, lineY);
		if (titleLength)
		{
			MoveToEx(hdc, titleRight + 3, lineY, NULL);
			LineTo(hdc, bounds.right - 1, lineY);
		}
		LineTo(hdc, bounds.right - 1, bounds.bottom - 1);
		LineTo(hdc, bounds.left, bounds.bottom - 1);
		LineTo(hdc, bounds.left, lineY);
		SelectObject(hdc, oldPen);
		DeleteObject(borderPen);

		if (titleLength)
		{
			RECT titleBackground = { titleLeft - 2, 0, titleRight + 2, titleSize.cy + 1 };
			FillRect(hdc, &titleBackground, g_backgroundBrush);
			SetBkMode(hdc, TRANSPARENT);
			SetTextColor(hdc, IsWindowEnabled(hWnd) ? g_text : g_muted);
			TextOutW(hdc, titleLeft, 0, title, titleLength);
		}

		if (oldFont) SelectObject(hdc, oldFont);
	}

	LRESULT CALLBACK GroupBoxThemeSubclass(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam,
		UINT_PTR subclassId, DWORD_PTR)
	{
		switch (message)
		{
			case WM_ERASEBKGND:
				return TRUE;

			case WM_PAINT:
			{
				PAINTSTRUCT paint;
				HDC hdc = BeginPaint(hWnd, &paint);
				DrawGroupBox(hWnd, hdc);
				EndPaint(hWnd, &paint);
				return 0;
			}

			case WM_PRINTCLIENT:
				DrawGroupBox(hWnd, reinterpret_cast<HDC>(wParam));
				return 0;

			case WM_ENABLE:
			case WM_SETTEXT:
			case WM_SETFONT:
				InvalidateRect(hWnd, NULL, TRUE);
				break;

			case WM_NCDESTROY:
				RemoveWindowSubclass(hWnd, GroupBoxThemeSubclass, subclassId);
				break;
		}
		return DefSubclassProc(hWnd, message, wParam, lParam);
	}

	LRESULT CALLBACK DialogThemeSubclass(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam,
		UINT_PTR subclassId, DWORD_PTR)
	{
		switch (message)
		{
			case WM_ERASEBKGND:
			{
				RECT area;
				GetClientRect(hWnd, &area);
				FillRect(reinterpret_cast<HDC>(wParam), &area, g_backgroundBrush);
				return TRUE;
			}

			case WM_CTLCOLORDLG:
				return reinterpret_cast<LRESULT>(g_backgroundBrush);

			case WM_CTLCOLORSTATIC:
			case WM_CTLCOLORBTN:
			{
				HDC hdc = reinterpret_cast<HDC>(wParam);
				SetTextColor(hdc, IsWindowEnabled(reinterpret_cast<HWND>(lParam)) ? g_text : g_muted);
				SetBkMode(hdc, TRANSPARENT);
				return reinterpret_cast<LRESULT>(g_backgroundBrush);
			}

			case WM_CTLCOLOREDIT:
			case WM_CTLCOLORLISTBOX:
			{
				HDC hdc = reinterpret_cast<HDC>(wParam);
				SetTextColor(hdc, IsWindowEnabled(reinterpret_cast<HWND>(lParam)) ? g_text : g_muted);
				SetBkColor(hdc, g_surface);
				return reinterpret_cast<LRESULT>(g_surfaceBrush);
			}

			case WM_NCDESTROY:
				RemoveWindowSubclass(hWnd, DialogThemeSubclass, subclassId);
				break;
		}
		return DefSubclassProc(hWnd, message, wParam, lParam);
	}

	void ApplyDialogTheme(HWND hWnd)
	{
		PdwThemeApplyToWindow(hWnd);
		SetWindowSubclass(hWnd, DialogThemeSubclass, 0x50445754, 0);
		EnumChildWindows(hWnd, ApplyChildTheme, 0);
		InvalidateRect(hWnd, NULL, TRUE);
	}

	LRESULT CALLBACK DialogCbtHook(int code, WPARAM wParam, LPARAM lParam)
	{
		if (code == HCBT_ACTIVATE) ApplyDialogTheme(reinterpret_cast<HWND>(wParam));
		return CallNextHookEx(g_dialogHook, code, wParam, lParam);
	}

	BOOL CALLBACK ApplyThreadWindow(HWND hWnd, LPARAM)
	{
		char className[32];
		className[0] = '\0';
		GetClassNameA(hWnd, className, sizeof(className));
		if (lstrcmpA(className, "#32770") == 0) ApplyDialogTheme(hWnd);
		else PdwThemeApplyToWindow(hWnd);
		return TRUE;
	}

	void PrepareMainMenu(HWND window)
	{
		HMENU menu = GetMenu(window);
		if (!menu) return;
		const int itemCount = (std::min)(GetMenuItemCount(menu),
			static_cast<int>(_countof(kTopMenuLabels)));
		for (int index = 0; index < itemCount; ++index)
		{
			MENUITEMINFOA item;
			ZeroMemory(&item, sizeof(item));
			item.cbSize = sizeof(item);
			item.fMask = MIIM_FTYPE | MIIM_DATA;
			item.fType = MFT_OWNERDRAW;
			item.dwItemData = reinterpret_cast<ULONG_PTR>(kTopMenuLabels[index]);
			SetMenuItemInfoA(menu, index, TRUE, &item);
		}
		MENUINFO info;
		ZeroMemory(&info, sizeof(info));
		info.cbSize = sizeof(info);
		info.fMask = MIM_BACKGROUND;
		info.hbrBack = g_headerBrush;
		SetMenuInfo(menu, &info);
	}

	void OpenSetting(HWND hDlg, UINT command)
	{
		EndDialog(hDlg, TRUE);
		PostMessage(ghWnd, WM_COMMAND, MAKEWPARAM(command, 0), 0);
	}
}

void PdwThemeInitialize(void)
{
	if (g_initialized) return;
	g_initialized = true;
	RefreshPalette();
}

void PdwThemeShutdown(void)
{
	if (g_dialogHook) UnhookWindowsHookEx(g_dialogHook);
	g_dialogHook = NULL;
	DeleteThemeObjects();
	g_initialized = false;
}

void PdwThemeApplyToWindow(HWND hWnd)
{
	if (!hWnd) return;
	if (!g_initialized) PdwThemeInitialize();
	BOOL darkFrame = g_dark && !g_highContrast ? TRUE : FALSE;
	HMODULE themeModule = GetModuleHandleW(L"uxtheme.dll");
	AllowDarkModeForWindowProc allowDark = themeModule ?
		reinterpret_cast<AllowDarkModeForWindowProc>(
			GetProcAddress(themeModule, MAKEINTRESOURCEA(133))) : NULL;
	if (allowDark) allowDark(hWnd, darkFrame);
	DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkFrame, sizeof(darkFrame));
	SetWindowTheme(hWnd, darkFrame ? L"DarkMode_Explorer" : L"Explorer", NULL);
	SendMessage(hWnd, WM_THEMECHANGED, 0, 0);
}

void PdwThemeApplyToMainWindow(HWND hWnd, HWND toolbar)
{
	if (!g_initialized) PdwThemeInitialize();
	PdwThemeApplyToWindow(hWnd);
	PrepareMainMenu(hWnd);
	if (toolbar)
	{
		// PDW custom-draws this flat toolbar so its background and text can
		// transition reliably between light and dark modes on Common Controls v6.
		SetWindowTheme(toolbar, L"", L"");
		SendMessage(toolbar, CCM_SETBKCOLOR, 0, g_header);
		if (g_uiFont) SendMessage(toolbar, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
		ToolbarRefreshTheme();
		InvalidateRect(toolbar, NULL, TRUE);
	}
	DrawMenuBar(hWnd);
	RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

LRESULT PdwThemeHandleToolbarCustomDraw(LPARAM lParam)
{
	LPNMHDR header = reinterpret_cast<LPNMHDR>(lParam);
	if (!header || header->hwndFrom != hToolbar || header->code != NM_CUSTOMDRAW) return 0;

	LPNMTBCUSTOMDRAW draw = reinterpret_cast<LPNMTBCUSTOMDRAW>(lParam);
	if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
	{
		RECT toolbarArea;
		GetClientRect(header->hwndFrom, &toolbarArea);
		HBRUSH toolbarBackground = CreateSolidBrush(g_header);
		FillRect(draw->nmcd.hdc, &toolbarArea, toolbarBackground);
		DeleteObject(toolbarBackground);
		return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
	}
	if (draw->nmcd.dwDrawStage == CDDS_POSTPAINT) return CDRF_DODEFAULT;
	if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
	{
		draw->clrText = g_text;
		draw->clrBtnFace = g_header;
		draw->clrHighlightHotTrack = g_surface;
		draw->clrTextHighlight = g_text;
		return TBCDRF_USECDCOLORS | TBCDRF_NOEDGES | TBCDRF_NOOFFSET |
			TBCDRF_NOBACKGROUND | TBCDRF_HILITEHOTTRACK;
	}
	return CDRF_DODEFAULT;
}

BOOL PdwThemeHandleMenuMeasure(LPARAM lParam)
{
	MEASUREITEMSTRUCT* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
	if (!measure || measure->CtlType != ODT_MENU || !measure->itemData) return FALSE;
	const char* label = reinterpret_cast<const char*>(measure->itemData);
	bool topLevel = false;
	for (int index = 0; index < static_cast<int>(_countof(kTopMenuLabels)); ++index)
		if (label == kTopMenuLabels[index]) { topLevel = true; break; }
	if (!topLevel) return FALSE;

	HDC dc = GetDC(ghWnd);
	HGDIOBJ oldFont = g_uiFont ? SelectObject(dc, g_uiFont) : NULL;
	SIZE textSize = { 0, 0 };
	GetTextExtentPoint32A(dc, label, lstrlenA(label), &textSize);
	const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
	if (oldFont) SelectObject(dc, oldFont);
	ReleaseDC(ghWnd, dc);
	measure->itemWidth = textSize.cx + MulDiv(20, dpi, 96);
	measure->itemHeight = MulDiv(30, dpi, 96);
	return TRUE;
}

BOOL PdwThemeHandleMenuDraw(LPARAM lParam)
{
	DRAWITEMSTRUCT* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
	if (!item || item->CtlType != ODT_MENU || !item->itemData) return FALSE;
	const char* label = reinterpret_cast<const char*>(item->itemData);
	bool topLevel = false;
	for (int index = 0; index < static_cast<int>(_countof(kTopMenuLabels)); ++index)
		if (label == kTopMenuLabels[index]) { topLevel = true; break; }
	if (!topLevel) return FALSE;

	RECT bounds = item->rcItem;
	FillRect(item->hDC, &bounds, g_headerBrush);
	if (item->itemState & (ODS_SELECTED | ODS_HOTLIGHT))
	{
		RECT highlight = bounds;
		InflateRect(&highlight, -2, -3);
		HBRUSH brush = CreateSolidBrush(g_dark ? RGB(18, 58, 75) : RGB(229, 243, 255));
		HGDIOBJ oldBrush = SelectObject(item->hDC, brush);
		HGDIOBJ oldPen = SelectObject(item->hDC, GetStockObject(NULL_PEN));
		RoundRect(item->hDC, highlight.left, highlight.top, highlight.right,
			highlight.bottom, 5, 5);
		SelectObject(item->hDC, oldPen);
		SelectObject(item->hDC, oldBrush);
		DeleteObject(brush);
	}

	SetBkMode(item->hDC, TRANSPARENT);
	SetTextColor(item->hDC,
		(item->itemState & ODS_DISABLED) ? g_muted : g_text);
	HGDIOBJ oldFont = g_uiFont ? SelectObject(item->hDC, g_uiFont) : NULL;
	UINT format = DT_SINGLELINE | DT_CENTER | DT_VCENTER;
	if (item->itemState & ODS_NOACCEL) format |= DT_HIDEPREFIX;
	DrawTextA(item->hDC, label, -1, &bounds, format);
	if (oldFont) SelectObject(item->hDC, oldFont);
	if (item->itemState & ODS_FOCUS)
	{
		InflateRect(&bounds, -3, -3);
		DrawFocusRect(item->hDC, &bounds);
	}
	return TRUE;
}

void PdwThemeSystemSettingChanged(HWND hWnd)
{
	const bool highContrast = ReadHighContrast();
	if (Profile.uiTheme != PDW_THEME_SYSTEM && highContrast == g_highContrast) return;
	RefreshPalette();
	PdwThemeApplyToMainWindow(hWnd, hToolbar);
	EnumThreadWindows(GetCurrentThreadId(), ApplyThreadWindow, 0);
	SettingsCenterNotifyThemeChanged();
}

void PdwThemeSetMode(int mode, HWND owner)
{
	if (mode < PDW_THEME_SYSTEM || mode > PDW_THEME_DARK) mode = PDW_THEME_SYSTEM;
	Profile.uiTheme = mode;
	RefreshPalette();
	PdwThemeApplyToMainWindow(ghWnd, hToolbar);
	EnumThreadWindows(GetCurrentThreadId(), ApplyThreadWindow, 0);
	SettingsCenterNotifyThemeChanged();
	set_menu_items();
	WriteSettings();
	if (owner) InvalidateRect(owner, NULL, TRUE);
}

bool PdwThemeIsDark(void) { return g_dark; }
bool PdwThemeIsHighContrast(void) { return g_highContrast; }
COLORREF PdwThemeBackgroundColor(void) { return g_background; }
COLORREF PdwThemeSurfaceColor(void) { return g_surface; }
COLORREF PdwThemeHeaderColor(void) { return g_header; }
COLORREF PdwThemeTextColor(void) { return g_text; }
COLORREF PdwThemeMutedTextColor(void) { return g_muted; }
COLORREF PdwThemeBorderColor(void) { return g_border; }
COLORREF PdwThemeAccentColor(void) { return g_accent; }
HFONT PdwThemeUiFont(void) { return g_uiFont; }
HFONT PdwThemeUiSemiboldFont(void) { return g_uiSemiboldFont; }

void PdwThemeBeginDialogHook(void)
{
	if (!g_initialized) PdwThemeInitialize();
	if (!g_dialogHook)
		g_dialogHook = SetWindowsHookEx(WH_CBT, DialogCbtHook, NULL, GetCurrentThreadId());
}

void PdwThemeEndDialogHook(void)
{
	if (g_dialogHook) UnhookWindowsHookEx(g_dialogHook);
	g_dialogHook = NULL;
}

BOOL FAR PASCAL SettingsHubDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM)
{
	switch (uMsg)
	{
		case WM_INITDIALOG:
			CenterWindow(hDlg);
			SendDlgItemMessage(hDlg, IDC_SETTINGS_THEME, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Follow Windows"));
			SendDlgItemMessage(hDlg, IDC_SETTINGS_THEME, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Light"));
			SendDlgItemMessage(hDlg, IDC_SETTINGS_THEME, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Dark"));
			SendDlgItemMessage(hDlg, IDC_SETTINGS_THEME, CB_SETCURSEL, Profile.uiTheme, 0);
			SetDlgItemText(hDlg, IDC_SETTINGS_THEME_STATUS,
				Profile.uiTheme == PDW_THEME_SYSTEM ? "PDW changes automatically with Windows." :
				"PDW will keep this appearance until you change it.");
			CheckDlgButton(hDlg, IDC_SETTINGS_START_WINDOWS,
				IsStartWithWindowsEnabled() ? BST_CHECKED : BST_UNCHECKED);
			SetDlgItemText(hDlg, IDC_SETTINGS_START_STATUS,
				IsStartWithWindowsEnabled() ? "Enabled. PDW starts 5 seconds after sign-in." :
				"Disabled. PDW will not start automatically.");
			return TRUE;

		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
				case IDC_SETTINGS_THEME:
					if (HIWORD(wParam) == CBN_SELCHANGE)
					{
						int mode = static_cast<int>(SendDlgItemMessage(hDlg, IDC_SETTINGS_THEME, CB_GETCURSEL, 0, 0));
						PdwThemeSetMode(mode, hDlg);
						SetDlgItemText(hDlg, IDC_SETTINGS_THEME_STATUS,
							mode == PDW_THEME_SYSTEM ? "PDW changes automatically with Windows." :
							"PDW will keep this appearance until you change it.");
					}
					return TRUE;

				case IDC_SETTINGS_START_WINDOWS:
					if (HIWORD(wParam) == BN_CLICKED)
					{
						BOOL enabled = IsDlgButtonChecked(hDlg, IDC_SETTINGS_START_WINDOWS) == BST_CHECKED;
						char errorText[384];
						if (SetStartWithWindowsEnabled(enabled, errorText, sizeof(errorText)))
						{
							SetDlgItemText(hDlg, IDC_SETTINGS_START_STATUS,
								enabled ? "Enabled. PDW starts 5 seconds after sign-in." :
								"Disabled. PDW will not start automatically.");
						}
						else
						{
							CheckDlgButton(hDlg, IDC_SETTINGS_START_WINDOWS,
								enabled ? BST_UNCHECKED : BST_CHECKED);
							MessageBoxA(hDlg, errorText, "Start with Windows", MB_OK | MB_ICONERROR);
						}
					}
					return TRUE;

				case IDC_SETTINGS_COLORS: OpenSetting(hDlg, IDM_COLOR); return TRUE;
				case IDC_SETTINGS_FONT: OpenSetting(hDlg, IDM_FONT); return TRUE;
				case IDC_SETTINGS_VIEW: OpenSetting(hDlg, IDM_SCREENOPTIONS); return TRUE;
				case IDC_SETTINGS_TRAY: OpenSetting(hDlg, IDM_SYSTEMTRAY); return TRUE;
				case IDC_SETTINGS_DECODER: OpenSetting(hDlg, IDM_OPTIONS); return TRUE;
				case IDC_SETTINGS_GENERAL: OpenSetting(hDlg, IDM_GENERAL); return TRUE;
				case IDC_SETTINGS_INPUT: OpenSetting(hDlg, IDM_INTERFACE); return TRUE;
				case IDC_SETTINGS_SIGNAL_SOURCE: OpenSetting(hDlg, IDM_SIGNAL_SOURCES); return TRUE;
				case IDC_SETTINGS_VOLUME: OpenSetting(hDlg, IDM_VOLUME); return TRUE;
				case IDC_SETTINGS_EMAIL: OpenSetting(hDlg, IDM_MAIL); return TRUE;
				case IDC_SETTINGS_NOTIFY: OpenSetting(hDlg, IDM_APPRISE); return TRUE;
				case IDC_SETTINGS_TRANSFER: OpenSetting(hDlg, IDM_FTP); return TRUE;
				case IDC_SETTINGS_PUBLISH: OpenSetting(hDlg, IDM_PUBLISHING); return TRUE;
				case IDC_SETTINGS_DATA_OUTPUTS: OpenSetting(hDlg, IDM_DATA_OUTPUTS); return TRUE;
				case IDC_SETTINGS_OUTPUT_HEALTH: OpenSetting(hDlg, IDM_OUTPUT_HEALTH); return TRUE;
				case IDC_SETTINGS_SCROLLBACK: OpenSetting(hDlg, IDM_SCROLLBACK); return TRUE;
				case IDOK:
				case IDCANCEL:
					EndDialog(hDlg, TRUE);
					return TRUE;
			}
			break;

		case WM_CLOSE:
			EndDialog(hDlg, TRUE);
			return TRUE;
	}
	return FALSE;
}
