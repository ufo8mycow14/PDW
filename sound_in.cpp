// Sound_in.cpp
//
//  This file does the following:
//
//   1.Opens the audio device.
//   2.Captures audio data.
//   3.Converts audio data to bits based on baud rate.
//   4.Calls required routines to process data bits.
//   5.Closes the audio device.
//
//
#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>

#include "headers\resource.h"
#include "headers\PDW.h"
#include "headers\initapp.h"
#include "headers\sigind.h"
#include "headers\decode.h"
#include "headers\sound_in.h"
#include "headers\acars.h"
#include "headers\mobitex.h"
#include "headers\ermes.h"		// PH: new
#include "utils\audio_signal_core.h"
#include "utils\signal_recording_core.h"
#include "utils\wasapi_capture.h"
#include "utils\rtl_tcp_source.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <string>
#include <vector>

// #define AU_ACARS_BIT_TEST  1
// #define AU_PF_BIT_TEST     1

#define NUMBER_BUFFERS            10      // Number of buffers to place in input queue
#define SIZEOF_AUDIOBUFFER        8192    // Size of buffers used to store audio data
//#define SAMPLESPERSECOND_DEFAULT  44100
//#define NUM_BITS_PER_SAMPLE       8

#define COURSE_CLKT_HI            2.02     // Course clock for
#define COURSE_CLKT_LO            0.18     // POCSAG 512.
#define FINE_CLKT_HI              1.90     // Fine clock for
#define FINE_CLKT_LO              0.32     // 1200/2400/1600 baud rates.
#define FINE_CLKT3200_HI          1.92     // Fine clock for
#define FINE_CLKT3200_LO          0.34     // 3200 baud rate(6400 flex).


// These help decide when we cross the zero line.
int au_threshold[10] = {0, 1, 2, 5, 9, 14, 17, 24, 30, 44};

// Used for offsetting bit center / zero center
int au_offset_center[10] = { 0, 1, -1, 2, -2, 3, -3, 4, -4, 5 };

HWAVEIN  hWaveIn;                    // Handle to audio device
HWAVEOUT hWaveOut;                   // Handle to audio device
WAVEHDR WaveHeader[NUMBER_BUFFERS];  // Audio buffers to be put into audio queue
int buffers_ready=0;                 // Used by callback function to indicate buffer(s) ready
int last_buff_processed = -1;        // Used for predicting next buffer to be filled.
bool bCapturing=false;               // Used to check to see if capturing is enabled.
bool bUsingWasapiFallback=false;
char high_audio=DEFAULT_HI_AUDIO;
char low_audio =DEFAULT_LO_AUDIO;

// Preamble search variables - Used by Audio_To_Bits()
static int val=0;
int nSamples=0;
int preamble_count[3]={0};
int flex_cnt_1600=0;
int sync_bit=DEFAULT_LO_AUDIO;
int crossing=0;
int pre_threshold=0;

// Main loop variables - Used by Audio_To_Bits()
long atb_ctr;
char atb_bit=0;
int atb_value;
int atb_len=0;
long double WatchStep;
long double clkt_hi = FINE_CLKT_HI;
long double clkt_lo = FINE_CLKT_LO;
double WatchCtr;
long BaudRate = 1600;	// default
long last_baud_rate = 0;
int atb_sig_cnt=0;           // When to update signal indicator.
int atb_center[5];           // Used for centering bit stream.
int atb_threshold[5];
int atb_sample_offset[5];
int config_index = 1;
int cross_over = 0;
int skipped_sc = 0;

// pocsag globals
extern POCSAG pocsag;
extern int pocsag_baud_rate, pocbit;

// ACARS globals
int process_acars_bit = 0;

HGLOBAL h_audio_memory_block[NUMBER_BUFFERS];
int audio_buffer_cnt = 0;

namespace
{
	struct WasapiQueuedBlock
	{
		std::vector<float> samples;
		std::uint32_t sampleRate;
		bool discontinuity;
	};

	class WasapiFallbackSink : public pdw::signal::WasapiCaptureSink
	{
	public:
		WasapiFallbackSink() : dropped_(false) { InitializeCriticalSection(&lock_); }
		~WasapiFallbackSink() { DeleteCriticalSection(&lock_); }

		void OnAudioSamples(const float* samples, std::size_t sampleCount,
			std::uint32_t sampleRate, bool discontinuity)
		{
			if (!samples || !sampleCount) return;
			WasapiQueuedBlock block;
			block.samples.assign(samples, samples + sampleCount);
			block.sampleRate = sampleRate;
			block.discontinuity = discontinuity;
			EnterCriticalSection(&lock_);
			if (blocks_.size() >= 32)
			{
				blocks_.pop_front();
				dropped_ = true;
			}
			if (dropped_)
			{
				block.discontinuity = true;
				dropped_ = false;
			}
			blocks_.push_back(block);
			LeaveCriticalSection(&lock_);
		}

		bool Pop(WasapiQueuedBlock& block)
		{
			EnterCriticalSection(&lock_);
			const bool available = !blocks_.empty();
			if (available)
			{
				block = blocks_.front();
				blocks_.pop_front();
			}
			LeaveCriticalSection(&lock_);
			return available;
		}

		void Clear()
		{
			EnterCriticalSection(&lock_);
			blocks_.clear();
			dropped_ = false;
			LeaveCriticalSection(&lock_);
		}

	private:
		CRITICAL_SECTION lock_;
		std::deque<WasapiQueuedBlock> blocks_;
		bool dropped_;
	};

	WasapiFallbackSink g_wasapiFallbackSink;
	pdw::signal::WasapiCaptureSource g_wasapiFallbackSource;
	pdw::signal::RtlTcpSource g_rtlTcpSource;
	pdw::signal::RtlSdrSource g_rtlSdrSource;
	pdw::signal::AdaptiveSlicer g_enhancedAudioSlicer;
	std::uint32_t g_activeAudioSampleRate = 44100;
	int g_modernCaptureKind = 0; // 0=none, 1=WASAPI fallback, 2=rtl_tcp, 3=RTL-SDR
	pdw::signal::SignalRecording g_diagnosticRecording;
	std::string g_diagnosticRecordingPath;
	bool g_diagnosticRecordingActive = false;
	bool g_diagnosticRecordingTruncated = false;
	pdw::signal::SignalRecording g_diagnosticReplay;
	std::size_t g_diagnosticReplayPosition = 0;
	bool g_diagnosticReplayActive = false;
	bool g_diagnosticReplayResumeAudio = false;
	bool g_diagnosticReplayResumeSerial = false;
	const std::size_t MAX_DIAGNOSTIC_SAMPLES = 25u * 1000u * 1000u;

	void CopyDiagnosticError(char* destination, std::size_t destinationSize,
		const std::string& error)
	{
		if (!destination || destinationSize == 0) return;
		strncpy(destination, error.c_str(), destinationSize - 1);
		destination[destinationSize - 1] = '\0';
	}

	std::string LowercasePath(const std::string& path)
	{
		std::string lowered(path);
		std::transform(lowered.begin(), lowered.end(), lowered.begin(),
			[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
		return lowered;
	}

	bool EndsWith(const std::string& value, const char* suffix)
	{
		const std::size_t suffixLength = strlen(suffix);
		return value.size() >= suffixLength &&
			value.compare(value.size() - suffixLength, suffixLength, suffix) == 0;
	}

	std::string SigMfBasePath(const std::string& path)
	{
		const std::string lowered = LowercasePath(path);
		if (EndsWith(lowered, ".sigmf-meta")) return path.substr(0, path.size() - 11);
		if (EndsWith(lowered, ".sigmf-data")) return path.substr(0, path.size() - 11);
		if (EndsWith(lowered, ".sigmf")) return path.substr(0, path.size() - 6);
		return path;
	}

	void AppendDiagnosticSamples(const float* samples, std::size_t sampleCount,
		std::uint32_t sampleRate)
	{
		if (!g_diagnosticRecordingActive || !samples || sampleCount == 0) return;
		if (g_diagnosticRecording.samples.empty())
			g_diagnosticRecording.sampleRate = sampleRate;
		if (sampleRate != g_diagnosticRecording.sampleRate)
		{
			g_diagnosticRecordingTruncated = true;
			return;
		}
		const std::size_t remaining = MAX_DIAGNOSTIC_SAMPLES -
			(std::min)(MAX_DIAGNOSTIC_SAMPLES, g_diagnosticRecording.samples.size());
		const std::size_t appendCount = (std::min)(remaining, sampleCount);
		g_diagnosticRecording.samples.insert(g_diagnosticRecording.samples.end(),
			samples, samples + appendCount);
		if (appendCount != sampleCount) g_diagnosticRecordingTruncated = true;
	}

	void FeedNormalizedSamples(const float* samples, std::size_t sampleCount)
	{
		if (!samples || sampleCount == 0) return;
		std::vector<char> pcm8(sampleCount);
		for (std::size_t index = 0; index < sampleCount; ++index)
		{
			int value = static_cast<int>(samples[index] * 128.0f + 128.0f);
			if (value < 0) value = 0;
			if (value > 255) value = 255;
			pcm8[index] = static_cast<char>(static_cast<unsigned char>(value));
		}
		if (Profile.monitor_paging)
			Audio_To_Bits(&pcm8[0], static_cast<long>(pcm8.size()));
		else if (Profile.monitor_acars)
			ACARS_To_Bits(&pcm8[0], static_cast<long>(pcm8.size()));
		else if (Profile.monitor_mobitex)
			MOBITEX_To_Bits(&pcm8[0], static_cast<long>(pcm8.size()));
	}

	std::uint32_t ActiveAudioSampleRate()
	{
		return g_activeAudioSampleRate ? g_activeAudioSampleRate : 44100;
	}

	void ProcessWasapiFallbackBlocks()
	{
		WasapiQueuedBlock block;
		int processedBlocks = 0;
		while (processedBlocks < 8 && g_wasapiFallbackSink.Pop(block))
		{
			if (block.sampleRate != g_activeAudioSampleRate || block.discontinuity)
			{
				g_activeAudioSampleRate = block.sampleRate;
				Reset_ATB();
			}
			AppendDiagnosticSamples(&block.samples[0], block.samples.size(), block.sampleRate);
			FeedNormalizedSamples(&block.samples[0], block.samples.size());
			processedBlocks++;
		}
	}

	void StopDiagnosticReplayInternal(bool resumeInput)
	{
		if (!g_diagnosticReplayActive) return;
		g_diagnosticReplayActive = false;
		g_diagnosticReplay.samples.clear();
		g_diagnosticReplayPosition = 0;
		bCapturing = false;
		Reset_ATB();
		if (resumeInput)
		{
			if (g_diagnosticReplayResumeSerial) LoadDriver();
			else if (g_diagnosticReplayResumeAudio) Start_Capturing();
			SetTimer(ghWnd, PDW_TIMER, 100, (TIMERPROC)NULL);
		}
		g_diagnosticReplayResumeAudio = false;
		g_diagnosticReplayResumeSerial = false;
	}

	bool TryStartWasapiFallback()
	{
		if (hWaveOut) { waveOutClose(hWaveOut); hWaveOut = NULL; }
		g_wasapiFallbackSink.Clear();
		if (!g_wasapiFallbackSource.StartDefault(&g_wasapiFallbackSink)) return false;
		bUsingWasapiFallback = true;
		g_modernCaptureKind = 1;
		bCapturing = true;
		Reset_ATB();
		return true;
	}

	bool TryStartRtlTcp()
	{
		pdw::signal::RtlTcpConfig config;
		config.host = Profile.rtlTcpHost;
		config.port = static_cast<std::uint16_t>(Profile.rtlTcpPort);
		config.frequencyHz = Profile.rtlFrequencyHz;
		config.sampleRate = Profile.rtlSampleRate;
		config.audioSampleRate = Profile.rtlAudioSampleRate;
		config.gainTenthsDb = Profile.rtlGainTenthsDb;
		config.frequencyCorrectionPpm = Profile.rtlFrequencyCorrectionPpm;
		config.automaticGain = Profile.rtlAutomaticGain != 0;
		g_wasapiFallbackSink.Clear();
		if (!g_rtlTcpSource.Start(config, &g_wasapiFallbackSink)) return false;
		bUsingWasapiFallback = true;
		g_modernCaptureKind = 2;
		bCapturing = true;
		g_activeAudioSampleRate = config.audioSampleRate;
		Reset_ATB();
		return true;
	}

	bool TryStartRtlSdr()
	{
		pdw::signal::RtlTcpConfig config;
		config.frequencyHz = Profile.rtlFrequencyHz;
		config.sampleRate = Profile.rtlSampleRate;
		config.audioSampleRate = Profile.rtlAudioSampleRate;
		config.gainTenthsDb = Profile.rtlGainTenthsDb;
		config.frequencyCorrectionPpm = Profile.rtlFrequencyCorrectionPpm;
		config.automaticGain = Profile.rtlAutomaticGain != 0;
		g_wasapiFallbackSink.Clear();
		if (!g_rtlSdrSource.Start(config, static_cast<unsigned int>(Profile.rtlDeviceIndex),
			&g_wasapiFallbackSink)) return false;
		bUsingWasapiFallback = true;
		g_modernCaptureKind = 3;
		bCapturing = true;
		g_activeAudioSampleRate = config.audioSampleRate;
		Reset_ATB();
		return true;
	}
}

// Routines and variables used for debugging.
#ifdef AUDIO_IN_DEBUG
void Display_Sync(char bit);
void Debug_MSG(char *msg);
BOOL Test_Sync(int next_bit);
void Debug_BIT_MSG(char *msg_bit);
#endif

extern bool bMode_IDLE;
extern int nDriverLoaded;

//   Start_Capturing
//
//   Starts capturing audio data from the soundcard.
//
BOOL Start_Capturing(void)
{
	WAVEFORMATEX my_wave_format={0};
	HGLOBAL h_memory_block = NULL;
	LPSTR  lp_memory_block = NULL;
	MMRESULT result;
	char *msg;

	bCapturing = false;
	bUsingWasapiFallback = false;
	g_modernCaptureKind = 0;
	g_activeAudioSampleRate = static_cast<std::uint32_t>(Profile.audioSampleRate);
	if (Profile.audioSource == AUDIO_SOURCE_RTL_TCP)
	{
		if (TryStartRtlTcp()) return(TRUE);
		MessageBox(ghWnd, g_rtlTcpSource.lastError().c_str(), "PDW RTL-TCP", MB_ICONERROR);
		return(FALSE);
	}
	if (Profile.audioSource == AUDIO_SOURCE_RTL_SDR)
	{
		if (TryStartRtlSdr()) return(TRUE);
		MessageBox(ghWnd, g_rtlSdrSource.lastError().c_str(), "PDW RTL-SDR", MB_ICONERROR);
		return(FALSE);
	}

	// Describe the type of audio connection we want to open
	my_wave_format.wFormatTag		= WAVE_FORMAT_PCM;
	my_wave_format.nChannels		= 1;
	my_wave_format.nSamplesPerSec	= Profile.audioSampleRate;
	my_wave_format.nAvgBytesPerSec	= (DWORD)Profile.audioSampleRate;
	my_wave_format.nBlockAlign		= 1;
	my_wave_format.wBitsPerSample	= 8;
	my_wave_format.cbSize			= 0;

	// Open audio device meeting our requirements
	waveOutOpen(&hWaveOut, WAVE_MAPPER, &my_wave_format,
			(DWORD)Callback_Function, 0, CALLBACK_FUNCTION);

	result = waveInOpen(&hWaveIn, Profile.audioDevice, &my_wave_format,
			(DWORD)Callback_Function, 0, CALLBACK_FUNCTION);

	if (result) // error?
	{
		switch(result)
		{
			case MMSYSERR_ALLOCATED:
				msg = "ERROR: Audio device already allocated!";
				break;
			case MMSYSERR_BADDEVICEID:
				msg = "ERROR: Audio device ID error!";
				break;
			case MMSYSERR_NODRIVER:
				msg = "ERROR: No Audio device driver present!";
				break;
			case MMSYSERR_NOMEM:
				msg = "ERROR: No memory for Audio device!";
				break;
			case WAVERR_BADFORMAT:
				msg = "ERROR: WAVE_FORMAT_PCM not supported!";
				break;
			default:
				msg = "ERROR: Unable to open the audio device!";
				break;
		}

		if (TryStartWasapiFallback()) return(TRUE);

		lstrcpy(szDialogErrorMsg, TEXT(msg));
		MessageBox(ghWnd, msg, "PDW Soundcard",MB_ICONERROR);

		return(FALSE);
	}
    
	// Prepare buffers and add them to the input queue for the Audio API to fill.
	for (int ctr=0; ctr<NUMBER_BUFFERS; ctr++)
	{
		h_memory_block = (HGLOBAL)GlobalAlloc(GHND, SIZEOF_AUDIOBUFFER);

		if(!h_memory_block)
		{
			waveInClose(hWaveIn);
			free_audio_buffers();
			return(FALSE);
		}
		lp_memory_block = (LPSTR)GlobalLock(h_memory_block);

		if(!lp_memory_block)
		{
			waveInClose(hWaveIn);
			free_audio_buffers();
			return(FALSE);
		}

		// Keep track of buffers allocated.
		h_audio_memory_block[ctr] = h_memory_block;
		audio_buffer_cnt++;

		WaveHeader[ctr].dwFlags			= 0;
		WaveHeader[ctr].dwLoops			= 0;
		WaveHeader[ctr].dwUser			= 0;
		WaveHeader[ctr].lpNext			= 0;
		WaveHeader[ctr].dwBufferLength	= SIZEOF_AUDIOBUFFER;
		WaveHeader[ctr].dwBytesRecorded	= 0;
		WaveHeader[ctr].lpData			= (LPSTR)lp_memory_block;

		waveInPrepareHeader(hWaveIn, &WaveHeader[ctr], (UINT)sizeof(WaveHeader[ctr]));

		// Add buffer to input queue
		waveInAddBuffer(hWaveIn, &WaveHeader[ctr], (UINT)sizeof(WaveHeader[ctr]));
	}
    
	last_buff_processed = -1;

	Reset_ATB(); // Reset all variables used by Audio_To_Bits().

	// Start capturing audio
	if (waveInStart(hWaveIn) == MMSYSERR_NOERROR)
	{
		bCapturing = true;
		return(TRUE);     // OK!
	}
	waveInClose(hWaveIn);
	hWaveIn = NULL;
	free_audio_buffers();
	if (TryStartWasapiFallback()) return(TRUE);
	return(FALSE);
}

//   Stop_Capturing
//
//   Resets the connection to the audio device and closes it.
//
BOOL Stop_Capturing(void)
{   
	if (g_diagnosticReplayActive)
	{
		StopDiagnosticReplayInternal(false);
		return(TRUE);
	}
	bCapturing = false;
	if (bUsingWasapiFallback)
	{
		if (g_modernCaptureKind == 2) g_rtlTcpSource.Stop();
		else if (g_modernCaptureKind == 3) g_rtlSdrSource.Stop();
		else g_wasapiFallbackSource.Stop();
		g_wasapiFallbackSink.Clear();
		bUsingWasapiFallback = false;
		g_modernCaptureKind = 0;
		buffers_ready = 0;
		last_buff_processed = -1;
		return(FALSE);
	}
	if (!hWaveIn)
	{
		if (hWaveOut) { waveOutClose(hWaveOut); hWaveOut = NULL; }
		return(FALSE);
	}

	// Reset the audio connection... takes waiting buffers out of input queue
	waveInReset(hWaveIn);

	for (int header = 0; header < audio_buffer_cnt; ++header)
		waveInUnprepareHeader(hWaveIn, &WaveHeader[header], (UINT)sizeof(WaveHeader[header]));
	const MMRESULT closeResult = waveInClose(hWaveIn);
	hWaveIn = NULL;
	if (hWaveOut) { waveOutClose(hWaveOut); hWaveOut = NULL; }

	// Free memory used for audio buffers.
	free_audio_buffers();

	buffers_ready = 0;
	last_buff_processed = -1;

	return(closeResult == MMSYSERR_NOERROR ? TRUE : FALSE);
}

// Freeup audio buffers and reset "audio_buffer_cnt".
void free_audio_buffers(void)
{
	if (!audio_buffer_cnt) return;	// Were any buffers allocated?

	for (int i=0; i<audio_buffer_cnt; i++)
	{
		GlobalUnlock(h_audio_memory_block[i]);
		GlobalFree(h_audio_memory_block[i]);
		h_audio_memory_block[i] = NULL;
	}
	audio_buffer_cnt = 0;
	buffers_ready = 0;
}

//   Process_ReadyBuffers
//
//   Called by the timer control when buffers have been filled
//   and are ready to be processed. Calls Audio_To_Bits() to convert
//   digital audio data contained in the buffers into data bits.
//
//   This function also checks if messages want logging.
//
void Process_ReadyBuffers(HWND hwnd)
{
	int old_buffs_ready;

	if (flex_timer)	// If dropping out of FLEX mode reset and start over
	{
		bMode_IDLE = false;
		flex_timer--;

		if (flex_timer == 0)
		{
			bMode_IDLE = true;
			if (!pocbit)	// Don't reset if POCSAG signal found immediately after flex signal.
			{
				BaudRate = 1600;
				config_index=INDEX1600;
				display_showmo(MODE_IDLE);
			}
		}
	}
	else if (mb.timer)	// Check if dropped out of mobitex mode.
	{
		mb.timer--;
		if (mb.timer == 0) display_showmo(MODE_IDLE);
	}

	check_save_data();      // Log messages/status info.
	if (g_diagnosticReplayActive)
	{
		const std::size_t remaining = g_diagnosticReplay.samples.size() -
			g_diagnosticReplayPosition;
		const std::size_t desired = (std::max)(static_cast<std::size_t>(1),
			static_cast<std::size_t>(g_diagnosticReplay.sampleRate / 10u));
		const std::size_t chunk = (std::min)(remaining, desired);
		if (chunk)
		{
			const float* samples = &g_diagnosticReplay.samples[g_diagnosticReplayPosition];
			AppendDiagnosticSamples(samples, chunk, g_diagnosticReplay.sampleRate);
			FeedNormalizedSamples(samples, chunk);
			g_diagnosticReplayPosition += chunk;
		}
		if (g_diagnosticReplayPosition >= g_diagnosticReplay.samples.size())
			StopDiagnosticReplayInternal(true);
		return;
	}
	if (bUsingWasapiFallback)
	{
		const pdw::signal::WasapiCaptureState wasapiState = g_wasapiFallbackSource.state();
		if (g_modernCaptureKind == 1 &&
			(wasapiState == pdw::signal::WASAPI_CAPTURE_DEVICE_LOST ||
			wasapiState == pdw::signal::WASAPI_CAPTURE_FAILED))
		{
			static DWORD lastRestartAttempt = 0;
			const DWORD now = GetTickCount();
			if (now - lastRestartAttempt >= 2000)
			{
				lastRestartAttempt = now;
				g_wasapiFallbackSource.StartDefault(&g_wasapiFallbackSink);
			}
		}
		ProcessWasapiFallbackBlocks();
		return;
	}
	old_buffs_ready = buffers_ready;

	for (int ctr=0; ctr<old_buffs_ready; ctr++)
	{
		last_buff_processed++;

		if (last_buff_processed > (NUMBER_BUFFERS-1)) last_buff_processed = 0;

		// Do main data processing.
		if (g_diagnosticRecordingActive)
		{
			std::vector<float> diagnosticSamples(WaveHeader[last_buff_processed].dwBufferLength);
			for (std::size_t sample = 0; sample < diagnosticSamples.size(); ++sample)
			{
				const unsigned char value = static_cast<unsigned char>(
					WaveHeader[last_buff_processed].lpData[sample]);
				diagnosticSamples[sample] = pdw::signal::NormalizePcm8(value);
			}
			AppendDiagnosticSamples(&diagnosticSamples[0], diagnosticSamples.size(),
				static_cast<std::uint32_t>(Profile.audioSampleRate));
		}
 
		if (Profile.monitor_paging)		// POCSAG/FLEX decoding?
		{
			Audio_To_Bits(WaveHeader[last_buff_processed].lpData,
			WaveHeader[last_buff_processed].dwBufferLength);
		}
		else if (Profile.monitor_acars)	// ACARS..
		{
			ACARS_To_Bits(WaveHeader[last_buff_processed].lpData,
			WaveHeader[last_buff_processed].dwBufferLength);
		}
		else if (Profile.monitor_mobitex)// or MOBITEX....
		{
			MOBITEX_To_Bits(WaveHeader[last_buff_processed].lpData,
			WaveHeader[last_buff_processed].dwBufferLength);
		}
//		else if (Profile.monitor_ermes)	// or ERMES (test)
//		{
//			ERMES_To_Bits(WaveHeader[last_buff_processed].lpData,
//			WaveHeader[last_buff_processed].dwBufferLength);
//		}
            
		// Add audio buffer back to input queue
		waveInAddBuffer(hWaveIn, &WaveHeader[last_buff_processed],
							(UINT)sizeof(WaveHeader[last_buff_processed]));
	}
	buffers_ready -= old_buffs_ready;
}

bool SignalDiagnosticStartRecording(const char *path, char *error, size_t errorSize)
{
	CopyDiagnosticError(error, errorSize, "");
	if (g_diagnosticRecordingActive)
	{
		CopyDiagnosticError(error, errorSize, "A diagnostic recording is already active.");
		return false;
	}
	if (!path || !path[0])
	{
		CopyDiagnosticError(error, errorSize, "Choose a WAV or SigMF recording path first.");
		return false;
	}
	if (!bCapturing)
	{
		CopyDiagnosticError(error, errorSize,
			"Recording requires an active audio, radio, or replay source.");
		return false;
	}
	g_diagnosticRecording.samples.clear();
	g_diagnosticRecording.sampleRate = ActiveAudioSampleRate();
	g_diagnosticRecordingPath = path;
	g_diagnosticRecordingTruncated = false;
	g_diagnosticRecordingActive = true;
	return true;
}

bool SignalDiagnosticStopRecording(char *error, size_t errorSize)
{
	CopyDiagnosticError(error, errorSize, "");
	if (!g_diagnosticRecordingActive)
	{
		CopyDiagnosticError(error, errorSize, "No diagnostic recording is active.");
		return false;
	}
	g_diagnosticRecordingActive = false;
	std::string writeError;
	const std::string lowered = LowercasePath(g_diagnosticRecordingPath);
	bool written = false;
	if (EndsWith(lowered, ".wav"))
		written = pdw::signal::WriteWav16Mono(g_diagnosticRecordingPath,
			g_diagnosticRecording, writeError);
	else
		written = pdw::signal::WriteSigMfReal32(SigMfBasePath(g_diagnosticRecordingPath),
			g_diagnosticRecording, writeError);
	if (written && g_diagnosticRecordingTruncated)
		writeError = "Recording saved, but capture stopped at the 25-million-sample safety limit.";
	CopyDiagnosticError(error, errorSize, writeError);
	g_diagnosticRecording.samples.clear();
	g_diagnosticRecordingPath.clear();
	return written;
}

bool SignalDiagnosticStartReplay(const char *path, char *error, size_t errorSize)
{
	CopyDiagnosticError(error, errorSize, "");
	if (g_diagnosticRecordingActive)
	{
		CopyDiagnosticError(error, errorSize,
			"Stop the current diagnostic recording before starting replay.");
		return false;
	}
	if (g_diagnosticReplayActive)
	{
		CopyDiagnosticError(error, errorSize, "A diagnostic replay is already active.");
		return false;
	}
	if (!path || !path[0])
	{
		CopyDiagnosticError(error, errorSize, "Choose a WAV or SigMF recording first.");
		return false;
	}
	pdw::signal::SignalRecording recording;
	std::string readError;
	const std::string lowered = LowercasePath(path);
	const bool loaded = EndsWith(lowered, ".wav") ?
		pdw::signal::ReadWavMono(path, recording, readError) :
		pdw::signal::ReadSigMfReal32(SigMfBasePath(path), recording, readError);
	if (!loaded)
	{
		CopyDiagnosticError(error, errorSize, readError);
		return false;
	}

	g_diagnosticReplayResumeSerial = nDriverLoaded != 0;
	g_diagnosticReplayResumeAudio = bCapturing;
	if (g_diagnosticReplayResumeSerial) UnloadDriver();
	else if (g_diagnosticReplayResumeAudio) Stop_Capturing();
	g_diagnosticReplay = recording;
	g_diagnosticReplayPosition = 0;
	g_diagnosticReplayActive = true;
	bUsingWasapiFallback = false;
	g_modernCaptureKind = 0;
	g_activeAudioSampleRate = recording.sampleRate;
	bCapturing = true;
	Reset_ATB();
	SetTimer(ghWnd, PDW_TIMER, 100, (TIMERPROC)NULL);
	return true;
}

void SignalDiagnosticStopReplay(void)
{
	StopDiagnosticReplayInternal(true);
}

bool SignalDiagnosticIsRecording(void)
{
	return g_diagnosticRecordingActive;
}

bool SignalDiagnosticIsReplaying(void)
{
	return g_diagnosticReplayActive;
}

//   Callback_Function
//
//   Called with uMsg equal to WIM_DATA by the audio API when a data block
//   has been filled with digital audio data.
//   As we don't have much time here, we just keep track of how many
//   wave buffers are ready.
//
void CALLBACK Callback_Function(HWAVEIN hwi, UINT uMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2)
{
	if (uMsg == WIM_DATA) buffers_ready++;
}


// When new call is made to Start_Capturing()
// this resets all required variables for Audio_To_Bits().
void Reset_ATB(void)
{
	memset(preamble_count, 0, sizeof(preamble_count));
	nSamples = 0;
	flex_cnt_1600 = 0;
	WatchCtr = -1;
	atb_bit = low_audio;
	config_index=INDEX1600;
	cross_over = 0;
	skipped_sc = 0;
	process_acars_bit = 0;
	g_enhancedAudioSlicer.Reset();

	if (Profile.monitor_mobitex)
	{
		clkt_hi = COURSE_CLKT_HI;
		clkt_lo = COURSE_CLKT_LO;       
		BaudRate = mb.bitrate;
		last_baud_rate = mb.bitrate;
	}
	else
	{
		clkt_hi = FINE_CLKT_HI;
		clkt_lo = FINE_CLKT_LO;       
		BaudRate = 1600;
		last_baud_rate = 1600;
	}

	// WatchStep is how often to check for bit in buffer
	WatchStep = (long double) ActiveAudioSampleRate() / (long double) BaudRate;
}

/* Audio_To_Bits
 *
 * This routine does the following:
 *
 *   1.Takes the digitized audio captured by the Wave API and converts it to data bits.
 *   2.Calls the required routine to decode the data bits.
 *
 * (Used for decoding  POCSAG/FLEX signals)
 */
void Audio_To_Bits(char *lpAudioBuffer, long LenAudioBuffer)
{
	atb_sig_cnt = 0;

	// Loop through audio buffer turning audio samples into bits.
	for (atb_ctr = 0; atb_ctr < LenAudioBuffer; atb_ctr++)
	{
		// If this is the first time being called or if the baudrate rate has changed
		// since the last time we were called recalculate WatchStep.
		if (BaudRate != last_baud_rate)
		{
			// WatchStep is how often to check for bit in buffer
			WatchStep = (long double) ActiveAudioSampleRate() / (long double) BaudRate;

			if (BaudRate > 2400)	// 3200 baud = 6400 FLEX.
			{
				config_index=INDEX3200;
				clkt_hi = FINE_CLKT3200_HI;
				clkt_lo = FINE_CLKT3200_LO;                                      
			}
			else if (BaudRate == 1600)	// Variables for dealing with 1600/3200 crossover (see flex.cpp)
			{
				cross_over = 0;
				g_sps2 = 1600;
				skipped_sc = 0;
				config_index=INDEX1600;
				clkt_hi = FINE_CLKT_HI;
				clkt_lo = FINE_CLKT_LO;                                      
			}
			else WatchCtr = -1;			// If flex don't reset..

			last_baud_rate = BaudRate;
		}

		// Get a sample and correct it.
		val = pdw::signal::LegacyPcm8Value(
			static_cast<unsigned char>(lpAudioBuffer[atb_ctr]));
		g_enhancedAudioSlicer.Observe(static_cast<float>(val) / 128.0f);

		// ****** Need to find preamble!.  ***************

		nSamples++;

		// check for preamble every 0/1 or 1/0 crossing.
		if ((val < (-pre_threshold)) && (sync_bit == high_audio))
		{
			sync_bit = low_audio;
			crossing=1;
		}
		else if ((val > pre_threshold) && (sync_bit == low_audio))
		{
			sync_bit = high_audio;
			crossing=1;
		}

		if (crossing && !pocbit)		// Look for POCSAG preamble...
		{
			crossing=0;

			if (!flex_timer)
			{
				if (atb_sig_cnt < 3)		// Update signal indicator.
				{
					UpdateSigInd(0);		// Move signal indicator left 1
					atb_sig_cnt++;
				}
			}

			if (Profile.decodepocsag)
			{
				// Check for 512 Preamble.
				if ((nSamples > 64) && (nSamples < 108)) preamble_count[INDEX512]++;
				else									 preamble_count[INDEX512]=0;

				if (preamble_count[INDEX512] > 50)
				{
					preamble_count[INDEX512]=0;

					if (Profile.pocsag_512)
					{
						BaudRate = 512;							// used by audio_to_bits
						display_showmo(MODE_POCSAG+MODE_P512);
						pocsag_baud_rate = STAT_POCSAG512;	//used by POCSAG routines
						nSamples = 0;
						pocbit=1300;
						config_index=INDEX512;
						clkt_hi = COURSE_CLKT_HI;
						clkt_lo = COURSE_CLKT_LO;
						continue;
					}
				}
        
				// Check for 1200 Preamble.
				if ((nSamples > 28) && (nSamples < 44)) preamble_count[INDEX1200]++;
				else									preamble_count[INDEX1200]=0;

				if (preamble_count[INDEX1200] > 50)	// Found  1200 POCSAG?
				{
					preamble_count[INDEX1200]=0;
					if (Profile.pocsag_1200)
					{
						BaudRate = 1200;
						display_showmo(MODE_POCSAG+MODE_P1200);
						pocsag_baud_rate = STAT_POCSAG1200;
						nSamples = 0;
						pocbit=1250;
						config_index=INDEX1200;
						continue;
					}
				}

				// Check for 2400 Preamble.
				if ((nSamples > 14) && (nSamples < 22)) preamble_count[INDEX2400]++;
				else									preamble_count[INDEX2400]=0;

				if (preamble_count[INDEX2400] > 50)	// Found  2400 POCSAG?
				{
					preamble_count[INDEX2400]=0;
					if (Profile.pocsag_2400)
					{
						BaudRate = 2400;
						display_showmo(MODE_POCSAG+MODE_P2400);
						pocsag_baud_rate = STAT_POCSAG2400;
						nSamples = 0;
						pocbit=1250;
						config_index=INDEX2400;
						continue;
					}
				}                
			}
			nSamples=0;
		}

		/***endof preamble search****/

		/*** Process data bits *****/

		atb_value = val;

		if (pocbit || flex_timer)
		{
			bMode_IDLE = false;

			if (atb_sig_cnt < 3)			// Update signal indicator.
			{
				UpdateSigInd(1);	// Move signal indicator right 1
				atb_sig_cnt++;
			}
		}
		else bMode_IDLE = true;

		atb_len++; // Keep count of number of 1/0 samples.

		// Resync on 0/1 and 1/0 crossings.
		// Only resync if last sample count was equal to a single 1/0 bit.
		if ((atb_value < (atb_center[config_index] + (-atb_threshold[config_index]))) && (atb_bit == high_audio))
		{
			atb_bit = low_audio;
			if (BaudRate < 3200)
			{
				if ((atb_len < WatchStep * 2) && ((atb_len / WatchStep) > clkt_lo) && ((atb_len / WatchStep) < clkt_hi))
				{
					// center of bit == 1/2 data bit.
					WatchCtr = atb_ctr + (WatchStep / 2);
					WatchCtr += atb_sample_offset[config_index];
				}
			}             
			atb_len=0;
		}
		else if ((atb_value > (atb_center[config_index] + atb_threshold[config_index])) && (atb_bit == low_audio))
		{
			atb_bit = high_audio;

			if ((atb_len < WatchStep * 2) && ((atb_len / WatchStep) > clkt_lo) && ((atb_len / WatchStep) < clkt_hi))
			{
				// center of bit == 1/2 data bit.
				WatchCtr = atb_ctr + (WatchStep / 2);
				WatchCtr += atb_sample_offset[config_index];
			}
			atb_len=0;
		}

		// If found 1600/3200 crossover point, increment counter to skip unreadable data.
		// This unreadble data consists of around 80 bits of (3200 baud) two/four level data.

		if (cross_over)
		{                
			if (skipped_sc < 39) frame_flex(3);		// If skipping unreadable data, still need to keep flex routines happy!
			if (skipped_sc < 1080)					// Works out to around 40 "1600 bits" or 80 "3200 bits".
			{
				skipped_sc++;
				continue;
			}
			else
			{
				cross_over = 0;
				WatchCtr = atb_ctr + (WatchStep / 2);
				WatchCtr += atb_sample_offset[config_index];
				atb_bit = low_audio;
			}
		}

		// Get sample value and process it if on WatchStep
		if (WatchCtr - atb_ctr < 1 && WatchCtr != -1)
		{
			// Decode POCSAG?
			if (pocbit)
			{
				pocsag.frame(atb_bit);
				pocbit--;

				if (pocbit == 0)	// If pocbit==0, end of pocsag signal.
				{
					display_showmo(MODE_IDLE);
					pocsag.frame('X');      // Reset pocsag routine.
					BaudRate = 1600;        // Allow flex sync-ups again.
					config_index=INDEX1600;
				}
			}
			else	// Decode FLEX
			{
				if (Profile.decodeflex)
				{
					if (level == 4 && g_enhancedAudioSlicer.state().confidence >= 0.2f)
					{
						unsigned char legacySymbol = atb_bit ? 0 : 3;
						unsigned char enhancedSymbol = g_enhancedAudioSlicer.CurrentFourLevelSymbol();
						if (Profile.invert) enhancedSymbol ^= 0x03;
						const unsigned char symbol = pdw::signal::HybridFourLevelSymbol(
							legacySymbol, enhancedSymbol);
						frame_flex(static_cast<char>(symbol));
					}
					else
					{
						frame_flex(atb_bit ? 0 : 3);
					}
				}
				exc = 0.0;  // Not used by soundcard input-keep as 0.0 see - flex.cpp.
			}

#ifdef AU_PF_BIT_TEST
			if (atb_bit) misc_debug_msg("1");
			else		 misc_debug_msg("0");
#endif

			WatchCtr += WatchStep;
		}
	} // endof main "for" loop.
	WatchCtr = WatchCtr - (double)LenAudioBuffer;
}

/* MOBITEX To Bits
 *
 * This routine does the following:
 *
 *   1.Takes the digitized audio captured by the Wave API and converts it to data bits.
 *   2.Calls the required routine to decode the data bits.
 */
void MOBITEX_To_Bits(char *lpAudioBuffer, long LenAudioBuffer)
{
	atb_sig_cnt = 0;

	// Loop through audio buffer turning audio samples into bits.
	for (atb_ctr = 0; atb_ctr < LenAudioBuffer; atb_ctr++)
	{
     // If this is the first time being called or if the baudrate rate has changed
     // since the last time we were called recalculate WatchStep.
		if (BaudRate != last_baud_rate) 
		{
			// WatchStep is how often to check for bit in buffer
			WatchStep = (long double) ActiveAudioSampleRate() / (long double) BaudRate;
			WatchCtr = -1;
			last_baud_rate = BaudRate;
		}

		// Get a sample and correct it.
		val = pdw::signal::LegacyPcm8Value(
			static_cast<unsigned char>(lpAudioBuffer[atb_ctr]));

		/*** Process data bits *****/

		if (!mb.timer && ((val > 2) || (val < -2)))
		{
			if (atb_sig_cnt < 3)			// Update signal indicator.
			{
				UpdateSigInd(0);	// Move signal indicator left 1
				atb_sig_cnt++;
			}
		}
		else if (atb_sig_cnt < 3)		// Update signal indicator.
		{
			UpdateSigInd(1);		// Move signal indicator right 1
			atb_sig_cnt++;
		}
		atb_value = val;
		atb_len++; // Keep count of number of 1/0 samples.

		// Resync on 0/1 and 1/0 crossings.
		// Only resync if last sample count was equal to a single 1/0 bit.
		if ((atb_value < -1) && (atb_bit == high_audio))
		{    
			atb_bit = low_audio;

			if (((atb_len < WatchStep * 2) &&
				((atb_len / WatchStep) > clkt_lo) &&
				((atb_len / WatchStep) < clkt_hi)))
			{
				WatchCtr = atb_ctr + (WatchStep / 2);		// center of bit == 1/2 data bit.
			}
		}
		else if ((atb_value > -1) && (atb_bit == low_audio))
		{
			atb_bit = high_audio;
		}
		atb_len=0;
      
		// Get sample value and process it if on WatchStep
		if (WatchCtr - atb_ctr < 1 && WatchCtr != -1)
		{
			mb.frame_sync(atb_bit);

			WatchCtr += WatchStep;
		}
	} // endof main "for" loop.
	WatchCtr = WatchCtr - (double)LenAudioBuffer;
}


/* ACARS to bits
 *
 * This routine does the following:
 *
 *   1.Takes the digitized audio captured by the Wave API and converts it to data bits.
 *   2.Call the required routine to decode the data bits.
 */
void ACARS_To_Bits(char *lpAudioBuffer, long LenAudioBuffer)
{ 
	atb_sig_cnt = 0;

	// Loop through audio buffer turning audio samples into bits.
	for (atb_ctr = 0; atb_ctr < LenAudioBuffer; atb_ctr++)
	{
		// Get a sample and correct it.
		val = pdw::signal::LegacyPcm8Value(
			static_cast<unsigned char>(lpAudioBuffer[atb_ctr]));

		if ((!acars.ac_alive) && ((val > 2) || (val < -2)))
		{
			if (atb_sig_cnt < 3)			// Update signal indicator.
			{
				UpdateSigInd(0);	// Move signal indicator left 1
				atb_sig_cnt++;
			}
		}
		else if (acars.ac_alive)
		{
			if (atb_sig_cnt < 3)			// Update signal indicator.
			{
				UpdateSigInd(1);	// Move signal indicator right 1
				atb_sig_cnt++;
			}
			bMode_IDLE=false;
		}
		atb_value = val;
		atb_len++;			// Keep count of number of 1/0 samples.

      
		// Process bit on 1/0 or 0/1 crossing
		if ((atb_value < 0) && (atb_bit == high_audio))
		{    
			atb_bit = low_audio;

			if (atb_len < 12) continue;   // Check if on full or half wave. Skip if on half wave.

			process_acars_bit=1;				// get bit
			atb_len=0;
		}
		else if ((atb_value > 0) && (atb_bit == low_audio))
		{
			atb_bit = high_audio;

			if (atb_len < 12) continue;	// Check if on full or half wave. Skip if on half wave.              

			process_acars_bit=1; // get bit
			atb_len=0;
		}

		// If here we have a bit to process

		if (process_acars_bit)
		{
			process_acars_bit=0;

#ifdef AU_ACARS_BIT_TEST
			if (atb_bit) misc_debug_msg("1");	// Show raw bits.
			else		 misc_debug_msg("0");
#endif

			acars.frame(atb_bit);  // Decode acars packets.
		}
	} // endof main "for" loop.
}

/*
void ERMES_To_Bits(char *lpAudioBuffer, long LenAudioBuffer)
{
	atb_sig_cnt = 0;

	// Loop through audio buffer turning audio samples into bits.
	for (atb_ctr = 0; atb_ctr < LenAudioBuffer; atb_ctr++)
	{
		// If this is the first time being called or if the baudrate rate has changed
		// since the last time we were called recalculate WatchStep.
		if (BaudRate != last_baud_rate) 
		{
			// WatchStep is how often to check for bit in buffer
			WatchStep = (long double) ActiveAudioSampleRate() / (long double) BaudRate;
			WatchCtr = -1;
			last_baud_rate = BaudRate;
		}

		// Get a sample and correct it.
		val = pdw::signal::LegacyPcm8Value(
			static_cast<unsigned char>(lpAudioBuffer[atb_ctr]));

		/// Process data bits ///

		if ((!em.timer) && ((val > 2) || (val < -2)))
		{
			if (atb_sig_cnt < 3)			// Update signal indicator.
			{
				UpdateSigInd(0);	// Move signal indicator left 1
				atb_sig_cnt++;
			}
		}
		else if (atb_sig_cnt < 3)		// Update signal indicator.
		{
			UpdateSigInd(1);		// Move signal indicator right 1
			atb_sig_cnt++;
		}
		atb_value = val;
		atb_len++; // Keep count of number of 1/0 samples.

		// Resync on 0/1 and 1/0 crossings.
		// Only resync if last sample count was equal to a single 1/0 bit.
		if ((atb_value < -1) && (atb_bit == high_audio))
		{    
			atb_bit = low_audio;

			if (((atb_len < WatchStep * 2) &&
				((atb_len / WatchStep) > clkt_lo) &&
				((atb_len / WatchStep) < clkt_hi)))
			{
				WatchCtr = atb_ctr + (WatchStep / 2);		// center of bit == 1/2 data bit.
			}
			atb_len=0;
		}
		else if ((atb_value > -1) && (atb_bit == low_audio))
		{
			atb_bit = high_audio;
			atb_len=0;
		}
      
		// Get sample value and process it if on WatchStep
		if (WatchCtr - atb_ctr < 1 && WatchCtr != -1)
		{
			em.frame(atb_bit);

			WatchCtr += WatchStep;
		}
	} // endof main "for" loop.
	WatchCtr = WatchCtr - (double)LenAudioBuffer;
}
*/

// Sets the correct audio input configuration based on users selection from Interface dialog.
void SetAudioConfig(int sac_type)
{
	atb_center[INDEX512]  = 0;
	atb_center[INDEX1200] = 0;
	atb_center[INDEX1600] = 0;
	atb_center[INDEX2400] = 0;
	atb_center[INDEX3200] = 0;

	atb_sample_offset[INDEX512]  = 0;
	atb_sample_offset[INDEX1200] = 0;
	atb_sample_offset[INDEX1600] = 0;
	atb_sample_offset[INDEX2400] = 0;
	atb_sample_offset[INDEX3200] = 0;

	switch(sac_type)
	{
		case 0:       // Custom (3200 set to same as 2400)
		atb_threshold[INDEX512]  = au_threshold[Profile.audioThreshold[INDEX512]];
		atb_threshold[INDEX1200] = au_threshold[Profile.audioThreshold[INDEX1200]];
		atb_threshold[INDEX1600] = au_threshold[Profile.audioThreshold[INDEX1600]];
		atb_threshold[INDEX2400] = au_threshold[Profile.audioThreshold[INDEX2400]];
		atb_threshold[INDEX3200] = au_threshold[Profile.audioThreshold[INDEX2400]];

		atb_sample_offset[INDEX512]  = au_offset_center[Profile.audioResync[INDEX512]];
		atb_sample_offset[INDEX1200] = au_offset_center[Profile.audioResync[INDEX1200]];
		atb_sample_offset[INDEX1600] = au_offset_center[Profile.audioResync[INDEX1600]];
		atb_sample_offset[INDEX2400] = au_offset_center[Profile.audioResync[INDEX2400]];
		atb_sample_offset[INDEX3200] = au_offset_center[Profile.audioResync[INDEX2400]];

		atb_center[INDEX512]  = au_offset_center[Profile.audioCentering[INDEX512]];
		atb_center[INDEX1200] = au_offset_center[Profile.audioCentering[INDEX1200]];
		atb_center[INDEX1600] = au_offset_center[Profile.audioCentering[INDEX1600]];
		atb_center[INDEX2400] = au_offset_center[Profile.audioCentering[INDEX2400]];
		atb_center[INDEX3200] = au_offset_center[Profile.audioCentering[INDEX2400]];

		pre_threshold = 7;
		break;

		case 1:       // Discriminator 1
		atb_threshold[INDEX512]  = 16;
		atb_threshold[INDEX1200] = 16;
		atb_threshold[INDEX1600] = 16;
		atb_threshold[INDEX2400] = 16;
		atb_threshold[INDEX3200] = 6;

		pre_threshold = 11;
		break;

		case 2:       // Discriminator 2
		atb_threshold[INDEX512]  = 6;
		atb_threshold[INDEX1200] = 6;
		atb_threshold[INDEX1600] = 6;
		atb_threshold[INDEX2400] = 6;
		atb_threshold[INDEX3200] = 5;

		pre_threshold = 6;
		break;

		case 3:       // Discriminator 3
		atb_threshold[INDEX512]  = 44;
		atb_threshold[INDEX1200] = 44;
		atb_threshold[INDEX1600] = 44;
		atb_threshold[INDEX2400] = 44;
		atb_threshold[INDEX3200] = 22;

		pre_threshold = 15;
		break;

		case 4:       // Discriminator 4
		atb_threshold[INDEX512]  = 2;
		atb_threshold[INDEX1200] = 2;
		atb_threshold[INDEX1600] = 2;
		atb_threshold[INDEX2400] = 2;
		atb_threshold[INDEX3200] = 2;

		pre_threshold = 4;
		break;

		case 5:       // Earphone 1
		atb_threshold[INDEX512]  = 7;
		atb_threshold[INDEX1200] = 7;
		atb_threshold[INDEX1600] = 7;
		atb_threshold[INDEX2400] = 7;
		atb_threshold[INDEX3200] = 4;

		pre_threshold = 7;
		break;

		case 6:       // Earphone 2
		atb_threshold[INDEX512]  = 9;
		atb_threshold[INDEX1200] = 9;
		atb_threshold[INDEX1600] = 9;
		atb_threshold[INDEX2400] = 9;
		atb_threshold[INDEX3200] = 5;

		pre_threshold = 8;
		break;

		case 7:       // Earphone 3
		atb_threshold[INDEX512]  = 14;
		atb_threshold[INDEX1200] = 14;
		atb_threshold[INDEX1600] = 14;
		atb_threshold[INDEX2400] = 14;
		atb_threshold[INDEX3200] = 7;

		pre_threshold = 9;
		break;

		case 8:       // Speaker out 1
		atb_threshold[INDEX512]  = 14;
		atb_threshold[INDEX1200] = 14;
		atb_threshold[INDEX1600] = 14;
		atb_threshold[INDEX2400] = 14;
		atb_threshold[INDEX3200] = 7;

		pre_threshold = 10;
		break;

		case 9:       // Speaker out 2
		atb_threshold[INDEX512]  = 9;
		atb_threshold[INDEX1200] = 9;
		atb_threshold[INDEX1600] = 9;
		atb_threshold[INDEX2400] = 9;
		atb_threshold[INDEX3200] = 5;

		pre_threshold = 8;
		break;

		case 10:       // Speaker out 3
		atb_threshold[INDEX512]  = 34;
		atb_threshold[INDEX1200] = 34;
		atb_threshold[INDEX1600] = 34;
		atb_threshold[INDEX2400] = 34;
		atb_threshold[INDEX3200] = 14;

		pre_threshold = 14;
		break;

		case 11:       // Tape/Rec out 1
		atb_threshold[INDEX512]  = 7;
		atb_threshold[INDEX1200] = 7;
		atb_threshold[INDEX1600] = 7;
		atb_threshold[INDEX2400] = 7;
		atb_threshold[INDEX3200] = 3;

		pre_threshold = 7;
		break;

		case 12:       // Tape/Rec out 2
		atb_threshold[INDEX512]  = 5;
		atb_threshold[INDEX1200] = 5;
		atb_threshold[INDEX1600] = 5;
		atb_threshold[INDEX2400] = 5;
		atb_threshold[INDEX3200] = 3;

		pre_threshold = 6;
		break;

		case 13:       // Tape/Rec out 3
		atb_threshold[INDEX512]  = 15;
		atb_threshold[INDEX1200] = 15;
		atb_threshold[INDEX1600] = 15;
		atb_threshold[INDEX2400] = 15;
		atb_threshold[INDEX3200] = 7;

		pre_threshold = 10;
		break;

		default:
		atb_threshold[INDEX512]  = 8;
		atb_threshold[INDEX1200] = 8;
		atb_threshold[INDEX1600] = 8;
		atb_threshold[INDEX2400] = 8;
		atb_threshold[INDEX3200] = 4;

		pre_threshold = 8;
		break;
	}
}

namespace
{
	class SignalSourceTestSink : public pdw::signal::AudioSampleSink
	{
	public:
		void OnAudioSamples(const float*, std::size_t, std::uint32_t, bool) {}
	};

	void SetUnsignedControl(HWND dialog, int control, unsigned int value)
	{
		char text[32];
		snprintf(text, sizeof(text), "%u", value);
		SetDlgItemText(dialog, control, text);
	}

	bool GetIntegerControl(HWND dialog, int control, long minimum, long maximum,
		long& value, const char* label)
	{
		char text[64];
		GetDlgItemText(dialog, control, text, sizeof(text));
		char* end = NULL;
		value = strtol(text, &end, 10);
		if (!text[0] || !end || *end || value < minimum || value > maximum)
		{
			char message[160];
			snprintf(message, sizeof(message), "%s must be between %ld and %ld.", label, minimum, maximum);
			MessageBox(dialog, message, "PDW Signal Source", MB_ICONERROR);
			SetFocus(GetDlgItem(dialog, control));
			return false;
		}
		return true;
	}

	void EnableRtlControls(HWND dialog, int source)
	{
		const BOOL rtlTcp = source == AUDIO_SOURCE_RTL_TCP;
		const BOOL rtlAny = source == AUDIO_SOURCE_RTL_TCP || source == AUDIO_SOURCE_RTL_SDR;
		EnableWindow(GetDlgItem(dialog, IDC_RTL_HOST), rtlTcp);
		EnableWindow(GetDlgItem(dialog, IDC_RTL_PORT), rtlTcp);
		EnableWindow(GetDlgItem(dialog, IDC_RTL_DEVICE), source == AUDIO_SOURCE_RTL_SDR);
		EnableWindow(GetDlgItem(dialog, IDC_RTL_FREQUENCY), rtlAny);
		EnableWindow(GetDlgItem(dialog, IDC_RTL_SAMPLE_RATE), rtlAny);
		EnableWindow(GetDlgItem(dialog, IDC_RTL_AUDIO_RATE), rtlAny);
		EnableWindow(GetDlgItem(dialog, IDC_RTL_AUTOMATIC_GAIN), rtlAny);
		EnableWindow(GetDlgItem(dialog, IDC_RTL_GAIN), rtlAny && !IsDlgButtonChecked(dialog, IDC_RTL_AUTOMATIC_GAIN));
		EnableWindow(GetDlgItem(dialog, IDC_RTL_PPM), rtlAny);
	}

	bool ReadRtlDialog(HWND dialog, pdw::signal::RtlTcpConfig& config, int& deviceIndex)
	{
		char host[RTL_TCP_HOST_LEN+1];
		GetDlgItemText(dialog, IDC_RTL_HOST, host, sizeof(host));
		long port, frequency, iqRate, audioRate, gain, ppm, device;
		if (!GetIntegerControl(dialog, IDC_RTL_PORT, 1, 65535, port, "Port") ||
			!GetIntegerControl(dialog, IDC_RTL_FREQUENCY, 100000, 2000000000L, frequency, "Frequency") ||
			!GetIntegerControl(dialog, IDC_RTL_SAMPLE_RATE, 240000, 3200000, iqRate, "IQ sample rate") ||
			!GetIntegerControl(dialog, IDC_RTL_AUDIO_RATE, 8000, 192000, audioRate, "Audio sample rate") ||
			!GetIntegerControl(dialog, IDC_RTL_GAIN, -1000, 1000, gain, "Gain") ||
			!GetIntegerControl(dialog, IDC_RTL_PPM, -1000, 1000, ppm, "Frequency correction") ||
			!GetIntegerControl(dialog, IDC_RTL_DEVICE, 0, 255, device, "USB device index"))
			return false;
		if (audioRate > iqRate)
		{
			MessageBox(dialog, "Audio sample rate cannot exceed the IQ sample rate.",
				"PDW Signal Source", MB_ICONERROR);
			return false;
		}
		if (!host[0]) strcpy(host, "127.0.0.1");
		config.host = host;
		config.port = static_cast<std::uint16_t>(port);
		config.frequencyHz = static_cast<std::uint32_t>(frequency);
		config.sampleRate = static_cast<std::uint32_t>(iqRate);
		config.audioSampleRate = static_cast<std::uint32_t>(audioRate);
		config.gainTenthsDb = static_cast<int>(gain);
		config.frequencyCorrectionPpm = static_cast<int>(ppm);
		config.automaticGain = IsDlgButtonChecked(dialog, IDC_RTL_AUTOMATIC_GAIN) != 0;
		deviceIndex = static_cast<int>(device);
		return true;
	}

	void UpdateDiagnosticControls(HWND dialog)
	{
		const BOOL recording = SignalDiagnosticIsRecording() ? TRUE : FALSE;
		const BOOL replaying = SignalDiagnosticIsReplaying() ? TRUE : FALSE;
		EnableWindow(GetDlgItem(dialog, IDC_SIGNAL_RECORD_START), !recording && !replaying);
		EnableWindow(GetDlgItem(dialog, IDC_SIGNAL_RECORD_STOP), recording);
		EnableWindow(GetDlgItem(dialog, IDC_SIGNAL_RECORD_BROWSE), !recording);
		EnableWindow(GetDlgItem(dialog, IDC_SIGNAL_RECORD_PATH), !recording);
		EnableWindow(GetDlgItem(dialog, IDC_SIGNAL_REPLAY_START), !replaying && !recording);
		EnableWindow(GetDlgItem(dialog, IDC_SIGNAL_REPLAY_STOP), replaying);
		EnableWindow(GetDlgItem(dialog, IDC_SIGNAL_REPLAY_BROWSE), !replaying);
		EnableWindow(GetDlgItem(dialog, IDC_SIGNAL_REPLAY_PATH), !replaying);
	}

	bool BrowseDiagnosticPath(HWND dialog, int control, bool save)
	{
		char path[MAX_PATH] = {0};
		GetDlgItemText(dialog, control, path, sizeof(path));
		static const char saveFilter[] =
			"Wave audio (*.wav)\0*.wav\0SigMF recording (*.sigmf)\0*.sigmf\0All files (*.*)\0*.*\0\0";
		static const char openFilter[] =
			"Signal recordings (*.wav;*.sigmf;*.sigmf-meta;*.sigmf-data)\0*.wav;*.sigmf;*.sigmf-meta;*.sigmf-data\0All files (*.*)\0*.*\0\0";
		OPENFILENAMEA fileDialog = {0};
		fileDialog.lStructSize = sizeof(fileDialog);
		fileDialog.hwndOwner = dialog;
		fileDialog.lpstrFilter = save ? saveFilter : openFilter;
		fileDialog.lpstrFile = path;
		fileDialog.nMaxFile = sizeof(path);
		fileDialog.lpstrDefExt = "wav";
		fileDialog.Flags = OFN_EXPLORER | OFN_HIDEREADONLY |
			(save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
		const BOOL selected = save ? GetSaveFileNameA(&fileDialog) : GetOpenFileNameA(&fileDialog);
		if (selected) SetDlgItemText(dialog, control, path);
		return selected != FALSE;
	}
}

BOOL FAR PASCAL SignalSourceDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM)
{
	switch (uMsg)
	{
		case WM_INITDIALOG:
			CenterWindow(hDlg);
			SendDlgItemMessage(hDlg, IDC_SIGNAL_SOURCE, CB_ADDSTRING, 0, (LPARAM)"Local audio or serial (automatic fallback)");
			SendDlgItemMessage(hDlg, IDC_SIGNAL_SOURCE, CB_ADDSTRING, 0, (LPARAM)"RTL-TCP network radio");
			SendDlgItemMessage(hDlg, IDC_SIGNAL_SOURCE, CB_ADDSTRING, 0, (LPARAM)"RTL-SDR USB (optional DLL)");
			SendDlgItemMessage(hDlg, IDC_SIGNAL_SOURCE, CB_SETCURSEL, Profile.audioSource, 0);
			SetDlgItemText(hDlg, IDC_RTL_HOST, Profile.rtlTcpHost);
			SetUnsignedControl(hDlg, IDC_RTL_PORT, static_cast<unsigned int>(Profile.rtlTcpPort));
			SetUnsignedControl(hDlg, IDC_RTL_FREQUENCY, Profile.rtlFrequencyHz);
			SetUnsignedControl(hDlg, IDC_RTL_SAMPLE_RATE, Profile.rtlSampleRate);
			SetUnsignedControl(hDlg, IDC_RTL_AUDIO_RATE, Profile.rtlAudioSampleRate);
			SetDlgItemInt(hDlg, IDC_RTL_GAIN, Profile.rtlGainTenthsDb, TRUE);
			SetDlgItemInt(hDlg, IDC_RTL_PPM, Profile.rtlFrequencyCorrectionPpm, TRUE);
			SetDlgItemInt(hDlg, IDC_RTL_DEVICE, Profile.rtlDeviceIndex, FALSE);
			CheckDlgButton(hDlg, IDC_RTL_AUTOMATIC_GAIN, Profile.rtlAutomaticGain ? BST_CHECKED : BST_UNCHECKED);
			EnableRtlControls(hDlg, Profile.audioSource);
			SetDlgItemText(hDlg, IDC_SIGNAL_RECORD_PATH, "PDW-signal.wav");
			SetDlgItemText(hDlg, IDC_SIGNAL_REPLAY_PATH, "PDW-signal.wav");
			UpdateDiagnosticControls(hDlg);
			SetTimer(hDlg, 1, 250, NULL);
			if (Profile.audioSource == AUDIO_SOURCE_RTL_SDR && !pdw::signal::IsRtlSdrLibraryAvailable())
				SetDlgItemText(hDlg, IDC_RTL_STATUS, "RTL-SDR DLL not found. Legacy and RTL-TCP inputs remain available.");
			return TRUE;

		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
				case IDC_SIGNAL_SOURCE:
					if (HIWORD(wParam) == CBN_SELCHANGE)
					{
						const int source = static_cast<int>(SendDlgItemMessage(hDlg, IDC_SIGNAL_SOURCE, CB_GETCURSEL, 0, 0));
						EnableRtlControls(hDlg, source);
					}
					return TRUE;

				case IDC_RTL_AUTOMATIC_GAIN:
					EnableRtlControls(hDlg, static_cast<int>(SendDlgItemMessage(hDlg, IDC_SIGNAL_SOURCE, CB_GETCURSEL, 0, 0)));
					return TRUE;

				case IDC_RTL_TEST:
				{
					const int source = static_cast<int>(SendDlgItemMessage(hDlg, IDC_SIGNAL_SOURCE, CB_GETCURSEL, 0, 0));
					if (source == AUDIO_SOURCE_LOCAL)
					{
						SetDlgItemText(hDlg, IDC_RTL_STATUS, "Local input is available. WinMM is retained and WASAPI fallback passed its device test.");
						return TRUE;
					}
					pdw::signal::RtlTcpConfig config;
					int deviceIndex = 0;
					if (!ReadRtlDialog(hDlg, config, deviceIndex)) return TRUE;
					SetDlgItemText(hDlg, IDC_RTL_STATUS,
						source == AUDIO_SOURCE_RTL_SDR ? "Opening RTL-SDR device..." : "Connecting to RTL-TCP...");
					SignalSourceTestSink sink;
					if (source == AUDIO_SOURCE_RTL_SDR)
					{
						pdw::signal::RtlSdrSource test;
						if (test.Start(config, static_cast<unsigned int>(deviceIndex), &sink))
						{
							SetDlgItemText(hDlg, IDC_RTL_STATUS, "RTL-SDR device opened and accepted the tuner configuration.");
							test.Stop();
						}
						else SetDlgItemText(hDlg, IDC_RTL_STATUS, test.lastError().c_str());
					}
					else
					{
						pdw::signal::RtlTcpSource test;
						if (test.Start(config, &sink))
						{
							SetDlgItemText(hDlg, IDC_RTL_STATUS, "RTL-TCP connected and accepted the tuner configuration.");
							test.Stop();
						}
						else SetDlgItemText(hDlg, IDC_RTL_STATUS, test.lastError().c_str());
					}
					return TRUE;
				}

				case IDC_SIGNAL_RECORD_BROWSE:
					BrowseDiagnosticPath(hDlg, IDC_SIGNAL_RECORD_PATH, true);
					return TRUE;

				case IDC_SIGNAL_REPLAY_BROWSE:
					BrowseDiagnosticPath(hDlg, IDC_SIGNAL_REPLAY_PATH, false);
					return TRUE;

				case IDC_SIGNAL_RECORD_START:
				{
					char path[MAX_PATH], error[256];
					GetDlgItemText(hDlg, IDC_SIGNAL_RECORD_PATH, path, sizeof(path));
					if (SignalDiagnosticStartRecording(path, error, sizeof(error)))
						SetDlgItemText(hDlg, IDC_SIGNAL_DIAGNOSTIC_STATUS,
							"Recording the normalized signal without changing the live decoder path...");
					else MessageBox(hDlg, error, "PDW Signal Recording", MB_ICONERROR);
					UpdateDiagnosticControls(hDlg);
					return TRUE;
				}

				case IDC_SIGNAL_RECORD_STOP:
				{
					char error[256];
					if (SignalDiagnosticStopRecording(error, sizeof(error)))
						SetDlgItemText(hDlg, IDC_SIGNAL_DIAGNOSTIC_STATUS,
							error[0] ? error : "Recording saved successfully.");
					else MessageBox(hDlg, error, "PDW Signal Recording", MB_ICONERROR);
					UpdateDiagnosticControls(hDlg);
					return TRUE;
				}

				case IDC_SIGNAL_REPLAY_START:
				{
					char path[MAX_PATH], error[256];
					GetDlgItemText(hDlg, IDC_SIGNAL_REPLAY_PATH, path, sizeof(path));
					if (SignalDiagnosticStartReplay(path, error, sizeof(error)))
						SetDlgItemText(hDlg, IDC_SIGNAL_DIAGNOSTIC_STATUS,
							"Replaying through the existing PDW decoders; live input will resume automatically.");
					else MessageBox(hDlg, error, "PDW Signal Replay", MB_ICONERROR);
					UpdateDiagnosticControls(hDlg);
					return TRUE;
				}

				case IDC_SIGNAL_REPLAY_STOP:
					SignalDiagnosticStopReplay();
					SetDlgItemText(hDlg, IDC_SIGNAL_DIAGNOSTIC_STATUS,
						"Replay stopped and the previous live input was restored.");
					UpdateDiagnosticControls(hDlg);
					return TRUE;

				case IDOK:
				{
					const int source = static_cast<int>(SendDlgItemMessage(hDlg, IDC_SIGNAL_SOURCE, CB_GETCURSEL, 0, 0));
					pdw::signal::RtlTcpConfig config;
					int deviceIndex = Profile.rtlDeviceIndex;
					if (source != AUDIO_SOURCE_LOCAL && !ReadRtlDialog(hDlg, config, deviceIndex)) return TRUE;

					const int previousSource = Profile.audioSource;
					const int previousAudioEnabled = Profile.audioEnabled;
					const int previousComEnabled = Profile.comPortEnabled;
					if (bCapturing) Stop_Capturing();
					if (source != AUDIO_SOURCE_LOCAL) UnloadDriver();
					Profile.audioSource = source;
					if (source != AUDIO_SOURCE_LOCAL)
					{
						Profile.audioEnabled = 1;
						Profile.comPortEnabled = 0;
						strncpy(Profile.rtlTcpHost, config.host.c_str(), sizeof(Profile.rtlTcpHost)-1);
						Profile.rtlTcpHost[sizeof(Profile.rtlTcpHost)-1] = '\0';
						Profile.rtlTcpPort = config.port;
						Profile.rtlFrequencyHz = config.frequencyHz;
						Profile.rtlSampleRate = config.sampleRate;
						Profile.rtlAudioSampleRate = config.audioSampleRate;
						Profile.rtlGainTenthsDb = config.gainTenthsDb;
						Profile.rtlFrequencyCorrectionPpm = config.frequencyCorrectionPpm;
						Profile.rtlAutomaticGain = config.automaticGain ? 1 : 0;
						Profile.rtlDeviceIndex = deviceIndex;
					}

					bool started = true;
					if (Profile.audioEnabled) started = Start_Capturing() != FALSE;
					if (!started)
					{
						Profile.audioSource = previousSource;
						Profile.audioEnabled = previousAudioEnabled;
						Profile.comPortEnabled = previousComEnabled;
						if (previousComEnabled) LoadDriver();
						else if (previousAudioEnabled) Start_Capturing();
						return TRUE;
					}
					SetTimer(ghWnd, PDW_TIMER, 100, (TIMERPROC)NULL);
					WriteSettings();
					EndDialog(hDlg, TRUE);
					return TRUE;
				}

				case IDCANCEL:
					EndDialog(hDlg, FALSE);
					return TRUE;
			}
			break;

		case WM_CLOSE:
			EndDialog(hDlg, FALSE);
			return TRUE;

		case WM_TIMER:
			UpdateDiagnosticControls(hDlg);
			return TRUE;

		case WM_DESTROY:
			KillTimer(hDlg, 1);
			return TRUE;
	}
	return FALSE;
}
