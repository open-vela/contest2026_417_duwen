#ifndef UMCA_STATS_H
#define UMCA_STATS_H

#include <stdint.h>

typedef struct
{
  uint32_t tx_frames;
  uint32_t tx_errors;
  uint32_t rx_frames;
  uint32_t rx_delivered;
  uint32_t rx_crc_errors;
  uint32_t rx_length_errors;
  uint32_t rx_version_errors;
  uint32_t rx_semantic_errors;
  uint32_t rx_duplicates;
  uint32_t rx_stale;
  uint32_t rx_estimated_lost;
  uint32_t rx_unsubscribed;
  uint32_t node_online_events;
  uint32_t node_offline_events;
  uint32_t capacity_errors;
} umca_stats_t;

#endif
