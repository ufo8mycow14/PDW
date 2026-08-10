#include "headers\message_router.h"

#include "headers\notification.h"
#include "headers\publishing.h"

void MessageRouterPublishDecodedMessage(const DecodedMessageNotificationContext& event)
{
	NotificationPublishDecodedMessage(event);
	PublishingPublishDecodedMessage(event);
}
