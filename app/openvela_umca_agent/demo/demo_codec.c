#include "demo_topics.h"

#include "umca/umca_protocol.h"

static int valid_ptrs(const uint8_t *payload, void *message)
{
  return payload != NULL && message != NULL ? 0 : -1;
}

int demo_encode_temperature_sample(const demo_temperature_sample_t *message,
                                   uint8_t output[9])
{
  if (message == NULL || output == NULL ||
      (message->quality != 0 && message->quality != 1))
    {
      return -1;
    }
  umca_write_be32(output, (uint32_t)message->temperature_mc);
  umca_write_be32(output + 4, message->sample_time_ms);
  output[8] = message->quality;
  return 0;
}

int demo_decode_temperature_sample(const uint8_t *payload, uint16_t length,
                                   demo_temperature_sample_t *message)
{
  if (length != 9 || valid_ptrs(payload, message) != 0)
    {
      return -1;
    }
  message->temperature_mc = (int32_t)umca_read_be32(payload);
  message->sample_time_ms = umca_read_be32(payload + 4);
  message->quality = payload[8];
  return message->quality <= 1 ? 0 : -1;
}

int demo_encode_threshold_event(const demo_threshold_event_t *message,
                                uint8_t output[13])
{
  if (message == NULL || output == NULL || message->direction > 1)
    {
      return -1;
    }
  umca_write_be32(output, (uint32_t)message->temperature_mc);
  umca_write_be32(output + 4, (uint32_t)message->threshold_mc);
  output[8] = message->direction;
  umca_write_be32(output + 9, message->event_time_ms);
  return 0;
}

int demo_decode_threshold_event(const uint8_t *payload, uint16_t length,
                                demo_threshold_event_t *message)
{
  if (length != 13 || valid_ptrs(payload, message) != 0)
    {
      return -1;
    }
  message->temperature_mc = (int32_t)umca_read_be32(payload);
  message->threshold_mc = (int32_t)umca_read_be32(payload + 4);
  message->direction = payload[8];
  message->event_time_ms = umca_read_be32(payload + 9);
  return message->direction <= 1 ? 0 : -1;
}

int demo_encode_fan_command(const demo_fan_command_t *message,
                            uint8_t output[6])
{
  if (message == NULL || output == NULL || message->command > 1 ||
      message->source > 2 || message->request_id == 0)
    {
      return -1;
    }
  umca_write_be32(output, message->request_id);
  output[4] = message->command;
  output[5] = message->source;
  return 0;
}

int demo_decode_fan_command(const uint8_t *payload, uint16_t length,
                            demo_fan_command_t *message)
{
  if (length != 6 || valid_ptrs(payload, message) != 0)
    {
      return -1;
    }
  message->request_id = umca_read_be32(payload);
  message->command = payload[4];
  message->source = payload[5];
  return message->request_id != 0 && message->command <= 1 &&
         message->source <= 2 ? 0 : -1;
}

int demo_encode_fan_state(const demo_fan_state_t *message, uint8_t output[10])
{
  if (message == NULL || output == NULL || message->request_id == 0 ||
      message->state > 1)
    {
      return -1;
    }
  umca_write_be32(output, message->request_id);
  output[4] = message->state;
  output[5] = message->result;
  umca_write_be32(output + 6, message->applied_time_ms);
  return 0;
}

int demo_decode_fan_state(const uint8_t *payload, uint16_t length,
                          demo_fan_state_t *message)
{
  if (length != 10 || valid_ptrs(payload, message) != 0)
    {
      return -1;
    }
  message->request_id = umca_read_be32(payload);
  message->state = payload[4];
  message->result = payload[5];
  message->applied_time_ms = umca_read_be32(payload + 6);
  return message->request_id != 0 && message->state <= 1 ? 0 : -1;
}
