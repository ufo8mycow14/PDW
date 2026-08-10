#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include "headers\resource.h"
#include "headers\pdw.h"
#include "headers\initapp.h"
#include "headers\menu.h"
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

	void DeleteThemeObjects()
	{
		if (g_backgroundBrush) DeleteObject(g_backgroundBrush);
		if (g_surfaceBrush) DeleteObject(g_surfaceBrush);
		if (g_uiFont) DeleteObject(g_uiFont);
		if (g_uiSemiboldFont) DeleteObject(g_uiSemiboldFont);
		g_backgroundBrush = NULL;
		g_surfaceBrush = NULL;
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
			g_background = RGB(32, 32, 32);
			g_surface = RGB(43, 43, 43);
			g_header = RGB(50, 50, 50);
			g_text = RGB(243, 243, 243);
			g_muted = RGB(200, 200, 200);
			g_border = RGB(69, 69, 69);
			g_accent = RGB(96, 205, 255);
		}
		else
		{
			g_background = RGB(243, 243, 243);
			g_surface = RGB(255, 255, 255);
			g_header = RGB(249, 249, 249);
			g_text = RGB(32, 32, 32);
			g_muted = RGB(92, 92, 92);
			g_border = RGB(209, 209, 209);
			g_accent = RGB(0, 103, 192);
		}

		DeleteThemeObjects();
		g_backgroundBrush = CreateSolidBrush(g_background);
		g_surfaceBrush = CreateSolidBrush(g_surface);
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
			// The v6 group-box theme hard-codes dark text. Let WM_CTLCOLORBTN
			// provide the accessible palette used by the rest of the dialog.
			SetWindowTheme(hWnd, L"", L"");
		}
		else
		{
			SetWindowTheme(hWnd, g_dark && !g_highContrast ? L"DarkMode_Explorer" : L"Explorer", NULL);
		}
		return TRUE;
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
				SetTextColor(hdc, g_text);
				SetBkMode(hdc, TRANSPARENT);
				return reinterpret_cast<LRESULT>(g_backgroundBrush);
			}

			case WM_CTLCOLOREDIT:
			case WM_CTLCOLORLISTBOX:
			{
				HDC hdc = reinterpret_cast<HDC>(wParam);
				SetTextColor(hdc, g_text);
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
	DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkFrame, sizeof(darkFrame));
	SetWindowTheme(hWnd, darkFrame ? L"DarkMode_Explorer" : L"Explorer", NULL);
}

void PdwThemeApplyToMainWindow(HWND hWnd, HWND toolbar)
{
	if (!g_initialized) PdwThemeInitialize();
	PdwThemeApplyToWindow(hWnd);
	if (toolbar)
	{
		// PDW custom-draws this flat toolbar so its background and text can
		// transition reliably between light and dark modes on Common Controls v6.
		SetWindowTheme(toolbar, L"", L"");
		SendMessage(toolbar, CCM_SETBKCOLOR, 0, g_header);
		if (g_uiFont) SendMessage(toolbar, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
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
	if (draw->nmcd.dwDrawStage == CDDS_POSTPAINT)
	{
		DrawToolbarIndicators(draw->nmcd.hdc);
		return CDRF_DODEFAULT;
	}
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

void PdwThemeSystemSettingChanged(HWND hWnd)
{
	if (Profile.uiTheme != PDW_THEME_SYSTEM && !ReadHighContrast()) return;
	RefreshPalette();
	PdwThemeApplyToMainWindow(hWnd, hToolbar);
	EnumThreadWindows(GetCurrentThreadId(), ApplyThreadWindow, 0);
}

void PdwThemeSetMode(int mode, HWND owner)
{
	if (mode < PDW_THEME_SYSTEM || mode > PDW_THEME_DARK) mode = PDW_THEME_SYSTEM;
	Profile.uiTheme = mode;
	RefreshPalette();
	PdwThemeApplyToMainWindow(ghWnd, hToolbar);
	EnumThreadWindows(GetCurrentThreadId(), ApplyThreadWindow, 0);
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
