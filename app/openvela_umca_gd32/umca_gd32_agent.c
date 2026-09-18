#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include "tools/tool_registry.h"

#include "umca_gd32_agent.h"

#define UMCA_GD32_AUTO_QUESTION \
  "根据可信设备快照判断是否需要开关风扇，简短回答。"

static umca_gd32_agent_t *g_tool_agent;
static bool g_tool_provider_registered;

static uint32_t agent_now(const umca_gd32_agent_t *agent)
{
  return agent->ctx->platform.ops->time_ms(agent->ctx->platform.user);
}

static bool deadline_reached(uint32_t now, uint32_t deadline)
{
  return (uint32_t)(now - deadline) < UINT32_C(0x80000000);
}

static uint32_t allocate_request_id(umca_gd32_agent_t *agent)
{
  uint32_t request_id = agent->next_request_id++;
  if (request_id == 0)
    {
      request_id = agent->next_request_id++;
    }
  if (agent->next_request_id == 0)
    {
      agent->next_request_id = 1;
    }
  return request_id;
}

static int checked_snprintf(char *output, size_t output_size,
                            const char *format, ...)
{
  va_list args;
  int written;

  if (output == NULL || output_size == 0)
    {
      return UMCA_ERR_INVALID_ARG;
    }

  va_start(args, format);
  written = vsnprintf(output, output_size, format, args);
  va_end(args);
  if (written < 0 || (size_t)written >= output_size)
    {
      output[0] = '\0';
      return UMCA_ERR_CAPACITY;
    }
  return UMCA_OK;
}

static int node_session(const umca_gd32_agent_t *agent, umca_devid_t dev_id,
                        uint32_t *boot_id)
{
  umca_node_info_t node;
  if (agent == NULL || agent->ctx == NULL ||
      umca_node_get(agent->ctx, dev_id, &node) != UMCA_OK ||
      node.state != UMCA_NODE_ONLINE)
    {
      return UMCA_ERR_NOT_FOUND;
    }
  if (boot_id != NULL)
    {
      *boot_id = node.boot_id;
    }
  return UMCA_OK;
}

bool umca_gd32_agent_node_online(const umca_gd32_agent_t *agent,
                                 umca_devid_t dev_id)
{
  return node_session(agent, dev_id, NULL) == UMCA_OK;
}

static void capture_sensor_snapshot(umca_gd32_agent_t *agent,
                                    umca_gd32_sensor_snapshot_t *snapshot)
{
  umca_gd32_agent_state_t state;
  uint32_t age;

  memset(snapshot, 0, sizeof(*snapshot));
  state = agent->state;
  snapshot->sensor_online = umca_gd32_agent_node_online(
      agent, agent->sensor_dev_id);
  snapshot->quality = state.sample_quality;
  if (!snapshot->sensor_online || !state.sample_present)
    {
      return;
    }

  age = agent_now(agent) - state.sample_received_ms;
  if (!state.sample_valid || state.sample_quality != 1 ||
      age > UMCA_GD32_SAMPLE_MAX_AGE_MS)
    {
      return;
    }

  snapshot->available = true;
  snapshot->temperature_mc = state.temperature_mc;
  snapshot->age_ms = age > UMCA_GD32_SAMPLE_MAX_AGE_MS ?
                     UMCA_GD32_SAMPLE_MAX_AGE_MS : age;
}

static void on_temperature(umca_context_t *ctx,
                           const umca_message_t *message, void *user_data)
{
  umca_gd32_agent_t *agent = user_data;
  demo_temperature_sample_t sample;
  (void)ctx;
  if (message->source != agent->sensor_dev_id ||
      demo_decode_temperature_sample(message->payload,
                                     message->payload_length, &sample) != 0)
    {
      return;
    }

  agent->state.sample_present = true;
  agent->state.sample_valid = sample.quality == 1;
  agent->state.temperature_mc = sample.temperature_mc;
  agent->state.sample_time_ms = sample.sample_time_ms;
  agent->state.sample_received_ms = agent_now(agent);
  agent->state.sample_quality = sample.quality;
  agent->state.temperature_messages++;
}

static void on_threshold(umca_context_t *ctx,
                         const umca_message_t *message, void *user_data)
{
  umca_gd32_agent_t *agent = user_data;
  demo_threshold_event_t event;
  umca_gd32_sensor_snapshot_t snapshot;
  uint32_t now;
  (void)ctx;
  if (message->source != agent->sensor_dev_id ||
      demo_decode_threshold_event(message->payload, message->payload_length,
                                  &event) != 0)
    {
      return;
    }

  agent->state.threshold_events++;
  if (agent->state.auto_enabled)
    {
      agent->queued_control = true;
      agent->queued_control_target = event.direction == 1;
      agent->queued_control_source = UMCA_GD32_CONTROL_LOCAL_RULE;
    }

  if (!agent->state.agent_auto_enabled)
    {
      return;
    }

  capture_sensor_snapshot(agent, &snapshot);
  if (!snapshot.available)
    {
      agent->state.agent_auto_failures++;
      return;
    }
  if (agent->queued_agent_eval ||
      (agent->state.chat_request_pending && agent->chat_is_automatic))
    {
      agent->state.agent_auto_merged++;
      return;
    }

  now = agent_now(agent);
  if (agent->agent_eval_seen &&
      now - agent->last_agent_eval_ms < UMCA_GD32_AGENT_AUTO_COOLDOWN_MS)
    {
      agent->state.agent_auto_cooldown_skips++;
      return;
    }

  agent->queued_agent_eval = true;
  agent->agent_eval_seen = true;
  agent->last_agent_eval_ms = now;
  agent->state.agent_auto_events++;
}

static void finish_fan_request(umca_gd32_agent_t *agent, uint8_t result)
{
  agent->state.fan_request_pending = false;
  agent->state.fan_result = result;
}

static void on_fan_state(umca_context_t *ctx,
                         const umca_message_t *message, void *user_data)
{
  umca_gd32_agent_t *agent = user_data;
  demo_fan_state_t state;
  uint32_t boot_id;
  (void)ctx;
  if (message->source != agent->actuator_dev_id ||
      demo_decode_fan_state(message->payload, message->payload_length,
                            &state) != 0)
    {
      if (agent->state.fan_request_pending)
        {
          agent->pending_fan_mismatch = true;
          agent->state.fan_mismatches++;
        }
      return;
    }

  if (!agent->state.fan_request_pending)
    {
      agent->state.fan_mismatches++;
      return;
    }
  if (node_session(agent, agent->actuator_dev_id, &boot_id) != UMCA_OK ||
      boot_id != agent->pending_actuator_boot_id)
    {
      agent->state.fan_mismatches++;
      finish_fan_request(agent, UMCA_GD32_FAN_RESULT_MISMATCH);
      return;
    }
  if (state.request_id != agent->state.fan_request_id)
    {
      agent->pending_fan_mismatch = true;
      agent->state.fan_mismatches++;
      return;
    }

  if (state.result == 0 && state.state == (uint8_t)agent->pending_fan_target)
    {
      agent->state.fan_known = true;
      agent->state.fan_on = state.state == 1;
      agent->state.fan_acks++;
      finish_fan_request(agent, UMCA_GD32_FAN_RESULT_CONFIRMED);
    }
  else
    {
      agent->state.fan_rejections++;
      finish_fan_request(agent, UMCA_GD32_FAN_RESULT_REJECTED);
    }
}

static void fail_late_chat(umca_gd32_agent_t *agent)
{
  agent->state.chat_request_pending = false;
  agent->state.chat_status = DEMO_LLM_STATUS_TIMEOUT;
  agent->state.chat_text_length = 0;
  agent->state.chat_text[0] = '\0';
  agent->state.chat_timeouts++;
  agent->state.chat_failures++;
  if (agent->chat_is_automatic)
    {
      agent->state.agent_auto_failures++;
    }
}

static void on_chat_response(umca_context_t *ctx,
                             const umca_message_t *message, void *user_data)
{
  umca_gd32_agent_t *agent = user_data;
  demo_llm_chat_response_t response;
  uint32_t boot_id;
  (void)ctx;
  if (message->source != agent->esp32_dev_id)
    {
      if (agent->state.chat_request_pending)
        {
          agent->state.chat_mismatches++;
        }
      return;
    }
  if (demo_decode_llm_chat_response(message->payload,
                                    message->payload_length, &response) != 0)
    {
      agent->state.chat_invalid_responses++;
      return;
    }

  if (!agent->state.chat_request_pending ||
      response.requester_boot_id != agent->requester_boot_id ||
      response.request_id != agent->state.chat_request_id ||
      node_session(agent, agent->esp32_dev_id, &boot_id) != UMCA_OK ||
      boot_id != agent->pending_esp32_boot_id)
    {
      agent->state.chat_mismatches++;
      return;
    }
  if (deadline_reached(agent_now(agent), agent->chat_deadline_ms))
    {
      fail_late_chat(agent);
      return;
    }

  agent->state.chat_request_pending = false;
  agent->state.chat_status = response.status;
  agent->state.chat_action_type = response.action_type;
  agent->state.chat_action_value = response.action_value;
  agent->state.chat_text_length = response.text_length;
  if (response.text_length != 0)
    {
      memcpy(agent->state.chat_text, response.text, response.text_length);
    }
  agent->state.chat_text[response.text_length] = '\0';
  agent->state.chat_responses++;

  if (response.status != DEMO_LLM_STATUS_OK)
    {
      agent->state.chat_failures++;
      if (agent->chat_is_automatic)
        {
          agent->state.agent_auto_failures++;
        }
      return;
    }
  if (response.action_type == DEMO_LLM_ACTION_NONE)
    {
      return;
    }

  if (response.action_type == DEMO_LLM_ACTION_SET_FAN &&
      response.action_value <= 1 &&
      umca_gd32_agent_node_online(agent, agent->actuator_dev_id))
    {
      agent->queued_control = true;
      agent->queued_control_target = response.action_value == 1;
      agent->queued_control_source = UMCA_GD32_CONTROL_LLM;
      agent->state.llm_actions_accepted++;
    }
  else
    {
      agent->state.llm_actions_rejected++;
      if (agent->chat_is_automatic)
        {
          agent->state.agent_auto_failures++;
        }
    }
}

static char *umca_tools_json(void)
{
  static const char tools[] =
    "[{\"name\":\"sensor_query\",\"description\":\"Read the trusted "
    "GD32 temperature snapshot.\",\"input_schema\":{\"type\":\"object\","
    "\"properties\":{},\"required\":[]}},{\"name\":\"device_control\","
    "\"description\":\"Set the UMCA fan after GD32 authorization.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{\"device\":{"
    "\"type\":\"string\",\"enum\":[\"fan\"]},\"state\":{\"type\":"
    "\"string\",\"enum\":[\"on\",\"off\"]}},\"required\":[\"device\","
    "\"state\"]}}]";
  char *copy = malloc(sizeof(tools));
  if (copy != NULL)
    {
      memcpy(copy, tools, sizeof(tools));
    }
  return copy;
}

static int umca_tool_execute(const char *name, const char *input_json,
                             char *output, size_t output_size)
{
  bool on;
  if (g_tool_agent == NULL || name == NULL || input_json == NULL)
    {
      return -1;
    }
  if (strcmp(name, "sensor_query") == 0 && strcmp(input_json, "{}") == 0)
    {
      return umca_gd32_agent_sensor_query_skill(g_tool_agent, output,
                                                output_size) == UMCA_OK ?
             0 : -1;
    }
  if (strcmp(name, "device_control") != 0)
    {
      return -1;
    }
  if (strcmp(input_json, "{\"device\":\"fan\",\"state\":\"on\"}") == 0)
    {
      on = true;
    }
  else if (strcmp(input_json,
                  "{\"device\":\"fan\",\"state\":\"off\"}") == 0)
    {
      on = false;
    }
  else
    {
      (void)checked_snprintf(output, output_size,
                             "{\"result\":\"rejected\","
                             "\"reason\":\"invalid_arguments\"}");
      return -1;
    }
  return umca_gd32_agent_device_control_skill(g_tool_agent, on, output,
                                               output_size) == UMCA_OK ?
         0 : -1;
}

int umca_gd32_agent_init(umca_gd32_agent_t *agent, umca_context_t *ctx,
                         umca_devid_t sensor_dev_id,
                         umca_devid_t actuator_dev_id,
                         umca_devid_t esp32_dev_id)
{
  int ret;
  if (agent == NULL || ctx == NULL ||
      ctx->state != UMCA_CONTEXT_INITIALIZED ||
      sensor_dev_id == UMCA_INVALID_DEVID ||
      actuator_dev_id == UMCA_INVALID_DEVID ||
      esp32_dev_id == UMCA_INVALID_DEVID)
    {
      return UMCA_ERR_INVALID_ARG;
    }

  memset(agent, 0, sizeof(*agent));
  agent->ctx = ctx;
  agent->sensor_dev_id = sensor_dev_id;
  agent->actuator_dev_id = actuator_dev_id;
  agent->esp32_dev_id = esp32_dev_id;
  agent->requester_boot_id = ctx->local_boot_id;
  agent->next_request_id = 1;
  agent->state.auto_enabled = true;

  ret = umca_topic_register(ctx, DEMO_TOPIC_TEMPERATURE_NAME,
                            UMCA_TOPIC_SUBSCRIBER, on_temperature, agent,
                            NULL);
  if (ret == UMCA_OK)
    {
      ret = umca_topic_register(ctx, DEMO_TOPIC_THRESHOLD_NAME,
                                UMCA_TOPIC_SUBSCRIBER, on_threshold, agent,
                                NULL);
    }
  if (ret == UMCA_OK)
    {
      ret = umca_topic_register(ctx, DEMO_TOPIC_FAN_STATE_NAME,
                                UMCA_TOPIC_SUBSCRIBER, on_fan_state, agent,
                                NULL);
    }
  if (ret == UMCA_OK)
    {
      ret = umca_topic_register(ctx, DEMO_TOPIC_FAN_COMMAND_NAME,
                                UMCA_TOPIC_PUBLISHER, NULL, NULL, NULL);
    }
  if (ret == UMCA_OK)
    {
      ret = umca_topic_register(ctx, DEMO_TOPIC_LLM_CHAT_REQUEST_NAME,
                                UMCA_TOPIC_PUBLISHER, NULL, NULL, NULL);
    }
  if (ret == UMCA_OK)
    {
      ret = umca_topic_register(ctx, DEMO_TOPIC_LLM_CHAT_RESPONSE_NAME,
                                UMCA_TOPIC_SUBSCRIBER, on_chat_response,
                                agent, NULL);
    }
  if (ret == UMCA_OK)
    {
      g_tool_agent = agent;
      if (!g_tool_provider_registered)
        {
          tool_registry_register_provider("umca", umca_tools_json,
                                          umca_tool_execute);
          tool_registry_invalidate();
          g_tool_provider_registered = true;
        }
    }
  return ret;
}

void umca_gd32_agent_set_pump(umca_gd32_agent_t *agent,
                              umca_gd32_agent_pump_t pump, void *user)
{
  if (agent != NULL)
    {
      agent->pump = pump;
      agent->pump_user = user;
    }
}

int umca_gd32_agent_query_temperature(umca_gd32_agent_t *agent,
                                      char *output, size_t output_size)
{
  umca_gd32_sensor_snapshot_t snapshot;
  if (agent == NULL || output == NULL || output_size == 0)
    {
      return UMCA_ERR_INVALID_ARG;
    }

  capture_sensor_snapshot(agent, &snapshot);
  if (!snapshot.sensor_online)
    {
      (void)checked_snprintf(output, output_size, "sensor_offline");
      return UMCA_ERR_NOT_FOUND;
    }
  if (!agent->state.sample_present || !agent->state.sample_valid)
    {
      (void)checked_snprintf(output, output_size, "sample_unavailable");
      return UMCA_ERR_NOT_FOUND;
    }
  if (!snapshot.available)
    {
      uint32_t age = agent_now(agent) - agent->state.sample_received_ms;
      (void)checked_snprintf(output, output_size,
                             "sample_expired age_ms=%lu",
                             (unsigned long)age);
      return UMCA_ERR_TIMEOUT;
    }
  if (checked_snprintf(output, output_size,
                       "temperature_mc=%ld age_ms=%lu quality=%u",
                       (long)snapshot.temperature_mc,
                       (unsigned long)snapshot.age_ms,
                       snapshot.quality) != UMCA_OK)
    {
      return UMCA_ERR_CAPACITY;
    }
  return UMCA_OK;
}

int umca_gd32_agent_sensor_query_skill(umca_gd32_agent_t *agent,
                                       char *output, size_t output_size)
{
  const char *reason;
  int ret;
  if (agent == NULL || output == NULL || output_size == 0)
    {
      return UMCA_ERR_INVALID_ARG;
    }

  capture_sensor_snapshot(agent, &agent->skill_snapshot);
  if (agent->skill_snapshot.available)
    {
      ret = checked_snprintf(
          output, output_size,
          "{\"available\":true,\"temperature_mC\":%ld,\"age_ms\":%lu,"
          "\"quality\":%u,\"sensor_online\":true}",
          (long)agent->skill_snapshot.temperature_mc,
          (unsigned long)agent->skill_snapshot.age_ms,
          agent->skill_snapshot.quality);
      if (ret == UMCA_OK)
        {
          printf("[umca-gd32] skill=sensor_query result=ok available=1 "
                 "age_ms=%lu\n",
                 (unsigned long)agent->skill_snapshot.age_ms);
        }
      return ret;
    }

  if (!agent->skill_snapshot.sensor_online)
    {
      reason = "offline";
    }
  else if (!agent->state.sample_present || !agent->state.sample_valid)
    {
      reason = "quality";
    }
  else
    {
      reason = "stale";
    }
  ret = checked_snprintf(output, output_size,
                         "{\"available\":false,\"sensor_online\":%s,"
                         "\"reason\":\"%s\"}",
                         agent->skill_snapshot.sensor_online ? "true" :
                                                               "false",
                         reason);
  if (ret == UMCA_OK)
    {
      printf("[umca-gd32] skill=sensor_query result=unavailable "
             "reason=%s\n", reason);
    }
  return ret;
}

int umca_gd32_agent_control_fan(umca_gd32_agent_t *agent, bool on,
                                uint8_t source, uint32_t *request_id)
{
  demo_fan_command_t command;
  uint8_t payload[6];
  uint32_t boot_id;
  int ret;
  if (agent == NULL || source > UMCA_GD32_CONTROL_LOCAL_RULE)
    {
      return UMCA_ERR_INVALID_ARG;
    }
  if (agent->state.fan_request_pending)
    {
      return UMCA_ERR_BAD_STATE;
    }
  if (node_session(agent, agent->actuator_dev_id, &boot_id) != UMCA_OK)
    {
      agent->state.fan_result = UMCA_GD32_FAN_RESULT_OFFLINE;
      return UMCA_ERR_NOT_FOUND;
    }

  command.request_id = allocate_request_id(agent);
  command.command = on ? 1 : 0;
  command.source = source;
  if (demo_encode_fan_command(&command, payload) != 0)
    {
      return UMCA_ERR_BAD_PAYLOAD;
    }
  ret = umca_publish(agent->ctx, DEMO_TOPIC_FAN_COMMAND,
                     agent->actuator_dev_id, payload, sizeof(payload));
  if (ret != UMCA_OK)
    {
      return ret;
    }

  agent->pending_fan_target = on;
  agent->pending_fan_mismatch = false;
  agent->pending_actuator_boot_id = boot_id;
  agent->fan_deadline_ms = agent_now(agent) + UMCA_GD32_FAN_TIMEOUT_MS;
  agent->state.fan_request_pending = true;
  agent->state.fan_request_id = command.request_id;
  agent->state.fan_result = UMCA_GD32_FAN_RESULT_PENDING;
  agent->state.fan_commands++;
  if (request_id != NULL)
    {
      *request_id = command.request_id;
    }
  return UMCA_OK;
}

int umca_gd32_agent_device_control_skill(umca_gd32_agent_t *agent,
                                         bool on, char *output,
                                         size_t output_size)
{
  uint32_t request_id = 0;
  const char *result;
  int ret;

  if (agent == NULL || output == NULL || output_size == 0)
    {
      return UMCA_ERR_INVALID_ARG;
    }
  ret = umca_gd32_agent_control_fan(agent, on, UMCA_GD32_CONTROL_LLM,
                                    &request_id);
  if (ret != UMCA_OK)
    {
      result = ret == UMCA_ERR_NOT_FOUND ? "offline" : "rejected";
      (void)checked_snprintf(output, output_size,
                             "{\"result\":\"%s\"}", result);
      printf("[umca-gd32] skill=device_control requested=%s result=%s\n",
             on ? "on" : "off", result);
      return ret;
    }

  while (agent->state.fan_request_pending)
    {
      if (deadline_reached(agent_now(agent), agent->fan_deadline_ms))
        {
          agent->state.fan_timeouts++;
          finish_fan_request(agent, agent->pending_fan_mismatch ?
                             UMCA_GD32_FAN_RESULT_MISMATCH :
                             UMCA_GD32_FAN_RESULT_TIMEOUT);
          break;
        }
      if (agent->pump == NULL || agent->pump(agent->pump_user) != UMCA_OK)
        {
          usleep(1000);
        }
    }

  switch (agent->state.fan_result)
    {
      case UMCA_GD32_FAN_RESULT_CONFIRMED:
        result = "confirmed";
        ret = UMCA_OK;
        break;
      case UMCA_GD32_FAN_RESULT_MISMATCH:
        result = "mismatch";
        ret = UMCA_ERR_BAD_PAYLOAD;
        break;
      case UMCA_GD32_FAN_RESULT_REJECTED:
        result = "rejected";
        ret = UMCA_ERR_BAD_STATE;
        break;
      default:
        result = "timeout";
        ret = UMCA_ERR_TIMEOUT;
        break;
    }
  if (checked_snprintf(output, output_size,
                       "{\"result\":\"%s\",\"request_id\":%lu,"
                       "\"state\":\"%s\"}", result,
                       (unsigned long)request_id, on ? "on" : "off") !=
      UMCA_OK)
    {
      ret = UMCA_ERR_CAPACITY;
    }
  printf("[umca-gd32] skill=device_control requested=%s result=%s\n",
         on ? "on" : "off", result);
  return ret;
}

int umca_gd32_agent_validate_user_text(const uint8_t *text, size_t length)
{
  size_t i = 0;
  if (text == NULL || length == 0)
    {
      return UMCA_ERR_INVALID_ARG;
    }

  while (i < length)
    {
      uint32_t cp;
      uint8_t first = text[i++];
      if (first < 0x80)
        {
          cp = first;
        }
      else if (first >= 0xc2 && first <= 0xdf && i < length &&
               (text[i] & 0xc0) == 0x80)
        {
          cp = ((uint32_t)(first & 0x1f) << 6) |
               (uint32_t)(text[i++] & 0x3f);
        }
      else if (first >= 0xe0 && first <= 0xef && i + 1 < length &&
               (text[i] & 0xc0) == 0x80 && (text[i + 1] & 0xc0) == 0x80 &&
               !(first == 0xe0 && text[i] < 0xa0) &&
               !(first == 0xed && text[i] >= 0xa0))
        {
          cp = ((uint32_t)(first & 0x0f) << 12) |
               ((uint32_t)(text[i] & 0x3f) << 6) |
               (uint32_t)(text[i + 1] & 0x3f);
          i += 2;
        }
      else if (first >= 0xf0 && first <= 0xf4 && i + 2 < length &&
               (text[i] & 0xc0) == 0x80 && (text[i + 1] & 0xc0) == 0x80 &&
               (text[i + 2] & 0xc0) == 0x80 &&
               !(first == 0xf0 && text[i] < 0x90) &&
               !(first == 0xf4 && text[i] >= 0x90))
        {
          cp = ((uint32_t)(first & 0x07) << 18) |
               ((uint32_t)(text[i] & 0x3f) << 12) |
               ((uint32_t)(text[i + 1] & 0x3f) << 6) |
               (uint32_t)(text[i + 2] & 0x3f);
          i += 3;
        }
      else
        {
          return UMCA_ERR_BAD_PAYLOAD;
        }

      if (cp <= 0x1f || (cp >= 0x7f && cp <= 0x9f))
        {
          return UMCA_ERR_BAD_PAYLOAD;
        }
    }
  return UMCA_OK;
}

int umca_gd32_agent_build_c1(umca_gd32_agent_t *agent,
                             const umca_gd32_sensor_snapshot_t *snapshot,
                             const uint8_t *user_text,
                             uint16_t user_text_length, char *output,
                             size_t output_size, uint16_t *output_length)
{
  unsigned int fan_state;
  int prefix_length;
  size_t total;

  if (agent == NULL || snapshot == NULL || user_text == NULL ||
      user_text_length == 0 || output == NULL || output_length == NULL ||
      output_size == 0 ||
      umca_gd32_agent_validate_user_text(user_text, user_text_length) !=
      UMCA_OK)
    {
      return UMCA_ERR_INVALID_ARG;
    }
  fan_state = agent->state.fan_known ? (agent->state.fan_on ? 1u : 0u) : 2u;
  if (snapshot->available)
    {
      uint32_t age = snapshot->age_ms > UMCA_GD32_SAMPLE_MAX_AGE_MS ?
                     UMCA_GD32_SAMPLE_MAX_AGE_MS : snapshot->age_ms;
      prefix_length = snprintf(
          output, output_size,
          "C1;tv=1;t=%ld;age=%lu;q=%u;so=%u;ao=%u;f=%u\nQ:",
          (long)snapshot->temperature_mc, (unsigned long)age,
          snapshot->quality, snapshot->sensor_online ? 1u : 0u,
          umca_gd32_agent_node_online(agent, agent->actuator_dev_id) ? 1u : 0u,
          fan_state);
    }
  else
    {
      prefix_length = snprintf(
          output, output_size, "C1;tv=0;so=%u;ao=%u;f=%u\nQ:",
          snapshot->sensor_online ? 1u : 0u,
          umca_gd32_agent_node_online(agent, agent->actuator_dev_id) ? 1u : 0u,
          fan_state);
    }
  if (prefix_length < 0 || (size_t)prefix_length >= output_size)
    {
      output[0] = '\0';
      return UMCA_ERR_CAPACITY;
    }

  total = (size_t)prefix_length + user_text_length;
  if (total > DEMO_LLM_CHAT_REQUEST_TEXT_MAX || total >= output_size)
    {
      output[0] = '\0';
      return UMCA_ERR_PAYLOAD_TOO_LARGE;
    }
  memcpy(output + prefix_length, user_text, user_text_length);
  output[total] = '\0';
  *output_length = (uint16_t)total;
  return UMCA_OK;
}

int umca_gd32_agent_chat(umca_gd32_agent_t *agent, const uint8_t *text,
                         uint16_t text_length, uint8_t origin,
                         uint32_t *request_id)
{
  demo_llm_chat_request_t request;
  uint8_t payload[UMCA_MAX_PAYLOAD];
  char context[UMCA_GD32_C1_TEXT_CAPACITY];
  char skill_output[UMCA_GD32_SKILL_OUTPUT_MAX];
  uint16_t context_length;
  uint16_t payload_length;
  uint32_t boot_id;
  int ret;

  if (agent == NULL || text == NULL || text_length == 0 ||
      origin > DEMO_LLM_ORIGIN_AUTOMATIC)
    {
      return UMCA_ERR_INVALID_ARG;
    }
  ret = umca_gd32_agent_validate_user_text(text, text_length);
  if (ret != UMCA_OK)
    {
      return ret;
    }
  if (agent->state.chat_request_pending)
    {
      return UMCA_ERR_BAD_STATE;
    }
  if (node_session(agent, agent->esp32_dev_id, &boot_id) != UMCA_OK)
    {
      return UMCA_ERR_NOT_FOUND;
    }

  if (tool_registry_execute("sensor_query", "{}", skill_output,
                            sizeof(skill_output)) != 0)
    {
      agent->state.chat_failures++;
      return UMCA_ERR_BAD_STATE;
    }
  ret = umca_gd32_agent_build_c1(agent, &agent->skill_snapshot, text,
                                 text_length, context, sizeof(context),
                                 &context_length);
  if (ret != UMCA_OK)
    {
      return ret;
    }

  request.requester_boot_id = agent->requester_boot_id;
  request.request_id = allocate_request_id(agent);
  request.timeout_ms = UMCA_GD32_CHAT_TIMEOUT_MS;
  request.origin = origin;
  request.text_length = context_length;
  request.text = (const uint8_t *)context;
  if (demo_encode_llm_chat_request(&request, payload, &payload_length) != 0)
    {
      return UMCA_ERR_BAD_PAYLOAD;
    }
  ret = umca_publish(agent->ctx, DEMO_TOPIC_LLM_CHAT_REQUEST,
                     agent->esp32_dev_id, payload, payload_length);
  if (ret != UMCA_OK)
    {
      return ret;
    }

  agent->chat_deadline_ms = agent_now(agent) + request.timeout_ms;
  agent->pending_esp32_boot_id = boot_id;
  agent->chat_is_automatic = origin == DEMO_LLM_ORIGIN_AUTOMATIC;
  agent->state.chat_request_pending = true;
  agent->state.chat_request_id = request.request_id;
  agent->state.chat_requests++;
  if (agent->skill_snapshot.available)
    {
      printf("[umca-gd32] chat request=%lu snapshot_age_ms=%lu\n",
             (unsigned long)request.request_id,
             (unsigned long)agent->skill_snapshot.age_ms);
    }
  else
    {
      printf("[umca-gd32] chat request=%lu snapshot_age_ms=unavailable\n",
             (unsigned long)request.request_id);
    }
  if (request_id != NULL)
    {
      *request_id = request.request_id;
    }
  return UMCA_OK;
}

void umca_gd32_agent_set_auto(umca_gd32_agent_t *agent, bool enabled)
{
  if (agent != NULL)
    {
      agent->state.auto_enabled = enabled;
      if (enabled && agent->state.agent_auto_enabled)
        {
          agent->state.agent_auto_enabled = false;
          agent->queued_agent_eval = false;
          if (agent->state.chat_request_pending && agent->chat_is_automatic)
            {
              agent->state.chat_request_pending = false;
            }
        }
      if (!enabled && agent->queued_control_source ==
                      UMCA_GD32_CONTROL_LOCAL_RULE)
        {
          agent->queued_control = false;
        }
    }
}

int umca_gd32_agent_set_agent_auto(umca_gd32_agent_t *agent, bool enabled)
{
  if (agent == NULL)
    {
      return UMCA_ERR_INVALID_ARG;
    }
  if (enabled && agent->state.auto_enabled)
    {
      return UMCA_ERR_BAD_STATE;
    }
  agent->state.agent_auto_enabled = enabled;
  if (!enabled)
    {
      agent->queued_agent_eval = false;
      if (agent->state.chat_request_pending && agent->chat_is_automatic)
        {
          agent->state.chat_request_pending = false;
        }
    }
  return UMCA_OK;
}

const char *umca_gd32_agent_llm_status_name(uint8_t status)
{
  switch (status)
    {
      case DEMO_LLM_STATUS_OK:
        return "ok";
      case DEMO_LLM_STATUS_REJECTED:
        return "rejected";
      case DEMO_LLM_STATUS_UNAVAILABLE:
        return "unavailable";
      case DEMO_LLM_STATUS_TIMEOUT:
        return "timeout";
      case DEMO_LLM_STATUS_ERROR:
        return "error";
      default:
        return "unknown";
    }
}

void umca_gd32_agent_tick(umca_gd32_agent_t *agent)
{
  static const uint8_t auto_question[] = UMCA_GD32_AUTO_QUESTION;
  char tool_input[48];
  uint32_t now;
  int ret;
  if (agent == NULL || agent->ctx == NULL)
    {
      return;
    }

  now = agent_now(agent);
  if (agent->state.fan_request_pending &&
      deadline_reached(now, agent->fan_deadline_ms))
    {
      agent->state.fan_timeouts++;
      finish_fan_request(agent, agent->pending_fan_mismatch ?
                         UMCA_GD32_FAN_RESULT_MISMATCH :
                         UMCA_GD32_FAN_RESULT_TIMEOUT);
    }
  if (agent->state.chat_request_pending &&
      deadline_reached(now, agent->chat_deadline_ms))
    {
      fail_late_chat(agent);
    }

  if (agent->queued_control && !agent->state.fan_request_pending)
    {
      bool target = agent->queued_control_target;
      uint8_t source = agent->queued_control_source;
      agent->queued_control = false;
      if (source == UMCA_GD32_CONTROL_LLM)
        {
          ret = checked_snprintf(tool_input, sizeof(tool_input),
                                 "{\"device\":\"fan\",\"state\":\"%s\"}",
                                 target ? "on" : "off");
          if (ret == UMCA_OK)
            {
              ret = tool_registry_execute(
                  "device_control", tool_input, agent->last_action_result,
                  sizeof(agent->last_action_result));
            }
          if (ret != 0)
            {
              agent->state.llm_actions_rejected++;
              if (agent->chat_is_automatic)
                {
                  agent->state.agent_auto_failures++;
                }
            }
          agent->state.llm_actions_completed++;
        }
      else
        {
          (void)umca_gd32_agent_control_fan(agent, target, source, NULL);
        }
    }

  if (agent->queued_agent_eval && !agent->state.chat_request_pending)
    {
      agent->queued_agent_eval = false;
      ret = umca_gd32_agent_chat(
          agent, auto_question, (uint16_t)(sizeof(auto_question) - 1u),
          DEMO_LLM_ORIGIN_AUTOMATIC, NULL);
      if (ret != UMCA_OK)
        {
          agent->state.agent_auto_failures++;
        }
    }
}

void umca_gd32_agent_get_state(const umca_gd32_agent_t *agent,
                               umca_gd32_agent_state_t *state)
{
  if (agent != NULL && state != NULL)
    {
      *state = agent->state;
    }
}
