#include "multi_channel_manager.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>

#include "headers\pdw.h"
#include "decoded_event.h"
#include "smtp.h"

extern PROFILE Profile;
extern TCHAR szIniPathName[MAX_PATH];

namespace pdw
{
namespace multichannel
{
namespace
{
	const unsigned int MAX_CHANNELS = 4;
	const DWORD GRACEFUL_STOP_TIMEOUT_MS = 3000;
	const DWORD FORCED_STOP_TIMEOUT_MS = 2000;
	bool g_workerActive = false;
	bool g_workerRequested = false;
	unsigned int g_workerIndex = 0;
	HANDLE g_processes[MAX_CHANNELS] = { NULL, NULL, NULL, NULL };
	HANDLE g_jobs[MAX_CHANNELS] = { NULL, NULL, NULL, NULL };
	DWORD g_processIds[MAX_CHANNELS] = { 0, 0, 0, 0 };
	ChannelConfig g_activeChannels[MAX_CHANNELS];
	bool g_activeChannelValid[MAX_CHANNELS] = { false, false, false, false };

	std::string Section(unsigned int index)
	{
		char section[32] = {};
		std::snprintf(section, sizeof(section), "MultiChannel%u", index + 1);
		return section;
	}

	std::string ReadText(const std::string& section, const char* key, const char* fallback)
	{
		char value[256] = {};
		GetPrivateProfileStringA(section.c_str(), key, fallback, value, sizeof(value), szIniPathName);
		return value;
	}

	bool WriteNumber(const std::string& section, const char* key, unsigned int value)
	{
		char text[32] = {};
		std::snprintf(text, sizeof(text), "%u", value);
		return WritePrivateProfileStringA(section.c_str(), key, text, szIniPathName) != FALSE;
	}

	bool SameChannel(const ChannelConfig& left, const ChannelConfig& right)
	{
		return left.enabled == right.enabled && left.source == right.source &&
			left.label == right.label && left.host == right.host && left.port == right.port &&
			left.deviceIndex == right.deviceIndex && left.frequencyHz == right.frequencyHz;
	}

	BOOL CALLBACK CloseWorkerWindow(HWND window, LPARAM processId)
	{
		DWORD owner = 0;
		GetWindowThreadProcessId(window, &owner);
		if (owner == static_cast<DWORD>(processId)) PostMessage(window, WM_CLOSE, 0, 0);
		return TRUE;
	}

	HANDLE CreateWorkerJob()
	{
		HANDLE job = CreateJobObjectA(NULL, NULL);
		if (!job) return NULL;
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
		limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
			&limits, sizeof(limits)))
		{
			CloseHandle(job);
			return NULL;
		}
		return job;
	}

	void CloseWorkerSlot(unsigned int index)
	{
		// Closing the job is the final containment boundary: it also terminates
		// any receiver/history child process the worker may have created.
		if (g_jobs[index]) CloseHandle(g_jobs[index]);
		if (g_processes[index]) CloseHandle(g_processes[index]);
		g_jobs[index] = NULL;
		g_processes[index] = NULL;
		g_processIds[index] = 0;
		g_activeChannelValid[index] = false;
	}

	void CloseFinishedHandles()
	{
		for (unsigned int index = 0; index < MAX_CHANNELS; ++index)
		{
			if (g_processes[index] && WaitForSingleObject(g_processes[index], 0) == WAIT_OBJECT_0)
				CloseWorkerSlot(index);
			else if (!g_processes[index] && g_jobs[index]) CloseWorkerSlot(index);
		}
	}

	bool WaitForWorkers(const std::vector<unsigned int>& indexes, DWORD timeout)
	{
		HANDLE handles[MAX_CHANNELS] = {};
		DWORD count = 0;
		for (std::vector<unsigned int>::const_iterator index = indexes.begin();
			index != indexes.end(); ++index)
			if (*index < MAX_CHANNELS && g_processes[*index]) handles[count++] = g_processes[*index];
		if (!count) return true;
		return WaitForMultipleObjects(count, handles, TRUE, timeout) == WAIT_OBJECT_0;
	}

	bool StopWorkers(const std::vector<unsigned int>& indexes)
	{
		for (std::vector<unsigned int>::const_iterator index = indexes.begin();
			index != indexes.end(); ++index)
			if (*index < MAX_CHANNELS && g_processes[*index])
				EnumWindows(CloseWorkerWindow, static_cast<LPARAM>(g_processIds[*index]));

		const bool stoppedGracefully = WaitForWorkers(indexes, GRACEFUL_STOP_TIMEOUT_MS);
		if (!stoppedGracefully)
		{
			for (std::vector<unsigned int>::const_iterator index = indexes.begin();
				index != indexes.end(); ++index)
			{
				if (*index >= MAX_CHANNELS) continue;
				if (g_jobs[*index]) TerminateJobObject(g_jobs[*index], ERROR_PROCESS_ABORTED);
				else if (g_processes[*index] &&
					WaitForSingleObject(g_processes[*index], 0) == WAIT_TIMEOUT)
					TerminateProcess(g_processes[*index], ERROR_PROCESS_ABORTED);
			}
			WaitForWorkers(indexes, FORCED_STOP_TIMEOUT_MS);
		}
		for (std::vector<unsigned int>::const_iterator index = indexes.begin();
			index != indexes.end(); ++index)
			if (*index < MAX_CHANNELS) CloseWorkerSlot(*index);
		return stoppedGracefully;
	}
}

std::vector<ChannelConfig> LoadChannels()
{
	std::vector<ChannelConfig> channels(MAX_CHANNELS);
	for (unsigned int index = 0; index < MAX_CHANNELS; ++index)
	{
		const std::string section = Section(index);
		ChannelConfig& channel = channels[index];
		channel.enabled = GetPrivateProfileIntA(section.c_str(), "Enable", 0, szIniPathName) != 0;
		channel.source = GetPrivateProfileIntA(section.c_str(), "Source", AUDIO_SOURCE_RTL_TCP, szIniPathName);
		channel.label = ReadText(section, "Label", "");
		channel.host = ReadText(section, "Host", "127.0.0.1");
		channel.port = GetPrivateProfileIntA(section.c_str(), "Port", 1234, szIniPathName);
		channel.deviceIndex = GetPrivateProfileIntA(section.c_str(), "DeviceIndex", index, szIniPathName);
		channel.frequencyHz = GetPrivateProfileIntA(section.c_str(), "FrequencyHz", 148000000, szIniPathName);
	}
	return channels;
}

bool SaveChannels(const std::vector<ChannelConfig>& channels, std::string& error)
{
	if (!ValidateChannels(channels, NULL, error)) return false;
	CloseFinishedHandles();
	for (unsigned int index = 0; index < MAX_CHANNELS; ++index)
	{
		if (!g_processes[index]) continue;
		if (!g_activeChannelValid[index] ||
			!SameChannel(channels[index], g_activeChannels[index]))
		{
			error = "Stop all worker channels before changing or disabling a running slot.";
			return false;
		}
	}
	for (unsigned int index = 0; index < MAX_CHANNELS; ++index)
	{
		const ChannelConfig& channel = channels[index];
		const std::string section = Section(index);
		const bool written = WriteNumber(section, "Enable", channel.enabled ? 1 : 0) &&
			WriteNumber(section, "Source", static_cast<unsigned int>(channel.source)) &&
			WritePrivateProfileStringA(section.c_str(), "Label", channel.label.c_str(), szIniPathName) != FALSE &&
			WritePrivateProfileStringA(section.c_str(), "Host", channel.host.c_str(), szIniPathName) != FALSE &&
			WriteNumber(section, "Port", channel.port) &&
			WriteNumber(section, "DeviceIndex", channel.deviceIndex) &&
			WriteNumber(section, "FrequencyHz", channel.frequencyHz);
		if (!written)
		{
			error = "Windows could not persist the guarded channel configuration.";
			return false;
		}
	}
	if (!WritePrivateProfileStringA(NULL, NULL, NULL, szIniPathName))
	{
		error = "Windows could not flush the guarded channel configuration.";
		return false;
	}
	const std::vector<ChannelConfig> persisted = LoadChannels();
	for (unsigned int index = 0; index < MAX_CHANNELS; ++index)
		if (!SameChannel(channels[index], persisted[index]))
		{
			error = "The persisted guarded channel configuration did not verify.";
			return false;
		}
	return true;
}

bool LaunchEnabledChannels(const std::vector<ChannelConfig>& channels, std::string& error)
{
	error.clear();
	if (!Profile.messageHistoryEnabled)
	{
		error = "Enable local message history first. Worker channels use it as their safe combined feed.";
		return false;
	}
	ActiveReceiver mainReceiver;
	mainReceiver.active = Profile.audioEnabled != 0;
	mainReceiver.source = Profile.audioSource;
	mainReceiver.host = Profile.rtlTcpHost;
	mainReceiver.port = static_cast<unsigned int>(Profile.rtlTcpPort);
	mainReceiver.deviceIndex = static_cast<unsigned int>(Profile.rtlDeviceIndex);
	if (!ValidateChannels(channels, &mainReceiver, error)) return false;
	if (!SaveChannels(channels, error)) return false;
	CloseFinishedHandles();
	char executable[MAX_PATH] = {};
	const DWORD executableLength = GetModuleFileNameA(NULL, executable, sizeof(executable));
	if (!executableLength || executableLength >= sizeof(executable))
	{
		error = "Windows could not resolve the PDW executable for worker launch.";
		return false;
	}
	std::vector<unsigned int> started;
	for (unsigned int index = 0; index < MAX_CHANNELS; ++index)
	{
		if (!channels[index].enabled || g_processes[index]) continue;
		char command[MAX_PATH + 80] = {};
		std::snprintf(command, sizeof(command), "\"%s\" /channel-worker=%u", executable, index + 1);
		HANDLE job = CreateWorkerJob();
		if (!job)
		{
			StopWorkers(started);
			error = "Windows could not create a contained job for an isolated PDW channel worker.";
			return false;
		}
		STARTUPINFOA startup = {};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESHOWWINDOW;
		startup.wShowWindow = SW_SHOWMINNOACTIVE;
		PROCESS_INFORMATION process = {};
		if (!CreateProcessA(executable, command, NULL, NULL, FALSE,
			CREATE_NEW_PROCESS_GROUP | CREATE_SUSPENDED,
			NULL, NULL, &startup, &process))
		{
			CloseHandle(job);
			StopWorkers(started);
			error = "Windows could not launch one of the isolated PDW channel workers.";
			return false;
		}
		if (!AssignProcessToJobObject(job, process.hProcess))
		{
			TerminateProcess(process.hProcess, ERROR_PROCESS_ABORTED);
			WaitForSingleObject(process.hProcess, FORCED_STOP_TIMEOUT_MS);
			CloseHandle(process.hThread);
			CloseHandle(process.hProcess);
			CloseHandle(job);
			StopWorkers(started);
			error = "Windows could not contain an isolated PDW channel worker in its job.";
			return false;
		}
		if (ResumeThread(process.hThread) == static_cast<DWORD>(-1))
		{
			TerminateJobObject(job, ERROR_PROCESS_ABORTED);
			WaitForSingleObject(process.hProcess, FORCED_STOP_TIMEOUT_MS);
			CloseHandle(process.hThread);
			CloseHandle(process.hProcess);
			CloseHandle(job);
			StopWorkers(started);
			error = "Windows could not start one of the contained PDW channel workers.";
			return false;
		}
		CloseHandle(process.hThread);
		g_processes[index] = process.hProcess;
		g_jobs[index] = job;
		g_processIds[index] = process.dwProcessId;
		g_activeChannels[index] = channels[index];
		g_activeChannelValid[index] = true;
		started.push_back(index);
	}
	return true;
}

void StopAllChannels(std::string& status)
{
	CloseFinishedHandles();
	std::vector<unsigned int> running;
	for (unsigned int index = 0; index < MAX_CHANNELS; ++index)
		if (g_processes[index]) running.push_back(index);
	if (running.empty())
	{
		status = "No worker channels are running.";
		return;
	}
	const bool graceful = StopWorkers(running);
	status = graceful ? "All worker channels stopped." :
		"All worker channels stopped; forced termination was required.";
}

std::string ChannelStatus(unsigned int index)
{
	if (index >= MAX_CHANNELS) return "Invalid";
	CloseFinishedHandles();
	return g_processes[index] ? "Running" : "Stopped";
}

bool ConfigureWorkerFromCommandLine(const char* commandLine)
{
	g_workerActive = false;
	g_workerRequested = false;
	g_workerIndex = 0;
	if (!commandLine) return false;
	const char* marker = std::strstr(commandLine, "/channel-worker=");
	if (!marker) return false;
	g_workerRequested = true;
	char* end = NULL;
	const unsigned long slot = std::strtoul(marker + std::strlen("/channel-worker="), &end, 10);
	if (!end || *end != '\0') return false;
	if (slot < 1 || slot > MAX_CHANNELS) return false;
	const std::vector<ChannelConfig> channels = LoadChannels();
	std::string validationError;
	// Settings are loaded before command-line worker configuration. Reapply the
	// main-receiver conflict check here so a hand-crafted worker command cannot
	// bypass the validation performed by the manager window.
	ActiveReceiver mainReceiver;
	mainReceiver.active = Profile.audioEnabled != 0;
	mainReceiver.source = Profile.audioSource;
	mainReceiver.host = Profile.rtlTcpHost;
	mainReceiver.port = static_cast<unsigned int>(Profile.rtlTcpPort);
	mainReceiver.deviceIndex = static_cast<unsigned int>(Profile.rtlDeviceIndex);
	if (!ValidateChannels(channels, &mainReceiver, validationError)) return false;
	const ChannelConfig& channel = channels[slot - 1];
	if (!channel.enabled) return false;
	g_workerActive = true;
	g_workerIndex = static_cast<unsigned int>(slot - 1);
	Profile.audioEnabled = 1;
	Profile.comPortEnabled = 0;
	Profile.audioSource = channel.source;
	Profile.rtlFrequencyHz = channel.frequencyHz;
	Profile.rtlDeviceIndex = static_cast<int>(channel.deviceIndex);
	Profile.rtlTcpPort = static_cast<int>(channel.port);
	std::strncpy(Profile.rtlTcpHost, channel.host.c_str(), sizeof(Profile.rtlTcpHost) - 1);
	Profile.rtlTcpHost[sizeof(Profile.rtlTcpHost) - 1] = '\0';
	Profile.publishingEnabled = 0;
	Profile.dataOutputsEnabled = 0;
	Profile.SMTP = 0;
	Profile.nMailOptions &= ~MAIL_OPTION_ENABLE;
	Profile.ftpEnabled = 0;
	Profile.appriseEnabled = 0;
	Profile.windowsToastEnabled = 0;
	Profile.telnetOutputEnabled = 0;
	Profile.liveDashboardEnabled = 0;
	Profile.logfile_enabled = 0;
	Profile.filterfile_enabled = 0;
	Profile.stat_file_enabled = 0;
	Profile.filter_cmd_file_enabled = 0;
	Profile.filterbeep = 0;
	for (FILTERLIST::iterator filter = Profile.filters.begin(); filter != Profile.filters.end(); ++filter)
	{
		filter->cmd_enabled = 0;
		filter->smtp = 0;
		filter->sep_filterfile_en = 0;
		filter->wave_number = 0;
	}
	Profile.confirmExit = 0;
	const std::string utf8Label = channel.label.empty() ? "receiver" :
		pdw::events::PdwTextToUtf8(channel.label.c_str());
	std::ostringstream source;
	source << "PDW channel " << slot << " - " << utf8Label;
	pdw::events::SetDecodedEventSource(source.str());
	std::snprintf(Profile.windowTitle, sizeof(Profile.windowTitle), "Channel %lu - %s - %u Hz", slot,
		channel.label.empty() ? "receiver" : channel.label.c_str(), channel.frequencyHz);
	return true;
}

bool WorkerActive() { return g_workerActive; }
bool WorkerCommandRequested() { return g_workerRequested; }
unsigned int WorkerIndex() { return g_workerIndex; }

std::string WorkerMutexName(const char* baseName)
{
	return BuildInstanceMutexName(baseName ? baseName : "PDW",
		g_workerActive ? g_workerIndex + 1 : 0);
}

} // namespace multichannel
} // namespace pdw
