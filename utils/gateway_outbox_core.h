#ifndef PDW_GATEWAY_OUTBOX_CORE_H
#define PDW_GATEWAY_OUTBOX_CORE_H

#include <string>

struct sqlite3;

namespace pdw
{
namespace gateway
{

const int GATEWAY_EVENT_SCHEMA_VERSION = 1;
const int GATEWAY_DATABASE_SCHEMA_VERSION = 2;
const int GATEWAY_APPLICATION_ID = 0x50444757; // ASCII "PDGW"

struct GatewayEvent
{
	long long receiverSequence;
	std::string eventId;
	int schemaVersion;
	std::string receiverId;
	std::string decoderFinalizedUtc;
	std::string timestampMethod;
	std::string source;
	long long frequencyHz;
	std::string frequencyProvenance;
	std::string frequencyEffectiveUtc;
	std::string protocol;
	std::string protocolMetadataJson;
	std::string capcode;
	std::string messageType;
	std::string bitrate;
	std::string message;
	std::string contentHash;
	std::string pdwVersion;
	std::string filterLabel;
	bool filterMatched;
	bool monitorOnly;
	bool filtered;
	bool rejected;
	bool blockedDuplicate;
	bool groupCall;
	bool groupFinal;
	bool fragmented;
	bool assembled;
	int groupBit;
	int cycle;
	int frame;
	bool synthetic;

	GatewayEvent();
};

struct RetentionPolicy
{
	unsigned int retentionDays;
	unsigned int maximumMegabytes;

	RetentionPolicy() : retentionDays(30), maximumMegabytes(512) {}
};

struct StoreStatistics
{
	long long lastCommittedSequence;
	long long oldestRetainedSequence;
	long long retainedRecords;
	unsigned long long databaseBytes;
	unsigned long long availableDiskBytes;

	StoreStatistics() : lastCommittedSequence(0), oldestRetainedSequence(0),
		retainedRecords(0), databaseBytes(0), availableDiskBytes(0) {}
};

std::string GatewayEventCanonicalPayload(const GatewayEvent& event);
std::string GatewayEventContentHash(const GatewayEvent& event);
bool ValidateGatewayEvent(const GatewayEvent& event, std::string& error);

class GatewayOutboxStore
{
public:
	GatewayOutboxStore();
	~GatewayOutboxStore();

	bool Open(const std::string& utf8Path, std::string& error);
	void Close();
	bool IsOpen() const;
	bool Append(const GatewayEvent& event, std::string& error);
	bool RecordHighestAssignedSequence(long long sequence, std::string& error);
	bool GetHighestAssignedSequence(long long& sequence, std::string& error);
	bool EnforceRetention(const RetentionPolicy& policy, int& removed,
		std::string& error);
	bool GetStatistics(StoreStatistics& statistics, std::string& error);
	bool Checkpoint(std::string& error);
	const std::string& Path() const;

private:
	GatewayOutboxStore(const GatewayOutboxStore&);
	GatewayOutboxStore& operator=(const GatewayOutboxStore&);

	bool Execute(const char* sql, std::string& error);
	bool Migrate(std::string& error);
	bool ValidateSchema(std::string& error);
	bool RefreshFileStatistics(StoreStatistics& statistics);

	sqlite3* database_;
	std::string path_;
};

// Readers own their checkpoints outside PDW. This verifies the documented
// read-only URI contract without ever writing to the outbox.
bool VerifyGatewayReadOnlyAccess(const std::string& utf8Path,
	long long& lastSequence, std::string& error);

} // namespace gateway
} // namespace pdw

#endif
