#ifndef FTP_H
#define FTP_H

#include <windows.h>
#include <stddef.h>

void FtpInitialize(void);
void FtpShutdown(void);
void FtpSchedulerTick(void);
void FtpSettingsChanged(void);
bool FtpQueueUploadNow(void);
void FtpGetStatusText(char *buffer, size_t bufferSize);

BOOL FAR PASCAL FtpDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

#endif
