#include "umca_internal.h"

static void dispatch_data(umca_context_t *ctx,
                          const umca_frame_header_t *header,
                          const uint8_t *payload)
{
  umca_local_topic_t *topic;
  umca_topic_callback_t callback;
  void *user;
  umca_message_t message;
  if (header->destination != ctx->local_dev_id &&
      header->destination != UMCA_BROADCAST_DEVID)
    {
      return;
    }
  if (header->source == ctx->local_dev_id)
    {
      return;
    }
  topic = umca_local_topic_find(ctx, header->topic_id);
  if (topic == NULL || (topic->direction & UMCA_TOPIC_SUBSCRIBER) == 0 ||
      topic->callback == NULL)
    {
      UMCA_STAT(ctx, rx_unsubscribed);
      return;
    }
  callback = topic->callback;
  user = topic->callback_user;
  message.flags = header->flags;
  message.message_type = header->message_type;
  message.qos = header->qos;
  message.ttl = header->ttl;
  message.destination = header->destination;
  message.source = header->source;
  message.topic_id = header->topic_id;
  message.sequence = header->sequence;
  message.payload = payload;
  message.payload_length = header->payload_length;
  ctx->dispatch_depth++;
  callback(ctx, &message, user);
  ctx->dispatch_depth--;
  UMCA_STAT(ctx, rx_delivered);
}

int umca_handle_frame(umca_context_t *ctx, const umca_frame_header_t *header,
                      const uint8_t *payload)
{
  int ret;
  if (header->message_type == UMCA_MSG_DATA)
    {
      ret = umca_sequence_accept(ctx, header->source, header->topic_id,
                                 header->sequence, NULL);
      if (ret != UMCA_OK)
        {
          return ret;
        }
      dispatch_data(ctx, header, payload);
      return UMCA_OK;
    }
#if UMCA_ENABLE_DISCOVERY
  return umca_discovery_handle(ctx, header, payload);
#else
  (void)payload;
  return UMCA_ERR_UNSUPPORTED_TYPE;
#endif
}
