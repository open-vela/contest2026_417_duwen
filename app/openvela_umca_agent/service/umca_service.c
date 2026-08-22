#ifndef _DEFAULT_SOURCE
#  define _DEFAULT_SOURCE 1
#endif

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "demo/demo_topics.h"
#include "adapter/umca_agent_adapter.h"
#include "service/umca_service.h"
#include "umca/umca.h"
#include "umca_loopback.h"
#include "umca_openvela_pal.h"

#define UMCA_SENSOR_DEVID UINT64_C(0x0000000000001001)
#define UMCA_AGENT_DEVID UINT64_C(0x0000000000002001)
#define UMCA_ACTUATOR_DEVID UINT64_C(0x0000000000003001)
#define UMCA_SENSOR_BOOT_ID UINT32_C(0x10010001)
#define UMCA_AGENT_BOOT_ID UINT32_C(0x20010001)
#define UMCA_ACTUATOR_BOOT_ID UINT32_C(0x30010001)

typedef struct
{
  int32_t temperature_mc;
  int32_t on_threshold_mc;
  int32_t off_threshold_mc;
  uint32_t last_sample_ms;
  bool high_state;
} virtual_sensor_t;

typedef struct
{
  bool running;
  pthread_t thread;
  umca_loopback_bus_t bus;
  umca_loopback_endpoint_t endpoints[3];
  umca_phy_t phys[3];
  umca_platform_t platform;
  umca_context_t contexts[3];
  umca_agent_adapter_t adapter;
  virtual_sensor_t sensor;
  bool command_pending;
  bool desired_fan_state;
} umca_service_t;

static umca_service_t g_service;

static uint32_t service_now(void)
{
  return g_service.platform.ops->time_ms(g_service.platform.user);
}

static void service_pump(void *user)
{
  umca_service_t *service = user;
  unsigned int i;
  uint16_t processed;
  for (i = 0; i < 3; i++)
    {
      (void)umca_poll(&service->contexts[i], &processed);
    }
}

static void on_threshold(umca_context_t *ctx, const umca_message_t *message,
                         void *user_data)
{
  umca_service_t *service = user_data;
  demo_threshold_event_t event;
  (void)ctx;
  if (message->source != UMCA_SENSOR_DEVID ||
      demo_decode_threshold_event(message->payload, message->payload_length,
                                  &event) != 0)
    {
      return;
    }
  service->command_pending = true;
  service->desired_fan_state = event.direction == 1;
  printf("[umca] threshold event temperature=%ld direction=%u\n",
         (long)event.temperature_mc, event.direction);
}

static void on_fan_command(umca_context_t *ctx, const umca_message_t *message,
                           void *user_data)
{
  umca_service_t *service = user_data;
  demo_fan_command_t command;
  demo_fan_state_t state;
  uint8_t payload[10];
  (void)ctx;
  if (message->source != UMCA_AGENT_DEVID ||
      demo_decode_fan_command(message->payload, message->payload_length,
                              &command) != 0)
    {
      return;
    }
  state.request_id = command.request_id;
  state.state = command.command;
  state.result = 0;
  state.applied_time_ms = service_now();
  if (demo_encode_fan_state(&state, payload) == 0)
    {
      (void)umca_publish(&service->contexts[2], DEMO_TOPIC_FAN_STATE,
                         UMCA_AGENT_DEVID, payload, sizeof(payload));
      printf("[umca] actuator applied request=%lu state=%u\n",
             (unsigned long)state.request_id, state.state);
    }
}

static void sensor_publish(umca_service_t *service, uint32_t now)
{
  demo_temperature_sample_t sample;
  uint8_t payload[13];
  uint32_t phase = (now / 5000u) & 1u;
  bool high = phase == 0;
  sample.temperature_mc = high ? 31000 : 27000;
  sample.sample_time_ms = now;
  sample.quality = 1;
  if (demo_encode_temperature_sample(&sample, payload) == 0)
    {
      (void)umca_publish(&service->contexts[0], DEMO_TOPIC_TEMPERATURE,
                         UMCA_BROADCAST_DEVID, payload, 9);
    }
  if (high != service->sensor.high_state)
    {
      demo_threshold_event_t event;
      event.temperature_mc = sample.temperature_mc;
      event.threshold_mc = high ? service->sensor.on_threshold_mc :
                                  service->sensor.off_threshold_mc;
      event.direction = high ? 1 : 0;
      event.event_time_ms = now;
      if (demo_encode_threshold_event(&event, payload) == 0)
        {
          (void)umca_publish(&service->contexts[0], DEMO_TOPIC_THRESHOLD,
                             UMCA_BROADCAST_DEVID, payload, sizeof(payload));
        }
      service->sensor.high_state = high;
    }
  service->sensor.last_sample_ms = now;
  printf("[umca] sensor temperature=%ld\n", (long)sample.temperature_mc);
}

static int service_init(umca_service_t *service)
{
  int ret;
  unsigned int i;
  memset(service, 0, sizeof(*service));
  service->platform = umca_openvela_platform(NULL);
  umca_loopback_bus_init(&service->bus);
  for (i = 0; i < 3; i++)
    {
      if (umca_loopback_endpoint_init(&service->bus,
                                      &service->endpoints[i]) != 0)
        {
          return -1;
        }
      service->phys[i] = umca_loopback_phy(&service->endpoints[i]);
    }
  ret = umca_init(&service->contexts[0], UMCA_SENSOR_DEVID,
                  UMCA_SENSOR_BOOT_ID, &service->platform, &service->phys[0]);
  if (ret != UMCA_OK)
    {
      return ret;
    }
  ret = umca_init(&service->contexts[1], UMCA_AGENT_DEVID,
                  UMCA_AGENT_BOOT_ID, &service->platform, &service->phys[1]);
  if (ret != UMCA_OK)
    {
      return ret;
    }
  ret = umca_init(&service->contexts[2], UMCA_ACTUATOR_DEVID,
                  UMCA_ACTUATOR_BOOT_ID, &service->platform, &service->phys[2]);
  if (ret != UMCA_OK)
    {
      return ret;
    }
  ret = umca_topic_register(&service->contexts[0], "/sensors/temperature",
                            UMCA_TOPIC_PUBLISHER, NULL, NULL, NULL);
  if (ret == UMCA_OK)
    {
      ret = umca_topic_register(&service->contexts[0],
                                "/events/temperature/threshold",
                                UMCA_TOPIC_PUBLISHER, NULL, NULL, NULL);
    }
  if (ret == UMCA_OK)
    {
      ret = umca_topic_register(&service->contexts[1],
                                "/events/temperature/threshold",
                                UMCA_TOPIC_SUBSCRIBER, on_threshold, service,
                                NULL);
    }
  if (ret == UMCA_OK)
    {
      ret = umca_topic_register(&service->contexts[2],
                                "/actuators/fan/command",
                                UMCA_TOPIC_SUBSCRIBER, on_fan_command, service,
                                NULL);
    }
  if (ret != UMCA_OK)
    {
      return ret;
    }
  ret = umca_agent_adapter_init(&service->adapter, &service->contexts[1],
                                UMCA_SENSOR_DEVID, UMCA_ACTUATOR_DEVID);
  if (ret != UMCA_OK)
    {
      return ret;
    }
  umca_agent_adapter_set_pump(&service->adapter, service_pump, service);
  for (i = 0; i < 3; i++)
    {
      ret = umca_start(&service->contexts[i]);
      if (ret != UMCA_OK)
        {
          return ret;
        }
    }
  service->sensor.temperature_mc = 27000;
  service->sensor.on_threshold_mc = 30000;
  service->sensor.off_threshold_mc = 28000;
  service->sensor.last_sample_ms = service_now();
  service->sensor.high_state = false;
  return UMCA_OK;
}

static void *service_thread(void *arg)
{
  umca_service_t *service = arg;
  while (service->running)
    {
      uint32_t now = service_now();
      service_pump(service);
      if ((uint32_t)(now - service->sensor.last_sample_ms) >= 1000u)
        {
          sensor_publish(service, now);
        }
      if (service->command_pending && !service->adapter.request_pending)
        {
          char output[128];
          service->command_pending = false;
          (void)umca_agent_adapter_device_control(
              &service->adapter, service->desired_fan_state, output,
              sizeof(output));
          printf("[umca] agent control result=%s\n", output);
        }
      usleep(100000);
    }
  return NULL;
}

int umca_service_start(void)
{
  int ret;
  if (g_service.running)
    {
      return 0;
    }
  ret = service_init(&g_service);
  if (ret != UMCA_OK)
    {
      printf("[umca] service init failed: %d\n", ret);
      return ret;
    }
  g_service.running = true;
  if (pthread_create(&g_service.thread, NULL, service_thread, &g_service) != 0)
    {
      g_service.running = false;
      return -1;
    }
  pthread_detach(g_service.thread);
  printf("[umca] Goldfish three-node service started\n");
  return 0;
}

int umca_service_stop(void)
{
  unsigned int i;
  if (!g_service.running)
    {
      return 0;
    }
  g_service.running = false;
  for (i = 0; i < 3; i++)
    {
      (void)umca_stop(&g_service.contexts[i], 0);
      umca_deinit(&g_service.contexts[i]);
    }
  return 0;
}

bool umca_service_is_running(void)
{
  return g_service.running;
}
