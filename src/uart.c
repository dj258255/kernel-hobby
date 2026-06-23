// uart.c — NS16550 UART 드라이버 (QEMU virt: 0x1000_0000)
//
// QEMU virt 머신의 시리얼 포트는 메모리 매핑된 NS16550 호환 UART다.
// OpenSBI가 이미 초기화해 두므로, 우리는 송신 레지스터에 쓰기만 하면 된다.
// (이게 OS 출력의 본질 — 하드웨어 주소에 직접 쓴다. xv6의 uart.c와 같은 방식.)

#include "uart.h"

#define UART0 0x10000000L
#define UART_THR 0  // Transmit Holding Register (쓰면 한 글자 전송)
#define UART_LSR 5  // Line Status Register
#define LSR_TX_IDLE (1 << 5)  // THR가 비어 다음 글자를 받을 수 있음

static volatile unsigned char *const uart = (volatile unsigned char *)UART0;

void uart_init(void) {
    // OpenSBI가 이미 보레이트 등을 설정해 두어 추가 초기화가 필요 없다.
}

void uart_putc(char c) {
    // 송신 버퍼가 빌 때까지 기다린 뒤 한 글자 쓴다.
    while ((uart[UART_LSR] & LSR_TX_IDLE) == 0)
        ;
    uart[UART_THR] = (unsigned char)c;
}

void uart_puts(const char *s) {
    for (; *s; s++) {
        if (*s == '\n')
            uart_putc('\r');  // 터미널 줄바꿈 보정
        uart_putc(*s);
    }
}

void uart_dec(uint64 n) {
    char buf[21];
    int i = 0;
    if (n == 0) {
        uart_putc('0');
        return;
    }
    while (n > 0) {
        buf[i++] = (char)('0' + (n % 10));
        n /= 10;
    }
    while (i > 0)
        uart_putc(buf[--i]);
}

void uart_hex(uint64 n) {
    uart_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        int d = (int)((n >> shift) & 0xf);
        uart_putc(d < 10 ? (char)('0' + d) : (char)('a' + d - 10));
    }
}
