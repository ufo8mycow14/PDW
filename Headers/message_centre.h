#ifndef PDW_MESSAGE_CENTRE_H
#define PDW_MESSAGE_CENTRE_H

#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>

BOOL FAR PASCAL CapcodeDirectoryDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
BOOL FAR PASCAL MessageHistoryDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
BOOL FAR PASCAL LiveDashboardDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

#endif
