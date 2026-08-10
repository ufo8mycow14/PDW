#ifndef PDW_DATA_OUTPUTS_H
#define PDW_DATA_OUTPUTS_H

#include <windows.h>
#include <stddef.h>

namespace pdw { namespace publishing { struct PublishEvent; } }
struct DecodedMessageNotificationContext;

void DataOutputManagerInitialize(void);
void DataOutputManagerShutdown(void);
void DataOutputSettingsChanged(void);
void DataOutputGetStatusText(char* buffer, size_t bufferSize);
void DataOutputPublishEvent(const pdw::publishing::PublishEvent& event);
void DataOutputPublishDecodedMessage(const DecodedMessageNotificationContext& context);
BOOL FAR PASCAL DataOutputsDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

#endif
