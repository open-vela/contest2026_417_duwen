#include "umca_internal.h"

int umca_publish(umca_context_t *ctx, umca_topic_id_t topic_id,
                 umca_devid_t destination, const uint8_t *payload,
                 uint16_t payload_length)
{
  umca_local_topic_t *topic;
  umca_frame_header_t header;
  size_t length;
  size_t mtu;
  int ret;
  if (ctx == NULL || ctx->state != UMCA_CONTEXT_RUNNING)
    {
      return UMCA_ERR_BAD_STATE;
    }
  if (ctx->state == UMCA_CONTEXT_ID_CONFLICT ||
      destination == UMCA_INVALID_DEVID ||
      (payload == NULL && payload_length != 0))
    {
      return UMCA_ERR_INVALID_ARG;
    }
  ret = umca_lock(ctx);
  if (ret != UMCA_OK)
    {
      return ret;
    }
  topic = umca_local_topic_find(ctx, topic_id);
  if (topic == NULL || (topic->direction & UMCA_TOPIC_PUBLISHER) == 0)
    {
      ret = UMCA_ERR_NOT_FOUND;
      goto out;
    }
  mtu = ctx->phy.ops->get_mtu(ctx->phy.driver);
  if (payload_length > UMCA_MAX_PAYLOAD ||
      (size_t)payload_length + UMCA_FRAME_OVERHEAD > mtu)
    {
      ret = UMCA_ERR_PAYLOAD_TOO_LARGE;
      goto out;
    }
  header.flags = 0;
  header.message_type = UMCA_MSG_DATA;
  header.qos = 0;
  header.ttl = UMCA_DEFAULT_TTL;
  header.destination = destination;
  header.source = ctx->local_dev_id;
  header.topic_id = topic_id;
  header.sequence = topic->next_tx_sequence;
  header.payload_length = payload_length;
  ret = umca_frame_encode(&header, payload, ctx->tx_frame,
                          sizeof(ctx->tx_frame), &length);
  if (ret != UMCA_OK)
    {
      UMCA_STAT(ctx, tx_errors);
      goto out;
    }
  ret = ctx->phy.ops->send(ctx->phy.driver, ctx->tx_frame, length);
  if (ret != UMCA_OK)
    {
      UMCA_STAT(ctx, tx_errors);
      ret = UMCA_ERR_PHY;
      goto out;
    }
  topic->next_tx_sequence++;
  UMCA_STAT(ctx, tx_frames);
  ret = UMCA_OK;
out:
  umca_unlock(ctx);
  return ret;
}
