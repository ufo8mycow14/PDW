#ifndef PDW_STARTUP_H
#define PDW_STARTUP_H

#include <windows.h>
#include <stddef.h>

BOOL IsStartWithWindowsEnabled(void);
BOOL SetStartWithWindowsEnabled(BOOL enabled, char* errorText, size_t errorTextSize);

#endif
