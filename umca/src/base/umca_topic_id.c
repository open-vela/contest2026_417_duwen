#include <stddef.h>
#include <stdint.h>

#include "umca/umca.h"

static int topic_char_allowed(unsigned char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}

int umca_topic_validate(const char *name, size_t length)
{
  size_t i;
  size_t segment_start = 1;
  if (name == NULL || length == 0 || length > UMCA_MAX_TOPIC_NAME ||
      name[0] != '/')
    {
      return UMCA_ERR_INVALID_ARG;
    }
  if (length > 1 && name[length - 1] == '/')
    {
      return UMCA_ERR_INVALID_ARG;
    }
  for (i = 1; i < length; i++)
    {
      unsigned char c = (unsigned char)name[i];
      if (c == '/')
        {
          if (i == segment_start || (i - segment_start == 1 &&
              name[segment_start] == '.') ||
              (i - segment_start == 2 && name[segment_start] == '.' &&
               name[segment_start + 1] == '.'))
            {
              return UMCA_ERR_INVALID_ARG;
            }
          segment_start = i + 1;
        }
      else if (!topic_char_allowed(c))
        {
          return UMCA_ERR_INVALID_ARG;
        }
    }
  if (length > 1 && ((length - segment_start == 1 &&
      name[segment_start] == '.') || (length - segment_start == 2 &&
      name[segment_start] == '.' && name[segment_start + 1] == '.')))
    {
      return UMCA_ERR_INVALID_ARG;
    }
  return UMCA_OK;
}

int umca_topic_id_from_name(const char *name, size_t length,
                            umca_topic_id_t *out)
{
  size_t i;
  uint32_t hash = UINT32_C(0x811c9dc5);
  int ret;
  if (out == NULL)
    {
      return UMCA_ERR_INVALID_ARG;
    }
  ret = umca_topic_validate(name, length);
  if (ret != UMCA_OK)
    {
      return ret;
    }
  for (i = 0; i < length; i++)
    {
      hash ^= (uint8_t)name[i];
      hash *= UINT32_C(0x01000193);
    }
  if (hash == UMCA_SYSTEM_TOPIC_ID)
    {
      return UMCA_ERR_TOPIC_COLLISION;
    }
  *out = hash;
  return UMCA_OK;
}
