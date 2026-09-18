#include "demo_topics.h"

#include <string.h>

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

int demo_encode_fan_state(const demo_fan_state_t *message,
                          uint8_t output[10])
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

int demo_encode_llm_chat_request(const demo_llm_chat_request_t *message,
                                 uint8_t output[256], uint16_t *length)
{
  uint16_t total;
  if (message == NULL || output == NULL || length == NULL ||
      message->requester_boot_id == 0 || message->request_id == 0 ||
      message->timeout_ms == 0 || message->origin > DEMO_LLM_ORIGIN_AUTOMATIC ||
      message->text == NULL || message->text_length == 0 ||
      message->text_length > DEMO_LLM_CHAT_REQUEST_TEXT_MAX)
    {
      return -1;
    }

  total = (uint16_t)(DEMO_LLM_CHAT_REQUEST_HEADER_SIZE +
                     message->text_length);
  umca_write_be32(output, message->requester_boot_id);
  umca_write_be32(output + 4, message->request_id);
  umca_write_be32(output + 8, message->timeout_ms);
  output[12] = message->origin;
  umca_write_be16(output + 13, message->text_length);
  memcpy(output + DEMO_LLM_CHAT_REQUEST_HEADER_SIZE, message->text,
         message->text_length);
  *length = total;
  return 0;
}

int demo_decode_llm_chat_request(const uint8_t *payload, uint16_t length,
                                 demo_llm_chat_request_t *message)
{
  uint16_t text_length;
  if (length < DEMO_LLM_CHAT_REQUEST_HEADER_SIZE ||
      valid_ptrs(payload, message) != 0)
    {
      return -1;
    }

  text_length = umca_read_be16(payload + 13);
  if (text_length == 0 || text_length > DEMO_LLM_CHAT_REQUEST_TEXT_MAX ||
      length != (uint16_t)(DEMO_LLM_CHAT_REQUEST_HEADER_SIZE + text_length))
    {
      return -1;
    }

  message->requester_boot_id = umca_read_be32(payload);
  message->request_id = umca_read_be32(payload + 4);
  message->timeout_ms = umca_read_be32(payload + 8);
  message->origin = payload[12];
  message->text_length = text_length;
  message->text = payload + DEMO_LLM_CHAT_REQUEST_HEADER_SIZE;
  return message->requester_boot_id != 0 && message->request_id != 0 &&
         message->timeout_ms != 0 &&
         message->origin <= DEMO_LLM_ORIGIN_AUTOMATIC ? 0 : -1;
}

int demo_encode_llm_chat_response(const demo_llm_chat_response_t *message,
                                  uint8_t output[256], uint16_t *length)
{
  uint16_t total;
  if (message == NULL || output == NULL || length == NULL ||
      message->requester_boot_id == 0 || message->request_id == 0 ||
      message->status > DEMO_LLM_STATUS_ERROR ||
      message->action_type > DEMO_LLM_ACTION_SET_FAN ||
      message->action_value > 1 ||
      (message->status != DEMO_LLM_STATUS_OK &&
       message->action_type != DEMO_LLM_ACTION_NONE) ||
      (message->action_type == DEMO_LLM_ACTION_NONE &&
       message->action_value != 0) ||
      message->text_length > DEMO_LLM_CHAT_RESPONSE_TEXT_MAX ||
      (message->text_length != 0 && message->text == NULL))
    {
      return -1;
    }

  total = (uint16_t)(DEMO_LLM_CHAT_RESPONSE_HEADER_SIZE +
                     message->text_length);
  umca_write_be32(output, message->requester_boot_id);
  umca_write_be32(output + 4, message->request_id);
  output[8] = message->status;
  output[9] = message->action_type;
  output[10] = message->action_value;
  umca_write_be16(output + 11, message->text_length);
  if (message->text_length != 0)
    {
      memcpy(output + DEMO_LLM_CHAT_RESPONSE_HEADER_SIZE, message->text,
             message->text_length);
    }
  *length = total;
  return 0;
}

int demo_decode_llm_chat_response(const uint8_t *payload, uint16_t length,
                                  demo_llm_chat_response_t *message)
{
  uint16_t text_length;
  if (length < DEMO_LLM_CHAT_RESPONSE_HEADER_SIZE ||
      valid_ptrs(payload, message) != 0)
    {
      return -1;
    }

  text_length = umca_read_be16(payload + 11);
  if (text_length > DEMO_LLM_CHAT_RESPONSE_TEXT_MAX ||
      length != (uint16_t)(DEMO_LLM_CHAT_RESPONSE_HEADER_SIZE + text_length))
    {
      return -1;
    }

  message->requester_boot_id = umca_read_be32(payload);
  message->request_id = umca_read_be32(payload + 4);
  message->status = payload[8];
  message->action_type = payload[9];
  message->action_value = payload[10];
  message->text_length = text_length;
  message->text = payload + DEMO_LLM_CHAT_RESPONSE_HEADER_SIZE;
  return message->requester_boot_id != 0 && message->request_id != 0 &&
         message->status <= DEMO_LLM_STATUS_ERROR &&
         message->action_type <= DEMO_LLM_ACTION_SET_FAN &&
         message->action_value <= 1 &&
         (message->status == DEMO_LLM_STATUS_OK ||
          message->action_type == DEMO_LLM_ACTION_NONE) &&
         (message->action_type != DEMO_LLM_ACTION_NONE ||
          message->action_value == 0) ? 0 : -1;
}
