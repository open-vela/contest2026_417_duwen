/****************************************************************************
 * contest2026_417_duwen/app/openvela_umca_gd32/umca_gd32_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nuttx/config.h>
#include <nuttx/sched.h>

#include "umca/umca.h"
#include "umca_gd32_hw.h"
#include "umca_openvela_pal.h"
#include "umca_openvela_uart.h"
#include "umca_uart.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define UMCA_GD32_DEVICE_MAX 31u
#define UMCA_GD32_PRIORITY \
  CONFIG_LVX_USE_DEMO_CONTEST2026_417_UMCA_GD32_PRIORITY
#define UMCA_GD32_STACKSIZE \
  CONFIG_LVX_USE_DEMO_CONTEST2026_417_UMCA_GD32_STACKSIZE

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct
{
  volatile bool running;
  volatile bool active;
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

static int umca_gd32_task(int argc, char *argv[])
{
  umca_gd32_service_t *service = &g_service;
  umca_uart_io_t io;
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
  while (service->running)
    {
      ret = umca_poll(&service->context, &processed);
      if (ret != UMCA_OK && ret != UMCA_ERR_DUPLICATE &&
          ret != UMCA_ERR_STALE)
        {
          service->last_error = ret;
        }

      usleep(10000);
    }

  (void)umca_stop(&service->context, 0);
  umca_deinit(&service->context);
  umca_uart_deinit(&service->uart);
  umca_openvela_uart_close(&service->serial);
  service->active = false;
  printf("[umca-gd32] stopped\n");
  return 0;
}

static void usage(void)
{
  printf("Usage:\n");
  printf("  umca_gd32 start <dev-id> <boot-id>\n");
  printf("  umca_gd32 start <uart-device> <dev-id> <boot-id>\n");
  printf("  umca_gd32 status\n");
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

  if (argc == 2 && strcmp(argv[1], "status") == 0)
    {
      printf("UMCA GD32: %s device=%s error=%d\n",
             g_service.active ? "running" : "stopped",
             g_service.device[0] != '\0' ? g_service.device : "(none)",
             g_service.last_error);
      return 0;
    }

  if (argc == 2 && strcmp(argv[1], "pinout") == 0)
    {
      print_pinout();
      return 0;
    }

  if (argc == 2 && strcmp(argv[1], "stop") == 0)
    {
      g_service.running = false;
      return 0;
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

  memset(&g_service, 0, sizeof(g_service));
  memcpy(g_service.device, device, strlen(device) + 1u);
  g_service.dev_id = (umca_devid_t)dev_id;
  g_service.boot_id = boot_id;
  g_service.running = true;
  g_service.active = true;
  g_service.task = task_create("umca_gd32",
                               UMCA_GD32_PRIORITY,
                               UMCA_GD32_STACKSIZE,
                               umca_gd32_task, NULL);
  if (g_service.task < 0)
    {
      g_service.running = false;
      g_service.active = false;
      printf("UMCA GD32 task_create failed: %d\n", (int)g_service.task);
      return 1;
    }

  return 0;
}
