#ifndef TOOL_BAR_H
#define TOOL_BAR_H

// Keep the command-bar and pane layout on one shared geometry contract. The
// common toolbar control must not substitute its legacy single-line height.
enum { PDW_COMMAND_BAR_HEIGHT = 54 };

HWND ShowMakeToolBar(HWND parent_hwnd,HINSTANCE hThisInstance);
BOOL GetToolBarImages(HINSTANCE hThisInstance);
void FreeToolBarImages(HINSTANCE hThisInstance);
void SetToolBarButtons(void);
void Add_TB_ButtonsBitmaps(HWND tbar_hwnd,HINSTANCE hThisInstance);
void TB_AutoSize(HWND hTbar);
void SetToolTXT(HINSTANCE hThisInstance, LPARAM lParam);
void ToolbarRefreshState(void);
void ToolbarRefreshTheme(void);

#endif
