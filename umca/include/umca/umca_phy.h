#ifndef UMCA_PHY_H
#define UMCA_PHY_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
  int (*init)(void *driver);
  void (*deinit)(void *driver);
  int (*send)(void *driver, const uint8_t *data, size_t length);
  int (*poll)(void *driver, uint8_t *buffer, size_t capacity,
              size_t *received);
  size_t (*get_mtu)(void *driver);
  uint32_t (*get_capabilities)(void *driver);
} umca_phy_ops_t;

typedef struct
{
  const umca_phy_ops_t *ops;
  void *driver;
} umca_phy_t;

#endif
