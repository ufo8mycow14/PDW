#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>
#include <wincred.h>
#include <process.h>
#include <shellapi.h>
#include <shlobj.h>

#include <curl/curl.h>
#include <openssl/hmac.h>

#include <algorithm>
#include <deque>
#include <string>
#include <vector>

#include "headers\resource.h"
#include "headers\pdw.h"
#include "headers\initapp.h"
#include "headers\notification.h"
#include "headers\output_health.h"
#include "headers\publishing.h"
#include "publishing_core.h"
#include "publishing_delivery_state.h"
#include "publishing_job_store.h"
#include "curl_runtime.h"
#include "decoded_event.h"

namespace
{
	const std::size_t MAX_QUEUE = 500;
	const std::size_t MAX_FEED_EVENTS = 200;
	const DWORD FILE_OPERATION_RETRY_DELAY_MS = 5000;
	const UINT_PTR DIALOG_TIMER = 7;

	struct PublishTask
	{
		pdw::publishing::PublishEvent event;
		std::string payload;
		std::string spoolPath;
		std::string staticOutputPath;
		unsigned int targets;
		unsigned int completed;
		unsigned int failed;
		unsigned int staticAttempts;
		unsigned int webhookAttempts;
		// Configuration-only webhook tests are not persisted and retain one
		// simple in-memory retry counter.
		unsigned int attempts;
		bool test;
		std::string testUrl;
		std::string testBearer;
		std::string testHmac;
		PublishTask()
			: targets(0), completed(0), failed(0), staticAttempts(0),
			webhookAttempts(0), attempts(0), test(false) {}
	};

	void WipeTaskSecrets(PublishTask& task)
	{
		if (!task.testBearer.empty()) SecureZeroMemory(&task.testBearer[0], task.testBearer.size());
		if (!task.testHmac.empty()) SecureZeroMemory(&task.testHmac[0], task.testHmac.size());
		task.testBearer.clear();
		task.testHmac.clear();
	}

	void WipeString(std::string& value)
	{
		if (!value.empty()) SecureZeroMemory(&value[0], value.size());
		value.clear();
	}

	struct Config
	{
		bool enabled;
		bool acknowledged;
		bool paused;
		bool filteredOnly;
		bool staticEnabled;
		bool webhookEnabled;
		bool maskAddress;
		bool includeMessage;
		unsigned int minimumInterval;
		std::string outputPath;
		std::string webhookUrl;
		std::string sourceAlias;
		Config()
			: enabled(false), acknowledged(false), paused(false), filteredOnly(true),
			  staticEnabled(false), webhookEnabled(false), maskAddress(false),
			  includeMessage(true), minimumInterval(0) {}
	};

	CRITICAL_SECTION g_lock;
	bool g_initialized = false;
	volatile LONG g_stopping = 0;
	HANDLE g_workEvent = NULL;
	HANDLE g_stopEvent = NULL;
	HANDLE g_thread = NULL;
	bool g_curlInitialized = false;
	std::deque<PublishTask> g_queue;
	pdw::publishing::PendingPublishJobIds g_pendingIds;
	std::vector<pdw::publishing::PublishEvent> g_feed;
	std::string g_feedOutputPath;
	Config g_config;
	char g_status[512] = "Publishing has not been initialized.";

	void CopyText(char* destination, std::size_t size, const char* source)
	{
		if (!destination || !size) return;
		strncpy(destination, source ? source : "", size - 1);
		destination[size - 1] = '\0';
	}

	void SetStatus(const std::string& status)
	{
		if (!g_initialized) return;
		EnterCriticalSection(&g_lock);
		CopyText(g_status, sizeof(g_status), status.c_str());
		LeaveCriticalSection(&g_lock);
	}

	std::string JoinPath(const std::string& folder, const std::string& file)
	{
		if (folder.empty()) return file;
		const char last = folder[folder.size() - 1];
		return folder + ((last == '\\' || last == '/') ? "" : "\\") + file;
	}

	std::string QueueFolder() { return JoinPath(szPath, "PublishQueue"); }
	std::string DeadLetterFolder() { return JoinPath(QueueFolder(), "DeadLetter"); }

	void EnsureFolder(const std::string& path)
	{
		if (CreateDirectoryA(path.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS) return;
	}

	std::string ResolveOutputPath(const std::string& path)
	{
		if (path.empty()) return JoinPath(szPath, "Published");
		if ((path.size() >= 2 && path[1] == ':') ||
			(path.size() >= 2 && path[0] == '\\' && path[1] == '\\')) return path;
		return JoinPath(szPath, path);
	}

	Config ProfileConfig()
	{
		Config config;
		config.enabled = Profile.publishingEnabled != 0;
		config.acknowledged = Profile.publishingPermissionAcknowledged != 0;
		config.paused = Profile.publishingPaused != 0;
		config.filteredOnly = Profile.publishingFilteredOnly != 0;
		config.staticEnabled = Profile.publishingStaticEnabled != 0;
		config.webhookEnabled = Profile.publishingWebhookEnabled != 0;
		config.maskAddress = Profile.publishingMaskAddress != 0;
		config.includeMessage = Profile.publishingIncludeMessage != 0;
		config.minimumInterval = Profile.publishingMinimumIntervalSeconds;
		config.outputPath = ResolveOutputPath(Profile.publishingOutputPath);
		config.webhookUrl = Profile.publishingWebhookUrl;
		config.sourceAlias = Profile.publishingSourceAlias;
		return config;
	}

	Config SnapshotConfig()
	{
		EnterCriticalSection(&g_lock);
		const Config config(g_config);
		LeaveCriticalSection(&g_lock);
		return config;
	}

	unsigned int EnabledTargetMask(const Config& config)
	{
		return (config.staticEnabled ? pdw::publishing::PUBLISH_JOB_TARGET_STATIC : 0) |
			(config.webhookEnabled ? pdw::publishing::PUBLISH_JOB_TARGET_WEBHOOK : 0);
	}

	pdw::publishing::PublishRuntimeState RuntimeState(const Config& config)
	{
		pdw::publishing::PublishRuntimeState runtime;
		runtime.enabled = config.enabled;
		runtime.acknowledged = config.acknowledged;
		runtime.paused = config.paused;
		runtime.filteredOnly = config.filteredOnly;
		runtime.stopping = InterlockedCompareExchange(&g_stopping, 0, 0) != 0;
		runtime.enabledTargets = EnabledTargetMask(config);
		if (!g_curlInitialized)
			runtime.enabledTargets &= ~static_cast<unsigned int>(
				pdw::publishing::PUBLISH_JOB_TARGET_WEBHOOK);
		return runtime;
	}

	pdw::publishing::PublishJobState JobState(const PublishTask& task)
	{
		pdw::publishing::PublishJobState job;
		job.targets = task.targets;
		job.completed = task.completed;
		job.failed = task.failed;
		job.staticAttempts = task.staticAttempts;
		job.webhookAttempts = task.webhookAttempts;
		return job;
	}

	void ApplyJobState(PublishTask& task, const pdw::publishing::PublishJobState& job)
	{
		task.targets = job.targets;
		task.completed = job.completed;
		task.failed = job.failed;
		task.staticAttempts = job.staticAttempts;
		task.webhookAttempts = job.webhookAttempts;
	}

	bool WriteAtomic(const std::string& path, const std::string& contents)
	{
		std::string error;
		return pdw::publishing::WritePublishStaticFileAtomic(path, contents, error);
	}

	bool LoadStaticHistory(const Config& startupConfig);

	bool PublishStatic(const Config& config, const pdw::publishing::PublishEvent& event)
	{
		EnsureFolder(config.outputPath);
		EnterCriticalSection(&g_lock);
		const bool historyMatchesPath = g_feedOutputPath == config.outputPath;
		LeaveCriticalSection(&g_lock);
		if (!historyMatchesPath && !LoadStaticHistory(config)) return false;

		EnterCriticalSection(&g_lock);
		bool isNew = true;
		for (std::vector<pdw::publishing::PublishEvent>::const_iterator existing = g_feed.begin();
			existing != g_feed.end(); ++existing)
			if (existing->id == event.id) { isNew = false; break; }
		LeaveCriticalSection(&g_lock);

		// Commit the append-only history before exposing a new event through the
		// rolling feeds. If the append fails, a retry must attempt it again rather
		// than treating the in-memory event as durable.
		std::string historyError;
		if (isNew && !pdw::publishing::AppendPublishHistoryJsonLineDurable(
			JoinPath(config.outputPath, "messages.jsonl"),
			pdw::publishing::BuildJsonObject(event), historyError)) return false;

		EnterCriticalSection(&g_lock);
		if (isNew)
		{
			bool alreadyPresent = false;
			for (std::vector<pdw::publishing::PublishEvent>::const_iterator existing = g_feed.begin();
				existing != g_feed.end(); ++existing)
				if (existing->id == event.id) { alreadyPresent = true; break; }
			if (!alreadyPresent) g_feed.insert(g_feed.begin(), event);
		}
		if (g_feed.size() > MAX_FEED_EVENTS) g_feed.resize(MAX_FEED_EVENTS);
		const std::vector<pdw::publishing::PublishEvent> feed(g_feed);
		LeaveCriticalSection(&g_lock);
		return WriteAtomic(JoinPath(config.outputPath, "messages.json"), pdw::publishing::BuildJsonFeed(feed)) &&
			WriteAtomic(JoinPath(config.outputPath, "messages.rss"), pdw::publishing::BuildRssFeed(feed)) &&
			WriteAtomic(JoinPath(config.outputPath, "messages.atom"), pdw::publishing::BuildAtomFeed(feed)) &&
			WriteAtomic(JoinPath(config.outputPath, "index.html"), pdw::publishing::BuildHtmlFeed(feed));
	}

	bool LoadStaticHistory(const Config& startupConfig)
	{
		std::vector<pdw::publishing::PublishEvent> history;
		std::size_t rejectedLines = 0;
		std::string error;
		const bool loaded = pdw::publishing::LoadPublishHistoryJsonLines(
			JoinPath(startupConfig.outputPath, "messages.jsonl"), MAX_FEED_EVENTS,
			history, rejectedLines, error);
		if (!loaded)
		{
			SetStatus("Publishing could not restore its bounded static-feed history.");
			OutputHealthRecord(OUTPUT_HEALTH_PUBLISHING, OUTPUT_HEALTH_FAILURE,
				"Publishing static-feed history could not be restored.");
			return false;
		}
		EnterCriticalSection(&g_lock);
		g_feed.swap(history);
		g_feedOutputPath = startupConfig.outputPath;
		LeaveCriticalSection(&g_lock);
		if (rejectedLines)
		{
			SetStatus("Publishing restored its static feed but skipped malformed history lines.");
			OutputHealthRecord(OUTPUT_HEALTH_PUBLISHING, OUTPUT_HEALTH_FAILURE,
				"Publishing static-feed history contained malformed lines.");
		}
		return true;
	}

	std::string CredentialTarget(const char* name)
	{
		return std::string("PDW Publishing ") + name + ": " + szIniPathName;
	}

	bool ReadSecret(const char* name, std::string& value)
	{
		value.clear();
		PCREDENTIALA credential = NULL;
		if (!CredReadA(CredentialTarget(name).c_str(), CRED_TYPE_GENERIC, 0, &credential))
			return GetLastError() == ERROR_NOT_FOUND;
		if (credential->CredentialBlob && credential->CredentialBlobSize)
			value.assign(reinterpret_cast<const char*>(credential->CredentialBlob), credential->CredentialBlobSize);
		CredFree(credential);
		return true;
	}

	bool WriteSecret(const char* name, const char* value)
	{
		const std::string target = CredentialTarget(name);
		if (!value || !value[0])
			return CredDeleteA(target.c_str(), CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND;
		CREDENTIALA credential = {0};
		credential.Type = CRED_TYPE_GENERIC;
		credential.TargetName = const_cast<char*>(target.c_str());
		credential.CredentialBlobSize = static_cast<DWORD>(strlen(value));
		credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(value));
		credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
		credential.UserName = const_cast<char*>("PDW");
		return CredWriteA(&credential, 0) != FALSE;
	}

	std::string HmacSha256(const std::string& secret, const std::string& payload)
	{
		unsigned char digest[EVP_MAX_MD_SIZE];
		unsigned int digestLength = 0;
		HMAC(EVP_sha256(), secret.data(), static_cast<int>(secret.size()),
			reinterpret_cast<const unsigned char*>(payload.data()), payload.size(), digest, &digestLength);
		static const char hex[] = "0123456789abcdef";
		std::string output(digestLength * 2, '0');
		for (unsigned int index = 0; index < digestLength; ++index)
		{
			output[index * 2] = hex[digest[index] >> 4];
			output[index * 2 + 1] = hex[digest[index] & 15];
		}
		SecureZeroMemory(digest, sizeof(digest));
		return output;
	}

	std::size_t DiscardResponse(char* data, std::size_t size, std::size_t count, void*)
	{
		(void)data;
		return size * count;
	}

	bool DeliverWebhook(const Config& config, const PublishTask& task, std::string& error)
	{
		if (config.webhookUrl.compare(0, 8, "https://") != 0)
		{
			error = "Webhook delivery refused because the URL is not HTTPS.";
			return false;
		}
		if (!g_curlInitialized)
		{
			error = "Webhook delivery is unavailable because HTTPS support did not initialize.";
			return false;
		}
		std::string bearer = task.test ? task.testBearer : std::string();
		std::string hmac = task.test ? task.testHmac : std::string();
		if (!task.test)
		{
			if (!ReadSecret("Bearer", bearer) || !ReadSecret("HMAC", hmac))
			{
				error = "Windows Credential Manager could not read the publishing secret.";
				WipeString(bearer);
				WipeString(hmac);
				return false;
			}
		}
		CURL* curl = curl_easy_init();
		if (!curl)
		{
			error = "Unable to create HTTPS publishing request.";
			WipeString(bearer);
			WipeString(hmac);
			return false;
		}
		struct curl_slist* headers = NULL;
		headers = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");
		headers = curl_slist_append(headers, "Expect:");
		const std::string idempotency = "Idempotency-Key: " + task.event.id;
		headers = curl_slist_append(headers, idempotency.c_str());
		std::string authorization;
		if (!bearer.empty())
		{
			authorization = "Authorization: Bearer " + bearer;
			headers = curl_slist_append(headers, authorization.c_str());
		}
		std::string signature;
		if (!hmac.empty())
		{
			signature = "X-PDW-Signature: sha256=" + HmacSha256(hmac, task.payload);
			headers = curl_slist_append(headers, signature.c_str());
		}
		curl_easy_setopt(curl, CURLOPT_URL, config.webhookUrl.c_str());
		curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, task.payload.data());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(task.payload.size()));
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
		const std::string userAgent = std::string("PDW/") + (pdw_version ? pdw_version : "unknown") + " publishing";
		curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, DiscardResponse);
		char curlError[CURL_ERROR_SIZE] = {};
		curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curlError);
		const CURLcode result = curl_easy_perform(curl);
		long status = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
		curl_slist_free_all(headers);
		curl_easy_cleanup(curl);
		WipeString(bearer);
		WipeString(hmac);
		WipeString(authorization);
		WipeString(signature);
		if (result == CURLE_OK && status >= 200 && status < 300) return true;
		char message[256];
		snprintf(message, sizeof(message), "Webhook failed: %s (HTTP %ld).",
			curlError[0] ? curlError : curl_easy_strerror(result), status);
		error = message;
		return false;
	}

	bool PersistTask(PublishTask& task, std::string& error)
	{
		error.clear();
		if (task.test) return true;
		EnsureFolder(QueueFolder());
		if (task.spoolPath.empty())
			task.spoolPath = JoinPath(QueueFolder(), pdw::publishing::PublishJobFileName(task.event.id));
		pdw::publishing::PublishJobRecord record;
		record.event = task.event;
		record.payload = task.payload;
		record.staticOutputPath = task.staticOutputPath;
		record.targets = task.targets;
		record.completed = task.completed;
		record.failed = task.failed;
		record.staticAttempts = task.staticAttempts;
		record.webhookAttempts = task.webhookAttempts;
		return pdw::publishing::SavePublishJobFileAtomic(task.spoolPath, record, error);
	}

	void RemovePendingId(const PublishTask& task)
	{
		EnterCriticalSection(&g_lock);
		g_pendingIds.Remove(task.event.id);
		LeaveCriticalSection(&g_lock);
	}

	void RequeueTask(PublishTask& task, bool front)
	{
		EnterCriticalSection(&g_lock);
		if (front) g_queue.push_front(task);
		else g_queue.push_back(task);
		LeaveCriticalSection(&g_lock);
		WipeTaskSecrets(task);
		if (g_workEvent) SetEvent(g_workEvent);
	}

	bool DeleteSpool(PublishTask& task)
	{
		if (task.test || task.spoolPath.empty()) return true;
		std::string error;
		const bool deleted = pdw::publishing::DeletePublishJobFile(task.spoolPath, error);
		if (deleted) task.spoolPath.clear();
		else
		{
			SetStatus("Publishing completed, but its durable queue file could not be removed.");
			OutputHealthRecord(OUTPUT_HEALTH_PUBLISHING, OUTPUT_HEALTH_FAILURE,
				"Completed publishing queue file could not be removed.");
		}
		return deleted;
	}

	bool DeadLetter(PublishTask& task)
	{
		if (task.test || task.spoolPath.empty()) return false;
		EnsureFolder(DeadLetterFolder());
		std::string error;
		const std::string destination = JoinPath(DeadLetterFolder(),
			pdw::publishing::PublishJobBaseName(task.spoolPath));
		const bool moved = pdw::publishing::MovePublishJobFile(task.spoolPath, destination, error);
		if (moved) task.spoolPath.clear();
		return moved;
	}

	void FinishTask(PublishTask& task)
	{
		RemovePendingId(task);
		WipeTaskSecrets(task);
	}

	unsigned int __stdcall Worker(void*)
	{
		while (WaitForSingleObject(g_stopEvent, 0) != WAIT_OBJECT_0)
		{
			PublishTask task;
			bool available = false;
			EnterCriticalSection(&g_lock);
			if (!g_queue.empty())
			{
				task = g_queue.front();
				WipeTaskSecrets(g_queue.front());
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

			Config config = SnapshotConfig();
			if (task.test)
			{
				config.webhookEnabled = true;
				config.webhookUrl = task.testUrl;
				if (InterlockedCompareExchange(&g_stopping, 0, 0))
				{
					FinishTask(task);
					break;
				}
				if (!g_curlInitialized)
				{
					SetStatus("Webhook test could not run because HTTPS support is unavailable.");
					OutputHealthRecord(OUTPUT_HEALTH_PUBLISHING, OUTPUT_HEALTH_FAILURE,
						"Publishing test could not run because HTTPS support is unavailable.");
					FinishTask(task);
					continue;
				}
				std::string deliveryError;
				if (DeliverWebhook(config, task, deliveryError))
				{
					SetStatus("Webhook test delivered successfully.");
					OutputHealthRecord(OUTPUT_HEALTH_PUBLISHING, OUTPUT_HEALTH_SUCCESS,
						"Publishing test delivered successfully.");
					FinishTask(task);
					if (config.minimumInterval && WaitForSingleObject(g_stopEvent,
						config.minimumInterval * 1000u) == WAIT_OBJECT_0) break;
					continue;
				}

				++task.attempts;
				if (pdw::publishing::PublishJobAttemptLimitReached(task.attempts))
				{
					SetStatus("Webhook test failed after five attempts.");
					OutputHealthRecord(OUTPUT_HEALTH_PUBLISHING, OUTPUT_HEALTH_FAILURE,
						"Publishing test failed after five attempts.");
					FinishTask(task);
					continue;
				}
				SetStatus(deliveryError.empty() ? "Webhook test failed; retry is queued." : deliveryError);
				const DWORD delay = static_cast<DWORD>(1000u << (task.attempts - 1));
				if (WaitForSingleObject(g_stopEvent, delay) == WAIT_OBJECT_0)
				{
					FinishTask(task);
					break;
				}
				RequeueTask(task, false);
				continue;
			}

			const pdw::publishing::PublishWorkDecision work =
				pdw::publishing::DecidePublishWork(JobState(task), RuntimeState(config));
			if (work.action == pdw::publishing::PublishWorkAction::RETAIN_FOR_RELOAD)
			{
				WipeTaskSecrets(task);
				break;
			}
			if (work.action == pdw::publishing::PublishWorkAction::CLEANUP)
			{
				const bool deleted = DeleteSpool(task);
				const pdw::publishing::PublishTerminalDecision terminal =
					pdw::publishing::FinishPublishTerminal(deleted);
				if (terminal.action == pdw::publishing::PublishTerminalAction::FINISH)
				{
					SetStatus("Publishing delivery completed.");
					OutputHealthRecord(OUTPUT_HEALTH_PUBLISHING, OUTPUT_HEALTH_SUCCESS,
						"Publishing delivery completed.");
					if (terminal.releasePendingId) FinishTask(task);
				}
				else
				{
					RequeueTask(task, false);
					WaitForSingleObject(g_stopEvent, FILE_OPERATION_RETRY_DELAY_MS);
				}
				continue;
			}
			if (work.action == pdw::publishing::PublishWorkAction::DEAD_LETTER)
			{
				const bool moved = DeadLetter(task);
				const pdw::publishing::PublishTerminalDecision terminal =
					pdw::publishing::FinishPublishTerminal(moved);
				SetStatus(moved ? "Publishing had already reached five attempts; the event was moved to DeadLetter." :
					"Publishing reached five attempts, but its queue file could not be moved to DeadLetter.");
				OutputHealthRecord(OUTPUT_HEALTH_PUBLISHING, OUTPUT_HEALTH_FAILURE,
					moved ? "Publishing attempt limit survived restart; event moved to DeadLetter." :
					"Publishing attempt limit survived restart; DeadLetter move failed.");
				if (terminal.action == pdw::publishing::PublishTerminalAction::FINISH)
				{
					if (terminal.releasePendingId) FinishTask(task);
				}
				else
				{
					RequeueTask(task, false);
					WaitForSingleObject(g_stopEvent, FILE_OPERATION_RETRY_DELAY_MS);
				}
				continue;
			}
			if (work.action == pdw::publishing::PublishWorkAction::HOLD)
			{
				if (config.enabled && config.acknowledged && !config.paused)
					SetStatus("Publishing has durable queued work waiting for its selected destination to be enabled.");
				RequeueTask(task, false);
				WaitForSingleObject(g_stopEvent, 1000);
				continue;
			}

			std::string persistenceError;
			if (!PersistTask(task, persistenceError))
			{
				SetStatus("Publishing delivery is waiting because its durable queue file could not be saved.");
				OutputHealthRecord(OUTPUT_HEALTH_PUBLISHING, OUTPUT_HEALTH_FAILURE,
					"Publishing durable queue file could not be saved.");
				RequeueTask(task, true);
				WaitForSingleObject(g_stopEvent, 1000);
				continue;
			}

			pdw::publishing::PublishPassState pass =
				pdw::publishing::BeginPublishPass(JobState(task));
			bool durableState = true;
			bool stoppingDuringPass = false;
			std::string deliveryError;
			if (work.deliveryTargets & pdw::publishing::PUBLISH_JOB_TARGET_STATIC)
			{
				if (InterlockedCompareExchange(&g_stopping, 0, 0)) stoppingDuringPass = true;
				else
				{
					Config staticConfig(config);
					if (!task.staticOutputPath.empty())
						staticConfig.outputPath = task.staticOutputPath;
					const pdw::publishing::PublishTargetTransition target =
						pdw::publishing::RecordPublishTarget(pass,
							pdw::publishing::PUBLISH_JOB_TARGET_STATIC,
							PublishStatic(staticConfig, task.event));
					pass = target.pass;
					ApplyJobState(task, pass.job);
					if (target.persistCompletedState)
						durableState = PersistTask(task, persistenceError);
				}
			}
			if (durableState && !stoppingDuringPass &&
				(work.deliveryTargets & pdw::publishing::PUBLISH_JOB_TARGET_WEBHOOK))
			{
				if (InterlockedCompareExchange(&g_stopping, 0, 0)) stoppingDuringPass = true;
				else
				{
					const bool delivered = DeliverWebhook(config, task, deliveryError);
					const pdw::publishing::PublishTargetTransition target =
						pdw::publishing::RecordPublishTarget(pass,
							pdw::publishing::PUBLISH_JOB_TARGET_WEBHOOK, delivered);
					pass = target.pass;
					ApplyJobState(task, pass.job);
					if (target.persistCompletedState)
						durableState = PersistTask(task, persistenceError);
				}
			}

			if (!durableState)
			{
				SetStatus("Publishing delivered one target, but could not save its updated durable state.");
				OutputHealthRecord(OUTPUT_HEALTH_PUBLISHING, OUTPUT_HEALTH_FAILURE,
					"Publishing target state could not be saved after delivery.");
				RequeueTask(task, true);
				WaitForSingleObject(g_stopEvent, 1000);
				continue;
			}

			const pdw::publishing::PublishFinishPassDecision finish =
				pdw::publishing::FinishPublishPass(pass);
			ApplyJobState(task, finish.job);
			if (finish.persistAttemptState && !PersistTask(task, persistenceError))
			{
				SetStatus("Publishing failed and could not save its updated retry state.");
				OutputHealthRecord(OUTPUT_HEALTH_PUBLISHING, OUTPUT_HEALTH_FAILURE,
					"Publishing retry state could not be saved.");
				RequeueTask(task, true);
				WaitForSingleObject(g_stopEvent, 1000);
				continue;
			}
			if (stoppingDuringPass || InterlockedCompareExchange(&g_stopping, 0, 0))
			{
				WipeTaskSecrets(task);
				break;
			}

			if (finish.action == pdw::publishing::PublishFinishPassAction::CLEANUP)
			{
				const bool deleted = DeleteSpool(task);
				const pdw::publishing::PublishTerminalDecision terminal =
					pdw::publishing::FinishPublishTerminal(deleted);
				if (terminal.action == pdw::publishing::PublishTerminalAction::FINISH)
				{
					SetStatus("Publishing delivery completed.");
					OutputHealthRecord(OUTPUT_HEALTH_PUBLISHING, OUTPUT_HEALTH_SUCCESS,
						"Publishing delivery completed.");
					if (terminal.releasePendingId) FinishTask(task);
				}
				else
				{
					RequeueTask(task, false);
					WaitForSingleObject(g_stopEvent, FILE_OPERATION_RETRY_DELAY_MS);
					continue;
				}
			}
			else if (finish.action == pdw::publishing::PublishFinishPassAction::DEAD_LETTER)
			{
				const bool moved = DeadLetter(task);
				const pdw::publishing::PublishTerminalDecision terminal =
					pdw::publishing::FinishPublishTerminal(moved);
				SetStatus(moved ? "Publishing failed after five attempts; the event was moved to DeadLetter." :
					"Publishing failed after five attempts; the DeadLetter move failed.");
				OutputHealthRecord(OUTPUT_HEALTH_PUBLISHING, OUTPUT_HEALTH_FAILURE,
					moved ? "Publishing failed after five attempts; event moved to DeadLetter." :
					"Publishing failed after five attempts; DeadLetter move failed.");
				if (terminal.action == pdw::publishing::PublishTerminalAction::FINISH)
				{
					if (terminal.releasePendingId) FinishTask(task);
				}
				else
				{
					RequeueTask(task, false);
					WaitForSingleObject(g_stopEvent, FILE_OPERATION_RETRY_DELAY_MS);
					continue;
				}
			}
			else if (finish.action == pdw::publishing::PublishFinishPassAction::RETRY)
			{
				SetStatus(deliveryError.empty() ? "Static publishing failed; retry is queued." : deliveryError);
				if (WaitForSingleObject(g_stopEvent, finish.retryDelayMilliseconds) == WAIT_OBJECT_0)
				{
					WipeTaskSecrets(task);
					break;
				}
				RequeueTask(task, false);
			}
			else
			{
				SetStatus(pass.attemptedTargets ? "Publishing is waiting for its remaining destination." :
					"Publishing has durable queued work waiting for its selected destination to be enabled.");
				if (pass.attemptedTargets && config.minimumInterval && WaitForSingleObject(g_stopEvent,
					config.minimumInterval * 1000u) == WAIT_OBJECT_0)
				{
					WipeTaskSecrets(task);
					break;
				}
				RequeueTask(task, false);
				WaitForSingleObject(g_stopEvent, 1000);
				continue;
			}
			if (pass.attemptedTargets && config.minimumInterval && WaitForSingleObject(g_stopEvent,
				config.minimumInterval * 1000u) == WAIT_OBJECT_0) break;
		}
		return 0;
	}

	bool QueueTask(PublishTask& task)
	{
		if (!g_thread || !g_workEvent)
		{
			WipeTaskSecrets(task);
			SetStatus("Publishing worker is unavailable; the message remains in PDW's legacy outputs.");
			OutputHealthRecord(OUTPUT_HEALTH_PUBLISHING, OUTPUT_HEALTH_DROPPED,
				"Publishing worker is unavailable; legacy outputs were unaffected.");
			return false;
		}
		if (!pdw::publishing::IsSafePublishJobId(task.event.id))
		{
			WipeTaskSecrets(task);
			SetStatus("Publishing rejected an event with an unsafe identifier.");
			OutputHealthRecord(OUTPUT_HEALTH_PUBLISHING, OUTPUT_HEALTH_DROPPED,
				"Publishing event identifier was unsafe.");
			return false;
		}
		bool reserved = false;
		bool duplicate = false;
		EnterCriticalSection(&g_lock);
		if (g_pendingIds.Size() < MAX_QUEUE)
		{
			if (g_pendingIds.Add(task.event.id)) reserved = true;
			else duplicate = true;
		}
		LeaveCriticalSection(&g_lock);
		if (!reserved)
		{
			WipeTaskSecrets(task);
			if (duplicate)
			{
				SetStatus("Publishing suppressed a duplicate event identifier that is already pending.");
				return false;
			}
			SetStatus("Publishing queue is full; new events are being retained only in PDW's legacy outputs.");
			OutputHealthRecord(OUTPUT_HEALTH_PUBLISHING, OUTPUT_HEALTH_DROPPED,
				"Publishing queue is full; optional delivery was not queued.");
			return false;
		}

		// Reserve the identifier before touching disk, then make every normal job
		// durable before it becomes visible to the worker. This closes the restart
		// window between decoder intake and the worker's first persistence pass.
		if (!task.test)
		{
			std::string persistenceError;
			if (!PersistTask(task, persistenceError))
			{
				EnterCriticalSection(&g_lock);
				g_pendingIds.Remove(task.event.id);
				LeaveCriticalSection(&g_lock);
				WipeTaskSecrets(task);
				SetStatus("Publishing could not save the event to its durable queue; legacy outputs were unaffected.");
				OutputHealthRecord(OUTPUT_HEALTH_PUBLISHING, OUTPUT_HEALTH_FAILURE,
					"Publishing durable queue write failed before enqueue.");
				return false;
			}
		}

		bool queued = false;
		EnterCriticalSection(&g_lock);
		const pdw::publishing::PublishEnqueueAction enqueueAction =
			pdw::publishing::DecidePublishEnqueue(
				InterlockedCompareExchange(&g_stopping, 0, 0) != 0);
		if (enqueueAction == pdw::publishing::PublishEnqueueAction::ACTIVE)
		{
			g_queue.push_back(task);
			queued = true;
		}
		else if (task.test) g_pendingIds.Remove(task.event.id);
		LeaveCriticalSection(&g_lock);
		WipeTaskSecrets(task);
		if (queued)
		{
			SetEvent(g_workEvent);
			return true;
		}

		// A normal task is already durable and will reload on the next start. Test
		// jobs intentionally have no durable representation and are discarded.
		if (!task.test)
		{
			SetStatus("Publishing saved the event for recovery while the worker was stopping.");
			return true;
		}
		SetStatus("Publishing stopped before the webhook test could be queued.");
		return false;
	}

	bool QuarantinePersistentFile(const std::string& path, const char* suffix)
	{
		EnsureFolder(DeadLetterFolder());
		std::string error;
		const std::string destination = JoinPath(DeadLetterFolder(),
			pdw::publishing::PublishJobBaseName(path) + suffix);
		const bool moved = pdw::publishing::MovePublishJobFile(path, destination, error);
		if (!moved)
			OutputHealthRecord(OUTPUT_HEALTH_PUBLISHING, OUTPUT_HEALTH_FAILURE,
				"Publishing queue recovery could not quarantine a file.");
		return moved;
	}

	void RecoverPersistentTempPattern(const char* pattern, bool allowLegacyPayload,
		unsigned int& quarantined, unsigned int& quarantineFailures)
	{
		WIN32_FIND_DATAA found;
		HANDLE search = FindFirstFileA(JoinPath(QueueFolder(), pattern).c_str(), &found);
		if (search == INVALID_HANDLE_VALUE) return;
		do
		{
			if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
			const std::string path = JoinPath(QueueFolder(), found.cFileName);
			pdw::publishing::PublishJobRecord record;
			std::string error;
			if (!pdw::publishing::LoadPublishJobFile(path, allowLegacyPayload, record, error))
			{
				if (QuarantinePersistentFile(path, ".invalid")) ++quarantined;
				else ++quarantineFailures;
				continue;
			}

			// Strip only the recovery suffix. A valid monotonic candidate may replace
			// an older final record; equal, stale, or conflicting candidates remain
			// available for inspection in DeadLetter.
			const std::string suffix = ".tmp";
			if (path.size() <= suffix.size())
			{
				if (QuarantinePersistentFile(path, ".invalid")) ++quarantined;
				else ++quarantineFailures;
				continue;
			}
			const std::string finalPath = path.substr(0, path.size() - suffix.size());
			if (GetFileAttributesA(finalPath.c_str()) != INVALID_FILE_ATTRIBUTES)
			{
				pdw::publishing::PublishJobRecord finalRecord;
				std::string finalError;
				if (!pdw::publishing::LoadPublishJobFile(finalPath, allowLegacyPayload,
					finalRecord, finalError))
				{
					if (QuarantinePersistentFile(finalPath, ".invalid-final")) ++quarantined;
					else
					{
						++quarantineFailures;
						continue;
					}
				}
			}
			if (!pdw::publishing::PromotePublishJobTempFile(path, finalPath, error))
			{
				if (QuarantinePersistentFile(path, ".stale-or-conflicting")) ++quarantined;
				else ++quarantineFailures;
			}
		} while (FindNextFileA(search, &found));
		FindClose(search);
	}

	void LoadPersistentPattern(const char* pattern, bool allowLegacyPayload,
		const Config& startupConfig, unsigned int& rejected,
		unsigned int& quarantineFailures)
	{
		WIN32_FIND_DATAA found;
		HANDLE search = FindFirstFileA(JoinPath(QueueFolder(), pattern).c_str(), &found);
		if (search == INVALID_HANDLE_VALUE) return;
		do
		{
			if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
			const std::string path = JoinPath(QueueFolder(), found.cFileName);
			pdw::publishing::PublishJobRecord record;
			std::string error;
			if (!pdw::publishing::LoadPublishJobFile(path, allowLegacyPayload, record, error))
			{
				if (QuarantinePersistentFile(path, ".invalid")) ++rejected;
				else ++quarantineFailures;
				continue;
			}
			if (record.legacyPayloadOnly)
			{
				// The v4.5 payload-only loader treated static output as already done
				// and retried only the currently enabled webhook. Preserve that
				// compatibility behavior while upgrading the file on first use.
				record.targets = startupConfig.webhookEnabled ?
					pdw::publishing::PUBLISH_JOB_TARGET_WEBHOOK : 0;
				record.completed = 0;
				record.failed = 0;
				record.staticAttempts = 0;
				record.webhookAttempts = 0;
			}
			if ((record.targets & pdw::publishing::PUBLISH_JOB_TARGET_STATIC) &&
				record.staticOutputPath.empty())
				record.staticOutputPath = startupConfig.outputPath;
			bool accepted = false;
			bool duplicate = false;
			EnterCriticalSection(&g_lock);
			if (g_pendingIds.Size() < MAX_QUEUE)
			{
				if (g_pendingIds.Add(record.event.id)) accepted = true;
				else duplicate = true;
			}
			LeaveCriticalSection(&g_lock);
			if (!accepted)
			{
				if (QuarantinePersistentFile(path, duplicate ? ".duplicate" : ".overflow"))
					++rejected;
				else ++quarantineFailures;
				continue;
			}
			PublishTask task;
			task.event = record.event;
			task.payload = record.payload;
			task.spoolPath = path;
			task.staticOutputPath = record.staticOutputPath;
			task.targets = record.targets;
			task.completed = record.completed;
			task.failed = record.failed;
			task.staticAttempts = record.staticAttempts;
			task.webhookAttempts = record.webhookAttempts;
			EnterCriticalSection(&g_lock);
			g_queue.push_back(task);
			LeaveCriticalSection(&g_lock);
		} while (FindNextFileA(search, &found));
		FindClose(search);
	}

	void LoadPersistentQueue()
	{
		EnsureFolder(QueueFolder());
		EnsureFolder(DeadLetterFolder());
		const Config startupConfig = SnapshotConfig();
		unsigned int rejected = 0;
		unsigned int quarantineFailures = 0;
		RecoverPersistentTempPattern("*.pdwjob.tmp", false, rejected, quarantineFailures);
		RecoverPersistentTempPattern("*.json.tmp", true, rejected, quarantineFailures);
		LoadPersistentPattern("*.pdwjob", false, startupConfig, rejected, quarantineFailures);
		LoadPersistentPattern("*.json", true, startupConfig, rejected, quarantineFailures);
		if (quarantineFailures)
		{
			SetStatus("Publishing queue recovery found files that could not be quarantined.");
			OutputHealthRecord(OUTPUT_HEALTH_PUBLISHING, OUTPUT_HEALTH_FAILURE,
				"Publishing queue recovery could not quarantine one or more files.");
		}
		else if (rejected)
		{
			SetStatus("One or more invalid, duplicate, or excess publishing queue files were moved to DeadLetter.");
			OutputHealthRecord(OUTPUT_HEALTH_PUBLISHING, OUTPUT_HEALTH_FAILURE,
				"Publishing queue recovery quarantined one or more files.");
		}
	}

	void EnableDialogControls(HWND dialog)
	{
		const BOOL enabled = IsDlgButtonChecked(dialog, IDC_PUBLISH_ENABLE) == BST_CHECKED;
		const BOOL staticEnabled = IsDlgButtonChecked(dialog, IDC_PUBLISH_STATIC) == BST_CHECKED;
		const BOOL webhookEnabled = IsDlgButtonChecked(dialog, IDC_PUBLISH_WEBHOOK) == BST_CHECKED;
		EnableWindow(GetDlgItem(dialog, IDC_PUBLISH_ACK), enabled);
		EnableWindow(GetDlgItem(dialog, IDC_PUBLISH_STATIC), enabled);
		EnableWindow(GetDlgItem(dialog, IDC_PUBLISH_WEBHOOK), enabled);
		EnableWindow(GetDlgItem(dialog, IDC_PUBLISH_PATH), enabled && staticEnabled);
		EnableWindow(GetDlgItem(dialog, IDC_PUBLISH_BROWSE), enabled && staticEnabled);
		EnableWindow(GetDlgItem(dialog, IDC_PUBLISH_URL), enabled && webhookEnabled);
		EnableWindow(GetDlgItem(dialog, IDC_PUBLISH_BEARER), enabled && webhookEnabled);
		EnableWindow(GetDlgItem(dialog, IDC_PUBLISH_HMAC), enabled && webhookEnabled);
		EnableWindow(GetDlgItem(dialog, IDC_PUBLISH_TEST), enabled && webhookEnabled);
	}

	int CALLBACK BrowseFolderCallback(HWND window, UINT message, LPARAM, LPARAM data)
	{
		if (message == BFFM_INITIALIZED && data) SendMessageA(window, BFFM_SETSELECTIONA, TRUE, data);
		return 0;
	}

	bool BrowseFolder(HWND owner, char* path, std::size_t pathSize)
	{
		BROWSEINFOA browse = {0};
		browse.hwndOwner = owner;
		browse.lpszTitle = "Choose the folder where PDW will maintain JSON, RSS, Atom, HTML, and JSONL files.";
		browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
		browse.lpfn = BrowseFolderCallback;
		browse.lParam = reinterpret_cast<LPARAM>(path);
		PIDLIST_ABSOLUTE item = SHBrowseForFolderA(&browse);
		if (!item) return false;
		char selected[MAX_PATH];
		const bool ok = SHGetPathFromIDListA(item, selected) != FALSE;
		CoTaskMemFree(item);
		if (ok) CopyText(path, pathSize, selected);
		return ok;
	}
}

void PublishingManagerInitialize(void)
{
	if (g_initialized) return;
	InitializeCriticalSection(&g_lock);
	g_initialized = true;
	g_pendingIds.Clear();
	g_feed.clear();
	g_feedOutputPath.clear();
	const Config startupConfig = ProfileConfig();
	OutputHealthSetEnabled(OUTPUT_HEALTH_PUBLISHING,
		startupConfig.enabled && startupConfig.acknowledged && !startupConfig.paused &&
		(startupConfig.staticEnabled || startupConfig.webhookEnabled));
	InterlockedExchange(&g_stopping, 0);
	g_workEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	g_stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!g_workEvent || !g_stopEvent)
	{
		SetStatus("Publishing could not create its worker events; legacy outputs remain available.");
		OutputHealthRecord(OUTPUT_HEALTH_PUBLISHING, OUTPUT_HEALTH_FAILURE,
			"Publishing worker events could not be created.");
		return;
	}
	g_curlInitialized = CurlRuntimeAcquire();
	PublishingSettingsChanged();
	if (!g_curlInitialized)
	{
		SetStatus(startupConfig.webhookEnabled ?
			"Publishing HTTPS support is unavailable; static publishing remains available." :
			"Publishing HTTPS support is unavailable; the static publishing worker remains available.");
		OutputHealthRecord(OUTPUT_HEALTH_PUBLISHING, OUTPUT_HEALTH_FAILURE,
			"Publishing HTTPS support could not initialize; static publishing remains available.");
	}
	LoadStaticHistory(startupConfig);
	LoadPersistentQueue();
	const uintptr_t thread = _beginthreadex(NULL, 0, Worker, NULL, 0, NULL);
	g_thread = reinterpret_cast<HANDLE>(thread);
	if (!g_thread)
	{
		SetStatus("Publishing could not start its worker; legacy outputs remain available.");
		OutputHealthRecord(OUTPUT_HEALTH_PUBLISHING, OUTPUT_HEALTH_FAILURE,
			"Publishing worker could not start.");
		return;
	}
	if (!g_queue.empty()) SetEvent(g_workEvent);
}

void PublishingManagerShutdown(void)
{
	if (!g_initialized) return;
	InterlockedExchange(&g_stopping, 1);
	if (g_stopEvent) SetEvent(g_stopEvent);
	if (g_thread && WaitForSingleObject(g_thread, 20000) == WAIT_OBJECT_0)
	{
		CloseHandle(g_thread);
		g_thread = NULL;
	}
	if (g_thread) return;
	std::deque<PublishTask> remaining;
	EnterCriticalSection(&g_lock);
	remaining.swap(g_queue);
	LeaveCriticalSection(&g_lock);
	for (std::deque<PublishTask>::iterator task = remaining.begin(); task != remaining.end(); ++task)
	{
		std::string error;
		if (!task->test && !PersistTask(*task, error))
			OutputHealthRecord(OUTPUT_HEALTH_PUBLISHING, OUTPUT_HEALTH_FAILURE,
				"Publishing shutdown could not save one queued event.");
		WipeTaskSecrets(*task);
	}
	remaining.clear();
	EnterCriticalSection(&g_lock);
	g_pendingIds.Clear();
	LeaveCriticalSection(&g_lock);
	if (g_workEvent) CloseHandle(g_workEvent);
	if (g_stopEvent) CloseHandle(g_stopEvent);
	g_workEvent = g_stopEvent = NULL;
	if (g_curlInitialized) CurlRuntimeRelease();
	g_curlInitialized = false;
	g_initialized = false;
	DeleteCriticalSection(&g_lock);
}

void PublishingSettingsChanged(void)
{
	if (!g_initialized) return;
	const Config config = ProfileConfig();
	EnterCriticalSection(&g_lock);
	g_config = config;
	LeaveCriticalSection(&g_lock);
	OutputHealthSetEnabled(OUTPUT_HEALTH_PUBLISHING,
		config.enabled && config.acknowledged && !config.paused &&
		(config.staticEnabled || config.webhookEnabled));
	if (!config.enabled) SetStatus("Publishing is disabled.");
	else if (!config.acknowledged) SetStatus("Publishing cannot start until the jurisdiction and permission acknowledgement is accepted.");
	else if (!config.staticEnabled && !config.webhookEnabled) SetStatus("Choose static files, an HTTPS webhook, or both.");
	else if (config.webhookEnabled && config.webhookUrl.compare(0, 8, "https://") != 0) SetStatus("Publishing webhook must use HTTPS.");
	else if (config.webhookEnabled && !g_curlInitialized) SetStatus("Publishing HTTPS support is unavailable; static publishing remains available.");
	else if (config.paused) SetStatus("Publishing is enabled but paused; queued events are retained.");
	else SetStatus("Publishing is enabled and ready.");
	if (g_workEvent) SetEvent(g_workEvent);
}

void PublishingGetStatusText(char* buffer, size_t bufferSize)
{
	if (!buffer || !bufferSize) return;
	if (!g_initialized) { CopyText(buffer, bufferSize, "Publishing has not been initialized."); return; }
	EnterCriticalSection(&g_lock); CopyText(buffer, bufferSize, g_status); LeaveCriticalSection(&g_lock);
}

void PublishingPublishDecodedMessage(const DecodedMessageNotificationContext& context)
{
	PublishingPublishEvent(pdw::events::BuildDecodedEvent(context));
}

void PublishingPublishEvent(const pdw::publishing::PublishEvent& source)
{
	if (!g_initialized) return;
	const Config config = SnapshotConfig();
	pdw::publishing::PublishRuntimeState runtime = RuntimeState(config);
	// Intake freezes selected destinations even when a transport is temporarily
	// unavailable. Worker availability only controls when a durable target runs.
	runtime.enabledTargets = EnabledTargetMask(config);
	if (!pdw::publishing::PublishShouldIntake(runtime, source.filtered)) return;
	pdw::publishing::TransformOptions options;
	options.sourceAlias = pdw::events::PdwTextToUtf8(config.sourceAlias.c_str());
	options.maskAddress = config.maskAddress;
	options.includeMessage = config.includeMessage;
	PublishTask task;
	task.event = pdw::publishing::ApplyTransform(source, options);
	task.payload = pdw::publishing::BuildJsonObject(task.event);
	task.targets = runtime.enabledTargets;
	if (task.targets & pdw::publishing::PUBLISH_JOB_TARGET_STATIC)
		task.staticOutputPath = config.outputPath;
	QueueTask(task);
}

BOOL FAR PASCAL PublishingDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM)
{
	switch (uMsg)
	{
		case WM_INITDIALOG:
		{
			CenterWindow(hDlg);
			CheckDlgButton(hDlg, IDC_PUBLISH_ENABLE, Profile.publishingEnabled ? BST_CHECKED : BST_UNCHECKED);
			CheckDlgButton(hDlg, IDC_PUBLISH_ACK, Profile.publishingPermissionAcknowledged ? BST_CHECKED : BST_UNCHECKED);
			CheckRadioButton(hDlg, IDC_PUBLISH_FILTERED, IDC_PUBLISH_ALL,
				Profile.publishingFilteredOnly ? IDC_PUBLISH_FILTERED : IDC_PUBLISH_ALL);
			CheckDlgButton(hDlg, IDC_PUBLISH_STATIC, Profile.publishingStaticEnabled ? BST_CHECKED : BST_UNCHECKED);
			CheckDlgButton(hDlg, IDC_PUBLISH_WEBHOOK, Profile.publishingWebhookEnabled ? BST_CHECKED : BST_UNCHECKED);
			CheckDlgButton(hDlg, IDC_PUBLISH_MASK, Profile.publishingMaskAddress ? BST_CHECKED : BST_UNCHECKED);
			CheckDlgButton(hDlg, IDC_PUBLISH_MESSAGE, Profile.publishingIncludeMessage ? BST_CHECKED : BST_UNCHECKED);
			CheckDlgButton(hDlg, IDC_PUBLISH_PAUSED, Profile.publishingPaused ? BST_CHECKED : BST_UNCHECKED);
			SetDlgItemText(hDlg, IDC_PUBLISH_PATH, Profile.publishingOutputPath);
			SetDlgItemText(hDlg, IDC_PUBLISH_URL, Profile.publishingWebhookUrl);
			SetDlgItemText(hDlg, IDC_PUBLISH_ALIAS, Profile.publishingSourceAlias);
			SetDlgItemInt(hDlg, IDC_PUBLISH_INTERVAL, Profile.publishingMinimumIntervalSeconds, FALSE);
			SetDlgItemText(hDlg, IDC_PUBLISH_BEARER, "");
			SetDlgItemText(hDlg, IDC_PUBLISH_HMAC, "");
			char status[512]; PublishingGetStatusText(status, sizeof(status)); SetDlgItemText(hDlg, IDC_PUBLISH_STATUS, status);
			EnableDialogControls(hDlg);
			SetTimer(hDlg, DIALOG_TIMER, 500, NULL);
			return TRUE;
		}

		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
				case IDC_PUBLISH_ENABLE:
				case IDC_PUBLISH_STATIC:
				case IDC_PUBLISH_WEBHOOK:
					EnableDialogControls(hDlg); return TRUE;
				case IDC_PUBLISH_BROWSE:
				{
					char path[PUBLISH_PATH_LEN + 1]; GetDlgItemText(hDlg, IDC_PUBLISH_PATH, path, sizeof(path));
					if (BrowseFolder(hDlg, path, sizeof(path))) SetDlgItemText(hDlg, IDC_PUBLISH_PATH, path);
					return TRUE;
				}
				case IDC_PUBLISH_OPEN:
				{
					char path[PUBLISH_PATH_LEN + 1]; GetDlgItemText(hDlg, IDC_PUBLISH_PATH, path, sizeof(path));
					if (path[0]) ShellExecuteA(hDlg, "open", ResolveOutputPath(path).c_str(), NULL, NULL, SW_SHOWNORMAL);
					return TRUE;
				}
				case IDC_PUBLISH_TEST:
				{
					char url[PUBLISH_URL_LEN + 1]; GetDlgItemText(hDlg, IDC_PUBLISH_URL, url, sizeof(url));
					if (strncmp(url, "https://", 8) != 0) { MessageBox(hDlg, "Webhook tests require an HTTPS URL.", "PDW Publishing", MB_ICONERROR); return TRUE; }
					char bearer[2049], hmac[2049]; GetDlgItemText(hDlg, IDC_PUBLISH_BEARER, bearer, sizeof(bearer)); GetDlgItemText(hDlg, IDC_PUBLISH_HMAC, hmac, sizeof(hmac));
					PublishTask task; task.test = true; task.targets = pdw::publishing::PUBLISH_JOB_TARGET_WEBHOOK; task.testUrl = url; task.testBearer = bearer; task.testHmac = hmac; task.event = pdw::events::BuildTestEvent("Publishing"); task.event.message = "PDW webhook configuration test"; task.payload = pdw::publishing::BuildJsonObject(task.event); const bool queued = QueueTask(task);
					SecureZeroMemory(bearer, sizeof(bearer)); SecureZeroMemory(hmac, sizeof(hmac));
					if (queued) SetDlgItemText(hDlg, IDC_PUBLISH_STATUS, "Webhook test queued without decoded message content.");
					else { char status[512]; PublishingGetStatusText(status, sizeof(status)); SetDlgItemText(hDlg, IDC_PUBLISH_STATUS, status); }
					return TRUE;
				}
				case IDOK:
				{
					const bool enabled = IsDlgButtonChecked(hDlg, IDC_PUBLISH_ENABLE) == BST_CHECKED;
					const bool acknowledged = IsDlgButtonChecked(hDlg, IDC_PUBLISH_ACK) == BST_CHECKED;
					if (enabled && !acknowledged) { MessageBox(hDlg, "To enable publishing, acknowledge that you are responsible for checking permissions and laws in your own country or jurisdiction.", "PDW Publishing", MB_ICONWARNING); return TRUE; }
					char path[PUBLISH_PATH_LEN + 1], url[PUBLISH_URL_LEN + 1], alias[PUBLISH_ALIAS_LEN + 1], bearer[2049], hmac[2049];
					GetDlgItemText(hDlg, IDC_PUBLISH_PATH, path, sizeof(path)); GetDlgItemText(hDlg, IDC_PUBLISH_URL, url, sizeof(url)); GetDlgItemText(hDlg, IDC_PUBLISH_ALIAS, alias, sizeof(alias)); GetDlgItemText(hDlg, IDC_PUBLISH_BEARER, bearer, sizeof(bearer)); GetDlgItemText(hDlg, IDC_PUBLISH_HMAC, hmac, sizeof(hmac));
					const bool staticEnabled = IsDlgButtonChecked(hDlg, IDC_PUBLISH_STATIC) == BST_CHECKED;
					const bool webhookEnabled = IsDlgButtonChecked(hDlg, IDC_PUBLISH_WEBHOOK) == BST_CHECKED;
					if (enabled && !staticEnabled && !webhookEnabled) { MessageBox(hDlg, "Choose static files, an HTTPS webhook, or both.", "PDW Publishing", MB_ICONWARNING); return TRUE; }
					if (enabled && staticEnabled && !path[0]) { MessageBox(hDlg, "Choose a static publishing folder.", "PDW Publishing", MB_ICONWARNING); return TRUE; }
					if (enabled && webhookEnabled && strncmp(url, "https://", 8) != 0) { MessageBox(hDlg, "The publishing webhook must use HTTPS.", "PDW Publishing", MB_ICONWARNING); return TRUE; }
					BOOL translated = FALSE; UINT interval = GetDlgItemInt(hDlg, IDC_PUBLISH_INTERVAL, &translated, FALSE); if (!translated || interval > 86400) { MessageBox(hDlg, "Minimum interval must be between 0 and 86400 seconds.", "PDW Publishing", MB_ICONWARNING); return TRUE; }
					Profile.publishingEnabled = enabled; Profile.publishingPermissionAcknowledged = acknowledged; Profile.publishingPaused = IsDlgButtonChecked(hDlg, IDC_PUBLISH_PAUSED) == BST_CHECKED; Profile.publishingFilteredOnly = IsDlgButtonChecked(hDlg, IDC_PUBLISH_FILTERED) == BST_CHECKED; Profile.publishingStaticEnabled = staticEnabled; Profile.publishingWebhookEnabled = webhookEnabled; Profile.publishingMaskAddress = IsDlgButtonChecked(hDlg, IDC_PUBLISH_MASK) == BST_CHECKED; Profile.publishingIncludeMessage = IsDlgButtonChecked(hDlg, IDC_PUBLISH_MESSAGE) == BST_CHECKED; Profile.publishingMinimumIntervalSeconds = interval;
					CopyText(Profile.publishingOutputPath, sizeof(Profile.publishingOutputPath), path); CopyText(Profile.publishingWebhookUrl, sizeof(Profile.publishingWebhookUrl), url); CopyText(Profile.publishingSourceAlias, sizeof(Profile.publishingSourceAlias), alias);
					if ((bearer[0] && !WriteSecret("Bearer", bearer)) || (hmac[0] && !WriteSecret("HMAC", hmac))) { SecureZeroMemory(bearer, sizeof(bearer)); SecureZeroMemory(hmac, sizeof(hmac)); MessageBox(hDlg, "Windows Credential Manager could not save the publishing secret.", "PDW Publishing", MB_ICONERROR); return TRUE; }
					SecureZeroMemory(bearer, sizeof(bearer)); SecureZeroMemory(hmac, sizeof(hmac)); WriteSettings(); PublishingSettingsChanged(); EndDialog(hDlg, TRUE); return TRUE;
				}
				case IDCANCEL: EndDialog(hDlg, FALSE); return TRUE;
			}
			break;

		case WM_TIMER:
		{
			char status[512]; PublishingGetStatusText(status, sizeof(status)); SetDlgItemText(hDlg, IDC_PUBLISH_STATUS, status); return TRUE;
		}
		case WM_DESTROY: KillTimer(hDlg, DIALOG_TIMER); return TRUE;
		case WM_CLOSE: EndDialog(hDlg, FALSE); return TRUE;
	}
	return FALSE;
}
