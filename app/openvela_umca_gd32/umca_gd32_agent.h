#ifndef UMCA_GD32_AGENT_H
#define UMCA_GD32_AGENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "demo_topics.h"
#include "umca/umca.h"

#define UMCA_GD32_AGENT_DEVID UINT64_C(0x4700000000000001)
#define UMCA_GD32_SENSOR_DEVID UINT64_C(0x0000000000001001)
#define UMCA_GD32_ACTUATOR_DEVID UINT64_C(0x0000000000003001)
#define UMCA_GD32_ESP32_DEVID UINT64_C(0x3300000000000001)

#define UMCA_GD32_SAMPLE_MAX_AGE_MS UINT32_C(5000)
#define UMCA_GD32_FAN_TIMEOUT_MS UINT32_C(1000)
#define UMCA_GD32_CHAT_TIMEOUT_MS UINT32_C(15000)
#define UMCA_GD32_AGENT_AUTO_COOLDOWN_MS UINT32_C(60000)
#define UMCA_GD32_C1_TEXT_CAPACITY (DEMO_LLM_CHAT_REQUEST_TEXT_MAX + 1u)
#define UMCA_GD32_SKILL_OUTPUT_MAX 192u

enum umca_gd32_control_source
{
  UMCA_GD32_CONTROL_MANUAL = 0,
  UMCA_GD32_CONTROL_LLM = 1,
  UMCA_GD32_CONTROL_LOCAL_RULE = 2
};

enum umca_gd32_fan_result
{
  UMCA_GD32_FAN_RESULT_NONE = 0,
  UMCA_GD32_FAN_RESULT_PENDING,
  UMCA_GD32_FAN_RESULT_CONFIRMED,
  UMCA_GD32_FAN_RESULT_OFFLINE,
  UMCA_GD32_FAN_RESULT_TIMEOUT,
  UMCA_GD32_FAN_RESULT_MISMATCH,
  UMCA_GD32_FAN_RESULT_REJECTED
};

typedef struct
{
  bool available;
  bool sensor_online;
  int32_t temperature_mc;
  uint32_t age_ms;
  uint8_t quality;
} umca_gd32_sensor_snapshot_t;

typedef int (*umca_gd32_agent_pump_t)(void *user);

typedef struct
{
  bool sample_present;
  bool sample_valid;
  int32_t temperature_mc;
  uint32_t sample_time_ms;
  uint32_t sample_received_ms;
  uint8_t sample_quality;
  bool auto_enabled;
  bool agent_auto_enabled;
  bool fan_known;
  bool fan_on;
  bool fan_request_pending;
  uint32_t fan_request_id;
  uint8_t fan_result;
  bool chat_request_pending;
  uint32_t chat_request_id;
  uint8_t chat_status;
  uint8_t chat_action_type;
  uint8_t chat_action_value;
  uint16_t chat_text_length;
  char chat_text[DEMO_LLM_CHAT_RESPONSE_TEXT_MAX + 1u];
  uint32_t temperature_messages;
  uint32_t threshold_events;
  uint32_t fan_commands;
  uint32_t fan_acks;
  uint32_t fan_rejections;
  uint32_t fan_timeouts;
  uint32_t fan_mismatches;
  uint32_t chat_requests;
  uint32_t chat_responses;
  uint32_t chat_failures;
  uint32_t chat_timeouts;
  uint32_t chat_mismatches;
  uint32_t chat_invalid_responses;
  uint32_t llm_actions_accepted;
  uint32_t llm_actions_rejected;
  uint32_t llm_actions_completed;
  uint32_t agent_auto_events;
  uint32_t agent_auto_merged;
  uint32_t agent_auto_cooldown_skips;
  uint32_t agent_auto_failures;
} umca_gd32_agent_state_t;

typedef struct
{
  umca_context_t *ctx;
  umca_devid_t sensor_dev_id;
  umca_devid_t actuator_dev_id;
  umca_devid_t esp32_dev_id;
  uint32_t requester_boot_id;
  uint32_t next_request_id;
  uint32_t fan_deadline_ms;
  uint32_t chat_deadline_ms;
  uint32_t pending_actuator_boot_id;
  uint32_t pending_esp32_boot_id;
  uint32_t last_agent_eval_ms;
  bool pending_fan_target;
  bool pending_fan_mismatch;
  bool queued_control;
  bool queued_control_target;
  bool queued_agent_eval;
  bool agent_eval_seen;
  bool chat_is_automatic;
  uint8_t queued_control_source;
  umca_gd32_agent_pump_t pump;
  void *pump_user;
  umca_gd32_sensor_snapshot_t skill_snapshot;
  char last_action_result[UMCA_GD32_SKILL_OUTPUT_MAX];
  umca_gd32_agent_state_t state;
} umca_gd32_agent_t;

int umca_gd32_agent_init(umca_gd32_agent_t *agent, umca_context_t *ctx,
                         umca_devid_t sensor_dev_id,
                         umca_devid_t actuator_dev_id,
                         umca_devid_t esp32_dev_id);
void umca_gd32_agent_set_pump(umca_gd32_agent_t *agent,
                              umca_gd32_agent_pump_t pump, void *user);
void umca_gd32_agent_tick(umca_gd32_agent_t *agent);
int umca_gd32_agent_query_temperature(umca_gd32_agent_t *agent,
                                      char *output, size_t output_size);
int umca_gd32_agent_control_fan(umca_gd32_agent_t *agent, bool on,
                                uint8_t source, uint32_t *request_id);
int umca_gd32_agent_chat(umca_gd32_agent_t *agent, const uint8_t *text,
                         uint16_t text_length, uint8_t origin,
                         uint32_t *request_id);
void umca_gd32_agent_set_auto(umca_gd32_agent_t *agent, bool enabled);
int umca_gd32_agent_set_agent_auto(umca_gd32_agent_t *agent, bool enabled);
bool umca_gd32_agent_node_online(const umca_gd32_agent_t *agent,
                                 umca_devid_t dev_id);
int umca_gd32_agent_sensor_query_skill(umca_gd32_agent_t *agent,
                                       char *output, size_t output_size);
int umca_gd32_agent_device_control_skill(umca_gd32_agent_t *agent,
                                         bool on, char *output,
                                         size_t output_size);
int umca_gd32_agent_build_c1(umca_gd32_agent_t *agent,
                             const umca_gd32_sensor_snapshot_t *snapshot,
                             const uint8_t *user_text,
                             uint16_t user_text_length, char *output,
                             size_t output_size, uint16_t *output_length);
int umca_gd32_agent_validate_user_text(const uint8_t *text, size_t length);
const char *umca_gd32_agent_llm_status_name(uint8_t status);
void umca_gd32_agent_get_state(const umca_gd32_agent_t *agent,
                               umca_gd32_agent_state_t *state);

#endif
