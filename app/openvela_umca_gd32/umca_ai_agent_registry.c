/* External-provider-only backend for the audited ai_agent Tool Registry API.
 * The full registry pulls network/TLS/cJSON and all builtin tools, which are
 * not viable in the GD32 image. This retains its provider execution contract
 * while bounding storage to one provider and using no heap. */

#include <stddef.h>

#include "tools/tool_registry.h"

typedef struct
{
  const char *name;
  tool_provider_fn get_tools;
  tool_executor_fn execute;
} umca_tool_provider_t;

static umca_tool_provider_t g_provider;

void tool_registry_register_provider(const char *name,
                                     tool_provider_fn get_tools,
                                     tool_executor_fn execute)
{
  if (name != NULL && get_tools != NULL && execute != NULL &&
      g_provider.execute == NULL)
    {
      g_provider.name = name;
      g_provider.get_tools = get_tools;
      g_provider.execute = execute;
    }
}

void tool_registry_invalidate(void)
{
}

int tool_registry_execute(const char *name, const char *input_json,
                          char *output, size_t output_size)
{
  if (name == NULL || input_json == NULL || output == NULL ||
      output_size == 0 || g_provider.execute == NULL)
    {
      return -1;
    }

  output[0] = '\0';
  return g_provider.execute(name, input_json, output, output_size);
}
