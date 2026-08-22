#include "umca/umca_protocol.h"

uint32_t umca_crc32c(const uint8_t *data, size_t length)
{
  size_t i;
  uint32_t crc = UINT32_C(0xffffffff);
  if (data == NULL && length != 0)
    {
      return 0;
    }
  for (i = 0; i < length; i++)
    {
      unsigned int bit;
      crc ^= data[i];
      for (bit = 0; bit < 8; bit++)
        {
          crc = (crc & 1u) != 0u ? (crc >> 1) ^ UINT32_C(0x82f63b78)
                                  : crc >> 1;
        }
    }
  return crc ^ UINT32_C(0xffffffff);
}
