#ifndef UMCA_INTERNAL_H
#define UMCA_INTERNAL_H

#include "umca/umca.h"

int umca_send_system(umca_context_t *ctx, uint8_t type,
                     const uint8_t *payload, uint16_t length);
int umca_handle_frame(umca_context_t *ctx, const umca_frame_header_t *header,
                      const uint8_t *payload);
umca_local_topic_t *umca_local_topic_find(umca_context_t *ctx,
                                          umca_topic_id_t id);
int umca_sequence_accept(umca_context_t *ctx, umca_devid_t source,
                         umca_topic_id_t topic, umca_sequence_t sequence,
                         int *is_new_session);
void umca_sequence_clear_source(umca_context_t *ctx, umca_devid_t source);
#if UMCA_ENABLE_DISCOVERY
int umca_discovery_handle(umca_context_t *ctx,
                          const umca_frame_header_t *header,
                          const uint8_t *payload);
int umca_discovery_send_announce(umca_context_t *ctx);
int umca_discovery_send_heartbeat(umca_context_t *ctx);
int umca_discovery_send_teardown(umca_context_t *ctx, uint8_t reason);
void umca_discovery_tick(umca_context_t *ctx, uint32_t now);
#endif

#if UMCA_ENABLE_STATS
#  define UMCA_STAT(ctx, field) ((ctx)->stats.field++)
#else
#  define UMCA_STAT(ctx, field) do { (void)(ctx); } while (0)
#endif

static inline int umca_lock(umca_context_t *ctx)
{
#if UMCA_ENABLE_THREAD_SAFE
  return ctx->platform.ops->mutex_lock(ctx->platform.user,
                                       ctx->platform.mutex) == 0 ?
         UMCA_OK : UMCA_ERR_PHY;
#else
  (void)ctx;
  return UMCA_OK;
#endif
}

static inline void umca_unlock(umca_context_t *ctx)
{
#if UMCA_ENABLE_THREAD_SAFE
  (void)ctx->platform.ops->mutex_unlock(ctx->platform.user,
                                        ctx->platform.mutex);
#else
  (void)ctx;
#endif
}

#endif
