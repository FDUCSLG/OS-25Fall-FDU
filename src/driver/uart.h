#pragma once
#include <aarch64/intrinsic.h>

#define UART_DR (UARTBASE + 0x00)
#define UART_FR (UARTBASE + 0x18)
#define FR_RXFE (1 << 4) // Recieve fifo empty
#define FR_TXFF (1 << 5) // Transmit fifo full
#define FR_RXFF (1 << 6) // Recieve fifo full
#define FR_TXFE (1 << 7) // Transmit fifo empty
#define RXFE (device_get_u32(UART_FR) & FR_RXFE)
#define TXFF (device_get_u32(UART_FR) & FR_TXFF)
#define RXFF (device_get_u32(UART_FR) & FR_RXFF)
#define TXFE (device_get_u32(UART_FR) & FR_TXFE)
#define UART_IBRD (UARTBASE + 0x24)
#define UART_FBRD (UARTBASE + 0x28)
#define UART_LCRH (UARTBASE + 0x2c)
#define LCRH_FEN (1 << 4)
#define LCRH_WLEN_8BIT (3 << 5)
#define UART_CR (UARTBASE + 0x30)
#define UART_IMSC (UARTBASE + 0x38)
#define UART_ICR (UARTBASE + 0x44)

void uart_init();
void uart_put_char(char c);