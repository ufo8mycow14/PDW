#ifndef PDW_NOTIFICATION_H
#define PDW_NOTIFICATION_H

#include <windows.h>
#include <stddef.h>

struct DecodedMessageNotificationContext
{
	DecodedMessageNotificationContext()
		: filterMatched(false), monitorOnly(false), filtered(false), rejected(false),
		  blockedDuplicate(false), groupCall(false), fragmented(false), assembled(false),
		  groupFinal(false), selectedForEmail(0), filterIndex(-1), groupBit(-1),
		  cycle(-1), frame(-1), address(""),
		  time(""), date(""), mode(""), messageType(""), bitrate(""), message(""),
		  filterLabel("")
	{
	}

	bool filterMatched;
	bool monitorOnly;
	bool filtered;
	bool rejected;
	bool blockedDuplicate;
	bool groupCall;
	bool fragmented;
	bool assembled;
	bool groupFinal;
	int selectedForEmail;
	int filterIndex;
	int groupBit;
	int cycle;
	int frame;
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
