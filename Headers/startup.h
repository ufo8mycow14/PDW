#ifndef PDW_STARTUP_H
#define PDW_STARTUP_H

#include <windows.h>
#include <stddef.h>

BOOL IsStartWithWindowsEnabled(void);
BOOL SetStartWithWindowsEnabled(BOOL enabled, char* errorText, size_t errorTextSize);
bool RunReceiverWatchMode(const char* command, int& exitCode);
unsigned ReceiverRecoveryAttempt(const char* command);
void ArmReceiverRecovery(unsigned attempt);
void DisarmReceiverRecovery();

#endif
