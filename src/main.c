// main.c — C 커널 시작점
// entry.S가 스택을 잡고 여기로 넘어온다.

#include "uart.h"

void kmain(void) {
    uart_init();
    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("  hobby-kernel v0.1  (C / RISC-V)\n");
    uart_puts("  booted in S-mode under OpenSBI\n");
    uart_puts("========================================\n");
    uart_puts("Hello from a C kernel!\n");

    // 아직 할 일이 없으니 무한 대기 (entry.S가 wfi 루프로 받는다).
    for (;;)
        ;
}
