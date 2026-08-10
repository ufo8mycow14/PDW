#include "output_health_core.h"

#include <stdexcept>

namespace pdw
{
namespace health
{

DestinationState::DestinationState()
	: enabled(false), successes(0), failures(0), dropped(0), consecutiveFailures(0),
	  alertPending(false), lastOutcome(OUTCOME_INFO), summary("Disabled.") {}

Registry::Registry(std::size_t maximumHistory)
	: maximumHistory_(maximumHistory ? maximumHistory : 1), alertsEnabled_(true),
	  failureThreshold_(3) {}

void Registry::ConfigureAlerts(bool enabled, unsigned int failureThreshold)
{
	alertsEnabled_ = enabled;
	failureThreshold_ = failureThreshold < 1 ? 1 : (failureThreshold > 20 ? 20 : failureThreshold);
	if (!alertsEnabled_)
		for (int index = 0; index < DESTINATION_COUNT; ++index) states_[index].alertPending = false;
}

void Registry::SetEnabled(Destination destination, bool enabled, const std::string& utc)
{
	if (!IsValid(destination)) return;
	DestinationState& state = states_[destination];
	state.enabled = enabled;
	state.lastUtc = utc;
	state.lastOutcome = OUTCOME_INFO;
	state.summary = enabled ? "Enabled; waiting for activity." : "Disabled.";
	state.consecutiveFailures = 0;
	state.alertPending = false;
}

bool Registry::Record(Destination destination, Outcome outcome, const std::string& utc,
	const std::string& summary)
{
	if (!IsValid(destination)) return false;
	DestinationState& state = states_[destination];
	state.lastOutcome = outcome;
	state.lastUtc = utc;
	state.summary = summary;
	bool triggered = false;
	switch (outcome)
	{
		case OUTCOME_SUCCESS:
			++state.successes;
			state.consecutiveFailures = 0;
			state.alertPending = false;
			break;
		case OUTCOME_FAILURE:
			++state.failures;
			++state.consecutiveFailures;
			break;
		case OUTCOME_DROPPED:
			++state.dropped;
			++state.consecutiveFailures;
			break;
		case OUTCOME_INFO:
		default:
			break;
	}
	if (alertsEnabled_ && state.enabled &&
		(outcome == OUTCOME_FAILURE || outcome == OUTCOME_DROPPED) &&
		state.consecutiveFailures >= failureThreshold_ && !state.alertPending)
	{
		state.alertPending = true;
		triggered = true;
	}
	Event event;
	event.destination = destination;
	event.outcome = outcome;
	event.utc = utc;
	event.summary = summary;
	history_.push_front(event);
	while (history_.size() > maximumHistory_) history_.pop_back();
	return triggered;
}

void Registry::AcknowledgeAll()
{
	for (int index = 0; index < DESTINATION_COUNT; ++index)
	{
		states_[index].alertPending = false;
		states_[index].consecutiveFailures = 0;
	}
}

unsigned int Registry::PendingAlertCount() const
{
	unsigned int count = 0;
	for (int index = 0; index < DESTINATION_COUNT; ++index)
		if (states_[index].alertPending) ++count;
	return count;
}

bool Registry::AlertsEnabled() const { return alertsEnabled_; }
unsigned int Registry::FailureThreshold() const { return failureThreshold_; }

const DestinationState& Registry::State(Destination destination) const
{
	if (!IsValid(destination)) throw std::out_of_range("invalid output-health destination");
	return states_[destination];
}

std::vector<Event> Registry::History() const
{
	return std::vector<Event>(history_.begin(), history_.end());
}

bool Registry::IsValid(Destination destination) const
{
	return destination >= DESTINATION_SMTP && destination < DESTINATION_COUNT;
}

const char* DestinationName(Destination destination)
{
	switch (destination)
	{
		case DESTINATION_SMTP: return "Email (SMTP)";
		case DESTINATION_APPRISE: return "Apprise";
		case DESTINATION_FTP: return "File transfer";
		case DESTINATION_PUBLISHING: return "Web publishing";
		case DESTINATION_MQTT: return "MQTT";
		case DESTINATION_SQLITE: return "SQLite";
		case DESTINATION_MYSQL_ODBC: return "MySQL ODBC";
		case DESTINATION_TELNET: return "Telnet JSON";
		case DESTINATION_WINDOWS_TOAST: return "Windows notification";
		case DESTINATION_DATA_ROUTER: return "Data-output router";
		default: return "Unknown output";
	}
}

const char* OutcomeName(Outcome outcome)
{
	switch (outcome)
	{
		case OUTCOME_SUCCESS: return "Success";
		case OUTCOME_FAILURE: return "Failure";
		case OUTCOME_DROPPED: return "Dropped";
		case OUTCOME_INFO:
		default: return "Info";
	}
}

} // namespace health
} // namespace pdw
