#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tools/tool_registry.h"

#include "demo/demo_topics.h"
#include "umca_agent_adapter.h"

/* The UMCA service can run without the optional ai_agent application.  When
 * ai_agent is linked into the same NuttX image this symbol is provided by its
 * tool registry; otherwise the weak reference keeps the three-node service
 * self-contained and the provider is simply not registered. */
extern void tool_registry_register_provider(const char *name,
                                            tool_provider_fn get_tools,
                                            tool_executor_fn execute)
  __attribute__((weak));
extern void tool_registry_invalidate(void) __attribute__((weak));

static umca_agent_adapter_t *g_adapter;
static bool g_provider_registered;

static uint32_t adapter_now(umca_agent_adapter_t *adapter)
{
  return adapter->ctx->platform.ops->time_ms(adapter->ctx->platform.user);
}

static void on_temperature(umca_context_t *ctx, const umca_message_t *message,
                           void *user_data)
{
  umca_agent_adapter_t *adapter = user_data;
  demo_temperature_sample_t sample;
  (void)ctx;
  if (message->source == adapter->sensor_dev_id &&
      demo_decode_temperature_sample(message->payload,
                                      message->payload_length, &sample) == 0)
    {
      adapter->sample_present = true;
      adapter->sample_valid = sample.quality == 1;
      adapter->temperature_mc = sample.temperature_mc;
      adapter->sample_time_ms = sample.sample_time_ms;
      adapter->received_time_ms = adapter_now(adapter);
      adapter->sample_source = message->source;
    }
}

static void on_fan_state(umca_context_t *ctx, const umca_message_t *message,
                         void *user_data)
{
  umca_agent_adapter_t *adapter = user_data;
  demo_fan_state_t state;
  (void)ctx;
  if (message->source == adapter->actuator_dev_id &&
      demo_decode_fan_state(message->payload, message->payload_length,
                            &state) == 0 && adapter->request_pending &&
      state.request_id == adapter->pending_request_id)
    {
      adapter->request_completed = true;
      adapter->pending_result = state.result;
      adapter->pending_state = state.state;
    }
}

static char *adapter_tools(void)
{
  static const char tools[] =
    "[{\"name\":\"sensor_query\",\"description\":\"Read the latest "
    "UMCA temperature sample.\",\"input_schema\":{\"type\":\"object\","
    "\"properties\":{\"sensor\":{\"type\":\"string\"}},"
    "\"required\":[\"sensor\"]}},{\"name\":\"device_control\","
    "\"description\":\"Control the UMCA virtual fan and wait for its "
    "acknowledged state.\",\"input_schema\":{\"type\":\"object\","
    "\"properties\":{\"device\":{\"type\":\"string\"},"
    "\"command\":{\"type\":\"string\"}},\"required\":[\"device\","
    "\"command\"]}}]";
  size_t length = sizeof(tools);
  char *copy = malloc(length);
  if (copy != NULL)
    {
      memcpy(copy, tools, length);
    }
  return copy;
}

static int tool_sensor_query(const char *input, char *output,
                             size_t output_size)
{
  (void)input;
  if (g_adapter == NULL)
    {
      return -1;
    }
  return umca_agent_adapter_sensor_query(g_adapter, output,
                                         (uint32_t)output_size);
}

static int tool_device_control(const char *input, char *output,
                               size_t output_size)
{
  bool on;
  if (g_adapter == NULL || input == NULL || output == NULL)
    {
      return -1;
    }
  if (strstr(input, "\"command\":\"on\"") != NULL)
    {
      on = true;
    }
  else if (strstr(input, "\"command\":\"off\"") != NULL)
    {
      on = false;
    }
  else
    {
      snprintf(output, output_size, "{\"ok\":false,\"error\":\"bad_command\"}");
      return -1;
    }
  return umca_agent_adapter_device_control(g_adapter, on, output,
                                            (uint32_t)output_size);
}

static int tool_execute(const char *name, const char *input, char *output,
                        size_t output_size)
{
  if (strcmp(name, "sensor_query") == 0)
    {
      return tool_sensor_query(input, output, output_size);
    }
  if (strcmp(name, "device_control") == 0)
    {
      return tool_device_control(input, output, output_size);
    }
  return -1;
}

int umca_agent_adapter_init(umca_agent_adapter_t *adapter,
                            umca_context_t *ctx,
                            umca_devid_t sensor_dev_id,
                            umca_devid_t actuator_dev_id)
{
  int ret;
  if (adapter == NULL || ctx == NULL || ctx->state != UMCA_CONTEXT_INITIALIZED)
    {
      return UMCA_ERR_INVALID_ARG;
    }
  memset(adapter, 0, sizeof(*adapter));
  adapter->ctx = ctx;
  adapter->sensor_dev_id = sensor_dev_id;
  adapter->actuator_dev_id = actuator_dev_id;
  adapter->sample_max_age_ms = 5000;
  adapter->next_request_id = 1;
  ret = umca_topic_register(ctx, "/sensors/temperature",
                            UMCA_TOPIC_SUBSCRIBER, on_temperature, adapter,
                            NULL);
  if (ret != UMCA_OK)
    {
      return ret;
    }
  ret = umca_topic_register(ctx, "/actuators/fan/state",
                            UMCA_TOPIC_SUBSCRIBER, on_fan_state, adapter,
                            NULL);
  if (ret != UMCA_OK)
    {
      return ret;
    }
  ret = umca_topic_register(ctx, "/actuators/fan/command",
                            UMCA_TOPIC_PUBLISHER, NULL, NULL, NULL);
  if (ret != UMCA_OK)
    {
      return ret;
    }
  adapter->initialized = true;
  g_adapter = adapter;
  if (!g_provider_registered && tool_registry_register_provider != NULL)
    {
      tool_registry_register_provider("umca", adapter_tools, tool_execute);
      g_provider_registered = true;
      if (tool_registry_invalidate != NULL)
        {
          tool_registry_invalidate();
        }
    }
  return UMCA_OK;
}

void umca_agent_adapter_tick(umca_agent_adapter_t *adapter)
{
  uint16_t processed;
  if (adapter != NULL && adapter->initialized)
    {
      (void)umca_poll(adapter->ctx, &processed);
    }
}

void umca_agent_adapter_set_pump(umca_agent_adapter_t *adapter,
                                 umca_agent_pump_t pump, void *user)
{
  if (adapter != NULL)
    {
      adapter->pump = pump;
      adapter->pump_user = user;
    }
}

int umca_agent_adapter_sensor_query(umca_agent_adapter_t *adapter,
                                    char *output, uint32_t output_size)
{
  uint32_t age;
  umca_node_info_t node;
  if (adapter == NULL || output == NULL || output_size == 0 ||
      !adapter->initialized)
    {
      return UMCA_ERR_BAD_STATE;
    }
  if (!adapter->sample_present || !adapter->sample_valid ||
      umca_node_get(adapter->ctx, adapter->sensor_dev_id, &node) != UMCA_OK ||
      node.state != UMCA_NODE_ONLINE)
    {
      snprintf(output, output_size, "{\"ok\":false,\"error\":\"sensor_offline\"}");
      return UMCA_ERR_NOT_FOUND;
    }
  age = adapter_now(adapter) - adapter->received_time_ms;
  if (age > adapter->sample_max_age_ms)
    {
      snprintf(output, output_size, "{\"ok\":false,\"error\":\"sample_expired\"}");
      return UMCA_ERR_TIMEOUT;
    }
  snprintf(output, output_size,
           "{\"ok\":true,\"online\":true,\"valid\":true,"
           "\"temperature_mc\":%ld,\"age_ms\":%lu}",
           (long)adapter->temperature_mc, (unsigned long)age);
  return UMCA_OK;
}

int umca_agent_adapter_device_control(umca_agent_adapter_t *adapter,
                                      bool on, char *output,
                                      uint32_t output_size)
{
  demo_fan_command_t command;
  uint32_t deadline;
  umca_node_info_t node;
  int ret;
  if (adapter == NULL || output == NULL || output_size == 0 ||
      !adapter->initialized)
    {
      return UMCA_ERR_BAD_STATE;
    }
  if (umca_node_get(adapter->ctx, adapter->actuator_dev_id, &node) !=
      UMCA_OK || node.state != UMCA_NODE_ONLINE)
    {
      snprintf(output, output_size, "{\"ok\":false,\"error\":\"actuator_offline\"}");
      return UMCA_ERR_NOT_FOUND;
    }
  if (adapter->next_request_id == 0)
    {
      adapter->next_request_id = 1;
    }
  adapter->pending_request_id = adapter->next_request_id++;
  adapter->request_pending = true;
  adapter->request_completed = false;
  command.request_id = adapter->pending_request_id;
  command.command = on ? 1 : 0;
  command.source = 1;
  {
    uint8_t payload[6];
    if (demo_encode_fan_command(&command, payload) != 0)
      {
        adapter->request_pending = false;
        return UMCA_ERR_BAD_PAYLOAD;
      }
    ret = umca_publish(adapter->ctx, DEMO_TOPIC_FAN_COMMAND,
                       adapter->actuator_dev_id, payload, sizeof(payload));
  }
  if (ret != UMCA_OK)
    {
      adapter->request_pending = false;
      snprintf(output, output_size, "{\"ok\":false,\"error\":\"publish_failed\"}");
      return ret;
    }
  deadline = adapter_now(adapter) + 1000u;
  while (!adapter->request_completed &&
         (uint32_t)(deadline - adapter_now(adapter)) < UINT32_C(0x80000000))
    {
      if (adapter->pump != NULL)
        {
          adapter->pump(adapter->pump_user);
        }
      else
        {
          umca_agent_adapter_tick(adapter);
        }
    }
  ret = adapter->request_completed && adapter->pending_result == 0 ?
        UMCA_OK : UMCA_ERR_TIMEOUT;
  if (ret == UMCA_OK)
    {
      snprintf(output, output_size,
               "{\"ok\":true,\"request_id\":%lu,\"state\":%u}",
               (unsigned long)adapter->pending_request_id,
               adapter->pending_state);
    }
  else
    {
      snprintf(output, output_size,
               "{\"ok\":false,\"request_id\":%lu,\"error\":\"timeout_or_rejected\"}",
               (unsigned long)adapter->pending_request_id);
    }
  adapter->request_pending = false;
  return ret;
}
