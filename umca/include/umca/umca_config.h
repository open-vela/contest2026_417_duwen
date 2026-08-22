#ifndef UMCA_CONFIG_H
#define UMCA_CONFIG_H

#include <stdint.h>

#if (defined(UMCA_PROFILE_MINIMAL) + defined(UMCA_PROFILE_DISCOVERY) + defined(UMCA_PROFILE_CONTEST)) != 1
#  error "Select exactly one UMCA profile"
#endif

#define UMCA_ENABLE_PUBSUB 1
#define UMCA_ENABLE_FRAGMENTATION 0
#define UMCA_ENABLE_RPC 0
#define UMCA_ENABLE_QOS1 0
#define UMCA_ENABLE_QOS2 0
#define UMCA_ENABLE_QOS3 0
#define UMCA_ENABLE_DEBUG 0
#define UMCA_ENABLE_ACL 0
#define UMCA_ENABLE_ENCRYPTION 0
#define UMCA_ENABLE_CHANNEL_BONDING 0
#define UMCA_USE_DYNAMIC_MEMORY 0

#if defined(UMCA_PROFILE_MINIMAL)
#  define UMCA_ENABLE_DISCOVERY 0
#  define UMCA_ENABLE_RX_SEQUENCE_TRACKING 0
#  define UMCA_ENABLE_REMOTE_TOPIC_NAMES 0
#  define UMCA_ENABLE_STATS 0
#  define UMCA_ENABLE_THREAD_SAFE 0
#  define UMCA_MAX_NODES 0u
#  define UMCA_MAX_TOPICS_PER_NODE 0u
#  define UMCA_MAX_RX_SEQUENCE_TRACKS 0u
#elif defined(UMCA_PROFILE_DISCOVERY) || defined(UMCA_PROFILE_CONTEST)
#  define UMCA_ENABLE_DISCOVERY 1
#  define UMCA_ENABLE_RX_SEQUENCE_TRACKING 1
#  define UMCA_ENABLE_REMOTE_TOPIC_NAMES 1
#  define UMCA_ENABLE_STATS 1
#  if defined(UMCA_PROFILE_CONTEST)
#    define UMCA_ENABLE_THREAD_SAFE 1
#  else
#    define UMCA_ENABLE_THREAD_SAFE 0
#  endif
#  define UMCA_MAX_NODES 8u
#  define UMCA_MAX_TOPICS_PER_NODE 8u
#  define UMCA_MAX_RX_SEQUENCE_TRACKS 16u
#endif

#ifndef UMCA_MAX_PAYLOAD
#  define UMCA_MAX_PAYLOAD 256u
#endif
#ifndef UMCA_MAX_FRAME
#  define UMCA_MAX_FRAME (40u + UMCA_MAX_PAYLOAD)
#endif
#ifndef UMCA_MAX_TOPICS
#  define UMCA_MAX_TOPICS 16u
#endif
#ifndef UMCA_RX_QUEUE_DEPTH
#  define UMCA_RX_QUEUE_DEPTH 4u
#endif
#ifndef UMCA_TX_QUEUE_DEPTH
#  define UMCA_TX_QUEUE_DEPTH 4u
#endif
#ifndef UMCA_MAX_TOPIC_NAME
#  define UMCA_MAX_TOPIC_NAME 63u
#endif
#ifndef UMCA_POLL_RX_BUDGET
#  define UMCA_POLL_RX_BUDGET 4u
#endif

#if UMCA_MAX_PAYLOAD > 256u
#  error "UMCA_MAX_PAYLOAD exceeds the MVP wire profile"
#endif
#if UMCA_MAX_FRAME < (40u + UMCA_MAX_PAYLOAD)
#  error "UMCA_MAX_FRAME is too small"
#endif
#if UMCA_MAX_TOPICS == 0u
#  error "UMCA_MAX_TOPICS must be non-zero"
#endif
#if UMCA_ENABLE_DISCOVERY && (UMCA_MAX_NODES == 0u || UMCA_MAX_TOPICS_PER_NODE == 0u)
#  error "Discovery requires node and remote topic capacity"
#endif
#if UMCA_ENABLE_DISCOVERY && !UMCA_ENABLE_REMOTE_TOPIC_NAMES
#  error "MVP discovery requires remote topic names"
#endif
#if UMCA_MAX_TOPIC_NAME == 0u || UMCA_MAX_TOPIC_NAME > 63u
#  error "UMCA_MAX_TOPIC_NAME must be 1..63"
#endif
#if UMCA_RX_QUEUE_DEPTH == 0u || UMCA_TX_QUEUE_DEPTH == 0u
#  error "PHY queue depths must be non-zero"
#endif
#if UMCA_ENABLE_RX_SEQUENCE_TRACKING && UMCA_MAX_RX_SEQUENCE_TRACKS == 0u
#  error "RX sequence tracking requires capacity"
#endif
#if UMCA_ENABLE_FRAGMENTATION || UMCA_ENABLE_RPC || UMCA_ENABLE_QOS1 || UMCA_ENABLE_QOS2 || UMCA_ENABLE_QOS3 || UMCA_ENABLE_DEBUG || UMCA_ENABLE_ACL || UMCA_ENABLE_ENCRYPTION || UMCA_ENABLE_CHANNEL_BONDING
#  error "An unsupported MVP feature was enabled"
#endif

#endif
