#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "umca_openvela_uart.h"

static int openvela_uart_read(void *user, uint8_t *buffer, size_t capacity,
                              size_t *received)
{
  umca_openvela_uart_t *uart = user;
  ssize_t result;
  if (uart == NULL || buffer == NULL || received == NULL || !uart->opened)
    {
      return -1;
    }
  result = read(uart->fd, buffer, capacity);
  if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK ||
                     errno == EINTR))
    {
      *received = 0;
      return 0;
    }
  if (result < 0)
    {
      return -1;
    }
  *received = (size_t)result;
  return 0;
}

static int openvela_uart_write(void *user, const uint8_t *buffer,
                               size_t length, size_t *sent)
{
  umca_openvela_uart_t *uart = user;
  ssize_t result;
  if (uart == NULL || buffer == NULL || sent == NULL || !uart->opened)
    {
      return -1;
    }
  result = write(uart->fd, buffer, length);
  if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK ||
                     errno == EINTR))
    {
      *sent = 0;
      return 0;
    }
  if (result < 0)
    {
      return -1;
    }
  *sent = (size_t)result;
  return 0;
}

int umca_openvela_uart_open(umca_openvela_uart_t *uart, const char *device)
{
  int fd;
  if (uart == NULL || device == NULL)
    {
      return -1;
    }
  fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0)
    {
      return -1;
    }
  uart->fd = fd;
  uart->opened = true;
  return 0;
}

void umca_openvela_uart_close(umca_openvela_uart_t *uart)
{
  if (uart != NULL && uart->opened)
    {
      (void)close(uart->fd);
      uart->opened = false;
      uart->fd = -1;
    }
}

umca_uart_io_t umca_openvela_uart_io(umca_openvela_uart_t *uart)
{
  umca_uart_io_t io =
  {
    openvela_uart_read,
    openvela_uart_write,
    uart
  };
  return io;
}
