// uart.h — QEMU virt 머신의 시리얼(UART) 출력
#ifndef UART_H
#define UART_H

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);

#endif
