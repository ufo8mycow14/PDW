#ifndef STRICT
#define STRICT 1
#endif
#include <windows.h>
#include <winsqlite/winsqlite3.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "Headers/gateway_outbox.h"
#include "Headers/pdw.h"
#include "utils/gateway_outbox_core.h"

PROFILE Profile = {};
TCHAR szPath[MAX_PATH] = {};
void WriteSettings() {}

namespace
{
	int g_failures = 0;

	void Expect(bool condition, const char* message)
	{
		if (condition) return;
		std::cerr << "FAIL: " << message << "\n";
		++g_failures;
	}

	std::string TempPath(const char* suffix)
	{
		char directory[MAX_PATH] = {};
		GetTempPathA(MAX_PATH, directory);
		char path[MAX_PATH] = {};
		std::snprintf(path, sizeof(path), "%spdw-gateway-%lu-%lu-%s.sqlite3",
			directory, static_cast<unsigned long>(GetCurrentProcessId()),
			static_cast<unsigned long>(GetTickCount()), suffix);
		return path;
	}

	void RemoveDatabase(const std::string& path)
	{
		DeleteFileA((path + "-shm").c_str());
		DeleteFileA((path + "-wal").c_str());
		DeleteFileA(path.c_str());
	}

	pdw::gateway::GatewayEvent SyntheticEvent(long long sequence,
		const char* protocol, const char* message)
	{
		pdw::gateway::GatewayEvent event;
		event.receiverSequence = sequence;
		event.eventId = std::string("20300101T000000-synthetic-") + std::to_string(sequence);
		event.receiverId = "synthetic-receiver-01";
		event.decoderFinalizedUtc = "2030-01-01T00:00:00.000Z";
		event.timestampMethod = "synthetic_test_clock";
		event.source = "PDW synthetic test";
		event.frequencyHz = 148337500;
		event.frequencyProvenance = "synthetic_fixture";
		event.frequencyEffectiveUtc = event.decoderFinalizedUtc;
		event.protocol = protocol;
		event.protocolMetadataJson = protocol == std::string("FLEX") ?
			"{\"mode\":\"FLEX\",\"cycle\":7,\"frame\":42,\"group_bit\":3}" :
			"{\"mode\":\"POCSAG-1200\",\"cycle\":-1,\"frame\":-1,\"group_bit\":-1}";
		event.capcode = protocol == std::string("FLEX") ? "123456789" : "1234567";
		event.messageType = "ALPHA";
		event.bitrate = protocol == std::string("FLEX") ? "1600" : "1200";
		event.message = message;
		event.pdwVersion = "PDW synthetic test version";
		event.filterLabel = "Synthetic label";
		event.synthetic = true;
		event.contentHash = pdw::gateway::GatewayEventContentHash(event);
		return event;
	}

	bool Exec(sqlite3* database, const char* sql)
	{
		char* error = NULL;
		const int result = sqlite3_exec(database, sql, NULL, NULL, &error);
		if (error) sqlite3_free(error);
		return result == SQLITE_OK;
	}

	long long QueryInteger(const std::string& path, const char* sql)
	{
		sqlite3* database = NULL;
		if (sqlite3_open_v2(path.c_str(), &database, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK)
		{
			if (database) sqlite3_close_v2(database);
			return -1;
		}
		sqlite3_stmt* statement = NULL;
		long long value = -1;
		if (sqlite3_prepare_v2(database, sql, -1, &statement, NULL) == SQLITE_OK &&
			sqlite3_step(statement) == SQLITE_ROW)
			value = sqlite3_column_int64(statement, 0);
		if (statement) sqlite3_finalize(statement);
		sqlite3_close_v2(database);
		return value;
	}

	void TestIdentityHashAndValidation()
	{
		pdw::gateway::GatewayEvent event = SyntheticEvent(1, "POCSAG", "SYNTHETIC POCSAG MESSAGE");
		std::string error;
		Expect(pdw::gateway::ValidateGatewayEvent(event, error), "canonical synthetic event validates");
		const std::string firstHash = event.contentHash;
		Expect(firstHash.size() == 64, "content hash is SHA-256 hex");
		event.message += " changed";
		Expect(pdw::gateway::GatewayEventContentHash(event) != firstHash,
			"content changes alter the canonical hash");
		Expect(!pdw::gateway::ValidateGatewayEvent(event, error),
			"stale content hash is rejected");
		const std::string payload = pdw::gateway::GatewayEventCanonicalPayload(event);
		Expect(payload.find("firebase") == std::string::npos &&
			payload.find("webhook") == std::string::npos &&
			payload.find("bearer") == std::string::npos,
			"canonical event contains no cloud destination or credential fields");
	}

	void TestWalReadOnlyRestartAndStates()
	{
		const std::string path = TempPath("wal");
		RemoveDatabase(path);
		pdw::gateway::GatewayOutboxStore store;
		std::string error;
		Expect(store.Open(path, error), "new gateway outbox opens");
		pdw::gateway::GatewayEvent pocsag = SyntheticEvent(1, "POCSAG", "SYNTHETIC POCSAG MESSAGE");
		pocsag.filterMatched = true;
		pocsag.rejected = true;
		pocsag.contentHash = pdw::gateway::GatewayEventContentHash(pocsag);
		Expect(store.Append(pocsag, error), "POCSAG rejected-state event appends");
		Expect(store.RecordHighestAssignedSequence(1, error), "assigned sequence persists");
		pdw::gateway::GatewayEvent flex = SyntheticEvent(2, "FLEX", "SYNTHETIC FLEX GROUP FRAGMENT");
		flex.groupCall = true;
		flex.fragmented = true;
		flex.filterMatched = true;
		flex.filtered = true;
		flex.blockedDuplicate = true;
		flex.groupBit = 3;
		flex.cycle = 7;
		flex.frame = 42;
		flex.contentHash = pdw::gateway::GatewayEventContentHash(flex);
		Expect(store.Append(flex, error), "FLEX group fragment appends");
		Expect(store.RecordHighestAssignedSequence(3, error), "dropped sequence gap high-water persists");
		long long readerLast = 0;
		Expect(pdw::gateway::VerifyGatewayReadOnlyAccess(path, readerLast, error),
			"read-only gateway reader opens while WAL writer remains open");
		Expect(readerLast == 2, "read-only reader sees last committed sequence");

		sqlite3* attemptedWriter = NULL;
		std::string uri = "file:" + path + "?mode=ro";
		Expect(sqlite3_open_v2(uri.c_str(), &attemptedWriter,
			SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, NULL) == SQLITE_OK,
			"external reader opens mode=ro");
		Expect(!Exec(attemptedWriter, "UPDATE gateway_events SET message='not allowed';"),
			"read-only gateway reader cannot mutate events");
		sqlite3_close_v2(attemptedWriter);
		store.Close();

		Expect(store.Open(path, error), "gateway outbox recovers after restart");
		long long assigned = 0;
		Expect(store.GetHighestAssignedSequence(assigned, error) && assigned == 3,
			"restart restores highest assigned sequence including gap");
		pdw::gateway::GatewayEvent afterRestart = SyntheticEvent(4, "FLEX", "SYNTHETIC ASSEMBLED GROUP MESSAGE");
		afterRestart.groupCall = true;
		afterRestart.groupFinal = true;
		afterRestart.assembled = true;
		afterRestart.contentHash = pdw::gateway::GatewayEventContentHash(afterRestart);
		Expect(store.Append(afterRestart, error), "sequence continues after restart and gap");
		pdw::gateway::StoreStatistics statistics;
		Expect(store.GetStatistics(statistics, error) &&
			statistics.lastCommittedSequence == 4 && statistics.retainedRecords == 3,
			"statistics expose committed sequence, gap and retained count");
		store.Close();
		RemoveDatabase(path);
	}

	void TestMigrationAndOwnership()
	{
		const std::string path = TempPath("migration");
		RemoveDatabase(path);
		sqlite3* database = NULL;
		Expect(sqlite3_open_v2(path.c_str(), &database,
			SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) == SQLITE_OK,
			"legacy fixture database opens");
		const char* v1 =
			"PRAGMA application_id=1346651991;PRAGMA user_version=1;"
			"CREATE TABLE gateway_outbox_state(singleton INTEGER PRIMARY KEY,highest_assigned_sequence INTEGER NOT NULL);"
			"INSERT INTO gateway_outbox_state VALUES(1,0);"
			"CREATE TABLE gateway_events(receiver_sequence INTEGER PRIMARY KEY,event_id TEXT NOT NULL UNIQUE,"
			"event_schema_version INTEGER NOT NULL,receiver_id TEXT NOT NULL,decoder_finalized_utc TEXT NOT NULL,"
			"timestamp_method TEXT NOT NULL,source TEXT NOT NULL,frequency_hz INTEGER,frequency_provenance TEXT NOT NULL,"
			"protocol TEXT NOT NULL,protocol_metadata_json TEXT NOT NULL,capcode TEXT NOT NULL,message_type TEXT NOT NULL,"
			"bitrate TEXT NOT NULL,message TEXT NOT NULL,content_hash TEXT NOT NULL,pdw_version TEXT NOT NULL,"
			"filter_label TEXT NOT NULL,filter_matched INTEGER NOT NULL,monitor_only INTEGER NOT NULL,filtered INTEGER NOT NULL,"
			"rejected INTEGER NOT NULL,blocked_duplicate INTEGER NOT NULL,group_call INTEGER NOT NULL,group_final INTEGER NOT NULL,"
			"fragmented INTEGER NOT NULL,assembled INTEGER NOT NULL,group_bit INTEGER NOT NULL,cycle INTEGER NOT NULL,"
			"frame INTEGER NOT NULL,synthetic INTEGER NOT NULL,committed_utc TEXT NOT NULL);"
			"CREATE INDEX gateway_events_finalized_idx ON gateway_events(decoder_finalized_utc,receiver_sequence);"
			"CREATE TRIGGER gateway_events_immutable_update BEFORE UPDATE ON gateway_events "
			"BEGIN SELECT RAISE(ABORT,'gateway events are immutable'); END;";
		Expect(Exec(database, v1), "version-one gateway fixture is created");
		sqlite3_close_v2(database);
		pdw::gateway::GatewayOutboxStore store;
		std::string error;
		Expect(store.Open(path, error), "version-one schema migrates additively");
		Expect(QueryInteger(path, "PRAGMA user_version;") == 2, "migration advances schema to version two");
		store.Close();
		RemoveDatabase(path);

		const std::string foreign = TempPath("foreign");
		RemoveDatabase(foreign);
		database = NULL;
		sqlite3_open_v2(foreign.c_str(), &database,
			SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
		Exec(database, "CREATE TABLE unrelated(secret TEXT);");
		sqlite3_close_v2(database);
		Expect(!store.Open(foreign, error), "unrelated SQLite file is never claimed or migrated");
		RemoveDatabase(foreign);
	}

	void TestRetentionAndWriteFailures()
	{
		const std::string path = TempPath("retention");
		RemoveDatabase(path);
		pdw::gateway::GatewayOutboxStore store;
		std::string error;
		Expect(store.Open(path, error), "retention outbox opens");
		pdw::gateway::GatewayEvent old = SyntheticEvent(1, "POCSAG", "SYNTHETIC OLD MESSAGE");
		old.decoderFinalizedUtc = "2000-01-01T00:00:00.000Z";
		old.frequencyEffectiveUtc = old.decoderFinalizedUtc;
		old.contentHash = pdw::gateway::GatewayEventContentHash(old);
		Expect(store.Append(old, error), "old synthetic event appends");
		pdw::gateway::RetentionPolicy policy;
		policy.retentionDays = 30;
		policy.maximumMegabytes = 16;
		int removed = 0;
		Expect(store.EnforceRetention(policy, removed, error) && removed == 1,
			"age retention removes only expired records");
		pdw::gateway::GatewayEvent current = SyntheticEvent(2, "FLEX", "SYNTHETIC CURRENT MESSAGE");
		current.decoderFinalizedUtc = "2099-01-01T00:00:00.000Z";
		current.frequencyEffectiveUtc = current.decoderFinalizedUtc;
		current.contentHash = pdw::gateway::GatewayEventContentHash(current);
		Expect(store.Append(current, error), "current synthetic event appends");
		Expect(!store.Append(current, error), "duplicate sequence/event ID reports a write failure");
		store.Close();
		RemoveDatabase(path);

		const std::string impossible = "Z:\\PDW-SYNTHETIC-MISSING\\gateway.sqlite3";
		Expect(!store.Open(impossible, error), "unwritable or missing path fails independently");
	}

	void ConfigureManager(const std::string& path, bool enabled)
	{
		std::memset(&Profile, 0, sizeof(Profile));
		Profile.gatewayOutboxEnabled = enabled ? 1 : 0;
		std::strncpy(Profile.gatewayOutboxPath, path.c_str(), GATEWAY_OUTBOX_PATH_LEN);
		std::strcpy(Profile.gatewayReceiverId, "synthetic-manager-receiver");
		Profile.gatewayOutboxRetentionDays = 30;
		Profile.gatewayOutboxMaximumMegabytes = 16;
		Profile.gatewayOutboxQueueCapacity = 16;
		Profile.audioSource = AUDIO_SOURCE_RTL_SDR;
		Profile.rtlFrequencyHz = 148337500;
	}

	void WaitForDrain(unsigned long timeoutMs)
	{
		const ULONGLONG deadline = GetTickCount64() + timeoutMs;
		while (GetTickCount64() < deadline)
		{
			if (GatewayOutboxGetHealth().queueDepth == 0) return;
			Sleep(10);
		}
	}

	void TestDisabledManagerSyntheticAndSaturation()
	{
		const std::string disabledPath = TempPath("disabled");
		RemoveDatabase(disabledPath);
		ConfigureManager(disabledPath, false);
		GatewayOutboxInitialize();
		DecodedMessageNotificationContext context;
		context.address = "1234567";
		context.mode = "POCSAG-1200";
		context.messageType = "ALPHA";
		context.bitrate = "1200";
		context.message = "SYNTHETIC DISABLED EVENT";
		GatewayOutboxPublishDecodedMessage(context);
		GatewayOutboxShutdown();
		Expect(GetFileAttributesA(disabledPath.c_str()) == INVALID_FILE_ATTRIBUTES,
			"disabled outbox opens no database and changes no decoder path");

		const std::string path = TempPath("manager");
		RemoveDatabase(path);
		ConfigureManager(path, true);
		GatewayOutboxSetWorkerDelayForTest(40);
		GatewayOutboxInitialize();
		std::string error;
		Expect(GatewayOutboxGenerateSynthetic("POCSAG", error),
			"explicit POCSAG synthetic control queues directly to outbox");
		Expect(GatewayOutboxGenerateSynthetic("FLEX", error),
			"explicit FLEX synthetic control queues directly to outbox");
		for (int index = 0; index < 100; ++index)
			GatewayOutboxPublishDecodedMessage(context);
		const GatewayOutboxHealth saturated = GatewayOutboxGetHealth();
		Expect(saturated.queueHighWaterMark <= 16, "worker queue remains bounded");
		Expect(saturated.droppedEvents > 0, "worker saturation exposes dropped sequence gaps");
		WaitForDrain(5000);
		GatewayOutboxShutdown();
		GatewayOutboxSetWorkerDelayForTest(0);
		Expect(QueryInteger(path,
			"SELECT COUNT(*) FROM gateway_events WHERE synthetic=1 AND protocol IN ('POCSAG','FLEX');") == 2,
			"synthetic control writes fixed POCSAG and FLEX events only to local outbox");
		Expect(QueryInteger(path,
			"SELECT COUNT(DISTINCT event_id) FROM gateway_events WHERE synthetic=1;") == 2,
			"synthetic events receive stable unique event IDs");
		Expect(QueryInteger(path,
			"SELECT COUNT(*) FROM gateway_events WHERE synthetic=1 AND frequency_hz=148337500 "
			"AND frequency_provenance='synthetic_fixture' AND "
			"((protocol='POCSAG' AND capcode='1234567' AND bitrate='1200') OR "
			"(protocol='FLEX' AND capcode='123456789' AND cycle=7 AND frame=42));") == 2,
			"synthetic control uses only fixed invented protocol and frequency fixtures");
		const long long firstHighest = QueryInteger(path,
			"SELECT highest_assigned_sequence FROM gateway_outbox_state WHERE singleton=1;");
		ConfigureManager(path, true);
		GatewayOutboxInitialize();
		Expect(GatewayOutboxGenerateSynthetic("POCSAG", error), "manager queues after restart recovery");
		WaitForDrain(3000);
		GatewayOutboxShutdown();
		Expect(QueryInteger(path,
			"SELECT MAX(receiver_sequence) FROM gateway_events;") > firstHighest,
			"receiver sequence continues above the persisted high-water after restart");
		RemoveDatabase(path);
	}
}

int main()
{
	TestIdentityHashAndValidation();
	TestWalReadOnlyRestartAndStates();
	TestMigrationAndOwnership();
	TestRetentionAndWriteFailures();
	TestDisabledManagerSyntheticAndSaturation();
	if (g_failures)
	{
		std::cerr << g_failures << " gateway outbox test(s) failed.\n";
		return EXIT_FAILURE;
	}
	std::cout << "Local Gateway Outbox tests passed with synthetic data only.\n";
	return EXIT_SUCCESS;
}
