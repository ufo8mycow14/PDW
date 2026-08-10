#include "publishing_delivery_state.h"

#include <cstdlib>
#include <iostream>

namespace
{

int failures = 0;
int casesRun = 0;

void Expect(bool condition, const char* description)
{
	if (condition) return;
	std::cerr << "FAILED: " << description << '\n';
	++failures;
}

pdw::publishing::PublishRuntimeState ReadyRuntime()
{
	pdw::publishing::PublishRuntimeState runtime;
	runtime.enabled = true;
	runtime.acknowledged = true;
	runtime.filteredOnly = true;
	runtime.enabledTargets = pdw::publishing::PUBLISH_JOB_TARGET_ALL;
	return runtime;
}

pdw::publishing::PublishJobState PendingJob()
{
	pdw::publishing::PublishJobState job;
	job.targets = pdw::publishing::PUBLISH_JOB_TARGET_ALL;
	return job;
}

void BeginCase()
{
	++casesRun;
}

void TestIntakeRequiresEnablementPermissionAndDestination()
{
	BeginCase();
	pdw::publishing::PublishRuntimeState runtime = ReadyRuntime();
	Expect(pdw::publishing::PublishShouldIntake(runtime, true),
		"ready publishing accepts a filtered event");
	runtime.enabled = false;
	Expect(!pdw::publishing::PublishShouldIntake(runtime, true),
		"disabled publishing rejects intake");
	runtime = ReadyRuntime();
	runtime.acknowledged = false;
	Expect(!pdw::publishing::PublishShouldIntake(runtime, true),
		"publishing without permission acknowledgement rejects intake");
	runtime = ReadyRuntime();
	runtime.enabledTargets = 0;
	Expect(!pdw::publishing::PublishShouldIntake(runtime, true),
		"publishing without an enabled destination rejects intake");
	runtime = ReadyRuntime();
	runtime.stopping = true;
	Expect(!pdw::publishing::PublishShouldIntake(runtime, true),
		"publishing rejects new intake after shutdown begins");
}

void TestFilteredOnlyIntake()
{
	BeginCase();
	pdw::publishing::PublishRuntimeState runtime = ReadyRuntime();
	Expect(!pdw::publishing::PublishShouldIntake(runtime, false),
		"filtered-only publishing rejects an unfiltered event");
	runtime.filteredOnly = false;
	Expect(pdw::publishing::PublishShouldIntake(runtime, false),
		"all-message publishing accepts an unfiltered event");
}

void TestPauseAcceptsIntake()
{
	BeginCase();
	pdw::publishing::PublishRuntimeState runtime = ReadyRuntime();
	runtime.paused = true;
	Expect(pdw::publishing::PublishShouldIntake(runtime, true),
		"paused publishing still accepts durable intake");
}

void TestEnqueueActionDuringShutdown()
{
	BeginCase();
	Expect(pdw::publishing::DecidePublishEnqueue(false) ==
		pdw::publishing::PublishEnqueueAction::ACTIVE,
		"running worker activates an enqueued job");
	Expect(pdw::publishing::DecidePublishEnqueue(true) ==
		pdw::publishing::PublishEnqueueAction::RETAIN_FOR_RELOAD,
		"stopping worker retains an enqueued job for reload");
}

void TestStoppingWorkIsRetainedFirst()
{
	BeginCase();
	pdw::publishing::PublishRuntimeState runtime = ReadyRuntime();
	runtime.stopping = true;
	pdw::publishing::PublishJobState job = PendingJob();
	job.completed = job.targets;
	const pdw::publishing::PublishWorkDecision decision =
		pdw::publishing::DecidePublishWork(job, runtime);
	Expect(decision.action == pdw::publishing::PublishWorkAction::RETAIN_FOR_RELOAD &&
		decision.deliveryTargets == 0,
		"shutdown retains even completed work before file cleanup");
}

void TestCompletedWorkCleansBeforeAttemptLimit()
{
	BeginCase();
	pdw::publishing::PublishJobState job = PendingJob();
	job.completed = job.targets;
	job.staticAttempts = pdw::publishing::PUBLISH_JOB_MAX_ATTEMPTS;
	job.webhookAttempts = pdw::publishing::PUBLISH_JOB_MAX_ATTEMPTS;
	const pdw::publishing::PublishWorkDecision decision =
		pdw::publishing::DecidePublishWork(job, ReadyRuntime());
	Expect(decision.action == pdw::publishing::PublishWorkAction::CLEANUP,
		"completed attempt-five work is cleaned up instead of dead-lettered");
}

void TestAttemptLimitDeadLettersIncompleteWork()
{
	BeginCase();
	pdw::publishing::PublishJobState job = PendingJob();
	job.failed = job.targets;
	job.staticAttempts = pdw::publishing::PUBLISH_JOB_MAX_ATTEMPTS;
	job.webhookAttempts = pdw::publishing::PUBLISH_JOB_MAX_ATTEMPTS;
	const pdw::publishing::PublishWorkDecision decision =
		pdw::publishing::DecidePublishWork(job, ReadyRuntime());
	Expect(decision.action == pdw::publishing::PublishWorkAction::DEAD_LETTER,
		"incomplete attempt-five work is dead-lettered before delivery");
}

void TestRuntimeGatesHoldWork()
{
	BeginCase();
	pdw::publishing::PublishRuntimeState runtime = ReadyRuntime();
	const pdw::publishing::PublishJobState job = PendingJob();
	runtime.enabled = false;
	Expect(pdw::publishing::DecidePublishWork(job, runtime).action ==
		pdw::publishing::PublishWorkAction::HOLD, "disabled runtime holds durable work");
	runtime = ReadyRuntime();
	runtime.acknowledged = false;
	Expect(pdw::publishing::DecidePublishWork(job, runtime).action ==
		pdw::publishing::PublishWorkAction::HOLD, "unacknowledged runtime holds durable work");
	runtime = ReadyRuntime();
	runtime.paused = true;
	Expect(pdw::publishing::DecidePublishWork(job, runtime).action ==
		pdw::publishing::PublishWorkAction::HOLD, "paused runtime holds durable work");
}

void TestWorkDeliversOnlyRemainingEnabledTargets()
{
	BeginCase();
	pdw::publishing::PublishJobState job = PendingJob();
	pdw::publishing::PublishRuntimeState runtime = ReadyRuntime();
	runtime.enabledTargets = pdw::publishing::PUBLISH_JOB_TARGET_STATIC;
	pdw::publishing::PublishWorkDecision decision =
		pdw::publishing::DecidePublishWork(job, runtime);
	Expect(decision.action == pdw::publishing::PublishWorkAction::DELIVER &&
		decision.deliveryTargets == pdw::publishing::PUBLISH_JOB_TARGET_STATIC,
		"work decision attempts only the selected target that is currently enabled");

	job.completed = pdw::publishing::PUBLISH_JOB_TARGET_STATIC;
	runtime = ReadyRuntime();
	decision =
		pdw::publishing::DecidePublishWork(job, runtime);
	Expect(decision.action == pdw::publishing::PublishWorkAction::DELIVER &&
		decision.deliveryTargets == pdw::publishing::PUBLISH_JOB_TARGET_WEBHOOK,
		"work decision delivers only an outstanding target");
}

void TestPartialSuccessFailureSurvivesReload()
{
	BeginCase();
	pdw::publishing::PublishPassState pass =
		pdw::publishing::BeginPublishPass(PendingJob());
	pdw::publishing::PublishTargetTransition staticResult =
		pdw::publishing::RecordPublishTarget(pass,
			pdw::publishing::PUBLISH_JOB_TARGET_STATIC, true);
	Expect(staticResult.persistCompletedState,
		"static completion is persisted before the webhook is attempted");
	pdw::publishing::PublishTargetTransition webhookResult =
		pdw::publishing::RecordPublishTarget(staticResult.pass,
			pdw::publishing::PUBLISH_JOB_TARGET_WEBHOOK, false);
	const pdw::publishing::PublishFinishPassDecision finish =
		pdw::publishing::FinishPublishPass(webhookResult.pass);
	Expect(finish.action == pdw::publishing::PublishFinishPassAction::RETRY &&
		finish.job.completed == pdw::publishing::PUBLISH_JOB_TARGET_STATIC &&
		finish.job.staticAttempts == 0 && finish.job.webhookAttempts == 1 &&
		finish.retryTargets == pdw::publishing::PUBLISH_JOB_TARGET_WEBHOOK,
		"a webhook failure retains static completion and consumes one webhook attempt");
	const pdw::publishing::PublishWorkDecision reloaded =
		pdw::publishing::DecidePublishWork(finish.job, ReadyRuntime());
	Expect(reloaded.action == pdw::publishing::PublishWorkAction::DELIVER &&
		reloaded.deliveryTargets == pdw::publishing::PUBLISH_JOB_TARGET_WEBHOOK,
		"a reload retries only the incomplete webhook target");
}

void TestPartialCompletionRetainsAcrossShutdown()
{
	BeginCase();
	pdw::publishing::PublishJobState job = PendingJob();
	job.completed = pdw::publishing::PUBLISH_JOB_TARGET_STATIC;
	pdw::publishing::PublishRuntimeState stopping = ReadyRuntime();
	stopping.stopping = true;
	Expect(pdw::publishing::DecidePublishWork(job, stopping).action ==
		pdw::publishing::PublishWorkAction::RETAIN_FOR_RELOAD,
		"shutdown retains a partially completed job without another delivery");
	const pdw::publishing::PublishWorkDecision reloaded =
		pdw::publishing::DecidePublishWork(job, ReadyRuntime());
	Expect(reloaded.action == pdw::publishing::PublishWorkAction::DELIVER &&
		reloaded.deliveryTargets == pdw::publishing::PUBLISH_JOB_TARGET_WEBHOOK,
		"the simulated reload resumes only the outstanding target");
}

void TestUnavailableRemainingTargetHoldsWork()
{
	BeginCase();
	pdw::publishing::PublishJobState job = PendingJob();
	job.completed = pdw::publishing::PUBLISH_JOB_TARGET_STATIC;
	pdw::publishing::PublishRuntimeState runtime = ReadyRuntime();
	runtime.enabledTargets = pdw::publishing::PUBLISH_JOB_TARGET_STATIC;
	const pdw::publishing::PublishWorkDecision decision =
		pdw::publishing::DecidePublishWork(job, runtime);
	Expect(decision.action == pdw::publishing::PublishWorkAction::HOLD &&
		decision.deliveryTargets == 0,
		"work waits when its remaining destination is not enabled");
}

void TestSuccessfulTargetRequestsImmediatePersistence()
{
	BeginCase();
	const pdw::publishing::PublishPassState pass =
		pdw::publishing::BeginPublishPass(PendingJob());
	const pdw::publishing::PublishTargetTransition transition =
		pdw::publishing::RecordPublishTarget(pass,
			pdw::publishing::PUBLISH_JOB_TARGET_STATIC, true);
	Expect(transition.persistCompletedState &&
		transition.pass.job.completed == pdw::publishing::PUBLISH_JOB_TARGET_STATIC &&
		transition.pass.attemptedTargets == pdw::publishing::PUBLISH_JOB_TARGET_STATIC &&
		transition.pass.failedTargets == 0,
		"successful target completion is returned for immediate durable persistence");
}

void TestTargetIsRecordedOnlyOncePerPass()
{
	BeginCase();
	pdw::publishing::PublishPassState pass =
		pdw::publishing::BeginPublishPass(PendingJob());
	pdw::publishing::PublishTargetTransition first =
		pdw::publishing::RecordPublishTarget(pass,
			pdw::publishing::PUBLISH_JOB_TARGET_WEBHOOK, false);
	const pdw::publishing::PublishTargetTransition duplicate =
		pdw::publishing::RecordPublishTarget(first.pass,
			pdw::publishing::PUBLISH_JOB_TARGET_WEBHOOK, true);
	Expect(first.pass.failedTargets == pdw::publishing::PUBLISH_JOB_TARGET_WEBHOOK &&
		!first.persistCompletedState, "failed target is recorded without completion persistence");
	Expect(!duplicate.persistCompletedState && duplicate.pass.job.completed == 0 &&
		duplicate.pass.failedTargets == pdw::publishing::PUBLISH_JOB_TARGET_WEBHOOK,
		"a duplicate target result cannot rewrite the first result in one pass");
}

void TestPassWithoutFailuresHoldsIncompleteWork()
{
	BeginCase();
	pdw::publishing::PublishPassState pass =
		pdw::publishing::BeginPublishPass(PendingJob());
	const pdw::publishing::PublishTargetTransition success =
		pdw::publishing::RecordPublishTarget(pass,
			pdw::publishing::PUBLISH_JOB_TARGET_STATIC, true);
	const pdw::publishing::PublishFinishPassDecision decision =
		pdw::publishing::FinishPublishPass(success.pass);
	Expect(decision.action == pdw::publishing::PublishFinishPassAction::HOLD &&
		decision.job.staticAttempts == 0 && decision.job.webhookAttempts == 0 &&
		!decision.persistAttemptState && decision.retryTargets == 0 &&
		decision.retryDelayMilliseconds == 0,
		"a successful partial pass holds without consuming a retry attempt");
}

void TestFailedPassUsesOneAttemptAndBoundedBackoff()
{
	BeginCase();
	const unsigned int expectedDelays[] = {1000u, 2000u, 4000u, 8000u};
	for (unsigned int attempt = 0; attempt < 4; ++attempt)
	{
		pdw::publishing::PublishJobState job = PendingJob();
		job.webhookAttempts = attempt;
		pdw::publishing::PublishPassState pass = pdw::publishing::BeginPublishPass(job);
		pdw::publishing::PublishTargetTransition failed = pdw::publishing::RecordPublishTarget(
			pass, pdw::publishing::PUBLISH_JOB_TARGET_WEBHOOK, false);
		const pdw::publishing::PublishFinishPassDecision decision =
			pdw::publishing::FinishPublishPass(failed.pass);
		Expect(decision.action == pdw::publishing::PublishFinishPassAction::RETRY &&
			decision.job.staticAttempts == 0 && decision.job.webhookAttempts == attempt + 1 &&
			decision.persistAttemptState &&
			decision.retryTargets == pdw::publishing::PUBLISH_JOB_TARGET_WEBHOOK &&
			decision.retryDelayMilliseconds == expectedDelays[attempt],
			"a failed target increments independently and selects its bounded retry delay");
	}

	pdw::publishing::PublishJobState lastJob = PendingJob();
	lastJob.webhookAttempts = 4;
	pdw::publishing::PublishPassState lastPass = pdw::publishing::BeginPublishPass(lastJob);
	lastPass = pdw::publishing::RecordPublishTarget(lastPass,
		pdw::publishing::PUBLISH_JOB_TARGET_WEBHOOK, false).pass;
	const pdw::publishing::PublishFinishPassDecision terminal =
		pdw::publishing::FinishPublishPass(lastPass);
	Expect(terminal.action == pdw::publishing::PublishFinishPassAction::HOLD &&
		terminal.job.failed == pdw::publishing::PUBLISH_JOB_TARGET_WEBHOOK &&
		terminal.job.webhookAttempts == pdw::publishing::PUBLISH_JOB_MAX_ATTEMPTS &&
		terminal.persistAttemptState && terminal.retryTargets == 0,
		"exhausting one target marks only that target terminal while another remains pending");
	const pdw::publishing::PublishWorkDecision remaining =
		pdw::publishing::DecidePublishWork(terminal.job, ReadyRuntime());
	Expect(remaining.action == pdw::publishing::PublishWorkAction::DELIVER &&
		remaining.deliveryTargets == pdw::publishing::PUBLISH_JOB_TARGET_STATIC,
		"an exhausted webhook does not prevent delivery to the remaining static target");

	pdw::publishing::PublishJobState allTerminal = terminal.job;
	allTerminal.staticAttempts = 4;
	pdw::publishing::PublishPassState staticPass = pdw::publishing::BeginPublishPass(allTerminal);
	staticPass = pdw::publishing::RecordPublishTarget(staticPass,
		pdw::publishing::PUBLISH_JOB_TARGET_STATIC, false).pass;
	const pdw::publishing::PublishFinishPassDecision dead =
		pdw::publishing::FinishPublishPass(staticPass);
	Expect(dead.action == pdw::publishing::PublishFinishPassAction::DEAD_LETTER &&
		dead.job.failed == dead.job.targets && dead.persistAttemptState,
		"a job dead-letters only after every selected target is terminal and one failed");
}

void TestTerminalFileActionPreservesPendingIdentityOnFailure()
{
	BeginCase();
	const pdw::publishing::PublishTerminalDecision succeeded =
		pdw::publishing::FinishPublishTerminal(true);
	Expect(succeeded.action == pdw::publishing::PublishTerminalAction::FINISH &&
		succeeded.releasePendingId,
		"successful cleanup or dead-lettering finishes and releases the pending ID");
	const pdw::publishing::PublishTerminalDecision failed =
		pdw::publishing::FinishPublishTerminal(false);
	Expect(failed.action == pdw::publishing::PublishTerminalAction::REQUEUE_PENDING &&
		!failed.releasePendingId,
		"failed cleanup or dead-lettering requeues without releasing the pending ID");
}

void TestHeldSweepDoesNotWaitPerJob()
{
	BeginCase();
	pdw::publishing::PublishHeldSweepState sweep;
	bool waitedEarly = false;
	for (std::size_t held = 0; held < 500; ++held)
		waitedEarly = pdw::publishing::PublishHeldSweepShouldWait(sweep, 501) || waitedEarly;
	Expect(!waitedEarly && sweep.heldJobsSeen == 500,
		"five hundred held jobs do not delay a ready job later in the same queue sweep");
	Expect(pdw::publishing::PublishHeldSweepShouldWait(sweep, 501) &&
		sweep.heldJobsSeen == 0,
		"the scheduler waits only after a complete held-only sweep");
}

void TestHeldOnlyQueueWaitsOncePerSweep()
{
	BeginCase();
	pdw::publishing::PublishHeldSweepState sweep;
	Expect(!pdw::publishing::PublishHeldSweepShouldWait(sweep, 3) &&
		!pdw::publishing::PublishHeldSweepShouldWait(sweep, 3) &&
		pdw::publishing::PublishHeldSweepShouldWait(sweep, 3),
		"a held-only queue waits once after all queued jobs have been inspected");
	Expect(!pdw::publishing::PublishHeldSweepShouldWait(sweep, 3) &&
		sweep.heldJobsSeen == 1,
		"a new held-only sweep starts immediately after the bounded wait");
}

void TestReadyWorkResetsHeldSweep()
{
	BeginCase();
	pdw::publishing::PublishHeldSweepState sweep;
	pdw::publishing::PublishHeldSweepShouldWait(sweep, 4);
	pdw::publishing::PublishHeldSweepShouldWait(sweep, 4);
	pdw::publishing::ResetPublishHeldSweep(sweep);
	Expect(sweep.heldJobsSeen == 0 &&
		!pdw::publishing::PublishHeldSweepShouldWait(sweep, 4) &&
		sweep.heldJobsSeen == 1,
		"ready work restarts held-sweep accounting instead of inheriting stale holds");
}

} // namespace

int main()
{
	TestIntakeRequiresEnablementPermissionAndDestination();
	TestFilteredOnlyIntake();
	TestPauseAcceptsIntake();
	TestEnqueueActionDuringShutdown();
	TestStoppingWorkIsRetainedFirst();
	TestCompletedWorkCleansBeforeAttemptLimit();
	TestAttemptLimitDeadLettersIncompleteWork();
	TestRuntimeGatesHoldWork();
	TestWorkDeliversOnlyRemainingEnabledTargets();
	TestUnavailableRemainingTargetHoldsWork();
	TestPartialSuccessFailureSurvivesReload();
	TestPartialCompletionRetainsAcrossShutdown();
	TestSuccessfulTargetRequestsImmediatePersistence();
	TestTargetIsRecordedOnlyOncePerPass();
	TestPassWithoutFailuresHoldsIncompleteWork();
	TestFailedPassUsesOneAttemptAndBoundedBackoff();
	TestTerminalFileActionPreservesPendingIdentityOnFailure();
	TestHeldSweepDoesNotWaitPerJob();
	TestHeldOnlyQueueWaitsOncePerSweep();
	TestReadyWorkResetsHeldSweep();

	if (casesRun != 20)
	{
		std::cerr << "FAILED: delivery-state test matrix did not run 20 cases\n";
		return 1;
	}
	if (failures != 0) return 1;
	std::cout << "Publishing delivery-state tests passed (20 cases).\n";
	return 0;
}
