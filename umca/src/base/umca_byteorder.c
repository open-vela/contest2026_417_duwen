#include "umca/umca_protocol.h"

uint16_t umca_read_be16(const uint8_t *p)
{
  return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

uint32_t umca_read_be32(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}

uint64_t umca_read_be64(const uint8_t *p)
{
  uint64_t value = 0;
  unsigned int i;
  for (i = 0; i < 8; i++)
    {
      value = (value << 8) | p[i];
    }
  return value;
}

void umca_write_be16(uint8_t *p, uint16_t value)
{
  p[0] = (uint8_t)(value >> 8);
  p[1] = (uint8_t)value;
}

void umca_write_be32(uint8_t *p, uint32_t value)
{
  p[0] = (uint8_t)(value >> 24);
  p[1] = (uint8_t)(value >> 16);
  p[2] = (uint8_t)(value >> 8);
  p[3] = (uint8_t)value;
}

void umca_write_be64(uint8_t *p, uint64_t value)
{
  unsigned int i;
  for (i = 0; i < 8; i++)
    {
      p[7 - i] = (uint8_t)value;
      value >>= 8;
    }
}
