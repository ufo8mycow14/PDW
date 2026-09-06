#ifndef SOUND_IN_H
#define SOUND_IN_H

#define DEFAULT_HI_AUDIO	1
#define DEFAULT_LO_AUDIO	0

#define INDEX512	0
#define INDEX1200	1
#define INDEX2400	2
#define INDEX1600	3
#define INDEX3200	4

extern bool bCapturing;
extern bool bUsingWasapiFallback;
extern char high_audio;
extern char low_audio;
extern long BaudRate;
extern int  cross_over;

#define PDW_LIVE_SIGNAL_WAVEFORM_COUNT 128

typedef struct PdwLiveSignalSnapshot
{
	float rmsLevel;
	float peakLevel;
	float noiseLevel;
	float clippingPercent;
	float signalQuality;
	unsigned long long sampleCount;
	unsigned int sampleRate;
	int sourceKind; // 0=WinMM/local, 1=WASAPI, 2=RTL-TCP, 3=RTL-SDR
	int configuredSource; // persisted AUDIO_SOURCE_* selection
	int receiverState; // RtlTcpState value for configured radio source
	int captureActive;
	unsigned long lastIqCallbackTick;
	unsigned long lastIqAgeMs;
	unsigned long retryInMs;
	unsigned int decodeQueueDepth;
	unsigned int decodeQueueCapacity;
	unsigned int decodeQueueHighWater;
	unsigned long long decodeQueueDrops;
	unsigned long long decodeDroppedSamples;
	unsigned long long decodeResetDiscardedBlocks;
	unsigned long long decodeResetDiscardedSamples;
	unsigned long long decodeQueuePreallocatedBytes;
	unsigned long long decodedAudioSamples;
	unsigned long long decoderLagMs;
	unsigned long long maximumDecoderLagMs;
	unsigned long long decoderThreadStarts;
	unsigned long long decoderThreadStops;
	unsigned long long decoderHandlesCreated;
	unsigned long long decoderHandlesClosed;
	unsigned long long decoderStopTimeouts;
	unsigned int decoderOwnedHandles;
	int decoderWorkerRunning;
	int decoderPriorityElevated;
	int decoderQuarantined;
	unsigned long acceptedDecodeRows;
	unsigned long lastAcceptedDecodeTick;
	unsigned long lastAcceptedDecodeAgeMs;
	unsigned long processHandleCount;
	unsigned long processGdiObjectCount;
	unsigned long processUserObjectCount;
	unsigned long meterPaintCount;
	unsigned long meterSkippedUpdateCount;
	unsigned long meterSuspendedUpdateCount;
	unsigned long meterTooltipUpdateCount;
	unsigned long meterBackbufferCreateCount;
	unsigned long meterBackbufferDeleteCount;
	unsigned long meterActiveBackbufferObjects;
	char receiverStatus[128];
	char lastReceiverError[256];
	int diagnosticRecording;
	int diagnosticReplay;
	unsigned int waveformCount;
	float waveform[PDW_LIVE_SIGNAL_WAVEFORM_COUNT];
} PdwLiveSignalSnapshot;

void CALLBACK Callback_Function(HWAVEIN hwi, UINT uMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2);
void Process_ReadyBuffers(HWND hwnd);
void free_audio_buffers(void);
BOOL Stop_Capturing(void);
BOOL Start_Capturing(void);
bool FinalizeCaptureForShutdown(void);
void SignalSourceService(void);

void Audio_To_Bits  (char *lpAudioBuffer, long LenAudioBuffer);
void MOBITEX_To_Bits(char *lpAudioBuffer, long LenAudioBuffer);
void ACARS_To_Bits  (char *lpAudioBuffer, long LenAudioBuffer);
void ERMES_To_Bits  (char *lpAudioBuffer, long LenAudioBuffer); // PH: test

void Reset_ATB(void);
void SetAudioConfig(int sac_type);
int Get_Percent(int x,int percent);
BOOL FAR PASCAL SignalSourceDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
bool SignalDiagnosticStartRecording(const char *path, char *error, size_t errorSize);
bool SignalDiagnosticStopRecording(char *error, size_t errorSize);
bool SignalDiagnosticStartReplay(const char *path, char *error, size_t errorSize);
bool SignalDiagnosticStopReplay(void);
bool SignalDiagnosticIsRecording(void);
bool SignalDiagnosticIsReplaying(void);
bool SignalDiagnosticToggleRecording(HWND owner);
void SignalDiagnosticsRecordDecodeResult(int errors);
void SignalDiagnosticsRecordAcceptedDecodeRow(void);
bool SignalDiagnosticsGetLiveSnapshot(PdwLiveSignalSnapshot* snapshot);
void SignalDecoderStateEnter(void);
void SignalDecoderStateLeave(void);

#ifdef __cplusplus
class PdwSignalDecoderStateGuard
{
public:
	PdwSignalDecoderStateGuard() { SignalDecoderStateEnter(); }
	~PdwSignalDecoderStateGuard() { SignalDecoderStateLeave(); }
private:
	PdwSignalDecoderStateGuard(const PdwSignalDecoderStateGuard&);
	PdwSignalDecoderStateGuard& operator=(const PdwSignalDecoderStateGuard&);
};
#endif

#endif


