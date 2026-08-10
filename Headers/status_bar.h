#ifndef PDW_STATUS_BAR_H
#define PDW_STATUS_BAR_H

#include <windows.h>

HWND PdwStatusBarCreate(HWND parent, UINT controlId);
void PdwStatusBarResize(HWND statusBar, int parentWidth, int parentHeight);
void PdwStatusBarRefresh(HWND statusBar);
int PdwStatusBarHeight(HWND parent);

#endif
