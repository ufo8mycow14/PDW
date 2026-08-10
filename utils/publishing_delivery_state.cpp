#include "publishing_delivery_state.h"

namespace pdw
{
namespace publishing
{
namespace
{

unsigned int KnownTargets(unsigned int targets)
{
	return targets & static_cast<unsigned int>(PUBLISH_JOB_TARGET_ALL);
}

unsigned int TerminalTargets(const PublishJobState& job)
{
	return KnownTargets(job.completed | job.failed) & KnownTargets(job.targets);
}

bool AllTargetsTerminal(const PublishJobState& job)
{
	const unsigned int targets = KnownTargets(job.targets);
	return TerminalTargets(job) == targets;
}

unsigned int& AttemptsForTarget(PublishJobState& job, unsigned int target)
{
	return target == PUBLISH_JOB_TARGET_STATIC ? job.staticAttempts : job.webhookAttempts;
}

} // namespace

bool PublishShouldIntake(const PublishRuntimeState& runtime, bool eventFiltered)
{
	if (runtime.stopping || !runtime.enabled || !runtime.acknowledged)
		return false;
	if (KnownTargets(runtime.enabledTargets) == 0)
		return false;
	return !runtime.filteredOnly || eventFiltered;
}

PublishEnqueueAction DecidePublishEnqueue(bool stopping)
{
	return stopping ? PublishEnqueueAction::RETAIN_FOR_RELOAD :
		PublishEnqueueAction::ACTIVE;
}

PublishWorkDecision DecidePublishWork(const PublishJobState& job,
	const PublishRuntimeState& runtime)
{
	PublishWorkDecision decision;
	if (runtime.stopping)
	{
		decision.action = PublishWorkAction::RETAIN_FOR_RELOAD;
		return decision;
	}
	if (AllTargetsTerminal(job))
	{
		decision.action = (KnownTargets(job.failed) & KnownTargets(job.targets)) != 0 ?
			PublishWorkAction::DEAD_LETTER : PublishWorkAction::CLEANUP;
		return decision;
	}
	if (!runtime.enabled || !runtime.acknowledged || runtime.paused)
		return decision;

	const unsigned int remaining = KnownTargets(job.targets) &
		~TerminalTargets(job);
	decision.deliveryTargets = remaining & KnownTargets(runtime.enabledTargets);
	if (decision.deliveryTargets != 0)
		decision.action = PublishWorkAction::DELIVER;
	return decision;
}

PublishPassState BeginPublishPass(const PublishJobState& job)
{
	PublishPassState pass;
	pass.job = job;
	return pass;
}

PublishTargetTransition RecordPublishTarget(const PublishPassState& pass,
	unsigned int target, bool succeeded)
{
	PublishTargetTransition transition;
	transition.pass = pass;
	const unsigned int outstanding = KnownTargets(pass.job.targets) &
		~TerminalTargets(pass.job) & ~pass.attemptedTargets;
	const unsigned int recordedTargets = KnownTargets(target) & outstanding;
	if (recordedTargets == 0)
		return transition;

	transition.pass.attemptedTargets |= recordedTargets;
	if (succeeded)
	{
		transition.pass.job.completed |= recordedTargets;
		transition.persistCompletedState = true;
	}
	else
	{
		transition.pass.failedTargets |= recordedTargets;
	}
	return transition;
}

PublishFinishPassDecision FinishPublishPass(const PublishPassState& pass)
{
	PublishFinishPassDecision decision;
	decision.job = pass.job;
	if (AllTargetsTerminal(decision.job))
	{
		decision.action = KnownTargets(decision.job.failed) != 0 ?
			PublishFinishPassAction::DEAD_LETTER : PublishFinishPassAction::CLEANUP;
		return decision;
	}

	const unsigned int relevantFailures = KnownTargets(pass.failedTargets) &
		KnownTargets(decision.job.targets) & ~TerminalTargets(decision.job);
	if (relevantFailures == 0)
		return decision;

	const unsigned int individualTargets[] =
	{
		PUBLISH_JOB_TARGET_STATIC,
		PUBLISH_JOB_TARGET_WEBHOOK
	};
	for (std::size_t index = 0; index < sizeof(individualTargets) / sizeof(individualTargets[0]); ++index)
	{
		const unsigned int target = individualTargets[index];
		if ((relevantFailures & target) == 0) continue;
		unsigned int& attempts = AttemptsForTarget(decision.job, target);
		if (attempts < PUBLISH_JOB_MAX_ATTEMPTS)
		{
			++attempts;
			decision.persistAttemptState = true;
		}
		if (attempts >= PUBLISH_JOB_MAX_ATTEMPTS)
		{
			if ((decision.job.failed & target) == 0)
				decision.persistAttemptState = true;
			decision.job.failed |= target;
		}
		else
		{
			decision.retryTargets |= target;
			const unsigned int delay = 1000u << (attempts - 1u);
			if (delay > decision.retryDelayMilliseconds)
				decision.retryDelayMilliseconds = delay;
		}
	}

	if (AllTargetsTerminal(decision.job))
		decision.action = KnownTargets(decision.job.failed) != 0 ?
			PublishFinishPassAction::DEAD_LETTER : PublishFinishPassAction::CLEANUP;
	else if (decision.retryTargets != 0)
		decision.action = PublishFinishPassAction::RETRY;
	return decision;
}

PublishTerminalDecision FinishPublishTerminal(bool fileActionSucceeded)
{
	PublishTerminalDecision decision;
	if (fileActionSucceeded)
	{
		decision.action = PublishTerminalAction::FINISH;
		decision.releasePendingId = true;
	}
	return decision;
}

} // namespace publishing
} // namespace pdw
