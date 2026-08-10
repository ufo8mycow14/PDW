#include <stdio.h>
#include <string>

#include "notification_core.h"

using namespace std;

namespace
{
	int failures = 0;

	void Expect(bool condition, const char *message)
	{
		if (condition) return;
		fprintf(stderr, "FAIL: %s\n", message);
		failures++;
	}

	struct FakeChannel
	{
		int deliveries;
		FakeChannel() : deliveries(0) {}
		void Publish(const NotificationEvent &) { deliveries++; }
	};

	void RouteToFakes(const NotificationRouteDecision &decision,
		const NotificationEvent &event, FakeChannel &email, FakeChannel &push)
	{
		if (decision.email) email.Publish(event);
		if (decision.push) push.Publish(event);
	}
}

int main()
{
	NotificationEvent event;
	event.identifier = "decoded.filtered-1";
	event.title = "PDW filtered message";
	event.message = "Address: 1234567";
	event.severity = NOTIFICATION_SEVERITY_INFORMATION;
	event.timestamp = "2026-08-10T00:00:00.000Z";

	FakeChannel email;
	FakeChannel push;
	RouteToFakes(NotificationDecideRoutes(true, false, true), event, email, push);
	Expect(email.deliveries == 1 && push.deliveries == 0,
		"email-only routing must preserve email and skip push");

	email.deliveries = push.deliveries = 0;
	RouteToFakes(NotificationDecideRoutes(false, true, true), event, email, push);
	Expect(email.deliveries == 0 && push.deliveries == 1,
		"push-only routing must send filtered messages");

	email.deliveries = push.deliveries = 0;
	RouteToFakes(NotificationDecideRoutes(true, true, true), event, email, push);
	Expect(email.deliveries == 1 && push.deliveries == 1,
		"both channels must be independently routable");

	email.deliveries = push.deliveries = 0;
	RouteToFakes(NotificationDecideRoutes(false, false, true), event, email, push);
	Expect(email.deliveries == 0 && push.deliveries == 0,
		"neither channel must produce no delivery");

	Expect(!NotificationDecideRoutes(false, true, false).push,
		"unfiltered messages must never route to Apprise");

	string privateBody = NotificationBuildFilteredBody("Dispatch", "1234567",
		"POCSAG", "ALPHA", "private decoded text", false, 480);
	Expect(privateBody.find("private decoded text") == string::npos,
		"privacy-safe notification body must omit decoded text");
	Expect(privateBody.find("Dispatch") == string::npos &&
		privateBody.find("1234567") == string::npos,
		"privacy-safe notification body must omit filter and address details");

	string detailedBody = NotificationBuildFilteredBody("Dispatch", "1234567",
		"POCSAG", "ALPHA", "private decoded text", true, 480);
	Expect(detailedBody.find("private decoded text") != string::npos,
		"explicit privacy opt-in must include decoded text");
	Expect(detailedBody.find("Dispatch") != string::npos &&
		detailedBody.find("1234567") != string::npos,
		"explicit privacy opt-in must include filter and address details");

	event.message = "quoted \"message\"\nnext";
	string payload = NotificationBuildApprisePayload(event, "ntfy://topic");
	Expect(payload.find("\\\"message\\\"") != string::npos,
		"Apprise JSON must escape quotes");
	Expect(payload.find("\\n") != string::npos,
		"Apprise JSON must escape newlines");
	Expect(payload.find("\"type\":\"info\"") != string::npos,
		"information severity must map to the Apprise info type");
	string statefulPayload = NotificationBuildApprisePayload(event, "");
	Expect(statefulPayload.find("\"urls\"") == string::npos,
		"stateful payloads must omit the stateless urls field");

	Expect(NotificationHttpStatusIsTransient(429),
		"HTTP 429 must be retryable");
	Expect(NotificationHttpStatusIsTransient(503),
		"HTTP 503 must be retryable");
	Expect(!NotificationHttpStatusIsTransient(401),
		"authentication failures must not be retried");
	Expect(!NotificationHttpStatusIsTransient(424),
		"partial delivery failures must not be retried");

	string authStatus = NotificationSanitizedHttpStatus(401);
	Expect(authStatus.find("secret") == string::npos &&
		authStatus.find("http") == string::npos,
		"sanitized authentication status must not contain caller data");

	if (failures)
	{
		fprintf(stderr, "%d notification test(s) failed.\n", failures);
		return 1;
	}

	printf("All notification core tests passed.\n");
	return 0;
}
