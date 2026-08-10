#ifndef PDW_MESSAGE_ROUTER_H
#define PDW_MESSAGE_ROUTER_H

#include "notification.h"

// All optional delivery and storage adapters enter through this function.
// Legacy decoding, filtering, display, logging, SMTP selection, and sound
// behavior remain authoritative in ShowMessage().
void MessageRouterPublishDecodedMessage(const DecodedMessageNotificationContext& event);

#endif
