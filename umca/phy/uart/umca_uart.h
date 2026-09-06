#ifndef UMCA_UART_H
#define UMCA_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "umca/umca_config.h"
#include "umca/umca_phy.h"

#ifndef UMCA_UART_RX_BUFFER_SIZE
#  define UMCA_UART_RX_BUFFER_SIZE (UMCA_MAX_FRAME * 4u)
#endif

#if UMCA_UART_RX_BUFFER_SIZE < UMCA_MAX_FRAME
#  error "UMCA_UART_RX_BUFFER_SIZE must hold at least one complete frame"
#endif

typedef int (*umca_uart_read_fn)(void *user, uint8_t *buffer,
                                 size_t capacity, size_t *received);
typedef int (*umca_uart_write_fn)(void *user, const uint8_t *buffer,
                                  size_t length, size_t *sent);

typedef struct
{
  umca_uart_read_fn read;
  umca_uart_write_fn write;
  void *user;
} umca_uart_io_t;

typedef struct
{
  umca_uart_io_t io;
  uint8_t stream[UMCA_UART_RX_BUFFER_SIZE];
  size_t stream_read;
  size_t stream_write;
  size_t stream_count;
  uint8_t frame[UMCA_MAX_FRAME];
  size_t frame_length;
  size_t expected_length;
  size_t ready_length;
  uint8_t parser_state;
  bool initialized;
} umca_uart_t;

int umca_uart_init(umca_uart_t *uart, const umca_uart_io_t *io);
void umca_uart_deinit(umca_uart_t *uart);
umca_phy_t umca_uart_phy(umca_uart_t *uart);

#endif
