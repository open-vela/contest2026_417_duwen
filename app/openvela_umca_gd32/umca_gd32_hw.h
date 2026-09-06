/****************************************************************************
 * contest2026_417_duwen/app/openvela_umca_gd32/umca_gd32_hw.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef UMCA_GD32_HW_H
#define UMCA_GD32_HW_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* GD32F470V-START hardware contract for the UMCA MVP. */

#define UMCA_GD32_BOARD_NAME          "GD32F470V-START"
#define UMCA_GD32_MCU_NAME            "GD32F470VKT6"

#define UMCA_GD32_CONSOLE_DEVICE      "/dev/ttyS0"
#define UMCA_GD32_CONSOLE_INSTANCE    "USART0"
#define UMCA_GD32_CONSOLE_TX_PIN      "PA9"
#define UMCA_GD32_CONSOLE_RX_PIN      "PA10"
#define UMCA_GD32_CONSOLE_BAUD        115200
#define UMCA_GD32_CONSOLE_FORMAT      "8N1"

#define UMCA_GD32_UART_DEVICE         "/dev/ttyS1"
#define UMCA_GD32_UART_INSTANCE       "UART4"
#define UMCA_GD32_UART_TX_PIN         "PC12"
#define UMCA_GD32_UART_RX_PIN         "PD2"
#define UMCA_GD32_UART_AF             8
#define UMCA_GD32_UART_BAUD           115200
#define UMCA_GD32_UART_FORMAT         "8N1"
#define UMCA_GD32_UART_ELECTRICAL     "3.3V CMOS TTL"

#endif
