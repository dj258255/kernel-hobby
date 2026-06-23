// main.c — C 커널 시작점
// entry.S가 스택을 잡고 여기로 넘어온다.

#include "uart.h"
#include "trap.h"

void kmain(void) {
    uart_init();
    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("  hobby-kernel v0.1  (C / RISC-V)\n");
    uart_puts("  booted in S-mode under OpenSBI\n");
    uart_puts("========================================\n");
    uart_puts("Hello from a C kernel!\n");

    trap_init();   // 트랩 핸들러(stvec) 등록
    timer_init();  // 타이머 인터럽트 시작
    uart_puts("[ok] traps + timer interrupts enabled\n");

    // 인터럽트를 기다리며 CPU를 재운다(wfi). 타이머가 주기적으로 깨운다.
    for (;;)
        asm volatile("wfi");
}
