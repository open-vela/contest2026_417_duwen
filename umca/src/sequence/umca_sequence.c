#include "umca_internal.h"

void umca_sequence_clear_source(umca_context_t *ctx, umca_devid_t source)
{
#if UMCA_ENABLE_RX_SEQUENCE_TRACKING
  unsigned int i;
  for (i = 0; i < UMCA_MAX_RX_SEQUENCE_TRACKS; i++)
    {
      if (ctx->rx_sequences[i].used && ctx->rx_sequences[i].source == source)
        {
          ctx->rx_sequences[i].used = false;
        }
    }
#else
  (void)ctx;
  (void)source;
#endif
}

int umca_sequence_accept(umca_context_t *ctx, umca_devid_t source,
                         umca_topic_id_t topic, umca_sequence_t sequence,
                         int *is_new_session)
{
#if UMCA_ENABLE_RX_SEQUENCE_TRACKING
  unsigned int i;
  umca_rx_sequence_entry_t *entry = NULL;
  (void)is_new_session;
  for (i = 0; i < UMCA_MAX_RX_SEQUENCE_TRACKS; i++)
    {
      if (ctx->rx_sequences[i].used && ctx->rx_sequences[i].source == source &&
          ctx->rx_sequences[i].topic_id == topic)
        {
          entry = &ctx->rx_sequences[i];
          break;
        }
    }
  if (entry == NULL)
    {
      for (i = 0; i < UMCA_MAX_RX_SEQUENCE_TRACKS; i++)
        {
          if (!ctx->rx_sequences[i].used)
            {
              entry = &ctx->rx_sequences[i];
              entry->used = true;
              entry->source = source;
              entry->topic_id = topic;
              entry->initialized = false;
              break;
            }
        }
    }
  if (entry == NULL)
    {
      UMCA_STAT(ctx, capacity_errors);
      return UMCA_ERR_CAPACITY;
    }
  if (!entry->initialized)
    {
      entry->initialized = true;
      entry->last = sequence;
      return UMCA_OK;
    }
  {
    uint32_t delta = sequence - entry->last;
    if (delta == 0)
      {
        UMCA_STAT(ctx, rx_duplicates);
        return UMCA_ERR_DUPLICATE;
      }
    if (delta >= UINT32_C(0x80000000))
      {
        UMCA_STAT(ctx, rx_stale);
        return UMCA_ERR_STALE;
      }
    if (delta > 1)
      {
#if UMCA_ENABLE_STATS
        ctx->stats.rx_estimated_lost += delta - 1;
#endif
      }
    entry->last = sequence;
  }
  return UMCA_OK;
#else
  (void)ctx;
  (void)source;
  (void)topic;
  (void)sequence;
  (void)is_new_session;
  return UMCA_OK;
#endif
}
