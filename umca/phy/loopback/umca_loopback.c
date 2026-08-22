#include <string.h>

#include "umca_loopback.h"

static int loop_init(void *driver)
{
  umca_loopback_endpoint_t *endpoint = driver;
  if (endpoint == NULL || endpoint->bus == NULL)
    {
      return -1;
    }
  endpoint->initialized = true;
  return 0;
}

static void loop_deinit(void *driver)
{
  umca_loopback_endpoint_t *endpoint = driver;
  if (endpoint != NULL)
    {
      endpoint->initialized = false;
    }
}

static int queue_has_space(const umca_loopback_queue_t *queue)
{
  return queue->count < UMCA_RX_QUEUE_DEPTH;
}

static void queue_push(umca_loopback_queue_t *queue, const uint8_t *data,
                       size_t length)
{
  umca_loopback_slot_t *slot = &queue->slots[queue->write_index];
  memcpy(slot->data, data, length);
  slot->length = (uint16_t)length;
  slot->used = true;
  queue->write_index = (uint8_t)((queue->write_index + 1u) %
                                 UMCA_RX_QUEUE_DEPTH);
  queue->count++;
}

static int loop_send(void *driver, const uint8_t *data, size_t length)
{
  umca_loopback_endpoint_t *source = driver;
  umca_loopback_bus_t *bus;
  unsigned int i;
  if (source == NULL || data == NULL || length > UMCA_MAX_FRAME ||
      !source->initialized)
    {
      return -1;
    }
  bus = source->bus;
  if (bus->drop_next)
    {
      bus->drop_next = false;
      return 0;
    }
  if (bus->force_full)
    {
      return -1;
    }
  for (i = 0; i < bus->endpoint_count; i++)
    {
      umca_loopback_endpoint_t *target = bus->endpoints[i];
      if (target != source && !queue_has_space(&target->rx_queue))
        {
          return -1;
        }
    }
  for (i = 0; i < bus->endpoint_count; i++)
    {
      umca_loopback_endpoint_t *target = bus->endpoints[i];
      uint8_t scratch[UMCA_MAX_FRAME];
      if (target == source)
        {
          continue;
        }
      if (bus->corrupt_next && bus->corrupt_offset < length)
        {
          memcpy(scratch, data, length);
          scratch[bus->corrupt_offset] ^= 0x01u;
          queue_push(&target->rx_queue, scratch, length);
        }
      else
        {
          queue_push(&target->rx_queue, data, length);
        }
      if (bus->duplicate_next)
        {
          if (!queue_has_space(&target->rx_queue))
            {
              return -1;
            }
          queue_push(&target->rx_queue, data, length);
        }
    }
  bus->corrupt_next = false;
  bus->duplicate_next = false;
  return 0;
}

static int loop_poll(void *driver, uint8_t *buffer, size_t capacity,
                     size_t *received)
{
  umca_loopback_endpoint_t *endpoint = driver;
  umca_loopback_slot_t *slot;
  if (endpoint == NULL || buffer == NULL || received == NULL ||
      !endpoint->initialized)
    {
      return -1;
    }
  *received = 0;
  if (endpoint->rx_queue.count == 0)
    {
      return 0;
    }
  slot = &endpoint->rx_queue.slots[endpoint->rx_queue.read_index];
  if (capacity < slot->length)
    {
      return -1;
    }
  memcpy(buffer, slot->data, slot->length);
  *received = slot->length;
  slot->used = false;
  endpoint->rx_queue.read_index = (uint8_t)((endpoint->rx_queue.read_index + 1u) %
                                            UMCA_RX_QUEUE_DEPTH);
  endpoint->rx_queue.count--;
  return 0;
}

static size_t loop_mtu(void *driver)
{
  umca_loopback_endpoint_t *endpoint = driver;
  return endpoint != NULL && endpoint->initialized ? UMCA_MAX_FRAME : 0;
}

static uint32_t loop_caps(void *driver)
{
  (void)driver;
  return 0;
}

static const umca_phy_ops_t g_loop_ops =
{
  loop_init,
  loop_deinit,
  loop_send,
  loop_poll,
  loop_mtu,
  loop_caps
};

void umca_loopback_bus_init(umca_loopback_bus_t *bus)
{
  if (bus != NULL)
    {
      memset(bus, 0, sizeof(*bus));
    }
}

int umca_loopback_endpoint_init(umca_loopback_bus_t *bus,
                                umca_loopback_endpoint_t *endpoint)
{
  if (bus == NULL || endpoint == NULL || bus->endpoint_count >=
      UMCA_LOOPBACK_MAX_ENDPOINTS)
    {
      return -1;
    }
  memset(endpoint, 0, sizeof(*endpoint));
  endpoint->bus = bus;
  endpoint->endpoint_index = bus->endpoint_count;
  bus->endpoints[bus->endpoint_count++] = endpoint;
  return 0;
}

void umca_loopback_endpoint_deinit(umca_loopback_endpoint_t *endpoint)
{
  if (endpoint != NULL)
    {
      endpoint->initialized = false;
    }
}

umca_phy_t umca_loopback_phy(umca_loopback_endpoint_t *endpoint)
{
  umca_phy_t phy = {&g_loop_ops, endpoint};
  return phy;
}

void umca_loopback_drop_next(umca_loopback_bus_t *bus)
{
  if (bus != NULL) bus->drop_next = true;
}

void umca_loopback_duplicate_next(umca_loopback_bus_t *bus)
{
  if (bus != NULL) bus->duplicate_next = true;
}

void umca_loopback_corrupt_next(umca_loopback_bus_t *bus, uint16_t offset)
{
  if (bus != NULL)
    {
      bus->corrupt_next = true;
      bus->corrupt_offset = offset;
    }
}

void umca_loopback_force_full(umca_loopback_bus_t *bus, bool enabled)
{
  if (bus != NULL) bus->force_full = enabled;
}
