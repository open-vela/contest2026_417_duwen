#ifndef UMCA_AGENT_ADAPTER_H
#define UMCA_AGENT_ADAPTER_H

#include <stdbool.h>
#include <stdint.h>

#include "umca/umca.h"

typedef struct
{
  void (*pump)(void *user);
  void *pump_user;
  umca_context_t *ctx;
  umca_devid_t sensor_dev_id;
  umca_devid_t actuator_dev_id;
  uint32_t sample_max_age_ms;
  bool initialized;
  bool sample_present;
  bool sample_valid;
  int32_t temperature_mc;
  uint32_t sample_time_ms;
  uint32_t received_time_ms;
  umca_devid_t sample_source;
  uint32_t next_request_id;
  bool request_pending;
  uint32_t pending_request_id;
  bool request_completed;
  uint8_t pending_result;
  uint8_t pending_state;
} umca_agent_adapter_t;

typedef void (*umca_agent_pump_t)(void *user);

int umca_agent_adapter_init(umca_agent_adapter_t *adapter,
                            umca_context_t *ctx,
                            umca_devid_t sensor_dev_id,
                            umca_devid_t actuator_dev_id);
void umca_agent_adapter_tick(umca_agent_adapter_t *adapter);
void umca_agent_adapter_set_pump(umca_agent_adapter_t *adapter,
                                 umca_agent_pump_t pump, void *user);
int umca_agent_adapter_sensor_query(umca_agent_adapter_t *adapter,
                                    char *output, uint32_t output_size);
int umca_agent_adapter_device_control(umca_agent_adapter_t *adapter,
                                      bool on, char *output,
                                      uint32_t output_size);

#endif
