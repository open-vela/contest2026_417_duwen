#ifndef UMCA_PROTOCOL_H
#define UMCA_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include "umca_error.h"

#define UMCA_MAGIC 0x554du
#define UMCA_PROTOCOL_VERSION 0x01u
#define UMCA_HEADER_SIZE 36u
#define UMCA_CRC_SIZE 4u
#define UMCA_FRAME_OVERHEAD 40u
#define UMCA_MVP_MAX_PAYLOAD 256u
#define UMCA_MVP_MAX_FRAME 296u
#define UMCA_BROADCAST_DEVID UINT64_C(0xffffffffffffffff)
#define UMCA_INVALID_DEVID UINT64_C(0)
#define UMCA_SYSTEM_TOPIC_ID UINT32_C(0)
#define UMCA_DEFAULT_TTL 8u

enum umca_message_type
{
  UMCA_MSG_DATA = 0x00,
  UMCA_MSG_REQUEST = 0x01,
  UMCA_MSG_RESPONSE = 0x02,
  UMCA_MSG_ANNOUNCE = 0x03,
  UMCA_MSG_HEARTBEAT = 0x04,
  UMCA_MSG_TEARDOWN = 0x05,
  UMCA_MSG_ERROR = 0x06
};

typedef uint64_t umca_devid_t;
typedef uint32_t umca_topic_id_t;
typedef uint32_t umca_sequence_t;

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
  uint16_t payload_length;
} umca_frame_header_t;

uint16_t umca_read_be16(const uint8_t *p);
uint32_t umca_read_be32(const uint8_t *p);
uint64_t umca_read_be64(const uint8_t *p);
void umca_write_be16(uint8_t *p, uint16_t value);
void umca_write_be32(uint8_t *p, uint32_t value);
void umca_write_be64(uint8_t *p, uint64_t value);
uint32_t umca_crc32c(const uint8_t *data, size_t length);
int umca_frame_encode(const umca_frame_header_t *header,
                      const uint8_t *payload, uint8_t *output,
                      size_t output_capacity, size_t *output_length);
int umca_frame_decode(const uint8_t *frame, size_t frame_length,
                      umca_frame_header_t *header,
                      const uint8_t **payload);

#endif
