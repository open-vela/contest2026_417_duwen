#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "service/umca_service.h"

extern char *tool_registry_get_tools_json(void) __attribute__((weak));
extern int tool_registry_execute(const char *name, const char *input_json,
                                 char *output, size_t output_size)
  __attribute__((weak));

static void umca_agent_tool_command(int argc, char *argv[])
{
  char output[256];
  int ret;

  if (argc < 2 || tool_registry_execute == NULL)
    {
      printf("ai_agent tool registry is unavailable\n");
      return;
    }

  if (strcmp(argv[1], "tools") == 0)
    {
      char *json = tool_registry_get_tools_json != NULL ?
                   tool_registry_get_tools_json() : NULL;
      printf("UMCA/ai_agent tools: %s\n", json != NULL ? json : "(null)");
      free(json);
      return;
    }

  if (strcmp(argv[1], "sensor_query") == 0)
    {
      ret = tool_registry_execute("sensor_query",
                                  "{\"sensor\":\"temperature\"}",
                                  output, sizeof(output));
    }
  else if (strcmp(argv[1], "device_control") == 0 && argc >= 3)
    {
      char input[96];
      snprintf(input, sizeof(input),
               "{\"device\":\"fan\",\"command\":\"%s\"}",
               argv[2]);
      ret = tool_registry_execute("device_control", input, output,
                                  sizeof(output));
    }
  else
    {
      printf("Usage: umca_agent [tools|sensor_query|device_control on|off]\n");
      return;
    }

  printf("UMCA tool ret=%d output=%s\n", ret, output);
}

int umca_agent_main(int argc, char *argv[])
{
  (void)argc;
  (void)argv;
  if (umca_service_start() != 0)
    {
      printf("UMCA service failed to start.\n");
      return 1;
    }
  if (argc > 1)
    {
      sleep(2);
      umca_agent_tool_command(argc, argv);
    }
  printf("UMCA service is running.\n");
  return 0;
}
