#ifndef UMCA_DEMO_TOPICS_H
#define UMCA_DEMO_TOPICS_H

#include <stdint.h>

#define DEMO_TOPIC_TEMPERATURE UINT32_C(0x0fc95cb0)
#define DEMO_TOPIC_THRESHOLD UINT32_C(0x5389c486)
#define DEMO_TOPIC_FAN_COMMAND UINT32_C(0xc72fe51a)
#define DEMO_TOPIC_FAN_STATE UINT32_C(0x6d3f9ebe)

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
int demo_encode_fan_state(const demo_fan_state_t *message, uint8_t output[10]);
int demo_decode_fan_state(const uint8_t *payload, uint16_t length,
                          demo_fan_state_t *message);

#endif
