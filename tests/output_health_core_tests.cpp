#include "output_health_core.h"

#include <iostream>

namespace
{
	int failures = 0;
	void Expect(bool condition, const char* description)
	{
		if (condition) return;
		std::cerr << "FAILED: " << description << '\n';
		++failures;
	}
}

int main()
{
	using namespace pdw::health;
	Registry health(3);
	health.ConfigureAlerts(true, 3);
	health.SetEnabled(DESTINATION_MQTT, true, "2026-08-10T00:00:00Z");
	Expect(!health.Record(DESTINATION_MQTT, OUTCOME_FAILURE, "t1", "network failure"),
		"first failure does not alert");
	Expect(!health.Record(DESTINATION_MQTT, OUTCOME_FAILURE, "t2", "network failure"),
		"second failure does not alert");
	Expect(health.Record(DESTINATION_MQTT, OUTCOME_FAILURE, "t3", "network failure"),
		"threshold failure alerts once");
	Expect(!health.Record(DESTINATION_MQTT, OUTCOME_FAILURE, "t4", "network failure"),
		"pending alert is not emitted repeatedly");
	Expect(health.PendingAlertCount() == 1, "pending alert count is tracked");
	Expect(health.State(DESTINATION_MQTT).failures == 4, "failure total is tracked");
	Expect(health.History().size() == 3, "history obeys its bounded capacity");
	Expect(health.Record(DESTINATION_MQTT, OUTCOME_SUCCESS, "t5", "published") == false,
		"success does not trigger an alert");
	Expect(health.PendingAlertCount() == 0, "success clears a pending alert");
	Expect(health.State(DESTINATION_MQTT).consecutiveFailures == 0,
		"success resets consecutive failures");
	health.Record(DESTINATION_MQTT, OUTCOME_DROPPED, "t6", "queue full");
	Expect(health.State(DESTINATION_MQTT).dropped == 1, "drop total is tracked separately");
	health.ConfigureAlerts(false, 2);
	health.Record(DESTINATION_MQTT, OUTCOME_FAILURE, "t7", "failure");
	health.Record(DESTINATION_MQTT, OUTCOME_FAILURE, "t8", "failure");
	Expect(health.PendingAlertCount() == 0, "disabled alerts stay cleared");
	health.SetEnabled(DESTINATION_MQTT, false, "t9");
	Expect(!health.State(DESTINATION_MQTT).enabled && health.State(DESTINATION_MQTT).summary == "Disabled.",
		"disabling an output preserves counters but resets live state");

	if (failures) return 1;
	std::cout << "Output-health core tests passed.\n";
	return 0;
}
