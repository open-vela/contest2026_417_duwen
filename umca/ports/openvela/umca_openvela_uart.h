#ifndef UMCA_OPENVELA_UART_H
#define UMCA_OPENVELA_UART_H

#include <stdbool.h>

#include "umca_uart.h"

typedef struct
{
  int fd;
  bool opened;
} umca_openvela_uart_t;

int umca_openvela_uart_open(umca_openvela_uart_t *uart,
                            const char *device);
void umca_openvela_uart_close(umca_openvela_uart_t *uart);
umca_uart_io_t umca_openvela_uart_io(umca_openvela_uart_t *uart);

#endif
