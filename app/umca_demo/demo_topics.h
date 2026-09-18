#ifndef UMCA_DEMO_TOPICS_H
#define UMCA_DEMO_TOPICS_H

#include <stdint.h>

#define DEMO_TOPIC_TEMPERATURE UINT32_C(0x0fc95cb0)
#define DEMO_TOPIC_THRESHOLD UINT32_C(0x5389c486)
#define DEMO_TOPIC_FAN_COMMAND UINT32_C(0xc72fe51a)
#define DEMO_TOPIC_FAN_STATE UINT32_C(0x6d3f9ebe)
#define DEMO_TOPIC_LLM_CHAT_REQUEST UINT32_C(0x7985c52e)
#define DEMO_TOPIC_LLM_CHAT_RESPONSE UINT32_C(0x9d3e1f72)

#define DEMO_TOPIC_TEMPERATURE_NAME "/sensors/temperature"
#define DEMO_TOPIC_THRESHOLD_NAME "/events/temperature/threshold"
#define DEMO_TOPIC_FAN_COMMAND_NAME "/actuators/fan/command"
#define DEMO_TOPIC_FAN_STATE_NAME "/actuators/fan/state"
#define DEMO_TOPIC_LLM_CHAT_REQUEST_NAME "/llm/chat/request"
#define DEMO_TOPIC_LLM_CHAT_RESPONSE_NAME "/llm/chat/response"

#define DEMO_LLM_CHAT_REQUEST_HEADER_SIZE 15u
#define DEMO_LLM_CHAT_RESPONSE_HEADER_SIZE 13u
#define DEMO_LLM_CHAT_REQUEST_TEXT_MAX 241u
#define DEMO_LLM_CHAT_RESPONSE_TEXT_MAX 243u

enum demo_llm_chat_origin
{
  DEMO_LLM_ORIGIN_TERMINAL = 0,
  DEMO_LLM_ORIGIN_AUTOMATIC = 1
};

enum demo_llm_status
{
  DEMO_LLM_STATUS_OK = 0,
  DEMO_LLM_STATUS_REJECTED = 1,
  DEMO_LLM_STATUS_UNAVAILABLE = 2,
  DEMO_LLM_STATUS_TIMEOUT = 3,
  DEMO_LLM_STATUS_ERROR = 4
};

enum demo_llm_action_type
{
  DEMO_LLM_ACTION_NONE = 0,
  DEMO_LLM_ACTION_SET_FAN = 1
};

typedef struct
{
  int32_t temperature_mc;
  uint32_t sample_time_ms;
  uint8_t quality;
} demo_temperature_sample_t;

typedef struct
{
  int32_t temperature_mc;
  int32_t threshold_mc;
  uint8_t direction;
  uint32_t event_time_ms;
} demo_threshold_event_t;

typedef struct
{
  uint32_t request_id;
  uint8_t command;
  uint8_t source;
} demo_fan_command_t;

typedef struct
{
  uint32_t request_id;
  uint8_t state;
  uint8_t result;
  uint32_t applied_time_ms;
} demo_fan_state_t;

typedef struct
{
  uint32_t requester_boot_id;
  uint32_t request_id;
  uint32_t timeout_ms;
  uint8_t origin;
  uint16_t text_length;
  const uint8_t *text;
} demo_llm_chat_request_t;

typedef struct
{
  uint32_t requester_boot_id;
  uint32_t request_id;
  uint8_t status;
  uint8_t action_type;
  uint8_t action_value;
  uint16_t text_length;
  const uint8_t *text;
} demo_llm_chat_response_t;

int demo_encode_temperature_sample(const demo_temperature_sample_t *message,
                                   uint8_t output[9]);
int demo_decode_temperature_sample(const uint8_t *payload, uint16_t length,
                                   demo_temperature_sample_t *message);
int demo_encode_threshold_event(const demo_threshold_event_t *message,
                                uint8_t output[13]);
int demo_decode_threshold_event(const uint8_t *payload, uint16_t length,
                                demo_threshold_event_t *message);
int demo_encode_fan_command(const demo_fan_command_t *message,
                            uint8_t output[6]);
int demo_decode_fan_command(const uint8_t *payload, uint16_t length,
                            demo_fan_command_t *message);
int demo_encode_fan_state(const demo_fan_state_t *message,
                          uint8_t output[10]);
int demo_decode_fan_state(const uint8_t *payload, uint16_t length,
                          demo_fan_state_t *message);
int demo_encode_llm_chat_request(const demo_llm_chat_request_t *message,
                                 uint8_t output[256], uint16_t *length);
int demo_decode_llm_chat_request(const uint8_t *payload, uint16_t length,
                                 demo_llm_chat_request_t *message);
int demo_encode_llm_chat_response(const demo_llm_chat_response_t *message,
                                  uint8_t output[256], uint16_t *length);
int demo_decode_llm_chat_response(const uint8_t *payload, uint16_t length,
                                  demo_llm_chat_response_t *message);

#endif
