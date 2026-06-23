// main.c — C 커널 시작점
// entry.S가 스택을 잡고 여기로 넘어온다.

#include "uart.h"
#include "trap.h"
#include "plic.h"
#include "shell.h"
#include "kalloc.h"
#include "vm.h"

void kmain(void) {
    uart_init();
    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("  hobby-kernel v0.1  (C / RISC-V)\n");
    uart_puts("  booted in S-mode under OpenSBI\n");
    uart_puts("========================================\n");

    trap_init();        // stvec + 타이머/외부 인터럽트 enable
    plic_init();        // 장치 우선순위
    plic_init_hart();   // 이 hart에 UART 인터럽트 라우팅
    uart_enable_rx();   // UART 수신 인터럽트
    timer_init();       // 타이머 시작
    uart_puts("[ok] interrupts + uart input ready\n");

    kinit();            // 물리 페이지 할당기 (페이지 테이블용 페이지 공급)
    uart_puts("[ok] page allocator: ");
    uart_dec(kfreecount());
    uart_puts(" free pages\n");

    kvminit();          // 커널 페이지 테이블 생성
    kvminithart();      // satp 적재 → 페이징 ON (Sv39, 식별 매핑)
    uart_puts("[ok] paging enabled (Sv39 kernel page table)\n");

    shell_init();       // 환영 + 프롬프트
    interrupts_on();    // 글로벌 인터럽트 켜기 (모든 준비 후 마지막)

    // 인터럽트(타이머/키보드)를 기다리며 CPU를 재운다.
    for (;;)
        asm volatile("wfi");
}
