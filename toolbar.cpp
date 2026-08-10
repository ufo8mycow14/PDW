// Modern task-oriented command bar for PDW.

#include <windows.h>
#include <commctrl.h>

#include <algorithm>

#include "headers\live_signal_meter.h"
#include "headers\resource.h"
#include "headers\sound_in.h"
#include "headers\toolbar.h"
#include "headers\ui_theme.h"

extern bool bPauseFlag;
extern bool bRecording;
extern HWND hToolbar;

namespace
{
	enum ToolbarIcon
	{
		ICON_SOURCE = 0,
		ICON_PAUSE,
		ICON_PLAY,
		ICON_RECORD,
		ICON_STOP,
		ICON_FILTER,
		ICON_CLEAR,
		ICON_SETTINGS,
		ICON_COUNT
	};

	struct ToolbarAction
	{
		UINT command;
		const char* label;
		const char* tooltip;
		int icon;
	};

	const ToolbarAction kActions[] =
	{
		{ IDM_SIGNAL_SOURCES, "Source",   "Choose and test the signal source", ICON_SOURCE },
		{ IDT_TOOLBAR_BTN9,   "Pause",    "Pause decoding",                    ICON_PAUSE },
		{ IDM_RECORD,         "Record",   "Record the active signal",          ICON_RECORD },
		{ IDM_FILTERS,        "Filters",  "Manage message filters",            ICON_FILTER },
		{ IDM_CLEARDISPLAY,   "Clear",    "Clear the monitor windows",         ICON_CLEAR },
		{ IDM_SETTINGS,       "Settings", "Open or focus PDW Settings",        ICON_SETTINGS }
	};

	HIMAGELIST g_images = NULL;
	HWND g_signalMeter = NULL;
	bool g_compact = false;

	const ToolbarAction* FindAction(UINT command)
	{
		for (int index = 0; index < static_cast<int>(_countof(kActions)); ++index)
			if (kActions[index].command == command) return &kActions[index];
		return NULL;
	}

	void DrawToolbarIcon(HDC dc, int icon, COLORREF colour)
	{
		HPEN pen = CreatePen(PS_SOLID, 2, colour);
		HGDIOBJ oldPen = SelectObject(dc, pen);
		HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
		POINT points[5];
		switch (icon)
		{
			case ICON_SOURCE:
				MoveToEx(dc, 10, 10, NULL); LineTo(dc, 10, 18);
				MoveToEx(dc, 7, 18, NULL); LineTo(dc, 13, 18);
				Arc(dc, 6, 6, 14, 14, 7, 7, 7, 13);
				Arc(dc, 3, 3, 17, 17, 5, 4, 5, 16);
				break;

			case ICON_PAUSE:
				SelectObject(dc, GetStockObject(NULL_PEN));
				SelectObject(dc, GetStockObject(DC_BRUSH));
				SetDCBrushColor(dc, colour);
				Rectangle(dc, 5, 4, 9, 17);
				Rectangle(dc, 12, 4, 16, 17);
				break;

			case ICON_PLAY:
				SelectObject(dc, GetStockObject(NULL_PEN));
				SelectObject(dc, GetStockObject(DC_BRUSH));
				SetDCBrushColor(dc, colour);
				points[0].x = 6; points[0].y = 4;
				points[1].x = 16; points[1].y = 10;
				points[2].x = 6; points[2].y = 17;
				Polygon(dc, points, 3);
				break;

			case ICON_RECORD:
				SelectObject(dc, GetStockObject(NULL_PEN));
				SelectObject(dc, GetStockObject(DC_BRUSH));
				SetDCBrushColor(dc, RGB(218, 54, 51));
				Ellipse(dc, 5, 5, 16, 16);
				break;

			case ICON_STOP:
				SelectObject(dc, GetStockObject(NULL_PEN));
				SelectObject(dc, GetStockObject(DC_BRUSH));
				SetDCBrushColor(dc, RGB(218, 54, 51));
				Rectangle(dc, 5, 5, 16, 16);
				break;

			case ICON_FILTER:
				points[0].x = 3; points[0].y = 4;
				points[1].x = 17; points[1].y = 4;
				points[2].x = 12; points[2].y = 10;
				points[3].x = 12; points[3].y = 17;
				points[4].x = 8; points[4].y = 15;
				Polyline(dc, points, 5);
				MoveToEx(dc, 8, 15, NULL); LineTo(dc, 8, 10);
				LineTo(dc, 3, 4);
				break;

			case ICON_CLEAR:
				points[0].x = 4; points[0].y = 13;
				points[1].x = 11; points[1].y = 4;
				points[2].x = 17; points[2].y = 9;
				points[3].x = 10; points[3].y = 17;
				points[4].x = 4; points[4].y = 13;
				Polyline(dc, points, 5);
				MoveToEx(dc, 8, 16, NULL); LineTo(dc, 18, 16);
				break;

			case ICON_SETTINGS:
				Ellipse(dc, 6, 6, 15, 15);
				Ellipse(dc, 9, 9, 12, 12);
				for (int index = 0; index < 8; ++index)
				{
					const int outerX[8] = { 10, 15, 18, 15, 10, 5, 2, 5 };
					const int outerY[8] = { 2, 5, 10, 15, 18, 15, 10, 5 };
					const int innerX[8] = { 10, 13, 15, 13, 10, 7, 5, 7 };
					const int innerY[8] = { 5, 7, 10, 13, 15, 13, 10, 7 };
					MoveToEx(dc, innerX[index], innerY[index], NULL);
					LineTo(dc, outerX[index], outerY[index]);
				}
				break;
		}
		SelectObject(dc, oldBrush);
		SelectObject(dc, oldPen);
		DeleteObject(pen);
	}

	HBITMAP CreateToolbarBitmap(int icon, COLORREF colour)
	{
		HDC screen = GetDC(NULL);
		HDC dc = CreateCompatibleDC(screen);
		HBITMAP bitmap = CreateCompatibleBitmap(screen, 20, 20);
		HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
		HBRUSH mask = CreateSolidBrush(RGB(255, 0, 255));
		RECT bounds = { 0, 0, 20, 20 };
		FillRect(dc, &bounds, mask);
		DeleteObject(mask);
		DrawToolbarIcon(dc, icon, colour);
		SelectObject(dc, oldBitmap);
		DeleteDC(dc);
		ReleaseDC(NULL, screen);
		return bitmap;
	}

	void BuildImageList(HWND toolbar)
	{
		HIMAGELIST images = ImageList_Create(20, 20, ILC_COLOR24 | ILC_MASK, ICON_COUNT, 1);
		if (!images) return;
		for (int icon = 0; icon < ICON_COUNT; ++icon)
		{
			HBITMAP bitmap = CreateToolbarBitmap(icon, PdwThemeTextColor());
			ImageList_AddMasked(images, bitmap, RGB(255, 0, 255));
			DeleteObject(bitmap);
		}
		HIMAGELIST previous = reinterpret_cast<HIMAGELIST>(SendMessage(toolbar,
			TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(images)));
		if (previous && previous != images) ImageList_Destroy(previous);
		g_images = images;
	}

	void SetButtonPresentation(HWND toolbar, UINT command, const char* text, int image)
	{
		TBBUTTONINFOA info;
		ZeroMemory(&info, sizeof(info));
		info.cbSize = sizeof(info);
		info.dwMask = TBIF_TEXT | TBIF_IMAGE;
		info.pszText = const_cast<char*>(text);
		info.iImage = image;
		SendMessageA(toolbar, TB_SETBUTTONINFOA, command, reinterpret_cast<LPARAM>(&info));
	}

	void SetCompactMode(HWND toolbar, bool compact)
	{
		if (g_compact == compact) return;
		g_compact = compact;
		for (int index = 0; index < static_cast<int>(_countof(kActions)); ++index)
		{
			TBBUTTONINFOA info;
			ZeroMemory(&info, sizeof(info));
			info.cbSize = sizeof(info);
			info.dwMask = TBIF_STYLE;
			if (SendMessageA(toolbar, TB_GETBUTTONINFOA, kActions[index].command,
				reinterpret_cast<LPARAM>(&info)) >= 0)
			{
				info.fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE |
					(compact ? 0 : BTNS_SHOWTEXT);
				SendMessageA(toolbar, TB_SETBUTTONINFOA, kActions[index].command,
					reinterpret_cast<LPARAM>(&info));
			}
		}
	}

	void LayoutSignalMeter(HWND toolbar)
	{
		if (!toolbar || !g_signalMeter) return;
		RECT client;
		GetClientRect(toolbar, &client);
		RECT lastButton = { 0, 0, 0, 0 };
		SendMessage(toolbar, TB_GETRECT, IDM_SETTINGS, reinterpret_cast<LPARAM>(&lastButton));
		const int clientWidth = static_cast<int>(client.right - client.left);
		const int clientHeight = static_cast<int>(client.bottom - client.top);
		const int available = clientWidth - static_cast<int>(lastButton.right) - 16;
		const int width = (std::min)(LiveSignalMeterPreferredWidth(),
			(std::max)(0, available));
		const int height = (std::min)(42, (std::max)(0, clientHeight - 12));
		LiveSignalMeterMove(g_signalMeter, clientWidth - width - 8,
			(std::max)(6, (clientHeight - height) / 2), width, height);
	}
}

HWND ShowMakeToolBar(HWND parent_hwnd, HINSTANCE)
{
	INITCOMMONCONTROLSEX controls = { sizeof(controls), ICC_BAR_CLASSES };
	InitCommonControlsEx(&controls);

	HWND toolbar = CreateWindowExA(0, TOOLBARCLASSNAMEA, NULL,
		WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS |
		CCS_TOP | CCS_NODIVIDER | CCS_NORESIZE | CCS_NOPARENTALIGN,
		0, 0, 0, 0, parent_hwnd,
		reinterpret_cast<HMENU>(IDW_TOOL_BAR), GetModuleHandle(NULL), NULL);
	if (!toolbar) return NULL;

	SendMessage(toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
	SendMessage(toolbar, TB_SETEXTENDEDSTYLE, 0,
		TBSTYLE_EX_MIXEDBUTTONS | TBSTYLE_EX_DOUBLEBUFFER);
	SendMessage(toolbar, TB_SETPADDING, 0, MAKELPARAM(9, 3));
	BuildImageList(toolbar);

	TBBUTTON buttons[_countof(kActions) + 1];
	ZeroMemory(buttons, sizeof(buttons));
	int buttonCount = 0;
	for (int index = 0; index < static_cast<int>(_countof(kActions)); ++index)
	{
		if (index == 3)
		{
			buttons[buttonCount].iBitmap = 8;
			buttons[buttonCount].fsStyle = BTNS_SEP;
			++buttonCount;
		}
		buttons[buttonCount].iBitmap = kActions[index].icon;
		buttons[buttonCount].idCommand = kActions[index].command;
		buttons[buttonCount].fsState = TBSTATE_ENABLED;
		buttons[buttonCount].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT;
		buttons[buttonCount].iString = SendMessageA(toolbar, TB_ADDSTRINGA, 0,
			reinterpret_cast<LPARAM>(kActions[index].label));
		++buttonCount;
	}
	if (!SendMessage(toolbar, TB_ADDBUTTONS, buttonCount,
		reinterpret_cast<LPARAM>(buttons)))
	{
		DestroyWindow(toolbar);
		return NULL;
	}
	SendMessage(toolbar, TB_SETBUTTONWIDTH, 0, MAKELPARAM(58, 76));
	SendMessage(toolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(64, 48));

	if (PdwThemeUiFont()) SendMessage(toolbar, WM_SETFONT,
		reinterpret_cast<WPARAM>(PdwThemeUiFont()), TRUE);
	g_signalMeter = LiveSignalMeterCreate(toolbar, 2198);
	// Establish the approved icon-and-label height before the parent is first
	// shown. Waiting for a later WM_SIZE leaves a one-frame legacy-height bar on
	// startup and after some remote-display changes.
	TB_AutoSize(toolbar);
	ToolbarRefreshState();
	return toolbar;
}

BOOL GetToolBarImages(HINSTANCE) { return TRUE; }

void FreeToolBarImages(HINSTANCE)
{
	if (g_images)
	{
		if (hToolbar) SendMessage(hToolbar, TB_SETIMAGELIST, 0, 0);
		ImageList_Destroy(g_images);
		g_images = NULL;
	}
	g_signalMeter = NULL;
}

void SetToolBarButtons(void) { ToolbarRefreshState(); }
void Add_TB_ButtonsBitmaps(HWND, HINSTANCE) {}

void TB_AutoSize(HWND toolbar)
{
	if (!toolbar) return;
	RECT parent;
	GetClientRect(GetParent(toolbar), &parent);
	const bool compact = parent.right < 720;
	SetCompactMode(toolbar, compact);
	SendMessage(toolbar, TB_SETBUTTONSIZE, 0,
		MAKELPARAM(compact ? 38 : 64, 48));
	// Let the common control recalculate the internal icon-and-text row first;
	// without this pass it clips labels to its legacy 31-pixel content height.
	// TB_AUTOSIZE may also alter the outer window, so immediately reassert PDW's
	// approved 54-pixel geometry afterward.
	SendMessage(toolbar, TB_AUTOSIZE, 0, 0);
	SetWindowPos(toolbar, HWND_TOP, 0, 0, parent.right,
		PDW_COMMAND_BAR_HEIGHT,
		SWP_NOACTIVATE | SWP_SHOWWINDOW);
	LayoutSignalMeter(toolbar);
	RedrawWindow(toolbar, NULL, NULL,
		RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void SetToolTXT(HINSTANCE, LPARAM lParam)
{
	LPTOOLTIPTEXT tooltip = reinterpret_cast<LPTOOLTIPTEXT>(lParam);
	const ToolbarAction* action = FindAction(static_cast<UINT>(tooltip->hdr.idFrom));
	if (!action) return;
	if (action->command == IDT_TOOLBAR_BTN9)
		tooltip->lpszText = const_cast<char*>(bPauseFlag ? "Resume decoding" : "Pause decoding");
	else if (action->command == IDM_RECORD)
		tooltip->lpszText = const_cast<char*>((bRecording || SignalDiagnosticIsRecording()) ?
			"Stop recording" : action->tooltip);
	else tooltip->lpszText = const_cast<char*>(action->tooltip);
}

void ToolbarRefreshState(void)
{
	if (!hToolbar) return;
	SetButtonPresentation(hToolbar, IDT_TOOLBAR_BTN9,
		bPauseFlag ? "Resume" : "Pause", bPauseFlag ? ICON_PLAY : ICON_PAUSE);
	const bool recording = bRecording || SignalDiagnosticIsRecording();
	SetButtonPresentation(hToolbar, IDM_RECORD,
		recording ? "Stop" : "Record", recording ? ICON_STOP : ICON_RECORD);
	InvalidateRect(hToolbar, NULL, FALSE);
}

void ToolbarRefreshTheme(void)
{
	if (!hToolbar) return;
	BuildImageList(hToolbar);
	LiveSignalMeterRefreshTheme(g_signalMeter);
	InvalidateRect(hToolbar, NULL, TRUE);
}
