#ifndef UMCA_LOOPBACK_H
#define UMCA_LOOPBACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "umca/umca_config.h"
#include "umca/umca_phy.h"

#define UMCA_LOOPBACK_MAX_ENDPOINTS 4u

typedef struct
{
  bool used;
  uint16_t length;
  uint8_t data[UMCA_MAX_FRAME];
} umca_loopback_slot_t;

typedef struct
{
  umca_loopback_slot_t slots[UMCA_RX_QUEUE_DEPTH];
  uint8_t read_index;
  uint8_t write_index;
  uint8_t count;
} umca_loopback_queue_t;

typedef struct umca_loopback_bus
{
  struct umca_loopback_endpoint *endpoints[UMCA_LOOPBACK_MAX_ENDPOINTS];
  uint8_t endpoint_count;
  bool drop_next;
  bool duplicate_next;
  bool corrupt_next;
  uint16_t corrupt_offset;
  bool force_full;
} umca_loopback_bus_t;

typedef struct umca_loopback_endpoint
{
  umca_loopback_bus_t *bus;
  uint8_t endpoint_index;
  bool initialized;
  umca_loopback_queue_t rx_queue;
} umca_loopback_endpoint_t;

void umca_loopback_bus_init(umca_loopback_bus_t *bus);
int umca_loopback_endpoint_init(umca_loopback_bus_t *bus,
                                umca_loopback_endpoint_t *endpoint);
void umca_loopback_endpoint_deinit(umca_loopback_endpoint_t *endpoint);
umca_phy_t umca_loopback_phy(umca_loopback_endpoint_t *endpoint);
void umca_loopback_drop_next(umca_loopback_bus_t *bus);
void umca_loopback_duplicate_next(umca_loopback_bus_t *bus);
void umca_loopback_corrupt_next(umca_loopback_bus_t *bus, uint16_t offset);
void umca_loopback_force_full(umca_loopback_bus_t *bus, bool enabled);

#endif
