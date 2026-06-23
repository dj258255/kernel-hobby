// uart.h — QEMU virt 머신의 시리얼(UART) 출력
#ifndef UART_H
#define UART_H

#include "types.h"

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_dec(uint64 n);     // 부호 없는 10진수 출력
void uart_hex(uint64 n);     // 0x... 16진수 출력

void uart_enable_rx(void);   // 수신 인터럽트 enable
int  uart_getc(void);        // 받은 글자 1개(없으면 -1)
void uart_intr(void);        // UART 인터럽트 발생 시 호출 (받은 글자를 셸로)

#endif
