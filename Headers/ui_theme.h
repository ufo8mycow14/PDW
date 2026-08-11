#ifndef PDW_UI_THEME_H
#define PDW_UI_THEME_H

#include <windows.h>

enum PdwThemeMode
{
	PDW_THEME_SYSTEM = 0,
	PDW_THEME_LIGHT = 1,
	PDW_THEME_DARK = 2
};

void PdwThemeInitialize(void);
void PdwThemeShutdown(void);
void PdwThemeApplyToMainWindow(HWND hWnd, HWND hToolbar);
LRESULT PdwThemeHandleToolbarCustomDraw(LPARAM lParam);
BOOL PdwThemeHandleMenuMeasure(LPARAM lParam);
BOOL PdwThemeHandleMenuDraw(LPARAM lParam);
void PdwThemeApplyToWindow(HWND hWnd);
void PdwThemeSystemSettingChanged(HWND hWnd);
bool PdwThemeSetMode(int mode, HWND owner);
bool PdwThemeIsDark(void);
bool PdwThemeIsHighContrast(void);
COLORREF PdwThemeBackgroundColor(void);
COLORREF PdwThemeSurfaceColor(void);
COLORREF PdwThemeHeaderColor(void);
COLORREF PdwThemeTextColor(void);
COLORREF PdwThemeMutedTextColor(void);
COLORREF PdwThemeBorderColor(void);
COLORREF PdwThemeAccentColor(void);
HFONT PdwThemeUiFont(void);
HFONT PdwThemeUiSemiboldFont(void);

void PdwThemeBeginDialogHook(void);
void PdwThemeEndDialogHook(void);

BOOL FAR PASCAL SettingsHubDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

#endif
