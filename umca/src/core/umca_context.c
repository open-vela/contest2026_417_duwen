#include <string.h>

#include "umca_internal.h"

static int platform_valid(const umca_platform_t *platform)
{
  return platform != NULL && platform->ops != NULL &&
         platform->ops->time_ms != NULL;
}

static int phy_valid(const umca_phy_t *phy)
{
  return phy != NULL && phy->ops != NULL && phy->ops->init != NULL &&
         phy->ops->deinit != NULL && phy->ops->send != NULL &&
         phy->ops->poll != NULL && phy->ops->get_mtu != NULL;
}

int umca_init(umca_context_t *ctx, umca_devid_t local_dev_id,
              uint32_t local_boot_id, const umca_platform_t *platform,
              const umca_phy_t *phy)
{
  int ret;
  if (ctx == NULL || local_dev_id == UMCA_INVALID_DEVID ||
      local_boot_id == 0 || !platform_valid(platform) || !phy_valid(phy))
    {
      return UMCA_ERR_INVALID_ARG;
    }
  memset(ctx, 0, sizeof(*ctx));
  ctx->state = UMCA_CONTEXT_UNINITIALIZED;
  ctx->local_dev_id = local_dev_id;
  ctx->local_boot_id = local_boot_id;
  ctx->platform = *platform;
  ctx->phy = *phy;
  if (UMCA_ENABLE_THREAD_SAFE &&
      (platform->mutex == NULL || platform->ops->mutex_lock == NULL ||
       platform->ops->mutex_unlock == NULL))
    {
      return UMCA_ERR_INVALID_ARG;
    }
  ret = ctx->phy.ops->init(ctx->phy.driver);
  if (ret != UMCA_OK)
    {
      return UMCA_ERR_PHY;
    }
  if (ctx->phy.ops->get_mtu(ctx->phy.driver) < UMCA_FRAME_OVERHEAD)
    {
      ctx->phy.ops->deinit(ctx->phy.driver);
      return UMCA_ERR_PHY;
    }
  ctx->system_tx_sequence = 1;
  ctx->state = UMCA_CONTEXT_INITIALIZED;
  return UMCA_OK;
}

int umca_start(umca_context_t *ctx)
{
  int ret;
  if (ctx == NULL || ctx->state != UMCA_CONTEXT_INITIALIZED)
    {
      return UMCA_ERR_BAD_STATE;
    }
#if UMCA_ENABLE_DISCOVERY
  ret = umca_discovery_send_announce(ctx);
  if (ret != UMCA_OK)
    {
      return ret;
    }
  ctx->last_announce_ms = ctx->platform.ops->time_ms(ctx->platform.user);
  ctx->last_heartbeat_ms = ctx->last_announce_ms;
#else
  ret = UMCA_OK;
#endif
  if (ret == UMCA_OK)
    {
      ctx->state = UMCA_CONTEXT_RUNNING;
    }
  return ret;
}

int umca_stop(umca_context_t *ctx, uint8_t teardown_reason)
{
  int ret = UMCA_OK;
  if (ctx == NULL || (ctx->state != UMCA_CONTEXT_RUNNING &&
                      ctx->state != UMCA_CONTEXT_ID_CONFLICT))
    {
      return UMCA_ERR_BAD_STATE;
    }
#if UMCA_ENABLE_DISCOVERY
  if (ctx->state != UMCA_CONTEXT_ID_CONFLICT)
    {
      ret = umca_discovery_send_teardown(ctx, teardown_reason);
    }
#else
  (void)teardown_reason;
#endif
  ctx->state = UMCA_CONTEXT_STOPPED;
  return ret;
}

void umca_deinit(umca_context_t *ctx)
{
  if (ctx == NULL || ctx->state == UMCA_CONTEXT_UNINITIALIZED)
    {
      return;
    }
  if (ctx->state == UMCA_CONTEXT_RUNNING ||
      ctx->state == UMCA_CONTEXT_ID_CONFLICT)
    {
      (void)umca_stop(ctx, 0);
    }
  ctx->phy.ops->deinit(ctx->phy.driver);
  ctx->state = UMCA_CONTEXT_UNINITIALIZED;
}

umca_context_state_t umca_get_state(const umca_context_t *ctx)
{
  return ctx == NULL ? UMCA_CONTEXT_UNINITIALIZED : ctx->state;
}

#if UMCA_ENABLE_STATS
int umca_stats_get(const umca_context_t *ctx, umca_stats_t *out)
{
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
  *out = ctx->stats;
  umca_unlock((umca_context_t *)ctx);
  return UMCA_OK;
}

void umca_stats_reset(umca_context_t *ctx)
{
  if (ctx != NULL)
    {
      if (umca_lock(ctx) != UMCA_OK)
        {
          return;
        }
      memset(&ctx->stats, 0, sizeof(ctx->stats));
      umca_unlock(ctx);
    }
}
#endif
