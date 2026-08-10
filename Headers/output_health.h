#ifndef PDW_OUTPUT_HEALTH_H
#define PDW_OUTPUT_HEALTH_H

#include <windows.h>
#include <stddef.h>

#define OUTPUT_HEALTH_ALERT_MESSAGE (WM_APP + 73)

enum OUTPUT_HEALTH_DESTINATION
{
	OUTPUT_HEALTH_SMTP = 0,
	OUTPUT_HEALTH_APPRISE,
	OUTPUT_HEALTH_FTP,
	OUTPUT_HEALTH_PUBLISHING,
	OUTPUT_HEALTH_MQTT,
	OUTPUT_HEALTH_SQLITE,
	OUTPUT_HEALTH_MYSQL_ODBC,
	OUTPUT_HEALTH_TELNET,
	OUTPUT_HEALTH_WINDOWS_TOAST,
	OUTPUT_HEALTH_DATA_ROUTER,
	OUTPUT_HEALTH_DESTINATION_COUNT
};

enum OUTPUT_HEALTH_OUTCOME
{
	OUTPUT_HEALTH_INFO = 0,
	OUTPUT_HEALTH_SUCCESS,
	OUTPUT_HEALTH_FAILURE,
	OUTPUT_HEALTH_DROPPED
};

typedef struct
{
	int destination;
	int enabled;
	unsigned long successes;
	unsigned long failures;
	unsigned long dropped;
	unsigned int consecutiveFailures;
	int alertPending;
	char name[40];
	char state[20];
	char lastUtc[40];
	char summary[256];
} OUTPUT_HEALTH_SNAPSHOT;

typedef struct
{
	int destination;
	int outcome;
	char destinationName[40];
	char outcomeName[20];
	char utc[40];
	char summary[256];
} OUTPUT_HEALTH_EVENT;

void OutputHealthInitialize(HWND alertWindow);
void OutputHealthShutdown(void);
void OutputHealthConfigure(int alertsEnabled, unsigned int failureThreshold);
void OutputHealthSetEnabled(int destination, int enabled);
void OutputHealthRecord(int destination, int outcome, const char* summary);
size_t OutputHealthGetSnapshots(OUTPUT_HEALTH_SNAPSHOT* snapshots, size_t capacity);
size_t OutputHealthGetHistory(OUTPUT_HEALTH_EVENT* events, size_t capacity);
unsigned int OutputHealthGetPendingAlertCount(void);
void OutputHealthAcknowledgeAll(void);
BOOL FAR PASCAL OutputHealthDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

#endif
