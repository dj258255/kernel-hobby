// uart.c — NS16550 UART 드라이버 (QEMU virt: 0x1000_0000)
//
// QEMU virt 머신의 시리얼 포트는 메모리 매핑된 NS16550 호환 UART다.
// QEMU virt에선 UART가 이미 사용 가능하고 OpenSBI도 같은 UART를 콘솔로 쓰므로,
// 별도 초기화 없이 송신 레지스터에 쓰기만 하면 된다. (SBI 콘솔 호출이 아니라
// MMIO에 직접 쓰는 방식 — OpenSBI와 같은 장치를 공유한다. xv6의 uart.c와 같은 방식.)

#include "uart.h"
#include "console.h"
#include "spinlock.h"

static struct spinlock uart_lock;  // 여러 코어 출력이 섞이지 않게(메시지 단위 직렬화)

#define UART0 0x10000000L
#define UART_RBR 0  // Receive Buffer Register (읽으면 받은 글자)
#define UART_THR 0  // Transmit Holding Register (쓰면 한 글자 전송)
#define UART_IER 1  // Interrupt Enable Register
#define UART_LSR 5  // Line Status Register
#define LSR_RX_READY (1 << 0)  // 받은 글자가 있음
#define LSR_TX_IDLE  (1 << 5)  // THR가 비어 다음 글자를 받을 수 있음

static volatile unsigned char *const uart = (volatile unsigned char *)UART0;

void uart_init(void) {
    initlock(&uart_lock);
    // QEMU virt에선 UART가 이미 사용 가능해(OpenSBI도 같은 UART 사용) 추가 초기화가 필요 없다.
}

// 락 없이 한 글자(내부용). 공개 함수가 메시지 단위로 락을 잡고 호출.
static void putc_raw(char c) {
    while ((uart[UART_LSR] & LSR_TX_IDLE) == 0)
        ;
    uart[UART_THR] = (unsigned char)c;
}

void uart_putc(char c) {
    acquire(&uart_lock);
    putc_raw(c);
    release(&uart_lock);
}

void uart_puts(const char *s) {
    acquire(&uart_lock);
    for (; *s; s++) {
        if (*s == '\n')
            putc_raw('\r');  // 터미널 줄바꿈 보정
        putc_raw(*s);
    }
    release(&uart_lock);
}

void uart_dec(uint64 n) {
    char buf[21];
    int i = 0;
    acquire(&uart_lock);
    if (n == 0) {
        putc_raw('0');
        release(&uart_lock);
        return;
    }
    while (n > 0) {
        buf[i++] = (char)('0' + (n % 10));
        n /= 10;
    }
    while (i > 0)
        putc_raw(buf[--i]);
    release(&uart_lock);
}

void uart_hex(uint64 n) {
    acquire(&uart_lock);
    putc_raw('0');
    putc_raw('x');
    for (int shift = 60; shift >= 0; shift -= 4) {
        int d = (int)((n >> shift) & 0xf);
        putc_raw(d < 10 ? (char)('0' + d) : (char)('a' + d - 10));
    }
    release(&uart_lock);
}

void uart_enable_rx(void) {
    uart[UART_IER] |= 0x01;  // "received data available" 인터럽트 enable
}

int uart_getc(void) {
    if (uart[UART_LSR] & LSR_RX_READY)
        return uart[UART_RBR];
    return -1;  // 받은 글자 없음
}

// UART 인터럽트가 났을 때 호출: 버퍼에 쌓인 글자를 모두 콘솔로 넘긴다.
void uart_intr(void) {
    int c;
    while ((c = uart_getc()) >= 0)
        console_intr((char)c);
}
