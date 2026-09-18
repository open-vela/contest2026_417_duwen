/****************************************************************************
 * contest2026_417_duwen/app/openvela_umca_gd32/umca_gd32_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <errno.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nuttx/config.h>
#include <nuttx/sched.h>

#include "umca/umca.h"
#include "umca_gd32_agent.h"
#include "umca_gd32_hw.h"
#include "umca_openvela_pal.h"
#include "umca_openvela_uart.h"
#include "umca_uart.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define UMCA_GD32_DEVICE_MAX 31u
#define UMCA_GD32_COMMAND_TEXT_MAX DEMO_LLM_CHAT_REQUEST_TEXT_MAX
#define UMCA_GD32_COMMAND_RESULT_MAX 768u
#define UMCA_GD32_PRIORITY \
  CONFIG_LVX_USE_DEMO_CONTEST2026_417_UMCA_GD32_PRIORITY
#define UMCA_GD32_STACKSIZE \
  CONFIG_LVX_USE_DEMO_CONTEST2026_417_UMCA_GD32_STACKSIZE

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct
{
  uint8_t type;
  bool value;
  uint16_t text_length;
  char text[UMCA_GD32_COMMAND_TEXT_MAX + 1u];
  int result;
  char output[UMCA_GD32_COMMAND_RESULT_MAX];
} umca_gd32_command_t;

enum umca_gd32_command_type
{
  UMCA_GD32_CMD_STATUS = 1,
  UMCA_GD32_CMD_QUERY,
  UMCA_GD32_CMD_CONTROL,
  UMCA_GD32_CMD_AUTO,
  UMCA_GD32_CMD_AGENT_AUTO,
  UMCA_GD32_CMD_AGENT_AUTO_STATUS,
  UMCA_GD32_CMD_CHAT,
  UMCA_GD32_CMD_NODES,
  UMCA_GD32_CMD_TOPICS,
  UMCA_GD32_CMD_STATS
};

typedef struct
{
  volatile bool running;
  volatile bool active;
  volatile bool ready;
  int last_error;
  pid_t task;
  char device[UMCA_GD32_DEVICE_MAX + 1u];
  umca_devid_t dev_id;
  uint32_t boot_id;
  umca_openvela_uart_t serial;
  umca_uart_t uart;
  umca_platform_t platform;
  umca_phy_t phy;
  umca_context_t context;
  umca_gd32_agent_t agent;
  bool mailbox_initialized;
  sem_t mailbox_slot;
  sem_t mailbox_request;
  sem_t mailbox_done;
  umca_gd32_command_t command;
} umca_gd32_service_t;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static umca_gd32_service_t g_service;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int parse_u64(const char *text, uint64_t *value)
{
  char *end;
  unsigned long long parsed;
  if (text == NULL || text[0] == '-')
    {
      return -1;
    }

  errno = 0;
  parsed = strtoull(text, &end, 0);
  if (errno != 0 || end == text || *end != '\0')
    {
      return -1;
    }

  *value = (uint64_t)parsed;
  return 0;
}

static int parse_u32(const char *text, uint32_t *value)
{
  uint64_t parsed;
  if (parse_u64(text, &parsed) != 0 || parsed == 0 ||
      parsed > UINT32_MAX)
    {
      return -1;
    }

  *value = (uint32_t)parsed;
  return 0;
}

static const char *node_state_name(umca_node_state_t state)
{
  switch (state)
    {
      case UMCA_NODE_ONLINE:
        return "online";
      case UMCA_NODE_STALE:
        return "stale";
      case UMCA_NODE_OFFLINE:
        return "offline";
      default:
        return "unknown";
    }
}

static int service_agent_pump(void *user)
{
  umca_gd32_service_t *service = user;
  uint16_t processed;
  int ret = umca_poll(&service->context, &processed);
  if (ret != UMCA_OK && ret != UMCA_ERR_DUPLICATE && ret != UMCA_ERR_STALE)
    {
      service->last_error = ret;
      return ret;
    }
  usleep(1000);
  return UMCA_OK;
}

static size_t append_text(char *output, size_t capacity, size_t used,
                          const char *format, ...)
{
  va_list args;
  int written;
  if (used >= capacity)
    {
      return used;
    }
  va_start(args, format);
  written = vsnprintf(output + used, capacity - used, format, args);
  va_end(args);
  if (written < 0)
    {
      return used;
    }
  if ((size_t)written >= capacity - used)
    {
      return capacity - 1u;
    }
  return used + (size_t)written;
}

static size_t append_node(umca_gd32_service_t *service,
                          umca_devid_t dev_id, const char *role,
                          char *output, size_t capacity, size_t used,
                          bool include_topics)
{
  umca_node_info_t node;
  unsigned int i;
  if (umca_node_get(&service->context, dev_id, &node) != UMCA_OK)
    {
      return append_text(output, capacity, used,
                         "%s dev=0x%016llx state=unknown\n", role,
                         (unsigned long long)dev_id);
    }
  used = append_text(output, capacity, used,
                     "%s dev=0x%016llx boot=0x%08lx state=%s topics=%u\n",
                     role, (unsigned long long)node.dev_id,
                     (unsigned long)node.boot_id, node_state_name(node.state),
                     node.topic_count);
  if (include_topics)
    {
      for (i = 0; i < node.topic_count; i++)
        {
          used = append_text(output, capacity, used,
                             "  0x%08lx dir=%u %s\n",
                             (unsigned long)node.topics[i].topic_id,
                             node.topics[i].direction, node.topics[i].name);
        }
    }
  return used;
}

static void process_command(umca_gd32_service_t *service)
{
  umca_gd32_command_t *command = &service->command;
  umca_gd32_agent_state_t state;
  umca_stats_t stats;
  uint32_t request_id;
  size_t used = 0;
  command->output[0] = '\0';
  command->result = UMCA_OK;
  umca_gd32_agent_get_state(&service->agent, &state);

  switch (command->type)
    {
      case UMCA_GD32_CMD_STATUS:
        snprintf(command->output, sizeof(command->output),
                 "running dev=0x%016llx boot=0x%08lx error=%d "
                 "sensor=%s actuator=%s esp32=%s auto=%s "
                 "agent_auto=%s "
                 "fan=%s fan_pending=%lu chat_pending=%lu",
                 (unsigned long long)service->dev_id,
                 (unsigned long)service->boot_id, service->last_error,
                 umca_gd32_agent_node_online(&service->agent,
                                             service->agent.sensor_dev_id) ?
                   "online" : "offline",
                 umca_gd32_agent_node_online(&service->agent,
                                             service->agent.actuator_dev_id) ?
                   "online" : "offline",
                 umca_gd32_agent_node_online(&service->agent,
                                             service->agent.esp32_dev_id) ?
                   "online" : "offline",
                 state.auto_enabled ? "on" : "off",
                 state.agent_auto_enabled ? "on" : "off",
                 state.fan_known ? (state.fan_on ? "on" : "off") :
                                   "unknown",
                 (unsigned long)(state.fan_request_pending ?
                                 state.fan_request_id : 0),
                 (unsigned long)(state.chat_request_pending ?
                                 state.chat_request_id : 0));
        break;

      case UMCA_GD32_CMD_QUERY:
        command->result = umca_gd32_agent_query_temperature(
            &service->agent, command->output, sizeof(command->output));
        break;

      case UMCA_GD32_CMD_CONTROL:
        command->result = umca_gd32_agent_control_fan(
            &service->agent, command->value, UMCA_GD32_CONTROL_MANUAL,
            &request_id);
        if (command->result == UMCA_OK)
          {
            snprintf(command->output, sizeof(command->output),
                     "fan request=%lu target=%s pending",
                     (unsigned long)request_id,
                     command->value ? "on" : "off");
          }
        else
          {
            snprintf(command->output, sizeof(command->output),
                     "fan command rejected error=%d", command->result);
          }
        break;

      case UMCA_GD32_CMD_AUTO:
        umca_gd32_agent_set_auto(&service->agent, command->value);
        snprintf(command->output, sizeof(command->output), "auto=%s",
                 command->value ? "on" : "off");
        break;

      case UMCA_GD32_CMD_AGENT_AUTO:
        command->result = umca_gd32_agent_set_agent_auto(
            &service->agent, command->value);
        if (command->result == UMCA_OK)
          {
            snprintf(command->output, sizeof(command->output),
                     "agent-auto=%s", command->value ? "on" : "off");
          }
        else
          {
            snprintf(command->output, sizeof(command->output),
                     "agent-auto rejected: local auto is on");
          }
        break;

      case UMCA_GD32_CMD_AGENT_AUTO_STATUS:
        snprintf(command->output, sizeof(command->output),
                 "agent-auto=%s local-auto=%s pending=%s cooldown_ms=%lu",
                 state.agent_auto_enabled ? "on" : "off",
                 state.auto_enabled ? "on" : "off",
                 service->agent.queued_agent_eval ? "yes" : "no",
                 (unsigned long)UMCA_GD32_AGENT_AUTO_COOLDOWN_MS);
        break;

      case UMCA_GD32_CMD_CHAT:
        command->result = umca_gd32_agent_chat(
            &service->agent, (const uint8_t *)command->text,
            command->text_length, DEMO_LLM_ORIGIN_TERMINAL, &request_id);
        if (command->result == UMCA_OK)
          {
            snprintf(command->output, sizeof(command->output),
                     "chat request=%lu pending", (unsigned long)request_id);
          }
        else
          {
            snprintf(command->output, sizeof(command->output),
                     "chat rejected error=%d", command->result);
          }
        break;

      case UMCA_GD32_CMD_NODES:
        used = append_node(service, service->agent.sensor_dev_id, "sensor",
                           command->output, sizeof(command->output), used,
                           false);
        used = append_node(service, service->agent.actuator_dev_id, "actuator",
                           command->output, sizeof(command->output), used,
                           false);
        (void)append_node(service, service->agent.esp32_dev_id, "esp32",
                          command->output, sizeof(command->output), used,
                          false);
        break;

      case UMCA_GD32_CMD_TOPICS:
        used = append_node(service, service->agent.sensor_dev_id, "sensor",
                           command->output, sizeof(command->output), used,
                           true);
        used = append_node(service, service->agent.actuator_dev_id, "actuator",
                           command->output, sizeof(command->output), used,
                           true);
        (void)append_node(service, service->agent.esp32_dev_id, "esp32",
                          command->output, sizeof(command->output), used,
                          true);
        break;

      case UMCA_GD32_CMD_STATS:
        command->result = umca_stats_get(&service->context, &stats);
        if (command->result == UMCA_OK)
          {
            snprintf(command->output, sizeof(command->output),
                     "core tx=%lu/%lu rx=%lu delivered=%lu crc=%lu len=%lu "
                     "semantic=%lu dup=%lu stale=%lu lost=%lu unsub=%lu "
                     "online=%lu offline=%lu capacity=%lu\n"
                     "agent temp=%lu threshold=%lu fan_cmd=%lu ack=%lu "
                     "reject=%lu timeout=%lu mismatch=%lu chat=%lu/%lu "
                     "chat_fail=%lu chat_timeout=%lu chat_mismatch=%lu "
                     "chat_invalid=%lu action=%lu/%lu/%lu\n"
                     "agent_auto event=%lu merged=%lu cooldown=%lu fail=%lu",
                     (unsigned long)stats.tx_frames,
                     (unsigned long)stats.tx_errors,
                     (unsigned long)stats.rx_frames,
                     (unsigned long)stats.rx_delivered,
                     (unsigned long)stats.rx_crc_errors,
                     (unsigned long)stats.rx_length_errors,
                     (unsigned long)stats.rx_semantic_errors,
                     (unsigned long)stats.rx_duplicates,
                     (unsigned long)stats.rx_stale,
                     (unsigned long)stats.rx_estimated_lost,
                     (unsigned long)stats.rx_unsubscribed,
                     (unsigned long)stats.node_online_events,
                     (unsigned long)stats.node_offline_events,
                     (unsigned long)stats.capacity_errors,
                     (unsigned long)state.temperature_messages,
                     (unsigned long)state.threshold_events,
                     (unsigned long)state.fan_commands,
                     (unsigned long)state.fan_acks,
                     (unsigned long)state.fan_rejections,
                     (unsigned long)state.fan_timeouts,
                     (unsigned long)state.fan_mismatches,
                     (unsigned long)state.chat_requests,
                     (unsigned long)state.chat_responses,
                     (unsigned long)state.chat_failures,
                     (unsigned long)state.chat_timeouts,
                     (unsigned long)state.chat_mismatches,
                     (unsigned long)state.chat_invalid_responses,
                     (unsigned long)state.llm_actions_accepted,
                     (unsigned long)state.llm_actions_rejected,
                     (unsigned long)state.llm_actions_completed,
                     (unsigned long)state.agent_auto_events,
                     (unsigned long)state.agent_auto_merged,
                     (unsigned long)state.agent_auto_cooldown_skips,
                     (unsigned long)state.agent_auto_failures);
          }
        break;

      default:
        command->result = UMCA_ERR_INVALID_ARG;
        snprintf(command->output, sizeof(command->output), "unknown command");
        break;
    }
}

static int submit_command(uint8_t type, bool value, const char *text,
                          uint16_t text_length)
{
  umca_gd32_command_t *command = &g_service.command;
  if (!g_service.active || !g_service.ready ||
      !g_service.mailbox_initialized)
    {
      printf("UMCA GD32 service is not running\n");
      return 1;
    }
  if (sem_wait(&g_service.mailbox_slot) != 0)
    {
      return 1;
    }
  if (!g_service.active || !g_service.ready)
    {
      (void)sem_post(&g_service.mailbox_slot);
      printf("UMCA GD32 service is not running\n");
      return 1;
    }
  memset(command, 0, sizeof(*command));
  command->type = type;
  command->value = value;
  command->text_length = text_length;
  if (text_length != 0)
    {
      memcpy(command->text, text, text_length);
      command->text[text_length] = '\0';
    }
  (void)sem_post(&g_service.mailbox_request);
  if (sem_wait(&g_service.mailbox_done) != 0)
    {
      (void)sem_post(&g_service.mailbox_slot);
      return 1;
    }
  printf("%s\n", command->output);
  type = command->result == UMCA_OK ? 0 : 1;
  (void)sem_post(&g_service.mailbox_slot);
  return type;
}

static int umca_gd32_task(int argc, char *argv[])
{
  umca_gd32_service_t *service = &g_service;
  umca_uart_io_t io;
  umca_gd32_agent_state_t previous_state;
  uint16_t processed;
  int ret;
  (void)argc;
  (void)argv;

  service->serial.fd = -1;
  ret = umca_openvela_uart_open(&service->serial, service->device);
  if (ret != 0)
    {
      service->last_error = ret;
      service->active = false;
      service->running = false;
      printf("[umca-gd32] open %s failed\n", service->device);
      return 1;
    }

  io = umca_openvela_uart_io(&service->serial);
  ret = umca_uart_init(&service->uart, &io);
  if (ret == 0)
    {
      service->platform = umca_openvela_platform(NULL);
      service->phy = umca_uart_phy(&service->uart);
      ret = umca_init(&service->context, service->dev_id, service->boot_id,
                      &service->platform, &service->phy);
    }

  if (ret == UMCA_OK)
    {
      ret = umca_gd32_agent_init(&service->agent, &service->context,
                                 UMCA_GD32_SENSOR_DEVID,
                                 UMCA_GD32_ACTUATOR_DEVID,
                                 UMCA_GD32_ESP32_DEVID);
    }

  if (ret == UMCA_OK)
    {
      umca_gd32_agent_set_pump(&service->agent, service_agent_pump, service);
    }

  if (ret == UMCA_OK)
    {
      ret = umca_start(&service->context);
    }

  if (ret != UMCA_OK)
    {
      service->last_error = ret;
      umca_uart_deinit(&service->uart);
      umca_openvela_uart_close(&service->serial);
      service->active = false;
      service->running = false;
      printf("[umca-gd32] initialization failed: %d\n", ret);
      return 1;
    }

  printf("[umca-gd32] running device=%s devid=0x%016llx boot=0x%08lx\n",
         service->device, (unsigned long long)service->dev_id,
         (unsigned long)service->boot_id);
  service->ready = true;
  memset(&previous_state, 0, sizeof(previous_state));
  while (service->running)
    {
      ret = umca_poll(&service->context, &processed);
      if (ret != UMCA_OK && ret != UMCA_ERR_DUPLICATE &&
          ret != UMCA_ERR_STALE)
        {
          service->last_error = ret;
        }

      umca_gd32_agent_tick(&service->agent);
      while (sem_trywait(&service->mailbox_request) == 0)
        {
          process_command(service);
          (void)sem_post(&service->mailbox_done);
        }

      {
        umca_gd32_agent_state_t state;
        umca_gd32_agent_get_state(&service->agent, &state);
        if (state.fan_acks != previous_state.fan_acks)
          {
            printf("[umca-gd32] fan request=%lu applied state=%s\n",
                   (unsigned long)state.fan_request_id,
                   state.fan_on ? "on" : "off");
          }
        if (state.fan_rejections != previous_state.fan_rejections ||
            state.fan_timeouts != previous_state.fan_timeouts)
          {
            printf("[umca-gd32] fan request=%lu failed\n",
                   (unsigned long)state.fan_request_id);
          }
        if (state.chat_responses != previous_state.chat_responses)
          {
            if (state.chat_status != DEMO_LLM_STATUS_OK)
              {
                printf("[umca-gd32] LLM request failed: status=%s(%u)\n",
                       umca_gd32_agent_llm_status_name(state.chat_status),
                       state.chat_status);
              }
            else
              {
                printf("[umca-gd32] chat request=%lu action=%u/%u: %s\n",
                       (unsigned long)state.chat_request_id,
                       state.chat_action_type, state.chat_action_value,
                       state.chat_text);
              }
          }
        if (state.chat_timeouts != previous_state.chat_timeouts)
          {
            printf("[umca-gd32] chat request=%lu timeout\n",
                   (unsigned long)state.chat_request_id);
          }
        if (state.llm_actions_completed !=
            previous_state.llm_actions_completed)
          {
            printf("[umca-gd32] agent action result=%s\n",
                   service->agent.last_action_result);
          }
        previous_state = state;
      }

      usleep(10000);
    }

  (void)umca_stop(&service->context, 0);
  umca_deinit(&service->context);
  umca_uart_deinit(&service->uart);
  umca_openvela_uart_close(&service->serial);
  service->active = false;
  service->ready = false;
  printf("[umca-gd32] stopped\n");
  return 0;
}

static void usage(void)
{
  printf("Usage:\n");
  printf("  umca_gd32 start <dev-id> <boot-id>\n");
  printf("  umca_gd32 start <uart-device> <dev-id> <boot-id>\n");
  printf("  umca_gd32 status\n");
  printf("  umca_gd32 query\n");
  printf("  umca_gd32 control on|off\n");
  printf("  umca_gd32 auto on|off\n");
  printf("  umca_gd32 agent-auto on|off|status\n");
  printf("  umca_gd32 chat <text>\n");
  printf("  umca_gd32 nodes|topics|stats\n");
  printf("  umca_gd32 pinout\n");
  printf("  umca_gd32 stop\n");
}

static void print_pinout(void)
{
  printf("Board: %s, MCU: %s\n", UMCA_GD32_BOARD_NAME,
         UMCA_GD32_MCU_NAME);
  printf("NSH: %s %s TX=%s RX=%s %d %s\n",
         UMCA_GD32_CONSOLE_DEVICE, UMCA_GD32_CONSOLE_INSTANCE,
         UMCA_GD32_CONSOLE_TX_PIN, UMCA_GD32_CONSOLE_RX_PIN,
         UMCA_GD32_CONSOLE_BAUD, UMCA_GD32_CONSOLE_FORMAT);
  printf("UMCA: %s %s TX=%s RX=%s AF%d %d %s\n",
         UMCA_GD32_UART_DEVICE, UMCA_GD32_UART_INSTANCE,
         UMCA_GD32_UART_TX_PIN, UMCA_GD32_UART_RX_PIN,
         UMCA_GD32_UART_AF, UMCA_GD32_UART_BAUD,
         UMCA_GD32_UART_FORMAT);
  printf("Electrical: %s, common GND, no RTS/CTS, no DMA\n",
         UMCA_GD32_UART_ELECTRICAL);
  printf("Reserved: PC10/PC11 for UMCA; SDIO must remain disabled\n");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int umca_gd32_main(int argc, char *argv[])
{
  const char *boot_arg;
  const char *device;
  const char *dev_arg;
  uint64_t dev_id;
  uint32_t boot_id;
  uint8_t command_type = 0;
  bool command_value = false;
  char command_text[UMCA_GD32_COMMAND_TEXT_MAX + 1u];
  uint16_t command_text_length = 0;
  int i;

  if (argc == 2 && strcmp(argv[1], "status") == 0)
    {
      if (!g_service.active)
        {
          printf("UMCA GD32: stopped device=%s error=%d\n",
                 g_service.device[0] != '\0' ? g_service.device : "(none)",
                 g_service.last_error);
          return 0;
        }
      if (!g_service.ready)
        {
          printf("UMCA GD32: starting device=%s error=%d\n",
                 g_service.device, g_service.last_error);
          return 0;
        }
      return submit_command(UMCA_GD32_CMD_STATUS, false, NULL, 0);
    }

  if (argc == 2 && strcmp(argv[1], "pinout") == 0)
    {
      print_pinout();
      return 0;
    }

  if (argc == 2 && strcmp(argv[1], "stop") == 0)
    {
      if (g_service.active && g_service.ready &&
          g_service.mailbox_initialized)
        {
          if (sem_wait(&g_service.mailbox_slot) != 0)
            {
              return 1;
            }
          g_service.ready = false;
          g_service.running = false;
          (void)sem_post(&g_service.mailbox_slot);
          return 0;
        }
      g_service.running = false;
      return 0;
    }

  if (argc == 2 && strcmp(argv[1], "query") == 0)
    {
      command_type = UMCA_GD32_CMD_QUERY;
    }
  else if (argc == 2 && strcmp(argv[1], "nodes") == 0)
    {
      command_type = UMCA_GD32_CMD_NODES;
    }
  else if (argc == 2 && strcmp(argv[1], "topics") == 0)
    {
      command_type = UMCA_GD32_CMD_TOPICS;
    }
  else if (argc == 2 && strcmp(argv[1], "stats") == 0)
    {
      command_type = UMCA_GD32_CMD_STATS;
    }
  else if (argc == 3 && strcmp(argv[1], "control") == 0 &&
           (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "off") == 0))
    {
      command_type = UMCA_GD32_CMD_CONTROL;
      command_value = strcmp(argv[2], "on") == 0;
    }
  else if (argc == 3 && strcmp(argv[1], "auto") == 0 &&
           (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "off") == 0))
    {
      command_type = UMCA_GD32_CMD_AUTO;
      command_value = strcmp(argv[2], "on") == 0;
    }
  else if (argc == 3 && strcmp(argv[1], "agent-auto") == 0 &&
           (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "off") == 0))
    {
      command_type = UMCA_GD32_CMD_AGENT_AUTO;
      command_value = strcmp(argv[2], "on") == 0;
    }
  else if (argc == 3 && strcmp(argv[1], "agent-auto") == 0 &&
           strcmp(argv[2], "status") == 0)
    {
      command_type = UMCA_GD32_CMD_AGENT_AUTO_STATUS;
    }
  else if (argc >= 3 && strcmp(argv[1], "chat") == 0)
    {
      command_type = UMCA_GD32_CMD_CHAT;
      for (i = 2; i < argc; i++)
        {
          size_t part_length = strlen(argv[i]);
          size_t extra = part_length + (i == 2 ? 0u : 1u);
          if ((size_t)command_text_length + extra >
              UMCA_GD32_COMMAND_TEXT_MAX)
            {
              printf("chat text exceeds %u bytes\n",
                     UMCA_GD32_COMMAND_TEXT_MAX);
              return 1;
            }
          if (i != 2)
            {
              command_text[command_text_length++] = ' ';
            }
          memcpy(command_text + command_text_length, argv[i], part_length);
          command_text_length = (uint16_t)(command_text_length + part_length);
        }
      command_text[command_text_length] = '\0';
    }

  if (command_type != 0)
    {
      return submit_command(command_type, command_value, command_text,
                            command_text_length);
    }

  if ((argc != 4 && argc != 5) || strcmp(argv[1], "start") != 0)
    {
      usage();
      return 1;
    }

  if (argc == 4)
    {
      device = UMCA_GD32_UART_DEVICE;
      dev_arg = argv[2];
      boot_arg = argv[3];
    }
  else
    {
      device = argv[2];
      dev_arg = argv[3];
      boot_arg = argv[4];
    }

  if (g_service.active)
    {
      printf("UMCA GD32 service is already running\n");
      return 1;
    }

  if (strlen(device) > UMCA_GD32_DEVICE_MAX ||
      parse_u64(dev_arg, &dev_id) != 0 || dev_id == UMCA_INVALID_DEVID ||
      parse_u32(boot_arg, &boot_id) != 0)
    {
      usage();
      return 1;
    }

  if (g_service.mailbox_initialized)
    {
      (void)sem_destroy(&g_service.mailbox_slot);
      (void)sem_destroy(&g_service.mailbox_request);
      (void)sem_destroy(&g_service.mailbox_done);
    }
  memset(&g_service, 0, sizeof(g_service));
  memcpy(g_service.device, device, strlen(device) + 1u);
  g_service.dev_id = (umca_devid_t)dev_id;
  g_service.boot_id = boot_id;
  g_service.running = true;
  g_service.active = true;
  if (sem_init(&g_service.mailbox_slot, 0, 1) != 0)
    {
      goto mailbox_init_failed;
    }
  if (sem_init(&g_service.mailbox_request, 0, 0) != 0)
    {
      (void)sem_destroy(&g_service.mailbox_slot);
      goto mailbox_init_failed;
    }
  if (sem_init(&g_service.mailbox_done, 0, 0) != 0)
    {
      (void)sem_destroy(&g_service.mailbox_request);
      (void)sem_destroy(&g_service.mailbox_slot);
      goto mailbox_init_failed;
    }
  g_service.mailbox_initialized = true;
  g_service.task = task_create("umca_gd32",
                               UMCA_GD32_PRIORITY,
                               UMCA_GD32_STACKSIZE,
                               umca_gd32_task, NULL);
  if (g_service.task < 0)
    {
      g_service.running = false;
      g_service.active = false;
      (void)sem_destroy(&g_service.mailbox_slot);
      (void)sem_destroy(&g_service.mailbox_request);
      (void)sem_destroy(&g_service.mailbox_done);
      g_service.mailbox_initialized = false;
      printf("UMCA GD32 task_create failed: %d\n", (int)g_service.task);
      return 1;
    }

  return 0;

mailbox_init_failed:
  g_service.running = false;
  g_service.active = false;
  printf("UMCA GD32 mailbox initialization failed\n");
  return 1;
}
