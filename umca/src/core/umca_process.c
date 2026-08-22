#include "umca_internal.h"

int umca_poll(umca_context_t *ctx, uint16_t *processed_frames)
{
  unsigned int count = 0;
  int first_error = UMCA_OK;
  if (ctx == NULL || ctx->state != UMCA_CONTEXT_RUNNING)
    {
      return UMCA_ERR_BAD_STATE;
    }
  while (count < UMCA_POLL_RX_BUDGET)
    {
      size_t received = 0;
      umca_frame_header_t header;
      const uint8_t *payload;
      int ret = ctx->phy.ops->poll(ctx->phy.driver, ctx->rx_frame,
                                   sizeof(ctx->rx_frame), &received);
      if (ret != UMCA_OK)
        {
          return UMCA_ERR_PHY;
        }
      if (received == 0)
        {
          break;
        }
      count++;
      UMCA_STAT(ctx, rx_frames);
      ret = umca_frame_decode(ctx->rx_frame, received, &header, &payload);
      if (ret == UMCA_OK)
        {
          ret = umca_handle_frame(ctx, &header, payload);
        }
      if (ret != UMCA_OK && first_error == UMCA_OK)
        {
          first_error = ret;
          if (ret == UMCA_ERR_CRC)
            {
              UMCA_STAT(ctx, rx_crc_errors);
            }
          else if (ret == UMCA_ERR_LENGTH_MISMATCH ||
                   ret == UMCA_ERR_PAYLOAD_TOO_LARGE)
            {
              UMCA_STAT(ctx, rx_length_errors);
            }
          else if (ret == UMCA_ERR_UNSUPPORTED_VERSION)
            {
              UMCA_STAT(ctx, rx_version_errors);
            }
          else if (ret != UMCA_ERR_DUPLICATE && ret != UMCA_ERR_STALE)
            {
              UMCA_STAT(ctx, rx_semantic_errors);
            }
        }
    }
  if (processed_frames != NULL)
    {
      *processed_frames = (uint16_t)count;
    }
#if UMCA_ENABLE_DISCOVERY
  umca_discovery_tick(ctx, ctx->platform.ops->time_ms(ctx->platform.user));
#endif
  return first_error;
}
