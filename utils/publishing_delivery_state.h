#ifndef PDW_PUBLISHING_DELIVERY_STATE_H
#define PDW_PUBLISHING_DELIVERY_STATE_H

#include "publishing_job_store.h"

namespace pdw
{
namespace publishing
{

struct PublishRuntimeState
{
	bool enabled;
	bool acknowledged;
	bool paused;
	bool filteredOnly;
	bool stopping;
	unsigned int enabledTargets;

	PublishRuntimeState()
		: enabled(false), acknowledged(false), paused(false), filteredOnly(true),
		stopping(false), enabledTargets(0) {}
};

struct PublishJobState
{
	unsigned int targets;
	unsigned int completed;
	unsigned int failed;
	unsigned int staticAttempts;
	unsigned int webhookAttempts;

	PublishJobState()
		: targets(0), completed(0), failed(0), staticAttempts(0),
		webhookAttempts(0) {}
};

enum class PublishEnqueueAction
{
	ACTIVE,
	RETAIN_FOR_RELOAD
};

enum class PublishWorkAction
{
	HOLD,
	DELIVER,
	CLEANUP,
	DEAD_LETTER,
	RETAIN_FOR_RELOAD
};

struct PublishWorkDecision
{
	PublishWorkAction action;
	unsigned int deliveryTargets;

	PublishWorkDecision()
		: action(PublishWorkAction::HOLD), deliveryTargets(0) {}
};

struct PublishPassState
{
	PublishJobState job;
	unsigned int attemptedTargets;
	unsigned int failedTargets;

	PublishPassState() : attemptedTargets(0), failedTargets(0) {}
};

struct PublishTargetTransition
{
	PublishPassState pass;
	bool persistCompletedState;

	PublishTargetTransition() : persistCompletedState(false) {}
};

enum class PublishFinishPassAction
{
	HOLD,
	RETRY,
	CLEANUP,
	DEAD_LETTER
};

struct PublishFinishPassDecision
{
	PublishFinishPassAction action;
	PublishJobState job;
	bool persistAttemptState;
	unsigned int retryTargets;
	unsigned int retryDelayMilliseconds;

	PublishFinishPassDecision()
		: action(PublishFinishPassAction::HOLD), persistAttemptState(false),
		retryTargets(0), retryDelayMilliseconds(0) {}
};

enum class PublishTerminalAction
{
	FINISH,
	REQUEUE_PENDING
};

struct PublishTerminalDecision
{
	PublishTerminalAction action;
	bool releasePendingId;

	PublishTerminalDecision()
		: action(PublishTerminalAction::REQUEUE_PENDING), releasePendingId(false) {}
};

bool PublishShouldIntake(const PublishRuntimeState& runtime, bool eventFiltered);
PublishEnqueueAction DecidePublishEnqueue(bool stopping);
PublishWorkDecision DecidePublishWork(const PublishJobState& job,
	const PublishRuntimeState& runtime);
PublishPassState BeginPublishPass(const PublishJobState& job);
PublishTargetTransition RecordPublishTarget(const PublishPassState& pass,
	unsigned int target, bool succeeded);
PublishFinishPassDecision FinishPublishPass(const PublishPassState& pass);
PublishTerminalDecision FinishPublishTerminal(bool fileActionSucceeded);

} // namespace publishing
} // namespace pdw

#endif
