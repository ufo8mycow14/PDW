#ifndef PDW_LIVE_SIGNAL_METER_H
#define PDW_LIVE_SIGNAL_METER_H

#include <windows.h>

HWND LiveSignalMeterCreate(HWND parent, UINT controlId);
void LiveSignalMeterMove(HWND meter, int x, int y, int width, int height);
void LiveSignalMeterRefreshTheme(HWND meter);
void LiveSignalMeterNoteLegacyActivity(int direction);
int LiveSignalMeterPreferredWidth(void);

typedef struct PdwLiveSignalMeterTelemetry
{
	unsigned long paintCount;
	unsigned long skippedUpdateCount;
	unsigned long suspendedUpdateCount;
	unsigned long tooltipUpdateCount;
	unsigned long backbufferCreateCount;
	unsigned long backbufferDeleteCount;
	unsigned long activeBackbufferObjects;
} PdwLiveSignalMeterTelemetry;

void LiveSignalMeterGetTelemetry(PdwLiveSignalMeterTelemetry* telemetry);

#endif
