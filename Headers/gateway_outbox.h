#ifndef PDW_GATEWAY_OUTBOX_H
#define PDW_GATEWAY_OUTBOX_H

#ifndef STRICT
#define STRICT 1
#endif
#include <windows.h>

#include <string>

#include "notification.h"

struct GatewayOutboxHealth
{
	bool enabled;
	bool workerRunning;
	unsigned long queueDepth;
	unsigned long queueHighWaterMark;
	unsigned long long droppedEvents;
	unsigned long long writeFailures;
	long long highestAssignedSequence;
	long long lastCommittedSequence;
	long long oldestRetainedSequence;
	long long retainedRecords;
	unsigned long long databaseBytes;
	unsigned long long availableDiskBytes;
	bool diskWarning;
	std::string path;
	std::string lastError;

	GatewayOutboxHealth();
};

void GatewayOutboxInitialize(void);
void GatewayOutboxShutdown(void);
void GatewayOutboxSettingsChanged(void);
void GatewayOutboxPublishDecodedMessage(const DecodedMessageNotificationContext& event);
bool GatewayOutboxGenerateSynthetic(const char* protocol, std::string& error);
GatewayOutboxHealth GatewayOutboxGetHealth(void);
std::string GatewayOutboxBuildDiagnosticText(void);
BOOL FAR PASCAL GatewayOutboxDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

#ifdef PDW_GATEWAY_OUTBOX_TEST_HOOKS
void GatewayOutboxSetWorkerDelayForTest(unsigned long milliseconds);
#endif

#endif
