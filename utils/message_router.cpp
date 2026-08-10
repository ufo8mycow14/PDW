#include "headers\message_router.h"

#include "headers\data_outputs.h"
#include "headers\message_archive_manager.h"
#include "headers\notification.h"
#include "headers\publishing.h"
#include "decoded_event.h"

void MessageRouterPublishDecodedMessage(const DecodedMessageNotificationContext& event)
{
	NotificationPublishDecodedMessage(event);
	pdw::publishing::PublishEvent routedEvent = pdw::events::BuildDecodedEvent(event);
	PublishingPublishEvent(routedEvent);
	DataOutputPublishEvent(routedEvent);
	// Directory aliases are local metadata. Annotate only the local archive
	// copy after all established external outputs received their unchanged
	// decoded-event schema.
	MessageArchiveAnnotateEvent(routedEvent);
	MessageArchivePublishEvent(routedEvent);
}
