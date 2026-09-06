#include <string.h>

#include "umca/umca_protocol.h"
#include "umca_uart.h"

#define UMCA_UART_STATE_SEARCH 0u
#define UMCA_UART_STATE_FRAME  1u

static void uart_parser_reset(umca_uart_t *uart)
{
  uart->frame_length = 0;
  uart->expected_length = 0;
  uart->parser_state = UMCA_UART_STATE_SEARCH;
}

static int uart_stream_push(umca_uart_t *uart, const uint8_t *data,
                            size_t length)
{
  size_t i;
  if (length > UMCA_UART_RX_BUFFER_SIZE - uart->stream_count)
    {
      return -1;
    }
  for (i = 0; i < length; i++)
    {
      uart->stream[uart->stream_write] = data[i];
      uart->stream_write = (uart->stream_write + 1u) %
                           UMCA_UART_RX_BUFFER_SIZE;
    }
  uart->stream_count += length;
  return 0;
}

static uint8_t uart_stream_pop(umca_uart_t *uart)
{
  uint8_t value = uart->stream[uart->stream_read];
  uart->stream_read = (uart->stream_read + 1u) % UMCA_UART_RX_BUFFER_SIZE;
  uart->stream_count--;
  return value;
}

static int uart_parser_consume(umca_uart_t *uart)
{
  uint8_t value;
  uint16_t payload_length;

  while (uart->stream_count != 0 && uart->ready_length == 0)
    {
      value = uart_stream_pop(uart);
      if (uart->parser_state == UMCA_UART_STATE_SEARCH)
        {
          if (value == 0x55u)
            {
              uart->frame[0] = value;
              uart->frame_length = 1;
              uart->parser_state = UMCA_UART_STATE_FRAME;
            }
          continue;
        }

      if (uart->frame_length >= sizeof(uart->frame))
        {
          uart_parser_reset(uart);
          continue;
        }
      uart->frame[uart->frame_length++] = value;

      if (uart->frame_length == 2u && value != 0x4du)
        {
          if (value == 0x55u)
            {
              uart->frame[0] = value;
              uart->frame_length = 1;
            }
          else
            {
              uart_parser_reset(uart);
            }
          continue;
        }

      if (uart->frame_length == UMCA_HEADER_SIZE)
        {
          if (uart->frame[2] != UMCA_PROTOCOL_VERSION ||
              uart->frame[3] != UMCA_HEADER_SIZE)
            {
              uart_parser_reset(uart);
              continue;
            }
          payload_length = umca_read_be16(uart->frame + 34u);
          if (payload_length > UMCA_MVP_MAX_PAYLOAD)
            {
              uart_parser_reset(uart);
              continue;
            }
          uart->expected_length = UMCA_FRAME_OVERHEAD + payload_length;
        }

      if (uart->expected_length != 0 &&
          uart->frame_length == uart->expected_length)
        {
          uart->ready_length = uart->frame_length;
          uart_parser_reset(uart);
        }
    }
  return 0;
}

static int uart_fill(umca_uart_t *uart)
{
  uint8_t scratch[64];
  size_t capacity;
  size_t received = 0;
  int ret;
  if (uart->stream_count == UMCA_UART_RX_BUFFER_SIZE)
    {
      return -1;
    }
  capacity = UMCA_UART_RX_BUFFER_SIZE - uart->stream_count;
  if (capacity > sizeof(scratch))
    {
      capacity = sizeof(scratch);
    }
  ret = uart->io.read(uart->io.user, scratch, capacity, &received);
  if (ret != 0 || received > capacity)
    {
      return -1;
    }
  if (received == 0)
    {
      return 1;
    }
  return uart_stream_push(uart, scratch, received);
}

static int uart_init_op(void *driver)
{
  umca_uart_t *uart = driver;
  return uart != NULL && uart->io.read != NULL && uart->io.write != NULL ?
         0 : -1;
}

static void uart_deinit_op(void *driver)
{
  umca_uart_t *uart = driver;
  if (uart != NULL)
    {
      uart->initialized = false;
    }
}

static int uart_send_op(void *driver, const uint8_t *data, size_t length)
{
  umca_uart_t *uart = driver;
  size_t offset = 0;
  size_t sent;
  int ret;
  if (uart == NULL || data == NULL || length > UMCA_MAX_FRAME ||
      !uart->initialized)
    {
      return -1;
    }
  while (offset < length)
    {
      sent = 0;
      ret = uart->io.write(uart->io.user, data + offset, length - offset,
                           &sent);
      if (ret != 0 || sent == 0 || sent > length - offset)
        {
          return -1;
        }
      offset += sent;
    }
  return 0;
}

static int uart_poll_op(void *driver, uint8_t *buffer, size_t capacity,
                        size_t *received)
{
  umca_uart_t *uart = driver;
  int fill_result;
  int ret;
  if (uart == NULL || buffer == NULL || received == NULL ||
      !uart->initialized)
    {
      return -1;
    }
  *received = 0;
  if (uart->ready_length != 0)
    {
      if (capacity < uart->ready_length)
        {
          return -1;
        }
      memcpy(buffer, uart->frame, uart->ready_length);
      *received = uart->ready_length;
      uart->ready_length = 0;
      return 0;
    }
  ret = uart_parser_consume(uart);
  if (ret != 0)
    {
      return ret;
    }
  while (uart->ready_length == 0)
    {
      fill_result = uart_fill(uart);
      if (fill_result < 0)
        {
          return fill_result;
        }
      ret = uart_parser_consume(uart);
      if (ret != 0)
        {
          return ret;
        }
      if (fill_result > 0)
        {
          break;
        }
    }
  if (uart->ready_length != 0)
    {
      if (capacity < uart->ready_length)
        {
          return -1;
        }
      memcpy(buffer, uart->frame, uart->ready_length);
      *received = uart->ready_length;
      uart->ready_length = 0;
    }
  return 0;
}

static size_t uart_mtu_op(void *driver)
{
  const umca_uart_t *uart = driver;
  return uart != NULL && uart->initialized ? UMCA_MAX_FRAME : 0;
}

static uint32_t uart_caps_op(void *driver)
{
  (void)driver;
  return 0;
}

static const umca_phy_ops_t g_uart_ops =
{
  uart_init_op,
  uart_deinit_op,
  uart_send_op,
  uart_poll_op,
  uart_mtu_op,
  uart_caps_op
};

int umca_uart_init(umca_uart_t *uart, const umca_uart_io_t *io)
{
  if (uart == NULL || io == NULL || io->read == NULL || io->write == NULL)
    {
      return -1;
    }
  memset(uart, 0, sizeof(*uart));
  uart->io = *io;
  uart->initialized = true;
  uart->parser_state = UMCA_UART_STATE_SEARCH;
  return 0;
}

void umca_uart_deinit(umca_uart_t *uart)
{
  if (uart != NULL)
    {
      uart->initialized = false;
    }
}

umca_phy_t umca_uart_phy(umca_uart_t *uart)
{
  umca_phy_t phy = {&g_uart_ops, uart};
  return phy;
}
