#include <string.h>

#include "umca/umca_protocol.h"

static int type_supported(uint8_t type)
{
  return type == UMCA_MSG_DATA || type == UMCA_MSG_ANNOUNCE ||
         type == UMCA_MSG_HEARTBEAT || type == UMCA_MSG_TEARDOWN;
}

static int semantic_valid(const umca_frame_header_t *h)
{
  if (h->destination == UMCA_INVALID_DEVID ||
      h->source == UMCA_INVALID_DEVID || h->ttl == 0)
    {
      return UMCA_ERR_BAD_HEADER;
    }
  if (!type_supported(h->message_type))
    {
      return UMCA_ERR_UNSUPPORTED_TYPE;
    }
  if (h->message_type == UMCA_MSG_DATA)
    {
      if (h->topic_id == UMCA_SYSTEM_TOPIC_ID)
        {
          return UMCA_ERR_BAD_HEADER;
        }
    }
  else if (h->topic_id != UMCA_SYSTEM_TOPIC_ID ||
           h->destination != UMCA_BROADCAST_DEVID)
    {
      return UMCA_ERR_BAD_HEADER;
    }
  return UMCA_OK;
}

int umca_frame_encode(const umca_frame_header_t *header,
                      const uint8_t *payload, uint8_t *output,
                      size_t output_capacity, size_t *output_length)
{
  size_t length;
  int ret;
  if (header == NULL || output == NULL || output_length == NULL ||
      (payload == NULL && header->payload_length != 0))
    {
      return UMCA_ERR_INVALID_ARG;
    }
  if (header->flags != 0)
    {
      return UMCA_ERR_UNSUPPORTED_FLAGS;
    }
  if (header->qos != 0)
    {
      return UMCA_ERR_UNSUPPORTED_QOS;
    }
  if (header->payload_length > UMCA_MVP_MAX_PAYLOAD)
    {
      return UMCA_ERR_PAYLOAD_TOO_LARGE;
    }
  ret = semantic_valid(header);
  if (ret != UMCA_OK)
    {
      return ret;
    }
  length = UMCA_FRAME_OVERHEAD + header->payload_length;
  if (output_capacity < length)
    {
      return UMCA_ERR_PAYLOAD_TOO_LARGE;
    }
  umca_write_be16(output, UMCA_MAGIC);
  output[2] = UMCA_PROTOCOL_VERSION;
  output[3] = UMCA_HEADER_SIZE;
  umca_write_be16(output + 4, header->flags);
  output[6] = header->message_type;
  output[7] = header->qos;
  output[8] = header->ttl;
  output[9] = 0;
  umca_write_be64(output + 10, header->destination);
  umca_write_be64(output + 18, header->source);
  umca_write_be32(output + 26, header->topic_id);
  umca_write_be32(output + 30, header->sequence);
  umca_write_be16(output + 34, header->payload_length);
  if (header->payload_length != 0)
    {
      memcpy(output + UMCA_HEADER_SIZE, payload, header->payload_length);
    }
  umca_write_be32(output + UMCA_HEADER_SIZE + header->payload_length,
                  umca_crc32c(output + 2, 34u + header->payload_length));
  *output_length = length;
  return UMCA_OK;
}

int umca_frame_decode(const uint8_t *frame, size_t frame_length,
                      umca_frame_header_t *header, const uint8_t **payload)
{
  uint16_t payload_length;
  uint32_t expected_crc;
  uint32_t actual_crc;
  int ret;
  if (frame == NULL || header == NULL || payload == NULL)
    {
      return UMCA_ERR_INVALID_ARG;
    }
  if (frame_length < UMCA_FRAME_OVERHEAD)
    {
      return UMCA_ERR_LENGTH_MISMATCH;
    }
  if (umca_read_be16(frame) != UMCA_MAGIC)
    {
      return UMCA_ERR_BAD_MAGIC;
    }
  if (frame[2] != UMCA_PROTOCOL_VERSION)
    {
      return UMCA_ERR_UNSUPPORTED_VERSION;
    }
  if (frame[3] != UMCA_HEADER_SIZE)
    {
      return UMCA_ERR_BAD_HEADER;
    }
  if (umca_read_be16(frame + 4) != 0)
    {
      return UMCA_ERR_UNSUPPORTED_FLAGS;
    }
  if (frame[7] != 0)
    {
      return UMCA_ERR_UNSUPPORTED_QOS;
    }
  if (frame[9] != 0)
    {
      return UMCA_ERR_BAD_HEADER;
    }
  payload_length = umca_read_be16(frame + 34);
  if (payload_length > UMCA_MVP_MAX_PAYLOAD)
    {
      return UMCA_ERR_PAYLOAD_TOO_LARGE;
    }
  if (frame_length != UMCA_FRAME_OVERHEAD + payload_length)
    {
      return UMCA_ERR_LENGTH_MISMATCH;
    }
  expected_crc = umca_read_be32(frame + UMCA_HEADER_SIZE + payload_length);
  actual_crc = umca_crc32c(frame + 2, 34u + payload_length);
  if (expected_crc != actual_crc)
    {
      return UMCA_ERR_CRC;
    }
  header->flags = 0;
  header->message_type = frame[6];
  header->qos = frame[7];
  header->ttl = frame[8];
  header->destination = umca_read_be64(frame + 10);
  header->source = umca_read_be64(frame + 18);
  header->topic_id = umca_read_be32(frame + 26);
  header->sequence = umca_read_be32(frame + 30);
  header->payload_length = payload_length;
  ret = semantic_valid(header);
  if (ret != UMCA_OK)
    {
      return ret;
    }
  *payload = frame + UMCA_HEADER_SIZE;
  return UMCA_OK;
}
