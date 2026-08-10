#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>

#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "publishing_job_store.h"

namespace
{
	int failures = 0;

	void Expect(bool condition, const char* description)
	{
		if (condition) return;
		std::cerr << "FAILED: " << description << '\n';
		++failures;
	}

	pdw::publishing::PublishJobRecord MakeJob()
	{
		pdw::publishing::PublishJobRecord job;
		job.event.id = "event-1";
		job.event.timestamp = "2026-08-10T12:00:00.000Z";
		job.event.source = "PDW test";
		job.event.address = "1234567";
		job.event.time = "12:00:00";
		job.event.date = "10-08-26";
		job.event.mode = "FLEX";
		job.event.messageType = "ALPHA";
		job.event.bitrate = "1600";
		job.event.message = "quoted \"message\"\nnext";
		job.event.filterLabel = "Test filter";
		job.event.filterMatched = true;
		job.event.filtered = true;
		job.event.groupBit = 3;
		job.event.cycle = 2;
		job.event.frame = 17;
		job.payload = pdw::publishing::BuildJsonObject(job.event);
		job.staticOutputPath = "C:\\PDW Publish\\Feed";
		job.targets = pdw::publishing::PUBLISH_JOB_TARGET_ALL;
		job.completed = pdw::publishing::PUBLISH_JOB_TARGET_STATIC;
		job.staticAttempts = 2;
		job.webhookAttempts = 4;
		job.attempts = 4;
		return job;
	}

	std::string CreateTemporaryFolder()
	{
		char root[MAX_PATH] = {};
		char placeholder[MAX_PATH] = {};
		if (!GetTempPathA(_countof(root), root) ||
			!GetTempFileNameA(root, "PJS", 0, placeholder)) return std::string();
		DeleteFileA(placeholder);
		if (!CreateDirectoryA(placeholder, NULL)) return std::string();
		return placeholder;
	}

	bool WriteRaw(const std::string& path, const std::string& contents)
	{
		std::ofstream output(path.c_str(), std::ios::binary | std::ios::trunc);
		output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
		return output.good();
	}
}

int main()
{
	using namespace pdw::publishing;
	std::string error;
	PublishJobRecord source = MakeJob();
	std::string serialized;
	Expect(SerializePublishJob(source, serialized, error), "versioned job serializes");
	Expect(serialized.find("PDW-PUBLISH-JOB 2\n") == 0, "new jobs use the version-2 format");

	PublishJobRecord parsed;
	Expect(ParsePublishJob(serialized, parsed, error), "versioned job parses");
	Expect(parsed.event.id == source.event.id, "event ID survives serialization");
	Expect(parsed.payload == source.payload && parsed.event.message == source.event.message,
		"payload and decoded event survive serialization");
	Expect(parsed.targets == source.targets && parsed.completed == source.completed &&
		parsed.failed == source.failed && parsed.staticAttempts == source.staticAttempts &&
		parsed.webhookAttempts == source.webhookAttempts,
		"target, terminal, and per-target attempt state survive serialization");
	Expect(parsed.staticOutputPath == source.staticOutputPath,
		"the job's static output path is frozen in version-2 persistence");
	PublishJobRecord unsafePath = source;
	unsafePath.staticOutputPath = "relative\\feed";
	Expect(!SerializePublishJob(unsafePath, serialized, error),
		"version-2 jobs reject a relative static output path");
	unsafePath = source;
	unsafePath.targets = PUBLISH_JOB_TARGET_WEBHOOK;
	unsafePath.completed = 0;
	unsafePath.staticAttempts = 0;
	Expect(!SerializePublishJob(unsafePath, serialized, error),
		"jobs without a static target reject a stored static output path");
	Expect(SerializePublishJob(source, serialized, error),
		"valid version-2 job reserializes after path validation cases");
	PublishEvent canonicalEvent;
	Expect(ParseCanonicalPublishEventJson(source.payload, canonicalEvent, error) &&
		canonicalEvent.id == source.event.id, "canonical event JSON parses strictly");
	Expect(!ParseCanonicalPublishEventJson(" " + source.payload, canonicalEvent, error),
		"non-canonical event JSON is rejected");
	Expect(!PublishJobAttemptLimitReached(4) && PublishJobAttemptLimitReached(5),
		"the five-attempt dead-letter threshold is exact");

	PendingPublishJobIds pending;
	Expect(pending.Add(source.event.id), "first live job ID is accepted");
	Expect(!pending.Add(source.event.id), "duplicate live or reloaded job ID is rejected");
	pending.Remove(source.event.id);
	Expect(pending.Add(source.event.id), "completed job ID can be reused after removal");
	pending.Clear();
	Expect(pending.Size() == 0, "pending-ID registry clears on shutdown");

	std::string malformed(serialized);
	std::size_t position = malformed.find("targets=3");
	if (position != std::string::npos) malformed.replace(position, 9, "targets=8");
	Expect(!ParsePublishJob(malformed, parsed, error), "unknown target bits are rejected");
	malformed = serialized;
	position = malformed.find("targets=3");
	if (position != std::string::npos) malformed.replace(position, 9, "targets=1");
	position = malformed.find("completed=1");
	if (position != std::string::npos) malformed.replace(position, 11, "completed=2");
	Expect(!ParsePublishJob(malformed, parsed, error), "completion outside the target mask is rejected");
	malformed = serialized;
	const std::string webhookFour = "webhook-attempts=4";
	position = malformed.find(webhookFour);
	if (position != std::string::npos)
		malformed.replace(position, webhookFour.size(), "webhook-attempts=6");
	Expect(!ParsePublishJob(malformed, parsed, error),
		"per-target attempt count beyond the dead-letter threshold is rejected");
	malformed = serialized;
	position = malformed.find(webhookFour);
	if (position != std::string::npos)
		malformed.replace(position, webhookFour.size(),
			"webhook-attempts=" + std::string(200, '9'));
	Expect(!ParsePublishJob(malformed, parsed, error),
		"very long unsigned metadata is rejected without overflow");
	malformed = serialized;
	position = malformed.find("failed=0");
	if (position != std::string::npos) malformed.replace(position, 8, "failed=2");
	Expect(!ParsePublishJob(malformed, parsed, error),
		"terminal failure requires the exact per-target attempt limit");
	malformed = serialized;
	position = malformed.find("static-output-path-bytes=19");
	if (position != std::string::npos)
		malformed.replace(position, 27, "static-output-path-bytes=99999999999999999999");
	Expect(!ParsePublishJob(malformed, parsed, error),
		"oversized path metadata is rejected without overflow");
	std::string oversizedInteger = source.payload;
	position = oversizedInteger.find("\"filter_index\":null");
	if (position != std::string::npos)
		oversizedInteger.replace(position, 19, "\"filter_index\":" + std::string(200, '9'));
	Expect(!ParseCanonicalPublishEventJson(oversizedInteger, canonicalEvent, error),
		"very long signed event integers are rejected without overflow");
	malformed = serialized;
	position = malformed.rfind("event-1");
	if (position != std::string::npos) malformed.replace(position, 7, "event-2");
	Expect(!ParsePublishJob(malformed, parsed, error), "header and payload event-ID mismatch is rejected");
	Expect(!IsSafePublishJobId("..\\escape") && !IsSafePublishJobId("event.json") &&
		IsSafePublishJobId("20260810T120000-123-1"), "job IDs cannot escape the queue folder");

	std::ostringstream version1;
	version1 << "PDW-PUBLISH-JOB 1\n"
		<< "id=" << source.event.id << "\n"
		<< "targets=" << source.targets << "\n"
		<< "completed=" << source.completed << "\n"
		<< "attempts=5\n"
		<< "payload-bytes=" << source.payload.size() << "\n\n"
		<< source.payload;
	Expect(ParsePublishJob(version1.str(), parsed, error), "version-1 jobs remain parseable");
	Expect(parsed.staticAttempts == 5 && parsed.webhookAttempts == 5 &&
		parsed.failed == PUBLISH_JOB_TARGET_WEBHOOK && parsed.staticOutputPath.empty(),
		"version-1 global retry state maps conservatively onto each selected target");

	const std::string folder = CreateTemporaryFolder();
	Expect(!folder.empty(), "temporary job-store folder is created");
	if (!folder.empty())
	{
		const std::string live = folder + "\\" + PublishJobFileName(source.event.id);
		Expect(SavePublishJobFileAtomic(live, source, error), "job is written atomically");
		Expect(GetFileAttributesA((live + ".tmp").c_str()) == INVALID_FILE_ATTRIBUTES,
			"atomic write leaves no temporary file");
		PublishJobRecord disk;
		Expect(LoadPublishJobFile(live, false, disk, error), "versioned job reloads from disk");
		Expect(disk.event.id == source.event.id && disk.attempts == 4,
			"disk reload preserves identity and attempt count");

		source.completed = source.targets;
		Expect(SavePublishJobFileAtomic(live, source, error), "durable state updates replace the same job");
		Expect(LoadPublishJobFile(live, false, disk, error) &&
			disk.completed == disk.targets && disk.failed == 0 &&
			disk.staticAttempts == 2 && disk.webhookAttempts == 4,
			"completed state and independent attempt counters survive restart");

		const std::string dead = folder + "\\dead-" + PublishJobBaseName(live);
		Expect(MovePublishJobFile(live, dead, error), "dead-letter move removes the live job atomically");
		Expect(GetFileAttributesA(live.c_str()) == INVALID_FILE_ATTRIBUTES &&
			LoadPublishJobFile(dead, false, disk, error), "dead-letter contains the complete durable job");
		Expect(DeletePublishJobFile(dead, error) &&
			GetFileAttributesA(dead.c_str()) == INVALID_FILE_ATTRIBUTES,
			"completed/dead-letter payload can be removed cleanly");

		PublishJobRecord recoverySource = MakeJob();
		recoverySource.event.id = "recovery-event";
		recoverySource.payload = BuildJsonObject(recoverySource.event);
		const std::string recoveryFinal = folder + "\\" + PublishJobFileName(recoverySource.event.id);
		const std::string recoveryTemp = recoveryFinal + ".tmp";
		Expect(SavePublishJobFileAtomic(recoveryTemp, recoverySource, error),
			"flushed recovery candidate is created");
		Expect(PromotePublishJobTempFile(recoveryTemp, recoveryFinal, error) &&
			LoadPublishJobFile(recoveryFinal, false, disk, error),
			"valid temporary job promotes when no final exists");
		Expect(SavePublishJobFileAtomic(recoveryTemp, recoverySource, error),
			"equal recovery candidate is created beside a valid final");
		Expect(!PromotePublishJobTempFile(recoveryTemp, recoveryFinal, error) &&
			LoadPublishJobFile(recoveryFinal, false, disk, error) && disk.attempts == 4,
			"equal temporary state stays quarantined instead of replacing the final job");
		DeletePublishJobFile(recoveryTemp, error);
		PublishJobRecord advancing = recoverySource;
		advancing.webhookAttempts = 5;
		advancing.failed = PUBLISH_JOB_TARGET_WEBHOOK;
		Expect(PublishJobRecoveryCandidateAdvances(recoverySource, advancing),
			"pure recovery comparison accepts strict monotonic per-target advancement");
		Expect(SavePublishJobFileAtomic(recoveryTemp, advancing, error) &&
			PromotePublishJobTempFile(recoveryTemp, recoveryFinal, error) &&
			LoadPublishJobFile(recoveryFinal, false, disk, error) &&
			disk.failed == PUBLISH_JOB_TARGET_WEBHOOK && disk.webhookAttempts == 5,
			"advancing recovery candidate atomically replaces the older final state");
		PublishJobRecord conflicting = advancing;
		conflicting.staticAttempts = 1;
		Expect(!PublishJobRecoveryCandidateAdvances(advancing, conflicting),
			"recovery comparison quarantines a candidate with regressed target state");
		DeletePublishJobFile(recoveryFinal, error);

		PublishEvent first = MakeJob().event;
		first.id = "history-first";
		first.message = "first";
		PublishEvent second(first);
		second.id = "history-second";
		second.message = "second";
		PublishEvent firstUpdated(first);
		firstUpdated.message = "first updated";
		const std::string history = folder + "\\messages.jsonl";
		const std::string historyContents = BuildJsonObject(first) + "\n" +
			"not-json\n" + BuildJsonObject(second) + "\n" +
			BuildJsonObject(firstUpdated) + "\n";
		Expect(WriteRaw(history, historyContents), "static publishing history fixture is created");
		std::vector<PublishEvent> historyEvents;
		std::size_t rejectedLines = 0;
		Expect(LoadPublishHistoryJsonLines(history, 2, historyEvents, rejectedLines, error),
			"bounded static history loads");
		Expect(historyEvents.size() == 2 && historyEvents[0].id == "history-first" &&
			historyEvents[0].message == "first updated" && historyEvents[1].id == "history-second",
			"static history restores newest-first and de-duplicates event IDs");
		Expect(rejectedLines == 1, "malformed static history lines are skipped and counted");
		DeletePublishJobFile(history, error);

		const std::string tornHistory = folder + "\\torn.jsonl";
		Expect(WriteRaw(tornHistory, BuildJsonObject(first) + "\n{\"torn\":"),
			"torn JSONL history fixture is created");
		Expect(AppendPublishHistoryJsonLineDurable(tornHistory, BuildJsonObject(second), error),
			"durable JSONL append removes a torn tail and flushes the complete new line");
		historyEvents.clear();
		rejectedLines = 0;
		Expect(LoadPublishHistoryJsonLines(tornHistory, 5, historyEvents, rejectedLines, error) &&
			historyEvents.size() == 2 && historyEvents[0].id == second.id &&
			historyEvents[1].id == first.id && rejectedLines == 0,
			"repaired JSONL history reloads without retaining the torn fragment");
		DeletePublishJobFile(tornHistory, error);

		const std::string staticFile = folder + "\\index.html";
		Expect(WritePublishStaticFileAtomic(staticFile, "first", error) &&
			WritePublishStaticFileAtomic(staticFile, "replacement", error),
			"static publishing files use flushed atomic replacement");
		std::ifstream staticInput(staticFile.c_str(), std::ios::binary);
		std::string staticContents((std::istreambuf_iterator<char>(staticInput)),
			std::istreambuf_iterator<char>());
		Expect(staticContents == "replacement" &&
			GetFileAttributesA((staticFile + ".tmp").c_str()) == INVALID_FILE_ATTRIBUTES,
			"atomic static replacement exposes only the complete replacement");
		DeletePublishJobFile(staticFile, error);

		PublishJobRecord legacySource = MakeJob();
		legacySource.event.id = "legacy-event";
		legacySource.payload = BuildJsonObject(legacySource.event);
		const std::string legacy = folder + "\\old-name.json";
		Expect(WriteRaw(legacy, legacySource.payload), "legacy payload-only job is created");
		PublishJobRecord legacyLoaded;
		Expect(LoadPublishJobFile(legacy, true, legacyLoaded, error),
			"payload-only JSON job remains loadable");
		Expect(legacyLoaded.legacyPayloadOnly && legacyLoaded.event.id == "legacy-event",
			"legacy recovery takes the stable event ID from the payload, not the filename");
		Expect(!LoadPublishJobFile(legacy, false, disk, error),
			"legacy payload is rejected where compatibility loading is not allowed");
		legacyLoaded.targets = PUBLISH_JOB_TARGET_WEBHOOK;
		legacyLoaded.completed = 0;
		legacyLoaded.webhookAttempts = 4;
		legacyLoaded.attempts = 4;
		legacyLoaded.legacyPayloadOnly = false;
		Expect(SavePublishJobFileAtomic(legacy, legacyLoaded, error),
			"legacy payload upgrades atomically in place");
		Expect(LoadPublishJobFile(legacy, true, disk, error) && !disk.legacyPayloadOnly &&
			disk.event.id == "legacy-event" && disk.webhookAttempts == 4 && disk.attempts == 4,
			"upgraded legacy job retains its event ID and retry state");
		DeletePublishJobFile(legacy, error);

		const std::string oversized = folder + "\\oversized.pdwjob";
		Expect(WriteRaw(oversized, std::string(PUBLISH_JOB_MAX_FILE_BYTES + 1, 'x')),
			"oversized fixture is created");
		Expect(!LoadPublishJobFile(oversized, false, disk, error),
			"oversized persisted jobs are rejected before parsing");
		DeletePublishJobFile(oversized, error);
		RemoveDirectoryA(folder.c_str());
	}

	if (failures) return 1;
	std::cout << "Publishing job-store tests passed.\n";
	return 0;
}
