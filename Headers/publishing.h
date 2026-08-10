#ifndef PDW_PUBLISHING_H
#define PDW_PUBLISHING_H

#include <windows.h>
#include <stddef.h>

struct DecodedMessageNotificationContext;

void PublishingManagerInitialize(void);
void PublishingManagerShutdown(void);
void PublishingSettingsChanged(void);
void PublishingGetStatusText(char* buffer, size_t bufferSize);
void PublishingPublishDecodedMessage(const DecodedMessageNotificationContext& context);
BOOL FAR PASCAL PublishingDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

#endif
