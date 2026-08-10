#ifndef PDW_SETTINGS_CENTER_H
#define PDW_SETTINGS_CENTER_H

#include <windows.h>

enum PdwSettingsPage
{
	PDW_SETTINGS_GENERAL = 0,
	PDW_SETTINGS_APPEARANCE,
	PDW_SETTINGS_DISPLAY,
	PDW_SETTINGS_DECODER,
	PDW_SETTINGS_SIGNAL,
	PDW_SETTINGS_FILTERS,
	PDW_SETTINGS_NOTIFICATIONS,
	PDW_SETTINGS_DATA_OUTPUTS,
	PDW_SETTINGS_HEALTH,
	PDW_SETTINGS_ABOUT,
	PDW_SETTINGS_PAGE_COUNT
};

void ShowSettingsCenter(HWND owner, int page = PDW_SETTINGS_GENERAL);
BOOL SettingsCenterIsDialogMessage(MSG* message);
void SettingsCenterNotifyThemeChanged(void);
void SettingsCenterClose(void);
HWND SettingsCenterWindow(void);

#endif
