#include <assert.h>
#include <string.h>

#include "umca/umca.h"
#include "../vectors/umca_vectors.h"

static void test_crc_topic(void)
{
  static const char *topics[] =
  {
    "/sensors/temperature",
    "/events/temperature/threshold",
    "/actuators/fan/command",
    "/actuators/fan/state"
  };
  static const uint32_t ids[] =
  {
    UINT32_C(0x0fc95cb0), UINT32_C(0x5389c486),
    UINT32_C(0xc72fe51a), UINT32_C(0x6d3f9ebe)
  };
  unsigned int i;
  assert(umca_crc32c((const uint8_t *)"123456789", 9) ==
         UMCA_VECTOR_CRC32C_123456789);
  for (i = 0; i < 4; i++)
    {
      umca_topic_id_t id;
      assert(umca_topic_id_from_name(topics[i], strlen(topics[i]), &id) ==
             UMCA_OK);
      assert(id == ids[i]);
    }
  assert(umca_topic_validate("/a//b", 5) != UMCA_OK);
  assert(umca_topic_validate("/a/../b", 7) != UMCA_OK);
  assert(umca_topic_validate("/a/é", strlen("/a/é")) != UMCA_OK);
}

static void test_vectors(void)
{
  static const uint8_t empty_frame[] =
  {
    0x55, 0x4d, 0x01, 0x24, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x01, 0x0f, 0xc9, 0x5c, 0xb0, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0xc8, 0xc2, 0x08, 0x2a
  };
  static const uint8_t temperature_frame[] =
  {
    0x55, 0x4d, 0x01, 0x24, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x20, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x01, 0x0f, 0xc9, 0x5c, 0xb0, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x09, 0x00, 0x00, 0x77, 0x24,
    0x00, 0x00, 0x03, 0xe8, 0x01, 0xf7, 0x26, 0xe5, 0x28
  };
  static const uint8_t announce_frame[] =
  {
    0x55, 0x4d, 0x01, 0x24, 0x00, 0x00, 0x03, 0x00,
    0x08, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01,
    0x00, 0x23, 0x00, 0x00, 0x00, 0x01, 0x10, 0x01,
    0x00, 0x01, 0x01, 0x0f, 0xc9, 0x5c, 0xb0, 0x01,
    0x14, 0x2f, 0x73, 0x65, 0x6e, 0x73, 0x6f, 0x72,
    0x73, 0x2f, 0x74, 0x65, 0x6d, 0x70, 0x65, 0x72,
    0x61, 0x74, 0x75, 0x72, 0x65, 0x86, 0x53, 0xd2,
    0x17
  };
  umca_frame_header_t header;
  const uint8_t *payload;
  uint8_t encoded[UMCA_MAX_FRAME];
  size_t length;
  assert(umca_frame_decode(empty_frame, sizeof(empty_frame), &header,
                           &payload) == UMCA_OK);
  assert(header.message_type == UMCA_MSG_DATA && header.payload_length == 0);
  assert(umca_frame_encode(&header, NULL, encoded, sizeof(encoded), &length) ==
         UMCA_OK);
  assert(length == sizeof(empty_frame));
  assert(memcmp(encoded, empty_frame, length) == 0);
  assert(umca_frame_decode(temperature_frame, sizeof(temperature_frame),
                           &header, &payload) == UMCA_OK);
  assert(header.payload_length == 9 && payload[8] == 1);
  {
    uint8_t sample[9] = {0x00, 0x00, 0x77, 0x24, 0x00, 0x00, 0x03, 0xe8, 0x01};
    assert(umca_frame_encode(&header, sample, encoded, sizeof(encoded),
                             &length) == UMCA_OK);
    assert(length == sizeof(temperature_frame));
    assert(memcmp(encoded, temperature_frame, length) == 0);
  }
  assert(umca_frame_decode(announce_frame, sizeof(announce_frame),
                           &header, &payload) == UMCA_OK);
  assert(header.message_type == UMCA_MSG_ANNOUNCE &&
         header.payload_length == 35);
  assert(umca_frame_encode(&header, payload, encoded, sizeof(encoded),
                           &length) == UMCA_OK);
  assert(length == sizeof(announce_frame));
  assert(memcmp(encoded, announce_frame, length) == 0);
  encoded[4] = 0x01;
  assert(umca_frame_decode(encoded, length, &header, &payload) ==
         UMCA_ERR_UNSUPPORTED_FLAGS);
}

static void test_frame_rejections(void)
{
  umca_frame_header_t header =
  {
    0, UMCA_MSG_DATA, 0, UMCA_DEFAULT_TTL, UMCA_BROADCAST_DEVID,
    UINT64_C(0x1001), UINT32_C(0x12345678), 1, UMCA_MAX_PAYLOAD
  };
  uint8_t payload[UMCA_MAX_PAYLOAD];
  uint8_t frame[UMCA_MAX_FRAME];
  size_t length;
  const uint8_t *decoded_payload;
  memset(payload, 0xa5, sizeof(payload));
  assert(umca_frame_encode(&header, payload, frame, sizeof(frame), &length) ==
         UMCA_OK);
  assert(length == UMCA_MAX_FRAME);
  assert(umca_frame_decode(frame, length, &header, &decoded_payload) ==
         UMCA_OK);
  frame[7] = 1;
  assert(umca_frame_decode(frame, length, &header, &decoded_payload) ==
         UMCA_ERR_UNSUPPORTED_QOS);
  frame[7] = 0;
  header.message_type = UMCA_MSG_REQUEST;
  assert(umca_frame_encode(&header, payload, frame, sizeof(frame), &length) ==
         UMCA_ERR_UNSUPPORTED_TYPE);
}

int main(void)
{
  test_crc_topic();
  test_vectors();
  test_frame_rejections();
  return 0;
}
