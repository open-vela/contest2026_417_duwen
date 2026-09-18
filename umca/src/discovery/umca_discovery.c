#include <string.h>

#include "umca_internal.h"

static umca_remote_node_t *node_find(umca_context_t *ctx, umca_devid_t id)
{
  unsigned int i;
  for (i = 0; i < UMCA_MAX_NODES; i++)
    {
      if (ctx->nodes[i].used && ctx->nodes[i].dev_id == id)
        {
          return &ctx->nodes[i];
        }
    }
  return NULL;
}

static umca_remote_node_t *node_alloc(umca_context_t *ctx, uint32_t now)
{
  unsigned int i;
  umca_remote_node_t *candidate = NULL;
  uint32_t oldest = 0;
  for (i = 0; i < UMCA_MAX_NODES; i++)
    {
      if (!ctx->nodes[i].used)
        {
          return &ctx->nodes[i];
        }
      if (ctx->nodes[i].state == UMCA_NODE_OFFLINE)
        {
          uint32_t elapsed = now - ctx->nodes[i].state_changed_ms;
          if (candidate == NULL || elapsed > oldest)
            {
              candidate = &ctx->nodes[i];
              oldest = elapsed;
            }
        }
    }
  return candidate;
}

static int announced_topics_collide(const umca_context_t *ctx,
                                    umca_devid_t source,
                                    const umca_remote_topic_t *topics,
                                    uint8_t count)
{
  unsigned int i;
  uint8_t j;
  for (j = 0; j < count; j++)
    {
      for (i = 0; i < UMCA_MAX_TOPICS; i++)
        {
          const umca_local_topic_t *local = &ctx->topics[i];
          if (local->used && local->topic_id == topics[j].topic_id &&
              (local->name_length != topics[j].name_length ||
               memcmp(local->name, topics[j].name, topics[j].name_length) != 0))
            {
              return UMCA_ERR_TOPIC_COLLISION;
            }
        }
      for (i = 0; i < UMCA_MAX_NODES; i++)
        {
          const umca_remote_node_t *node = &ctx->nodes[i];
          uint8_t k;
          if (!node->used || node->dev_id == source)
            {
              continue;
            }
          for (k = 0; k < node->topic_count; k++)
            {
              const umca_remote_topic_t *known = &node->topics[k];
              if (known->used && known->topic_id == topics[j].topic_id &&
                  (known->name_length != topics[j].name_length ||
                   memcmp(known->name, topics[j].name,
                          topics[j].name_length) != 0))
                {
                  return UMCA_ERR_TOPIC_COLLISION;
                }
            }
        }
    }
  return UMCA_OK;
}

static int parse_announce(const uint8_t *payload, uint16_t length,
                          uint32_t *capabilities, uint32_t *boot_id,
                          uint8_t *count, umca_remote_topic_t *topics)
{
  uint16_t offset = 0;
  uint8_t i;
  memset(topics, 0, sizeof(umca_remote_topic_t) * UMCA_MAX_TOPICS_PER_NODE);
  if (payload == NULL || length < 9)
    {
      return UMCA_ERR_BAD_PAYLOAD;
    }
  *capabilities = umca_read_be32(payload);
  *boot_id = umca_read_be32(payload + 4);
  *count = payload[8];
  if ((*capabilities & ~UINT32_C(1)) != 0 || *boot_id == 0 ||
      *count > UMCA_MAX_TOPICS_PER_NODE)
    {
      return UMCA_ERR_BAD_PAYLOAD;
    }
  offset = 9;
  for (i = 0; i < *count; i++)
    {
      uint8_t name_length;
      umca_topic_id_t expected;
      if ((uint16_t)(offset + 6) > length)
        {
          return UMCA_ERR_BAD_PAYLOAD;
        }
      topics[i].topic_id = umca_read_be32(payload + offset);
      topics[i].direction = payload[offset + 4];
      name_length = payload[offset + 5];
      offset = (uint16_t)(offset + 6);
      if (topics[i].direction == 0 ||
          (topics[i].direction & ~(UMCA_TOPIC_PUBLISHER |
          UMCA_TOPIC_SUBSCRIBER)) != 0 || name_length == 0 ||
          name_length > UMCA_MAX_TOPIC_NAME ||
          (uint16_t)(offset + name_length) > length)
        {
          return UMCA_ERR_BAD_PAYLOAD;
        }
      if (umca_topic_id_from_name((const char *)(payload + offset),
                                  name_length, &expected) != UMCA_OK ||
          expected != topics[i].topic_id)
        {
          return UMCA_ERR_BAD_PAYLOAD;
        }
      topics[i].name_length = name_length;
      memcpy(topics[i].name, payload + offset, name_length);
      topics[i].name[name_length] = '\0';
      topics[i].used = true;
      offset = (uint16_t)(offset + name_length);
    }
  return offset == length ? UMCA_OK : UMCA_ERR_BAD_PAYLOAD;
}

int umca_send_system(umca_context_t *ctx, uint8_t type,
                     const uint8_t *payload, uint16_t length)
{
  umca_frame_header_t header;
  size_t frame_length;
  int ret;
  header.flags = 0;
  header.message_type = type;
  header.qos = 0;
  header.ttl = UMCA_DEFAULT_TTL;
  header.destination = UMCA_BROADCAST_DEVID;
  header.source = ctx->local_dev_id;
  header.topic_id = UMCA_SYSTEM_TOPIC_ID;
  header.sequence = ctx->system_tx_sequence;
  header.payload_length = length;
  ret = umca_frame_encode(&header, payload, ctx->tx_frame,
                          sizeof(ctx->tx_frame), &frame_length);
  if (ret != UMCA_OK)
    {
      return ret;
    }
  if (ctx->phy.ops->send(ctx->phy.driver, ctx->tx_frame, frame_length) !=
      UMCA_OK)
    {
      UMCA_STAT(ctx, tx_errors);
      return UMCA_ERR_PHY;
    }
  ctx->system_tx_sequence++;
  UMCA_STAT(ctx, tx_frames);
  return UMCA_OK;
}

int umca_discovery_send_announce(umca_context_t *ctx)
{
  uint8_t payload[UMCA_MAX_PAYLOAD];
  uint16_t offset = 9;
  uint8_t count = 0;
  unsigned int i;
  int ret;
  umca_write_be32(payload, 1);
  umca_write_be32(payload + 4, ctx->local_boot_id);
  for (i = 0; i < UMCA_MAX_TOPICS; i++)
    {
      umca_local_topic_t *topic = &ctx->topics[i];
      if (topic->used && count < UMCA_MAX_TOPICS_PER_NODE)
        {
          if ((uint16_t)(offset + 6 + topic->name_length) >
              UMCA_MAX_PAYLOAD)
            {
              return UMCA_ERR_PAYLOAD_TOO_LARGE;
            }
          umca_write_be32(payload + offset, topic->topic_id);
          payload[offset + 4] = topic->direction;
          payload[offset + 5] = topic->name_length;
          memcpy(payload + offset + 6, topic->name, topic->name_length);
          offset = (uint16_t)(offset + 6 + topic->name_length);
          count++;
        }
      else if (topic->used)
        {
          return UMCA_ERR_PAYLOAD_TOO_LARGE;
        }
    }
  payload[8] = count;
  ret = umca_send_system(ctx, UMCA_MSG_ANNOUNCE, payload, offset);
  return ret;
}

int umca_discovery_send_heartbeat(umca_context_t *ctx)
{
  uint8_t payload[9];
  uint32_t now = ctx->platform.ops->time_ms(ctx->platform.user);
  umca_write_be32(payload, ctx->local_boot_id);
  umca_write_be32(payload + 4, now);
  payload[8] = 0;
  return umca_send_system(ctx, UMCA_MSG_HEARTBEAT, payload, sizeof(payload));
}

int umca_discovery_send_teardown(umca_context_t *ctx, uint8_t reason)
{
  uint8_t payload[5];
  umca_write_be32(payload, ctx->local_boot_id);
  payload[4] = reason;
  return umca_send_system(ctx, UMCA_MSG_TEARDOWN, payload, sizeof(payload));
}

int umca_discovery_handle(umca_context_t *ctx,
                          const umca_frame_header_t *header,
                          const uint8_t *payload)
{
  umca_remote_node_t *node;
  uint32_t now = ctx->platform.ops->time_ms(ctx->platform.user);
  uint32_t capabilities;
  uint32_t boot_id;
  uint8_t count;
  int ret;
  if (header->source == ctx->local_dev_id)
    {
      return UMCA_OK;
    }
  if (header->message_type == UMCA_MSG_ANNOUNCE)
    {
      umca_remote_topic_t topics[UMCA_MAX_TOPICS_PER_NODE];
      ret = parse_announce(payload, header->payload_length, &capabilities,
                           &boot_id, &count, topics);
      if (ret != UMCA_OK)
        {
          UMCA_STAT(ctx, rx_semantic_errors);
          return ret;
        }
      ret = announced_topics_collide(ctx, header->source, topics, count);
      if (ret != UMCA_OK)
        {
          UMCA_STAT(ctx, rx_semantic_errors);
          return ret;
        }
      node = node_find(ctx, header->source);
      if (node == NULL)
        {
          node = node_alloc(ctx, now);
          if (node == NULL)
            {
              UMCA_STAT(ctx, capacity_errors);
              return UMCA_ERR_CAPACITY;
            }
          memset(node, 0, sizeof(*node));
          node->used = true;
          node->dev_id = header->source;
        }
      if (node->boot_id != 0 && node->boot_id != boot_id)
        {
          umca_sequence_clear_source(ctx, header->source);
        }
      ret = umca_sequence_accept(ctx, header->source, UMCA_SYSTEM_TOPIC_ID,
                                 header->sequence, NULL);
      if (ret != UMCA_OK)
        {
          return ret;
        }
      memcpy(node->topics, topics, sizeof(topics));
      node->boot_id = boot_id;
      node->capabilities = capabilities;
      node->topic_count = count;
      node->last_seen_ms = now;
      node->state_changed_ms = now;
      if (node->state != UMCA_NODE_ONLINE)
        {
          UMCA_STAT(ctx, node_online_events);
        }
      node->state = UMCA_NODE_ONLINE;
      return UMCA_OK;
    }
  node = node_find(ctx, header->source);
  if (node == NULL || node->boot_id == 0)
    {
      return UMCA_OK;
    }
  if (header->message_type == UMCA_MSG_HEARTBEAT)
    {
      if (header->payload_length != 9 ||
          umca_read_be32(payload) != node->boot_id)
        {
          return UMCA_OK;
        }
      ret = umca_sequence_accept(ctx, header->source, UMCA_SYSTEM_TOPIC_ID,
                                 header->sequence, NULL);
      if (ret != UMCA_OK)
        {
          return ret;
        }
      node->last_uptime_ms = umca_read_be32(payload + 4);
      node->remote_status = payload[8];
      node->last_seen_ms = now;
      node->state = UMCA_NODE_ONLINE;
      return UMCA_OK;
    }
  if (header->message_type == UMCA_MSG_TEARDOWN)
    {
      if (header->payload_length != 5 ||
          umca_read_be32(payload) != node->boot_id)
        {
          return UMCA_OK;
        }
      ret = umca_sequence_accept(ctx, header->source, UMCA_SYSTEM_TOPIC_ID,
                                 header->sequence, NULL);
      if (ret != UMCA_OK)
        {
          return ret;
        }
      node->remote_status = payload[4];
      node->state = UMCA_NODE_OFFLINE;
      node->state_changed_ms = now;
      umca_sequence_clear_source(ctx, header->source);
      UMCA_STAT(ctx, node_offline_events);
      return UMCA_OK;
    }
  return UMCA_ERR_UNSUPPORTED_TYPE;
}

void umca_discovery_tick(umca_context_t *ctx, uint32_t now)
{
  unsigned int i;
  if ((uint32_t)(now - ctx->last_heartbeat_ms) >= 5000u)
    {
      (void)umca_discovery_send_heartbeat(ctx);
      ctx->last_heartbeat_ms = now;
    }
  if ((uint32_t)(now - ctx->last_announce_ms) >= 30000u)
    {
      (void)umca_discovery_send_announce(ctx);
      ctx->last_announce_ms = now;
    }
  for (i = 0; i < UMCA_MAX_NODES; i++)
    {
      umca_remote_node_t *node = &ctx->nodes[i];
      if (!node->used || node->state == UMCA_NODE_OFFLINE)
        {
          continue;
        }
      if ((uint32_t)(now - node->last_seen_ms) >= 15000u)
        {
          node->state = UMCA_NODE_OFFLINE;
          node->state_changed_ms = now;
          umca_sequence_clear_source(ctx, node->dev_id);
          UMCA_STAT(ctx, node_offline_events);
        }
      else if ((uint32_t)(now - node->last_seen_ms) >= 10000u)
        {
          node->state = UMCA_NODE_STALE;
        }
    }
}

int umca_node_get(const umca_context_t *ctx, umca_devid_t dev_id,
                  umca_node_info_t *out)
{
  unsigned int i;
  uint8_t j;
  int ret;
  if (ctx == NULL || out == NULL)
    {
      return UMCA_ERR_INVALID_ARG;
    }
  ret = umca_lock((umca_context_t *)ctx);
  if (ret != UMCA_OK)
    {
      return ret;
    }
  for (i = 0; i < UMCA_MAX_NODES; i++)
    {
      if (ctx->nodes[i].used && ctx->nodes[i].dev_id == dev_id)
        {
          memset(out, 0, sizeof(*out));
          out->dev_id = ctx->nodes[i].dev_id;
          out->boot_id = ctx->nodes[i].boot_id;
          out->state = ctx->nodes[i].state;
          out->capabilities = ctx->nodes[i].capabilities;
          out->last_seen_ms = ctx->nodes[i].last_seen_ms;
          out->state_changed_ms = ctx->nodes[i].state_changed_ms;
          out->last_uptime_ms = ctx->nodes[i].last_uptime_ms;
          out->remote_status = ctx->nodes[i].remote_status;
          out->topic_count = ctx->nodes[i].topic_count;
          for (j = 0; j < out->topic_count; j++)
            {
              out->topics[j].topic_id = ctx->nodes[i].topics[j].topic_id;
              out->topics[j].direction = ctx->nodes[i].topics[j].direction;
              out->topics[j].name_length =
                ctx->nodes[i].topics[j].name_length;
              memcpy(out->topics[j].name, ctx->nodes[i].topics[j].name,
                     (size_t)out->topics[j].name_length + 1u);
            }
          umca_unlock((umca_context_t *)ctx);
          return UMCA_OK;
        }
    }
  umca_unlock((umca_context_t *)ctx);
  return UMCA_ERR_NOT_FOUND;
}

size_t umca_node_count(const umca_context_t *ctx,
                       umca_node_state_t minimum_state)
{
  size_t count = 0;
  unsigned int i;
  int ret;
  if (ctx == NULL)
    {
      return 0;
    }
  ret = umca_lock((umca_context_t *)ctx);
  if (ret != UMCA_OK)
    {
      return 0;
    }
  for (i = 0; i < UMCA_MAX_NODES; i++)
    {
      if (ctx->nodes[i].used && ctx->nodes[i].state >= minimum_state)
        {
          count++;
        }
    }
  umca_unlock((umca_context_t *)ctx);
  return count;
}
