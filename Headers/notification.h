#ifndef PDW_NOTIFICATION_H
#define PDW_NOTIFICATION_H

#include <windows.h>
#include <stddef.h>

struct DecodedMessageNotificationContext
{
	bool filterMatched;
	bool monitorOnly;
	bool filtered;
	int selectedForEmail;
	const char *address;
	const char *time;
	const char *date;
	const char *mode;
	const char *messageType;
	const char *bitrate;
	const char *message;
	const char *filterLabel;
};

void NotificationManagerInitialize(void);
void NotificationManagerShutdown(void);
void NotificationSettingsChanged(void);
void NotificationGetStatusText(char *buffer, size_t bufferSize);
void NotificationPublishDecodedMessage(const DecodedMessageNotificationContext &context);
BOOL FAR PASCAL AppriseDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

#endif
