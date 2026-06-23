// main.c — C 커널 시작점
// entry.S가 스택을 잡고 여기로 넘어온다.

#include "uart.h"
#include "trap.h"
#include "plic.h"
#include "shell.h"
#include "kalloc.h"
#include "vm.h"
#include "proc.h"

// 데모용 커널 스레드: 스핀하며 자기 카운터를 증가(타이머가 선점).
static void counter_thread(void) {
    for (;;)
        current_proc()->counter++;
}

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

    // Stage 5: 커널 스레드 + 유저 프로세스(자체 주소공간)를 함께 스케줄.
    procinit();
    make_thread(counter_thread, "spinK");  // 커널 스레드(S-mode)
    make_user_proc("userP");               // 유저 프로세스(U-mode, 자체 페이지 테이블)
    uart_puts("[ok] scheduler: 1 kernel thread + 1 user process. try 'ps'.\n");

    // 셸 입력은 UART 인터럽트로 처리되어, 어느 프로세스가 돌든 응답한다.
    shell_init();   // 환영 + 프롬프트 (마지막)

    scheduler();    // RUNNABLE 프로세스를 선점형으로 실행 (돌아오지 않음)
}
