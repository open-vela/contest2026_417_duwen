#ifndef UMCA_H
#define UMCA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "umca_config.h"
#include "umca_error.h"
#include "umca_platform.h"
#include "umca_phy.h"
#include "umca_protocol.h"
#include "umca_stats.h"

typedef enum
{
  UMCA_CONTEXT_UNINITIALIZED = 0,
  UMCA_CONTEXT_INITIALIZED,
  UMCA_CONTEXT_RUNNING,
  UMCA_CONTEXT_ID_CONFLICT,
  UMCA_CONTEXT_STOPPED
} umca_context_state_t;

typedef enum
{
  UMCA_NODE_UNKNOWN = 0,
  UMCA_NODE_ONLINE,
  UMCA_NODE_STALE,
  UMCA_NODE_OFFLINE
} umca_node_state_t;

enum
{
  UMCA_TOPIC_PUBLISHER = 1u << 0,
  UMCA_TOPIC_SUBSCRIBER = 1u << 1
};

typedef struct umca_context umca_context_t;

typedef struct
{
  uint16_t flags;
  uint8_t message_type;
  uint8_t qos;
  uint8_t ttl;
  umca_devid_t destination;
  umca_devid_t source;
  umca_topic_id_t topic_id;
  umca_sequence_t sequence;
  const uint8_t *payload;
  uint16_t payload_length;
} umca_message_t;

typedef void (*umca_topic_callback_t)(umca_context_t *ctx,
                                      const umca_message_t *message,
                                      void *user_data);

#if UMCA_ENABLE_DISCOVERY
typedef struct
{
  umca_topic_id_t topic_id;
  uint8_t direction;
  uint8_t name_length;
  char name[UMCA_MAX_TOPIC_NAME + 1u];
} umca_node_topic_info_t;

typedef struct
{
  umca_devid_t dev_id;
  uint32_t boot_id;
  umca_node_state_t state;
  uint32_t capabilities;
  uint32_t last_seen_ms;
  uint32_t state_changed_ms;
  uint32_t last_uptime_ms;
  uint8_t remote_status;
  uint8_t topic_count;
  umca_node_topic_info_t topics[UMCA_MAX_TOPICS_PER_NODE];
} umca_node_info_t;
#endif

typedef struct
{
  bool used;
  umca_topic_id_t topic_id;
  const char *name;
  uint8_t name_length;
  uint8_t direction;
  umca_sequence_t next_tx_sequence;
  umca_topic_callback_t callback;
  void *callback_user;
} umca_local_topic_t;

#if UMCA_ENABLE_DISCOVERY
typedef struct
{
  bool used;
  umca_topic_id_t topic_id;
  uint8_t direction;
  uint8_t name_length;
  char name[UMCA_MAX_TOPIC_NAME + 1u];
} umca_remote_topic_t;

typedef struct
{
  bool used;
  umca_devid_t dev_id;
  uint32_t boot_id;
  umca_node_state_t state;
  uint32_t capabilities;
  uint32_t last_seen_ms;
  uint32_t state_changed_ms;
  uint32_t last_uptime_ms;
  uint8_t remote_status;
  uint8_t topic_count;
  umca_remote_topic_t topics[UMCA_MAX_TOPICS_PER_NODE];
} umca_remote_node_t;
#endif

#if UMCA_ENABLE_RX_SEQUENCE_TRACKING
typedef struct
{
  bool used;
  umca_devid_t source;
  umca_topic_id_t topic_id;
  bool initialized;
  umca_sequence_t last;
} umca_rx_sequence_entry_t;
#endif

struct umca_context
{
  umca_context_state_t state;
  umca_devid_t local_dev_id;
  uint32_t local_boot_id;
  umca_platform_t platform;
  umca_phy_t phy;
  uint32_t last_heartbeat_ms;
  uint32_t last_announce_ms;
  umca_sequence_t system_tx_sequence;
  uint8_t dispatch_depth;
  umca_local_topic_t topics[UMCA_MAX_TOPICS];
#if UMCA_ENABLE_DISCOVERY
  umca_remote_node_t nodes[UMCA_MAX_NODES];
#endif
#if UMCA_ENABLE_RX_SEQUENCE_TRACKING
  umca_rx_sequence_entry_t rx_sequences[UMCA_MAX_RX_SEQUENCE_TRACKS];
#endif
#if UMCA_ENABLE_STATS
  umca_stats_t stats;
#endif
  uint8_t rx_frame[UMCA_MAX_FRAME];
  uint8_t tx_frame[UMCA_MAX_FRAME];
};

int umca_init(umca_context_t *ctx, umca_devid_t local_dev_id,
              uint32_t local_boot_id, const umca_platform_t *platform,
              const umca_phy_t *phy);
int umca_start(umca_context_t *ctx);
int umca_stop(umca_context_t *ctx, uint8_t teardown_reason);
void umca_deinit(umca_context_t *ctx);
int umca_poll(umca_context_t *ctx, uint16_t *processed_frames);
int umca_publish(umca_context_t *ctx, umca_topic_id_t topic_id,
                 umca_devid_t destination, const uint8_t *payload,
                 uint16_t payload_length);
int umca_topic_register(umca_context_t *ctx, const char *name,
                        uint8_t direction, umca_topic_callback_t callback,
                        void *user_data, umca_topic_id_t *topic_id_out);
int umca_topic_validate(const char *name, size_t length);
int umca_topic_id_from_name(const char *name, size_t length,
                            umca_topic_id_t *out);
umca_context_state_t umca_get_state(const umca_context_t *ctx);

#if UMCA_ENABLE_DISCOVERY
int umca_node_get(const umca_context_t *ctx, umca_devid_t dev_id,
                  umca_node_info_t *out);
size_t umca_node_count(const umca_context_t *ctx,
                       umca_node_state_t minimum_state);
#endif

#if UMCA_ENABLE_STATS
int umca_stats_get(const umca_context_t *ctx, umca_stats_t *out);
void umca_stats_reset(umca_context_t *ctx);
#endif

#endif
