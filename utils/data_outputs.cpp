#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>
#include <wincred.h>
#include <process.h>
#include <commctrl.h>
#include <commdlg.h>

#include <curl/curl.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <sstream>
#include <string>
#include <vector>

#include "headers\data_outputs.h"
#include "headers\resource.h"
#include "headers\notification.h"
#include "headers\pdw.h"
#include "headers\initapp.h"
#include "data_output_core.h"
#include "decoded_event.h"
#include "curl_runtime.h"
#include "mysql_odbc_output.h"
#include "publishing_core.h"
#include "sqlite_output.h"
#include "telnet_output_server.h"
#include "windows_toast.h"

namespace
{
	const std::size_t MAX_QUEUE = 500;
	const int TARGET_MQTT = 1 << 0;
	const int TARGET_SQLITE = 1 << 1;
	const int TARGET_MYSQL = 1 << 2;
	const int TARGET_TELNET = 1 << 3;
	const int TARGET_TOAST = 1 << 4;

	struct Config
	{
		bool enabled;
		bool acknowledged;
		bool filteredOnly;
		bool maskAddress;
		bool includeMessage;
		std::string sourceAlias;

		bool mqttEnabled;
		bool mqttAllowInsecure;
		std::string mqttBrokerUrl;
		std::string mqttTopic;
		std::string mqttUsername;

		bool sqliteEnabled;
		std::string sqlitePath;
		std::string sqliteTable;

		bool mysqlEnabled;
		std::string mysqlDsn;
		std::string mysqlUsername;
		std::string mysqlTable;

		bool telnetEnabled;
		bool telnetAllowRemote;
		std::string telnetBindAddress;
		int telnetPort;

		bool toastEnabled;
		bool toastIncludeMessage;

		Config()
			: enabled(false), acknowledged(false), filteredOnly(true), maskAddress(true),
			  includeMessage(false), mqttEnabled(false), mqttAllowInsecure(false),
			  sqliteEnabled(false), mysqlEnabled(false), telnetEnabled(false),
			  telnetAllowRemote(false), telnetPort(8024), toastEnabled(false),
			  toastIncludeMessage(false) {}
	};

	struct AdapterState
	{
		std::string status;
		unsigned long delivered;
		unsigned long failed;
		AdapterState() : status("Disabled."), delivered(0), failed(0) {}
	};

	struct OutputTask
	{
		pdw::publishing::PublishEvent event;
		bool test;
		int targets;
		Config testConfig;
		std::string mqttPassword;
		std::string mysqlPassword;
		OutputTask() : test(false), targets(0) {}
	};

	CRITICAL_SECTION g_lock;
	bool g_initialized = false;
	volatile LONG g_stopping = 0;
	HANDLE g_workEvent = NULL;
	HANDLE g_stopEvent = NULL;
	HANDLE g_thread = NULL;
	bool g_curlAvailable = false;
	std::deque<OutputTask> g_queue;
	Config g_config;
	unsigned long g_configGeneration = 0;
	unsigned long g_dropped = 0;
	std::string g_managerStatus("Data outputs have not been initialized.");
	AdapterState g_mqttState;
	AdapterState g_sqliteState;
	AdapterState g_mysqlState;
	AdapterState g_telnetState;
	AdapterState g_toastState;
	pdw::outputs::SqliteOutput g_sqlite;
	pdw::outputs::MysqlOdbcOutput g_mysql;
	pdw::outputs::TelnetOutputServer g_telnet;
	pdw::outputs::WindowsToast g_toast;

	void WipeString(std::string& value)
	{
		if (!value.empty()) SecureZeroMemory(&value[0], value.size());
		value.clear();
	}

	void WipeTask(OutputTask& task)
	{
		WipeString(task.mqttPassword);
		WipeString(task.mysqlPassword);
	}

	Config ProfileConfig()
	{
		Config config;
		config.enabled = Profile.dataOutputsEnabled != 0;
		config.acknowledged = Profile.dataOutputsPermissionAcknowledged != 0;
		config.filteredOnly = Profile.dataOutputsFilteredOnly != 0;
		config.maskAddress = Profile.dataOutputsMaskAddress != 0;
		config.includeMessage = Profile.dataOutputsIncludeMessage != 0;
		config.sourceAlias = pdw::events::PdwTextToUtf8(Profile.dataOutputsSourceAlias);
		config.mqttEnabled = Profile.mqttEnabled != 0;
		config.mqttAllowInsecure = Profile.mqttAllowInsecure != 0;
		config.mqttBrokerUrl = pdw::events::PdwTextToUtf8(Profile.mqttBrokerUrl);
		config.mqttTopic = pdw::events::PdwTextToUtf8(Profile.mqttTopic);
		config.mqttUsername = pdw::events::PdwTextToUtf8(Profile.mqttUsername);
		config.sqliteEnabled = Profile.sqliteOutputEnabled != 0;
		config.sqlitePath = Profile.sqliteOutputPath;
		config.sqliteTable = Profile.sqliteOutputTable;
		config.mysqlEnabled = Profile.mysqlOdbcEnabled != 0;
		config.mysqlDsn = Profile.mysqlOdbcDsn;
		config.mysqlUsername = Profile.mysqlOdbcUsername;
		config.mysqlTable = Profile.mysqlOdbcTable;
		config.telnetEnabled = Profile.telnetOutputEnabled != 0;
		config.telnetAllowRemote = Profile.telnetAllowRemote != 0;
		config.telnetBindAddress = Profile.telnetBindAddress;
		config.telnetPort = Profile.telnetPort;
		config.toastEnabled = Profile.windowsToastEnabled != 0;
		config.toastIncludeMessage = Profile.windowsToastIncludeMessage != 0;
		return config;
	}

	Config SnapshotConfig(unsigned long* generation = NULL)
	{
		EnterCriticalSection(&g_lock);
		const Config config(g_config);
		if (generation) *generation = g_configGeneration;
		LeaveCriticalSection(&g_lock);
		return config;
	}

	int EnabledTargets(const Config& config)
	{
		int targets = 0;
		if (config.mqttEnabled) targets |= TARGET_MQTT;
		if (config.sqliteEnabled) targets |= TARGET_SQLITE;
		if (config.mysqlEnabled) targets |= TARGET_MYSQL;
		if (config.telnetEnabled) targets |= TARGET_TELNET;
		if (config.toastEnabled) targets |= TARGET_TOAST;
		return targets;
	}

	void SetManagerStatus(const std::string& status)
	{
		if (!g_initialized) return;
		EnterCriticalSection(&g_lock);
		g_managerStatus = status;
		LeaveCriticalSection(&g_lock);
	}

	AdapterState& StateForTarget(int target)
	{
		if (target == TARGET_MQTT) return g_mqttState;
		if (target == TARGET_SQLITE) return g_sqliteState;
		if (target == TARGET_MYSQL) return g_mysqlState;
		if (target == TARGET_TELNET) return g_telnetState;
		return g_toastState;
	}

	void SetAdapterStatus(int target, bool success, const std::string& status, bool count = true)
	{
		EnterCriticalSection(&g_lock);
		AdapterState& state = StateForTarget(target);
		state.status = status;
		if (count)
		{
			if (success) ++state.delivered;
			else ++state.failed;
		}
		LeaveCriticalSection(&g_lock);
	}

	std::string CredentialTarget(const char* name)
	{
		return std::string("PDW Data Output ") + name + ": " + szIniPathName;
	}

	bool ReadSecret(const char* name, std::string& value, std::string& error)
	{
		value.clear();
		PCREDENTIALA credential = NULL;
		if (!CredReadA(CredentialTarget(name).c_str(), CRED_TYPE_GENERIC, 0, &credential))
		{
			if (GetLastError() == ERROR_NOT_FOUND) return true;
			error = "Windows Credential Manager could not read the data-output password.";
			return false;
		}
		if (credential->CredentialBlob && credential->CredentialBlobSize)
			value.assign(reinterpret_cast<const char*>(credential->CredentialBlob), credential->CredentialBlobSize);
		CredFree(credential);
		return true;
	}

	bool WriteSecret(const char* name, const std::string& value, std::string& error)
	{
		const std::string target = CredentialTarget(name);
		if (value.empty())
		{
			if (CredDeleteA(target.c_str(), CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND)
				return true;
			error = "Windows Credential Manager could not clear the data-output password.";
			return false;
		}
		CREDENTIALA credential = {};
		credential.Type = CRED_TYPE_GENERIC;
		credential.TargetName = const_cast<char*>(target.c_str());
		credential.CredentialBlobSize = static_cast<DWORD>(value.size());
		credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(value.data()));
		credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
		credential.UserName = const_cast<char*>("PDW");
		if (CredWriteA(&credential, 0)) return true;
		error = "Windows Credential Manager could not save the data-output password.";
		return false;
	}

	std::string ResolvePath(const std::string& path)
	{
		if (path.empty()) return path;
		if ((path.size() >= 2 && path[1] == ':') ||
			(path.size() >= 2 && path[0] == '\\' && path[1] == '\\')) return path;
		std::string result(szPath);
		if (!result.empty() && result[result.size() - 1] != '\\') result += '\\';
		return result + path;
	}

	std::size_t DiscardResponse(char*, std::size_t size, std::size_t count, void*)
	{
		return size * count;
	}

	bool DeliverMqtt(const Config& config, const pdw::publishing::PublishEvent& event,
		const std::string& suppliedPassword, bool useSuppliedPassword, std::string& error)
	{
		if (!g_curlAvailable)
		{
			error = "MQTT is unavailable because libcurl could not initialize.";
			return false;
		}
		if (!pdw::outputs::ValidateMqttSettings(config.mqttBrokerUrl, config.mqttTopic,
			config.mqttAllowInsecure, error)) return false;
		std::string password(suppliedPassword);
		if (!useSuppliedPassword && !ReadSecret("MQTT Password", password, error)) return false;
		std::string utf8Password = pdw::events::PdwTextToUtf8(password.c_str());
		WipeString(password);
		password.swap(utf8Password);
		CURL* curl = curl_easy_init();
		if (!curl)
		{
			error = "MQTT could not create a publishing request.";
			WipeString(password);
			return false;
		}
		const std::string url = pdw::outputs::BuildMqttPublishUrl(config.mqttBrokerUrl, config.mqttTopic);
		const std::string payload = pdw::publishing::BuildJsonObject(event);
		char curlError[CURL_ERROR_SIZE] = {};
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR,
			config.mqttBrokerUrl.compare(0, 8, "mqtts://") == 0 ? "mqtts" : "mqtt");
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.data());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L);
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, DiscardResponse);
		curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curlError);
		if (!config.mqttUsername.empty())
		{
			curl_easy_setopt(curl, CURLOPT_USERNAME, config.mqttUsername.c_str());
			curl_easy_setopt(curl, CURLOPT_PASSWORD, password.c_str());
		}
		const CURLcode result = curl_easy_perform(curl);
		curl_easy_cleanup(curl);
		WipeString(password);
		if (result == CURLE_OK) return true;
		error = std::string("MQTT publish failed: ") +
			(curlError[0] ? curlError : curl_easy_strerror(result));
		return false;
	}

	pdw::publishing::PublishEvent TransformEvent(const Config& config,
		const pdw::publishing::PublishEvent& event, bool includeMessage)
	{
		pdw::publishing::TransformOptions options;
		options.sourceAlias = config.sourceAlias;
		options.maskAddress = config.maskAddress;
		options.includeMessage = includeMessage;
		return pdw::publishing::ApplyTransform(event, options);
	}

	void ProcessTask(OutputTask& task, const Config& currentConfig)
	{
		const Config config = task.test ? task.testConfig : currentConfig;
		const int targets = task.test ? task.targets : EnabledTargets(config);
		const pdw::publishing::PublishEvent event = TransformEvent(config, task.event, config.includeMessage);
		std::string error;

		if (targets & TARGET_SQLITE)
		{
			const std::string path = pdw::events::PdwTextToUtf8(ResolvePath(config.sqlitePath).c_str());
			const bool success = g_sqlite.Open(path, config.sqliteTable, error) && g_sqlite.Write(event, error);
			SetAdapterStatus(TARGET_SQLITE, success, success ? "SQLite message stored." : "SQLite: " + error);
		}
		if (targets & TARGET_MQTT)
		{
			error.clear();
			const bool success = DeliverMqtt(config, event, task.mqttPassword, task.test, error);
			SetAdapterStatus(TARGET_MQTT, success, success ? "MQTT QoS 0 message published." : error);
		}
		if (targets & TARGET_MYSQL)
		{
			error.clear();
			if (task.test) g_mysql.Close();
			std::string password(task.mysqlPassword);
			bool secretReady = task.test || ReadSecret("MySQL ODBC Password", password, error);
			const bool success = secretReady &&
				g_mysql.Open(config.mysqlDsn, config.mysqlUsername, password, config.mysqlTable, error) &&
				g_mysql.Write(event, error);
			WipeString(password);
			SetAdapterStatus(TARGET_MYSQL, success, success ? "MySQL ODBC message stored." : "MySQL ODBC: " + error);
		}
		if (targets & TARGET_TELNET)
		{
			if (g_telnet.ClientCount() == 0)
				SetAdapterStatus(TARGET_TELNET, true, "Telnet JSON output is listening; no clients are connected.", false);
			else
			{
				const bool success = g_telnet.Broadcast(pdw::publishing::BuildJsonObject(event));
				SetAdapterStatus(TARGET_TELNET, success, g_telnet.Status());
			}
		}
		if (targets & TARGET_TOAST)
		{
			error.clear();
			const pdw::publishing::PublishEvent toastEvent = TransformEvent(config, task.event,
				config.toastIncludeMessage);
			std::string title = toastEvent.source + " " + toastEvent.mode;
			if (!toastEvent.messageType.empty()) title += " " + toastEvent.messageType;
			std::string body;
			if (!toastEvent.address.empty()) body = toastEvent.address;
			if (config.toastIncludeMessage && !toastEvent.message.empty())
			{
				if (!body.empty()) body += " - ";
				body += toastEvent.message;
			}
			if (body.empty()) body = "Filtered pager message received.";
			const bool success = g_toast.Show(title, body, error);
			SetAdapterStatus(TARGET_TOAST, success, success ? "Windows notification shown." : error);
		}
	}

	unsigned int __stdcall Worker(void*)
	{
		unsigned long observedGeneration = 0;
		while (WaitForSingleObject(g_stopEvent, 0) != WAIT_OBJECT_0)
		{
			unsigned long generation = 0;
			const Config config = SnapshotConfig(&generation);
			if (generation != observedGeneration)
			{
				g_sqlite.Close();
				g_mysql.Close();
				observedGeneration = generation;
			}
			OutputTask task;
			bool available = false;
			EnterCriticalSection(&g_lock);
			if (!g_queue.empty())
			{
				task = g_queue.front();
				WipeTask(g_queue.front());
				g_queue.pop_front();
				available = true;
			}
			LeaveCriticalSection(&g_lock);
			if (!available)
			{
				HANDLE events[2] = {g_stopEvent, g_workEvent};
				WaitForMultipleObjects(2, events, FALSE, 1000);
				continue;
			}
			ProcessTask(task, config);
			WipeTask(task);
		}
		g_sqlite.Close();
		g_mysql.Close();
		g_toast.Shutdown();
		return 0;
	}

	bool QueueTask(OutputTask& task)
	{
		if (!g_initialized || InterlockedCompareExchange(&g_stopping, 0, 0))
		{
			WipeTask(task);
			return false;
		}
		bool queued = false;
		EnterCriticalSection(&g_lock);
		if (g_queue.size() < MAX_QUEUE)
		{
			g_queue.push_back(task);
			queued = true;
		}
		else
		{
			++g_dropped;
			g_managerStatus = "Data-output queue is full; the newest optional-output event was dropped.";
		}
		LeaveCriticalSection(&g_lock);
		WipeTask(task);
		if (queued && g_workEvent) SetEvent(g_workEvent);
		return queued;
	}

	std::string DialogText(HWND dialog, int control, int maximumLength = 4096)
	{
		int length = GetWindowTextLengthA(GetDlgItem(dialog, control));
		if (length < 0) length = 0;
		if (length > maximumLength) length = maximumLength;
		std::vector<char> buffer(static_cast<std::size_t>(length) + 2, '\0');
		GetDlgItemTextA(dialog, control, &buffer[0], static_cast<int>(buffer.size()));
		return std::string(&buffer[0]);
	}

	Config DialogConfig(HWND dialog)
	{
		Config config;
		config.enabled = IsDlgButtonChecked(dialog, IDC_OUTPUT_ENABLE) == BST_CHECKED;
		config.acknowledged = IsDlgButtonChecked(dialog, IDC_OUTPUT_ACK) == BST_CHECKED;
		config.filteredOnly = IsDlgButtonChecked(dialog, IDC_OUTPUT_FILTERED) == BST_CHECKED;
		config.maskAddress = IsDlgButtonChecked(dialog, IDC_OUTPUT_MASK) == BST_CHECKED;
		config.includeMessage = IsDlgButtonChecked(dialog, IDC_OUTPUT_MESSAGE) == BST_CHECKED;
		config.sourceAlias = pdw::events::PdwTextToUtf8(DialogText(dialog, IDC_OUTPUT_ALIAS, DATA_OUTPUT_ALIAS_LEN).c_str());
		config.mqttEnabled = IsDlgButtonChecked(dialog, IDC_OUTPUT_MQTT_ENABLE) == BST_CHECKED;
		config.mqttAllowInsecure = IsDlgButtonChecked(dialog, IDC_OUTPUT_MQTT_INSECURE) == BST_CHECKED;
		config.mqttBrokerUrl = pdw::events::PdwTextToUtf8(DialogText(dialog, IDC_OUTPUT_MQTT_BROKER, MQTT_BROKER_URL_LEN).c_str());
		config.mqttTopic = pdw::events::PdwTextToUtf8(DialogText(dialog, IDC_OUTPUT_MQTT_TOPIC, MQTT_TOPIC_LEN).c_str());
		config.mqttUsername = pdw::events::PdwTextToUtf8(DialogText(dialog, IDC_OUTPUT_MQTT_USER, MQTT_USERNAME_LEN).c_str());
		config.sqliteEnabled = IsDlgButtonChecked(dialog, IDC_OUTPUT_SQLITE_ENABLE) == BST_CHECKED;
		config.sqlitePath = DialogText(dialog, IDC_OUTPUT_SQLITE_PATH, SQLITE_OUTPUT_PATH_LEN);
		config.sqliteTable = DialogText(dialog, IDC_OUTPUT_SQLITE_TABLE, SQL_OUTPUT_TABLE_LEN);
		config.mysqlEnabled = IsDlgButtonChecked(dialog, IDC_OUTPUT_MYSQL_ENABLE) == BST_CHECKED;
		config.mysqlDsn = DialogText(dialog, IDC_OUTPUT_MYSQL_DSN, MYSQL_ODBC_DSN_LEN);
		config.mysqlUsername = DialogText(dialog, IDC_OUTPUT_MYSQL_USER, MYSQL_ODBC_USERNAME_LEN);
		config.mysqlTable = DialogText(dialog, IDC_OUTPUT_MYSQL_TABLE, SQL_OUTPUT_TABLE_LEN);
		config.telnetEnabled = IsDlgButtonChecked(dialog, IDC_OUTPUT_TELNET_ENABLE) == BST_CHECKED;
		config.telnetAllowRemote = IsDlgButtonChecked(dialog, IDC_OUTPUT_TELNET_REMOTE) == BST_CHECKED;
		config.telnetBindAddress = DialogText(dialog, IDC_OUTPUT_TELNET_BIND, TELNET_BIND_ADDRESS_LEN);
		BOOL portReady = FALSE;
		config.telnetPort = static_cast<int>(GetDlgItemInt(dialog, IDC_OUTPUT_TELNET_PORT, &portReady, FALSE));
		if (!portReady) config.telnetPort = 0;
		config.toastEnabled = IsDlgButtonChecked(dialog, IDC_OUTPUT_TOAST_ENABLE) == BST_CHECKED;
		config.toastIncludeMessage = IsDlgButtonChecked(dialog, IDC_OUTPUT_TOAST_MESSAGE) == BST_CHECKED;
		return config;
	}

	bool ValidateConfig(const Config& config, int targets, bool requireGlobal, std::string& error)
	{
		error.clear();
		if (requireGlobal && !config.enabled)
		{
			error = "Enable data outputs before running an output test.";
			return false;
		}
		if (requireGlobal && !config.acknowledged)
		{
			error = "Accept the country/jurisdiction permission acknowledgement before sending or storing a test.";
			return false;
		}
		if (config.enabled && config.sourceAlias.empty())
		{
			error = "Enter a source name for data-output records.";
			return false;
		}
		if (config.enabled && !EnabledTargets(config))
		{
			error = "Choose at least one data-output adapter, or turn off data outputs.";
			return false;
		}
		if ((targets & TARGET_MQTT) && !config.mqttEnabled)
		{
			error = "Enable MQTT before testing it.";
			return false;
		}
		if ((targets & TARGET_MQTT) && !pdw::outputs::ValidateMqttSettings(
			config.mqttBrokerUrl, config.mqttTopic, config.mqttAllowInsecure, error)) return false;
		if ((targets & TARGET_SQLITE) && !config.sqliteEnabled)
		{
			error = "Enable SQLite before testing it.";
			return false;
		}
		if ((targets & TARGET_SQLITE) && config.sqlitePath.empty())
		{
			error = "Choose a SQLite database file.";
			return false;
		}
		if ((targets & TARGET_SQLITE) && !pdw::outputs::IsSafeSqlIdentifier(config.sqliteTable))
		{
			error = "SQLite table name must begin with a letter or underscore and contain only letters, numbers, or underscores.";
			return false;
		}
		if ((targets & TARGET_MYSQL) && !config.mysqlEnabled)
		{
			error = "Enable MySQL ODBC before testing it.";
			return false;
		}
		if ((targets & TARGET_MYSQL) && config.mysqlDsn.empty())
		{
			error = "Enter the name of a configured MySQL ODBC DSN.";
			return false;
		}
		if ((targets & TARGET_MYSQL) && !pdw::outputs::IsSafeSqlIdentifier(config.mysqlTable))
		{
			error = "MySQL table name must begin with a letter or underscore and contain only letters, numbers, or underscores.";
			return false;
		}
		if ((targets & TARGET_TELNET) && !config.telnetEnabled)
		{
			error = "Enable the Telnet JSON stream before testing it.";
			return false;
		}
		if ((targets & TARGET_TELNET) && !pdw::outputs::ValidateTelnetEndpoint(
			config.telnetBindAddress, config.telnetPort, config.telnetAllowRemote, error)) return false;
		if ((targets & TARGET_TOAST) && !config.toastEnabled)
		{
			error = "Enable Windows notifications before testing them.";
			return false;
		}
		return true;
	}

	void ShowAdapterPage(HWND dialog)
	{
		static const int mqttControls[] = {
			IDC_OUTPUT_MQTT_PANEL, IDC_OUTPUT_MQTT_ENABLE, IDC_OUTPUT_MQTT_BROKER_LABEL,
			IDC_OUTPUT_MQTT_BROKER, IDC_OUTPUT_MQTT_TOPIC_LABEL, IDC_OUTPUT_MQTT_TOPIC,
			IDC_OUTPUT_MQTT_USER_LABEL, IDC_OUTPUT_MQTT_USER, IDC_OUTPUT_MQTT_PASSWORD_LABEL,
			IDC_OUTPUT_MQTT_PASSWORD, IDC_OUTPUT_MQTT_CLEAR_PASSWORD, IDC_OUTPUT_MQTT_INSECURE,
			IDC_OUTPUT_MQTT_TEST, IDC_OUTPUT_MQTT_NOTE };
		static const int sqliteControls[] = {
			IDC_OUTPUT_SQLITE_PANEL, IDC_OUTPUT_SQLITE_ENABLE, IDC_OUTPUT_SQLITE_PATH_LABEL,
			IDC_OUTPUT_SQLITE_PATH, IDC_OUTPUT_SQLITE_BROWSE, IDC_OUTPUT_SQLITE_TABLE_LABEL,
			IDC_OUTPUT_SQLITE_TABLE, IDC_OUTPUT_SQLITE_TEST, IDC_OUTPUT_SQLITE_NOTE };
		static const int mysqlControls[] = {
			IDC_OUTPUT_MYSQL_PANEL, IDC_OUTPUT_MYSQL_ENABLE, IDC_OUTPUT_MYSQL_DSN_LABEL,
			IDC_OUTPUT_MYSQL_DSN, IDC_OUTPUT_MYSQL_USER_LABEL, IDC_OUTPUT_MYSQL_USER,
			IDC_OUTPUT_MYSQL_PASSWORD_LABEL, IDC_OUTPUT_MYSQL_PASSWORD,
			IDC_OUTPUT_MYSQL_CLEAR_PASSWORD, IDC_OUTPUT_MYSQL_TABLE_LABEL,
			IDC_OUTPUT_MYSQL_TABLE, IDC_OUTPUT_MYSQL_TEST, IDC_OUTPUT_MYSQL_NOTE };
		static const int telnetControls[] = {
			IDC_OUTPUT_TELNET_PANEL, IDC_OUTPUT_TELNET_ENABLE, IDC_OUTPUT_TELNET_BIND_LABEL,
			IDC_OUTPUT_TELNET_BIND, IDC_OUTPUT_TELNET_PORT_LABEL, IDC_OUTPUT_TELNET_PORT,
			IDC_OUTPUT_TELNET_REMOTE, IDC_OUTPUT_TELNET_TEST, IDC_OUTPUT_TELNET_NOTE };
		static const int toastControls[] = {
			IDC_OUTPUT_TOAST_PANEL, IDC_OUTPUT_TOAST_ENABLE, IDC_OUTPUT_TOAST_MESSAGE,
			IDC_OUTPUT_TOAST_TEST, IDC_OUTPUT_TOAST_NOTE };
		struct Page { const int* controls; std::size_t count; };
		static const Page pages[] = {
			{mqttControls, _countof(mqttControls)}, {sqliteControls, _countof(sqliteControls)},
			{mysqlControls, _countof(mysqlControls)}, {telnetControls, _countof(telnetControls)},
			{toastControls, _countof(toastControls)} };
		int selectedPage = TabCtrl_GetCurSel(GetDlgItem(dialog, IDC_OUTPUT_TAB));
		if (selectedPage < 0 || selectedPage >= static_cast<int>(_countof(pages))) selectedPage = 0;
		for (int page = 0; page < static_cast<int>(_countof(pages)); ++page)
			for (std::size_t control = 0; control < pages[page].count; ++control)
				ShowWindow(GetDlgItem(dialog, pages[page].controls[control]), page == selectedPage ? SW_SHOW : SW_HIDE);
	}

	void EnableDialogControls(HWND dialog)
	{
		const bool acknowledged = IsDlgButtonChecked(dialog, IDC_OUTPUT_ACK) == BST_CHECKED;
		const bool globallyEnabled = IsDlgButtonChecked(dialog, IDC_OUTPUT_ENABLE) == BST_CHECKED;
		const bool canTest = acknowledged && globallyEnabled;
		const bool mqtt = IsDlgButtonChecked(dialog, IDC_OUTPUT_MQTT_ENABLE) == BST_CHECKED;
		const bool sqlite = IsDlgButtonChecked(dialog, IDC_OUTPUT_SQLITE_ENABLE) == BST_CHECKED;
		const bool mysql = IsDlgButtonChecked(dialog, IDC_OUTPUT_MYSQL_ENABLE) == BST_CHECKED;
		const bool telnet = IsDlgButtonChecked(dialog, IDC_OUTPUT_TELNET_ENABLE) == BST_CHECKED;
		const bool toast = IsDlgButtonChecked(dialog, IDC_OUTPUT_TOAST_ENABLE) == BST_CHECKED;
		const int mqttFields[] = {IDC_OUTPUT_MQTT_BROKER, IDC_OUTPUT_MQTT_TOPIC, IDC_OUTPUT_MQTT_USER,
			IDC_OUTPUT_MQTT_PASSWORD, IDC_OUTPUT_MQTT_CLEAR_PASSWORD, IDC_OUTPUT_MQTT_INSECURE};
		const int sqliteFields[] = {IDC_OUTPUT_SQLITE_PATH, IDC_OUTPUT_SQLITE_BROWSE, IDC_OUTPUT_SQLITE_TABLE};
		const int mysqlFields[] = {IDC_OUTPUT_MYSQL_DSN, IDC_OUTPUT_MYSQL_USER, IDC_OUTPUT_MYSQL_PASSWORD,
			IDC_OUTPUT_MYSQL_CLEAR_PASSWORD, IDC_OUTPUT_MYSQL_TABLE};
		const int telnetFields[] = {IDC_OUTPUT_TELNET_BIND, IDC_OUTPUT_TELNET_PORT, IDC_OUTPUT_TELNET_REMOTE};
		for (std::size_t index = 0; index < _countof(mqttFields); ++index) EnableWindow(GetDlgItem(dialog, mqttFields[index]), mqtt);
		for (std::size_t index = 0; index < _countof(sqliteFields); ++index) EnableWindow(GetDlgItem(dialog, sqliteFields[index]), sqlite);
		for (std::size_t index = 0; index < _countof(mysqlFields); ++index) EnableWindow(GetDlgItem(dialog, mysqlFields[index]), mysql);
		for (std::size_t index = 0; index < _countof(telnetFields); ++index) EnableWindow(GetDlgItem(dialog, telnetFields[index]), telnet);
		EnableWindow(GetDlgItem(dialog, IDC_OUTPUT_TOAST_MESSAGE), toast);
		EnableWindow(GetDlgItem(dialog, IDC_OUTPUT_MQTT_TEST), canTest && mqtt);
		EnableWindow(GetDlgItem(dialog, IDC_OUTPUT_SQLITE_TEST), canTest && sqlite);
		EnableWindow(GetDlgItem(dialog, IDC_OUTPUT_MYSQL_TEST), canTest && mysql);
		EnableWindow(GetDlgItem(dialog, IDC_OUTPUT_TELNET_TEST), canTest && telnet);
		EnableWindow(GetDlgItem(dialog, IDC_OUTPUT_TOAST_TEST), canTest && toast);
	}

	bool BrowseSqliteFile(HWND dialog)
	{
		char path[SQLITE_OUTPUT_PATH_LEN + 1] = {};
		GetDlgItemTextA(dialog, IDC_OUTPUT_SQLITE_PATH, path, sizeof(path));
		OPENFILENAMEA open = {};
		open.lStructSize = sizeof(open);
		open.hwndOwner = dialog;
		open.lpstrFilter = "SQLite databases (*.sqlite3;*.sqlite;*.db)\0*.sqlite3;*.sqlite;*.db\0All files (*.*)\0*.*\0";
		open.lpstrFile = path;
		open.nMaxFile = sizeof(path);
		open.lpstrDefExt = "sqlite3";
		open.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT;
		if (!GetSaveFileNameA(&open)) return false;
		SetDlgItemTextA(dialog, IDC_OUTPUT_SQLITE_PATH, path);
		return true;
	}

	void QueueAdapterTest(HWND dialog, int target)
	{
		Config config = DialogConfig(dialog);
		std::string error;
		if (!ValidateConfig(config, target, true, error))
		{
			MessageBoxA(dialog, error.c_str(), "PDW Data Outputs", MB_ICONWARNING);
			return;
		}
		if (target == TARGET_TELNET)
		{
			const bool sameEndpoint = Profile.dataOutputsEnabled && Profile.dataOutputsPermissionAcknowledged &&
				Profile.telnetOutputEnabled && config.telnetBindAddress == Profile.telnetBindAddress &&
				config.telnetPort == Profile.telnetPort && config.telnetAllowRemote == (Profile.telnetAllowRemote != 0);
			if (sameEndpoint && g_telnet.IsRunning())
				SetAdapterStatus(TARGET_TELNET, true, "Telnet JSON listener is active on the saved endpoint.", false);
			else
			{
				pdw::outputs::TelnetOutputServer probe;
				const bool success = probe.Start(config.telnetBindAddress,
					static_cast<unsigned short>(config.telnetPort), config.telnetAllowRemote, error);
				probe.Stop();
				SetAdapterStatus(TARGET_TELNET, success,
					success ? "Telnet JSON endpoint passed its bind/listen test." : "Telnet JSON: " + error, false);
			}
			return;
		}
		OutputTask task;
		task.test = true;
		task.targets = target;
		task.testConfig = config;
		task.event = pdw::events::BuildTestEvent(target == TARGET_MQTT ? "MQTT" :
			(target == TARGET_SQLITE ? "SQLite" : (target == TARGET_MYSQL ? "MySQL ODBC" : "Windows notification")));
		task.event.filtered = true;
		task.event.filterMatched = true;
		task.event.address = "1234567";
		if (target == TARGET_MQTT)
		{
			task.mqttPassword = DialogText(dialog, IDC_OUTPUT_MQTT_PASSWORD, 2048);
			if (task.mqttPassword.empty() && IsDlgButtonChecked(dialog, IDC_OUTPUT_MQTT_CLEAR_PASSWORD) != BST_CHECKED &&
				!ReadSecret("MQTT Password", task.mqttPassword, error))
			{
				MessageBoxA(dialog, error.c_str(), "PDW Data Outputs", MB_ICONERROR);
				return;
			}
		}
		if (target == TARGET_MYSQL)
		{
			task.mysqlPassword = DialogText(dialog, IDC_OUTPUT_MYSQL_PASSWORD, 2048);
			if (task.mysqlPassword.empty() && IsDlgButtonChecked(dialog, IDC_OUTPUT_MYSQL_CLEAR_PASSWORD) != BST_CHECKED &&
				!ReadSecret("MySQL ODBC Password", task.mysqlPassword, error))
			{
				MessageBoxA(dialog, error.c_str(), "PDW Data Outputs", MB_ICONERROR);
				return;
			}
		}
		if (QueueTask(task)) SetManagerStatus("Data-output configuration test queued without live pager content.");
	}

	void CopyProfileText(char* destination, std::size_t size, const std::string& value)
	{
		if (!destination || !size) return;
		strncpy(destination, value.c_str(), size - 1);
		destination[size - 1] = '\0';
	}

	bool SaveDialog(HWND dialog)
	{
		const Config config = DialogConfig(dialog);
		std::string error;
		if (config.enabled && !config.acknowledged)
		{
			MessageBoxA(dialog, "To enable data outputs, acknowledge that you are responsible for checking permissions and laws in your own country or jurisdiction.", "PDW Data Outputs", MB_ICONWARNING);
			return false;
		}
		if (!ValidateConfig(config, config.enabled ? EnabledTargets(config) : 0, false, error))
		{
			MessageBoxA(dialog, error.c_str(), "PDW Data Outputs", MB_ICONWARNING);
			return false;
		}

		std::string oldMqtt;
		std::string oldMysql;
		if (!ReadSecret("MQTT Password", oldMqtt, error) ||
			!ReadSecret("MySQL ODBC Password", oldMysql, error))
		{
			MessageBoxA(dialog, error.c_str(), "PDW Data Outputs", MB_ICONERROR);
			WipeString(oldMqtt); WipeString(oldMysql);
			return false;
		}
		std::string newMqtt = DialogText(dialog, IDC_OUTPUT_MQTT_PASSWORD, 2048);
		std::string newMysql = DialogText(dialog, IDC_OUTPUT_MYSQL_PASSWORD, 2048);
		const bool updateMqtt = !newMqtt.empty() || IsDlgButtonChecked(dialog, IDC_OUTPUT_MQTT_CLEAR_PASSWORD) == BST_CHECKED;
		const bool updateMysql = !newMysql.empty() || IsDlgButtonChecked(dialog, IDC_OUTPUT_MYSQL_CLEAR_PASSWORD) == BST_CHECKED;
		bool saved = true;
		if (updateMqtt) saved = WriteSecret("MQTT Password", newMqtt, error);
		if (saved && updateMysql) saved = WriteSecret("MySQL ODBC Password", newMysql, error);
		if (!saved)
		{
			std::string ignored;
			if (updateMqtt) WriteSecret("MQTT Password", oldMqtt, ignored);
			if (updateMysql) WriteSecret("MySQL ODBC Password", oldMysql, ignored);
			MessageBoxA(dialog, error.c_str(), "PDW Data Outputs", MB_ICONERROR);
			WipeString(oldMqtt); WipeString(oldMysql); WipeString(newMqtt); WipeString(newMysql);
			return false;
		}

		Profile.dataOutputsEnabled = config.enabled;
		Profile.dataOutputsPermissionAcknowledged = config.acknowledged;
		Profile.dataOutputsFilteredOnly = config.filteredOnly;
		Profile.dataOutputsMaskAddress = config.maskAddress;
		Profile.dataOutputsIncludeMessage = config.includeMessage;
		CopyProfileText(Profile.dataOutputsSourceAlias, sizeof(Profile.dataOutputsSourceAlias), DialogText(dialog, IDC_OUTPUT_ALIAS, DATA_OUTPUT_ALIAS_LEN));
		Profile.mqttEnabled = config.mqttEnabled;
		Profile.mqttAllowInsecure = config.mqttAllowInsecure;
		CopyProfileText(Profile.mqttBrokerUrl, sizeof(Profile.mqttBrokerUrl), DialogText(dialog, IDC_OUTPUT_MQTT_BROKER, MQTT_BROKER_URL_LEN));
		CopyProfileText(Profile.mqttTopic, sizeof(Profile.mqttTopic), DialogText(dialog, IDC_OUTPUT_MQTT_TOPIC, MQTT_TOPIC_LEN));
		CopyProfileText(Profile.mqttUsername, sizeof(Profile.mqttUsername), DialogText(dialog, IDC_OUTPUT_MQTT_USER, MQTT_USERNAME_LEN));
		Profile.sqliteOutputEnabled = config.sqliteEnabled;
		CopyProfileText(Profile.sqliteOutputPath, sizeof(Profile.sqliteOutputPath), config.sqlitePath);
		CopyProfileText(Profile.sqliteOutputTable, sizeof(Profile.sqliteOutputTable), config.sqliteTable);
		Profile.mysqlOdbcEnabled = config.mysqlEnabled;
		CopyProfileText(Profile.mysqlOdbcDsn, sizeof(Profile.mysqlOdbcDsn), config.mysqlDsn);
		CopyProfileText(Profile.mysqlOdbcUsername, sizeof(Profile.mysqlOdbcUsername), config.mysqlUsername);
		CopyProfileText(Profile.mysqlOdbcTable, sizeof(Profile.mysqlOdbcTable), config.mysqlTable);
		Profile.telnetOutputEnabled = config.telnetEnabled;
		Profile.telnetAllowRemote = config.telnetAllowRemote;
		CopyProfileText(Profile.telnetBindAddress, sizeof(Profile.telnetBindAddress), config.telnetBindAddress);
		Profile.telnetPort = config.telnetPort;
		Profile.windowsToastEnabled = config.toastEnabled;
		Profile.windowsToastIncludeMessage = config.toastIncludeMessage;
		WriteSettings();
		DataOutputSettingsChanged();
		WipeString(oldMqtt); WipeString(oldMysql); WipeString(newMqtt); WipeString(newMysql);
		return true;
	}
}

void DataOutputManagerInitialize(void)
{
	if (g_initialized) return;
	InitializeCriticalSection(&g_lock);
	g_initialized = true;
	InterlockedExchange(&g_stopping, 0);
	g_workEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	g_stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!g_workEvent || !g_stopEvent)
	{
		SetManagerStatus("Data outputs could not create worker events; every legacy output remains available.");
		InterlockedExchange(&g_stopping, 1);
		return;
	}
	g_curlAvailable = CurlRuntimeAcquire();
	DataOutputSettingsChanged();
	const uintptr_t thread = _beginthreadex(NULL, 0, Worker, NULL, 0, NULL);
	g_thread = reinterpret_cast<HANDLE>(thread);
	if (!g_thread)
	{
		SetManagerStatus("Data outputs could not start their worker; every legacy output remains available.");
		InterlockedExchange(&g_stopping, 1);
		return;
	}
}

void DataOutputManagerShutdown(void)
{
	if (!g_initialized) return;
	InterlockedExchange(&g_stopping, 1);
	g_telnet.Stop();
	if (g_stopEvent) SetEvent(g_stopEvent);
	if (g_thread && WaitForSingleObject(g_thread, 20000) == WAIT_OBJECT_0)
	{
		CloseHandle(g_thread);
		g_thread = NULL;
	}
	if (g_thread) return;
	EnterCriticalSection(&g_lock);
	for (std::deque<OutputTask>::iterator task = g_queue.begin(); task != g_queue.end(); ++task) WipeTask(*task);
	g_queue.clear();
	LeaveCriticalSection(&g_lock);
	if (g_workEvent) CloseHandle(g_workEvent);
	if (g_stopEvent) CloseHandle(g_stopEvent);
	g_workEvent = g_stopEvent = NULL;
	if (g_curlAvailable) CurlRuntimeRelease();
	g_curlAvailable = false;
	g_initialized = false;
	DeleteCriticalSection(&g_lock);
}

void DataOutputSettingsChanged(void)
{
	if (!g_initialized) return;
	const Config config = ProfileConfig();
	EnterCriticalSection(&g_lock);
	g_config = config;
	++g_configGeneration;
	LeaveCriticalSection(&g_lock);

	g_telnet.Stop();
	if (!config.enabled) SetManagerStatus("Data outputs are disabled; legacy outputs are unchanged.");
	else if (!config.acknowledged) SetManagerStatus("Data outputs require the country/jurisdiction permission acknowledgement.");
	else if (!EnabledTargets(config)) SetManagerStatus("Data outputs are enabled, but no adapter is selected.");
	else SetManagerStatus("Selected data outputs are enabled and isolated from legacy outputs.");

	if (config.enabled && config.acknowledged && config.telnetEnabled)
	{
		std::string error;
		const bool started = pdw::outputs::ValidateTelnetEndpoint(config.telnetBindAddress,
			config.telnetPort, config.telnetAllowRemote, error) &&
			g_telnet.Start(config.telnetBindAddress, static_cast<unsigned short>(config.telnetPort),
				config.telnetAllowRemote, error);
		SetAdapterStatus(TARGET_TELNET, started, started ? g_telnet.Status() : "Telnet JSON: " + error, false);
	}
	else SetAdapterStatus(TARGET_TELNET, true, "Disabled.", false);
	if (!config.mqttEnabled) SetAdapterStatus(TARGET_MQTT, true, "Disabled.", false);
	if (!config.sqliteEnabled) SetAdapterStatus(TARGET_SQLITE, true, "Disabled.", false);
	if (!config.mysqlEnabled) SetAdapterStatus(TARGET_MYSQL, true, "Disabled.", false);
	if (!config.toastEnabled) SetAdapterStatus(TARGET_TOAST, true, "Disabled.", false);
	if (g_workEvent) SetEvent(g_workEvent);
}

void DataOutputGetStatusText(char* buffer, size_t bufferSize)
{
	if (!buffer || !bufferSize) return;
	if (!g_initialized)
	{
		strncpy(buffer, "Data outputs have not been initialized.", bufferSize - 1);
		buffer[bufferSize - 1] = '\0';
		return;
	}
	EnterCriticalSection(&g_lock);
	_snprintf_s(buffer, bufferSize, _TRUNCATE,
		"%s\r\nMQTT: %s  SQLite: %s  MySQL: %s\r\nTelnet: %s  Toast: %s  Dropped: %lu",
		g_managerStatus.c_str(), g_mqttState.status.c_str(), g_sqliteState.status.c_str(),
		g_mysqlState.status.c_str(), g_telnetState.status.c_str(), g_toastState.status.c_str(), g_dropped);
	LeaveCriticalSection(&g_lock);
}

void DataOutputPublishDecodedMessage(const DecodedMessageNotificationContext& context)
{
	DataOutputPublishEvent(pdw::events::BuildDecodedEvent(context));
}

void DataOutputPublishEvent(const pdw::publishing::PublishEvent& event)
{
	if (!g_initialized || InterlockedCompareExchange(&g_stopping, 0, 0)) return;
	const Config config = SnapshotConfig();
	if (!config.enabled || !config.acknowledged || !EnabledTargets(config) ||
		(config.filteredOnly && !event.filtered)) return;
	OutputTask task;
	task.event = event;
	QueueTask(task);
}

BOOL FAR PASCAL DataOutputsDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	(void)lParam;
	const UINT_PTR STATUS_TIMER = 12;
	switch (uMsg)
	{
		case WM_INITDIALOG:
		{
			CenterWindow(hDlg);
			CheckDlgButton(hDlg, IDC_OUTPUT_ENABLE, Profile.dataOutputsEnabled ? BST_CHECKED : BST_UNCHECKED);
			CheckDlgButton(hDlg, IDC_OUTPUT_ACK, Profile.dataOutputsPermissionAcknowledged ? BST_CHECKED : BST_UNCHECKED);
			CheckRadioButton(hDlg, IDC_OUTPUT_FILTERED, IDC_OUTPUT_ALL,
				Profile.dataOutputsFilteredOnly ? IDC_OUTPUT_FILTERED : IDC_OUTPUT_ALL);
			CheckDlgButton(hDlg, IDC_OUTPUT_MASK, Profile.dataOutputsMaskAddress ? BST_CHECKED : BST_UNCHECKED);
			CheckDlgButton(hDlg, IDC_OUTPUT_MESSAGE, Profile.dataOutputsIncludeMessage ? BST_CHECKED : BST_UNCHECKED);
			SetDlgItemTextA(hDlg, IDC_OUTPUT_ALIAS, Profile.dataOutputsSourceAlias);

			CheckDlgButton(hDlg, IDC_OUTPUT_MQTT_ENABLE, Profile.mqttEnabled ? BST_CHECKED : BST_UNCHECKED);
			CheckDlgButton(hDlg, IDC_OUTPUT_MQTT_INSECURE, Profile.mqttAllowInsecure ? BST_CHECKED : BST_UNCHECKED);
			SetDlgItemTextA(hDlg, IDC_OUTPUT_MQTT_BROKER, Profile.mqttBrokerUrl);
			SetDlgItemTextA(hDlg, IDC_OUTPUT_MQTT_TOPIC, Profile.mqttTopic);
			SetDlgItemTextA(hDlg, IDC_OUTPUT_MQTT_USER, Profile.mqttUsername);
			SetDlgItemTextA(hDlg, IDC_OUTPUT_MQTT_PASSWORD, "");

			CheckDlgButton(hDlg, IDC_OUTPUT_SQLITE_ENABLE, Profile.sqliteOutputEnabled ? BST_CHECKED : BST_UNCHECKED);
			SetDlgItemTextA(hDlg, IDC_OUTPUT_SQLITE_PATH, Profile.sqliteOutputPath);
			SetDlgItemTextA(hDlg, IDC_OUTPUT_SQLITE_TABLE, Profile.sqliteOutputTable);

			CheckDlgButton(hDlg, IDC_OUTPUT_MYSQL_ENABLE, Profile.mysqlOdbcEnabled ? BST_CHECKED : BST_UNCHECKED);
			SetDlgItemTextA(hDlg, IDC_OUTPUT_MYSQL_DSN, Profile.mysqlOdbcDsn);
			SetDlgItemTextA(hDlg, IDC_OUTPUT_MYSQL_USER, Profile.mysqlOdbcUsername);
			SetDlgItemTextA(hDlg, IDC_OUTPUT_MYSQL_PASSWORD, "");
			SetDlgItemTextA(hDlg, IDC_OUTPUT_MYSQL_TABLE, Profile.mysqlOdbcTable);

			CheckDlgButton(hDlg, IDC_OUTPUT_TELNET_ENABLE, Profile.telnetOutputEnabled ? BST_CHECKED : BST_UNCHECKED);
			CheckDlgButton(hDlg, IDC_OUTPUT_TELNET_REMOTE, Profile.telnetAllowRemote ? BST_CHECKED : BST_UNCHECKED);
			SetDlgItemTextA(hDlg, IDC_OUTPUT_TELNET_BIND, Profile.telnetBindAddress);
			SetDlgItemInt(hDlg, IDC_OUTPUT_TELNET_PORT, Profile.telnetPort, FALSE);

			CheckDlgButton(hDlg, IDC_OUTPUT_TOAST_ENABLE, Profile.windowsToastEnabled ? BST_CHECKED : BST_UNCHECKED);
			CheckDlgButton(hDlg, IDC_OUTPUT_TOAST_MESSAGE, Profile.windowsToastIncludeMessage ? BST_CHECKED : BST_UNCHECKED);

			SendMessageW(GetDlgItem(hDlg, IDC_OUTPUT_MQTT_PASSWORD), EM_SETCUEBANNER, TRUE,
				reinterpret_cast<LPARAM>(L"Leave blank to keep saved password"));
			SendMessageW(GetDlgItem(hDlg, IDC_OUTPUT_MYSQL_PASSWORD), EM_SETCUEBANNER, TRUE,
				reinterpret_cast<LPARAM>(L"Leave blank to keep saved password"));

			TCITEMA item = {};
			item.mask = TCIF_TEXT;
			char mqtt[] = "MQTT"; item.pszText = mqtt; TabCtrl_InsertItem(GetDlgItem(hDlg, IDC_OUTPUT_TAB), 0, &item);
			char sqlite[] = "SQLite"; item.pszText = sqlite; TabCtrl_InsertItem(GetDlgItem(hDlg, IDC_OUTPUT_TAB), 1, &item);
			char mysql[] = "MySQL (ODBC)"; item.pszText = mysql; TabCtrl_InsertItem(GetDlgItem(hDlg, IDC_OUTPUT_TAB), 2, &item);
			char telnet[] = "Telnet JSON"; item.pszText = telnet; TabCtrl_InsertItem(GetDlgItem(hDlg, IDC_OUTPUT_TAB), 3, &item);
			char toast[] = "Windows"; item.pszText = toast; TabCtrl_InsertItem(GetDlgItem(hDlg, IDC_OUTPUT_TAB), 4, &item);
			TabCtrl_SetCurSel(GetDlgItem(hDlg, IDC_OUTPUT_TAB), 0);
			ShowAdapterPage(hDlg);
			EnableDialogControls(hDlg);
			char status[2048]; DataOutputGetStatusText(status, sizeof(status)); SetDlgItemTextA(hDlg, IDC_OUTPUT_STATUS, status);
			SetTimer(hDlg, STATUS_TIMER, 500, NULL);
			return TRUE;
		}

		case WM_NOTIFY:
			if (lParam && reinterpret_cast<LPNMHDR>(lParam)->idFrom == IDC_OUTPUT_TAB &&
				reinterpret_cast<LPNMHDR>(lParam)->code == TCN_SELCHANGE)
			{
				ShowAdapterPage(hDlg);
				EnableDialogControls(hDlg);
				return TRUE;
			}
			break;

		case WM_TIMER:
			if (wParam == STATUS_TIMER)
			{
				char status[2048]; DataOutputGetStatusText(status, sizeof(status)); SetDlgItemTextA(hDlg, IDC_OUTPUT_STATUS, status);
				return TRUE;
			}
			break;

		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
				case IDC_OUTPUT_ENABLE:
				case IDC_OUTPUT_ACK:
				case IDC_OUTPUT_MQTT_ENABLE:
				case IDC_OUTPUT_SQLITE_ENABLE:
				case IDC_OUTPUT_MYSQL_ENABLE:
				case IDC_OUTPUT_TELNET_ENABLE:
				case IDC_OUTPUT_TOAST_ENABLE:
					EnableDialogControls(hDlg);
					return TRUE;
				case IDC_OUTPUT_MQTT_PASSWORD:
					if (HIWORD(wParam) == EN_CHANGE && GetWindowTextLengthA(GetDlgItem(hDlg, IDC_OUTPUT_MQTT_PASSWORD)) > 0)
						CheckDlgButton(hDlg, IDC_OUTPUT_MQTT_CLEAR_PASSWORD, BST_UNCHECKED);
					return TRUE;
				case IDC_OUTPUT_MYSQL_PASSWORD:
					if (HIWORD(wParam) == EN_CHANGE && GetWindowTextLengthA(GetDlgItem(hDlg, IDC_OUTPUT_MYSQL_PASSWORD)) > 0)
						CheckDlgButton(hDlg, IDC_OUTPUT_MYSQL_CLEAR_PASSWORD, BST_UNCHECKED);
					return TRUE;
				case IDC_OUTPUT_SQLITE_BROWSE:
					BrowseSqliteFile(hDlg);
					return TRUE;
				case IDC_OUTPUT_MQTT_TEST: QueueAdapterTest(hDlg, TARGET_MQTT); return TRUE;
				case IDC_OUTPUT_SQLITE_TEST: QueueAdapterTest(hDlg, TARGET_SQLITE); return TRUE;
				case IDC_OUTPUT_MYSQL_TEST: QueueAdapterTest(hDlg, TARGET_MYSQL); return TRUE;
				case IDC_OUTPUT_TELNET_TEST: QueueAdapterTest(hDlg, TARGET_TELNET); return TRUE;
				case IDC_OUTPUT_TOAST_TEST: QueueAdapterTest(hDlg, TARGET_TOAST); return TRUE;
				case IDOK:
					if (SaveDialog(hDlg))
					{
						KillTimer(hDlg, STATUS_TIMER);
						EndDialog(hDlg, TRUE);
					}
					return TRUE;
				case IDCANCEL:
					KillTimer(hDlg, STATUS_TIMER);
					EndDialog(hDlg, FALSE);
					return TRUE;
			}
			break;

		case WM_CLOSE:
			KillTimer(hDlg, STATUS_TIMER);
			EndDialog(hDlg, FALSE);
			return TRUE;
	}
	return FALSE;
}
