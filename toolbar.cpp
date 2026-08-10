// Modern task-oriented toolbar for PDW.

#include <windows.h>
#include <commctrl.h>

#include "headers\resource.h"
#include "headers\toolbar.h"
#include "headers\ui_theme.h"

namespace
{
	struct ToolbarAction
	{
		UINT command;
		const char* label;
		const char* tooltip;
	};

	const ToolbarAction kActions[] =
	{
		{ IDM_LOGFILE,          "Log",        "Open or close the log file" },
		{ IDM_COPY_SELECTION,   "Copy",       "Copy selected decoded text" },
		{ IDM_FILTERS,          "Filters",    "Manage message filters" },
		{ IDM_SETTINGS,         "Settings",   "Open PDW settings" },
		{ IDM_MONSTAT,          "Statistics", "View decoder statistics" },
		{ IDT_TOOLBAR_BTN9,     "Pause",      "Pause or resume decoding" },
		{ IDM_CLEARDISPLAY,     "Clear",      "Clear the monitor windows" },
		{ IDT_TOOLBAR_BTN12,    "Mode",       "Change decoder mode" }
	};

	const UINT kButtonCommands[] =
	{
		IDM_LOGFILE,
		IDM_COPY_SELECTION,
		0,
		IDM_FILTERS,
		IDM_SETTINGS,
		0,
		IDM_MONSTAT,
		IDT_TOOLBAR_BTN9,
		IDM_CLEARDISPLAY,
		0,
		IDT_TOOLBAR_BTN12
	};

	const ToolbarAction* FindAction(UINT command)
	{
		for (int i = 0; i < static_cast<int>(sizeof(kActions) / sizeof(kActions[0])); ++i)
			if (kActions[i].command == command) return &kActions[i];
		return NULL;
	}
}

HWND ShowMakeToolBar(HWND parent_hwnd, HINSTANCE)
{
	INITCOMMONCONTROLSEX controls = { sizeof(controls), ICC_BAR_CLASSES };
	InitCommonControlsEx(&controls);

	HWND toolbar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL,
		WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | TBSTYLE_LIST | CCS_TOP,
		0, 0, 0, 0, parent_hwnd, reinterpret_cast<HMENU>(IDW_TOOL_BAR),
		GetModuleHandle(NULL), NULL);
	if (!toolbar) return NULL;

	SendMessage(toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
	SendMessage(toolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS | TBSTYLE_EX_DOUBLEBUFFER);
	SendMessage(toolbar, TB_SETPADDING, 0, MAKELPARAM(12, 7));

	TBBUTTON buttons[sizeof(kButtonCommands) / sizeof(kButtonCommands[0])];
	ZeroMemory(buttons, sizeof(buttons));
	for (int i = 0; i < static_cast<int>(sizeof(kButtonCommands) / sizeof(kButtonCommands[0])); ++i)
	{
		buttons[i].fsState = TBSTATE_ENABLED;
		if (kButtonCommands[i] == 0)
		{
			buttons[i].fsStyle = BTNS_SEP;
			buttons[i].iBitmap = 8;
			continue;
		}

		const ToolbarAction* action = FindAction(kButtonCommands[i]);
		buttons[i].iBitmap = I_IMAGENONE;
		buttons[i].idCommand = kButtonCommands[i];
		buttons[i].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT;
		buttons[i].iString = SendMessageA(toolbar, TB_ADDSTRINGA, 0,
			reinterpret_cast<LPARAM>(action ? action->label : ""));
	}

	if (!SendMessage(toolbar, TB_ADDBUTTONS,
		static_cast<WPARAM>(sizeof(buttons) / sizeof(buttons[0])),
		reinterpret_cast<LPARAM>(buttons)))
	{
		DestroyWindow(toolbar);
		return NULL;
	}

	if (PdwThemeUiFont()) SendMessage(toolbar, WM_SETFONT, reinterpret_cast<WPARAM>(PdwThemeUiFont()), TRUE);
	return toolbar;
}

BOOL GetToolBarImages(HINSTANCE) { return TRUE; }
void FreeToolBarImages(HINSTANCE) {}
void SetToolBarButtons(void) {}
void Add_TB_ButtonsBitmaps(HWND, HINSTANCE) {}

void TB_AutoSize(HWND hTbar)
{
	SendMessage(hTbar, TB_AUTOSIZE, 0, 0);
}

void SetToolTXT(HINSTANCE, LPARAM lParam)
{
	LPTOOLTIPTEXT tooltip = reinterpret_cast<LPTOOLTIPTEXT>(lParam);
	const ToolbarAction* action = FindAction(static_cast<UINT>(tooltip->hdr.idFrom));
	if (action) tooltip->lpszText = const_cast<char*>(action->tooltip);
}
