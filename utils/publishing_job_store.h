#ifndef PDW_PUBLISHING_JOB_STORE_H
#define PDW_PUBLISHING_JOB_STORE_H

#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include "publishing_core.h"

namespace pdw
{
namespace publishing
{

enum PublishJobTarget
{
	PUBLISH_JOB_TARGET_STATIC = 1 << 0,
	PUBLISH_JOB_TARGET_WEBHOOK = 1 << 1,
	PUBLISH_JOB_TARGET_ALL = PUBLISH_JOB_TARGET_STATIC | PUBLISH_JOB_TARGET_WEBHOOK
};

const unsigned int PUBLISH_JOB_MAX_ATTEMPTS = 5;
const std::size_t PUBLISH_JOB_MAX_FILE_BYTES = 1024u * 1024u;
const std::size_t PUBLISH_JOB_MAX_STATIC_PATH_BYTES = 32767u;
const std::size_t PUBLISH_HISTORY_MAX_READ_BYTES = 4u * 1024u * 1024u;

struct PublishJobRecord
{
	PublishEvent event;
	std::string payload;
	std::string staticOutputPath;
	unsigned int targets;
	unsigned int completed;
	unsigned int failed;
	unsigned int staticAttempts;
	unsigned int webhookAttempts;
	// Retained temporarily for source compatibility with the version-1 worker.
	// Version-2 persistence uses the independent per-target counters above.
	unsigned int attempts;
	bool legacyPayloadOnly;
	PublishJobRecord()
		: targets(0), completed(0), failed(0), staticAttempts(0),
		webhookAttempts(0), attempts(0), legacyPayloadOnly(false) {}
};

class PendingPublishJobIds
{
public:
	bool Add(const std::string& id);
	void Remove(const std::string& id);
	void Clear();
	std::size_t Size() const;

private:
	std::set<std::string> ids_;
};

bool IsSafePublishJobId(const std::string& id);
bool PublishJobAttemptLimitReached(unsigned int attempts);
std::string PublishJobFileName(const std::string& id);
std::string PublishJobBaseName(const std::string& path);

bool ParseCanonicalPublishEventJson(const std::string& payload,
	PublishEvent& event, std::string& error);
bool LoadPublishHistoryJsonLines(const std::string& path, std::size_t maxEvents,
	std::vector<PublishEvent>& events, std::size_t& rejectedLines, std::string& error);

bool SerializePublishJob(const PublishJobRecord& record, std::string& serialized,
	std::string& error);
bool ParsePublishJob(const std::string& serialized, PublishJobRecord& record,
	std::string& error);
bool PublishJobRecoveryCandidateAdvances(const PublishJobRecord& finalRecord,
	const PublishJobRecord& candidate);
bool LoadPublishJobFile(const std::string& path, bool allowLegacyPayload,
	PublishJobRecord& record, std::string& error);
bool SavePublishJobFileAtomic(const std::string& path,
	const PublishJobRecord& record, std::string& error);
bool DeletePublishJobFile(const std::string& path, std::string& error);
bool MovePublishJobFile(const std::string& source, const std::string& destination,
	std::string& error);
bool PromotePublishJobTempFile(const std::string& temporary,
	const std::string& finalPath, std::string& error);
bool WritePublishStaticFileAtomic(const std::string& path,
	const std::string& contents, std::string& error);
bool AppendPublishHistoryJsonLineDurable(const std::string& path,
	const std::string& canonicalJsonLine, std::string& error);

} // namespace publishing
} // namespace pdw

#endif
