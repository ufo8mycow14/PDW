#include "headers\message_router.h"

#include "headers\data_outputs.h"
#include "headers\notification.h"
#include "headers\publishing.h"
#include "decoded_event.h"

void MessageRouterPublishDecodedMessage(const DecodedMessageNotificationContext& event)
{
	NotificationPublishDecodedMessage(event);
	const pdw::publishing::PublishEvent routedEvent = pdw::events::BuildDecodedEvent(event);
	PublishingPublishEvent(routedEvent);
	DataOutputPublishEvent(routedEvent);
}
