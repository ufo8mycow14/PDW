#ifndef PDW_OUTPUT_HEALTH_CORE_H
#define PDW_OUTPUT_HEALTH_CORE_H

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

namespace pdw
{
namespace health
{

enum Destination
{
	DESTINATION_SMTP = 0,
	DESTINATION_APPRISE,
	DESTINATION_FTP,
	DESTINATION_PUBLISHING,
	DESTINATION_MQTT,
	DESTINATION_SQLITE,
	DESTINATION_MYSQL_ODBC,
	DESTINATION_TELNET,
	DESTINATION_WINDOWS_TOAST,
	DESTINATION_DATA_ROUTER,
	DESTINATION_COUNT
};

enum Outcome
{
	OUTCOME_INFO = 0,
	OUTCOME_SUCCESS,
	OUTCOME_FAILURE,
	OUTCOME_DROPPED
};

struct DestinationState
{
	bool enabled;
	unsigned long successes;
	unsigned long failures;
	unsigned long dropped;
	unsigned int consecutiveFailures;
	bool alertPending;
	Outcome lastOutcome;
	std::string lastUtc;
	std::string summary;
	DestinationState();
};

struct Event
{
	Destination destination;
	Outcome outcome;
	std::string utc;
	std::string summary;
};

class Registry
{
public:
	explicit Registry(std::size_t maximumHistory = 200);

	void ConfigureAlerts(bool enabled, unsigned int failureThreshold);
	void SetEnabled(Destination destination, bool enabled, const std::string& utc);
	bool Record(Destination destination, Outcome outcome, const std::string& utc,
		const std::string& summary);
	void AcknowledgeAll();
	unsigned int PendingAlertCount() const;
	bool AlertsEnabled() const;
	unsigned int FailureThreshold() const;
	const DestinationState& State(Destination destination) const;
	std::vector<Event> History() const;

private:
	bool IsValid(Destination destination) const;
	DestinationState states_[DESTINATION_COUNT];
	std::deque<Event> history_;
	std::size_t maximumHistory_;
	bool alertsEnabled_;
	unsigned int failureThreshold_;
};

const char* DestinationName(Destination destination);
const char* OutcomeName(Outcome outcome);

} // namespace health
} // namespace pdw

#endif
