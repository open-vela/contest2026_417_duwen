#include <string.h>

#include "umca/umca.h"

static umca_local_topic_t *find_topic(umca_context_t *ctx,
                                      umca_topic_id_t id)
{
  size_t i;
  for (i = 0; i < UMCA_MAX_TOPICS; i++)
    {
      if (ctx->topics[i].used && ctx->topics[i].topic_id == id)
        {
          return &ctx->topics[i];
        }
    }
  return NULL;
}

int umca_topic_register(umca_context_t *ctx, const char *name,
                        uint8_t direction, umca_topic_callback_t callback,
                        void *user_data, umca_topic_id_t *topic_id_out)
{
  size_t length;
  size_t i;
  umca_topic_id_t id;
  umca_local_topic_t *topic;
  if (ctx == NULL || name == NULL || ctx->state != UMCA_CONTEXT_INITIALIZED ||
      direction == 0 || (direction & ~(UMCA_TOPIC_PUBLISHER |
      UMCA_TOPIC_SUBSCRIBER)) != 0 ||
      ((direction & UMCA_TOPIC_SUBSCRIBER) != 0 && callback == NULL))
    {
      return UMCA_ERR_INVALID_ARG;
    }
  length = strlen(name);
  if (umca_topic_id_from_name(name, length, &id) != UMCA_OK)
    {
      return UMCA_ERR_INVALID_ARG;
    }
  topic = find_topic(ctx, id);
  if (topic != NULL)
    {
      if (topic->name_length != length ||
          memcmp(topic->name, name, length) != 0)
        {
          return UMCA_ERR_TOPIC_COLLISION;
        }
      if ((direction & UMCA_TOPIC_SUBSCRIBER) != 0 &&
          topic->callback != NULL && (topic->callback != callback ||
          topic->callback_user != user_data))
        {
          return UMCA_ERR_INVALID_ARG;
        }
      topic->direction |= direction;
      if (topic->callback == NULL)
        {
          topic->callback = callback;
          topic->callback_user = user_data;
        }
      if (topic_id_out != NULL)
        {
          *topic_id_out = id;
        }
      return UMCA_OK;
    }
  for (i = 0; i < UMCA_MAX_TOPICS; i++)
    {
      if (!ctx->topics[i].used)
        {
          topic = &ctx->topics[i];
          memset(topic, 0, sizeof(*topic));
          topic->used = true;
          topic->topic_id = id;
          topic->name = name;
          topic->name_length = (uint8_t)length;
          topic->direction = direction;
          topic->next_tx_sequence = 1;
          topic->callback = callback;
          topic->callback_user = user_data;
          if (topic_id_out != NULL)
            {
              *topic_id_out = id;
            }
          return UMCA_OK;
        }
    }
  return UMCA_ERR_CAPACITY;
}

umca_local_topic_t *umca_local_topic_find(umca_context_t *ctx,
                                          umca_topic_id_t id)
{
  return find_topic(ctx, id);
}
