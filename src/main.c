// main.c — C 커널 시작점
// entry.S가 스택을 잡고 여기로 넘어온다.

#include "uart.h"
#include "trap.h"
#include "plic.h"
#include "kalloc.h"
#include "vm.h"
#include "proc.h"
#include "virtio.h"
#include "fs.h"
#include "spinlock.h"
#include "csr.h"

#define NHART 3   // QEMU -smp 3 (하트 0 + 보조 2)

extern char _entry[];            // entry.S 진입점
static struct spinlock pr_lock;  // 보조 하트 출력 보호

// SBI HSM 확장으로 보조 하트를 깨운다(start_addr=_entry, a0=hartid로 진입).
static long sbi_hart_start(uint64 hartid, uint64 addr) {
    register uint64 a0 asm("a0") = hartid;
    register uint64 a1 asm("a1") = addr;
    register uint64 a2 asm("a2") = 0;
    register uint64 a6 asm("a6") = 0;          // FID 0 = hart_start
    register uint64 a7 asm("a7") = 0x48534D;   // EID "HSM"
    asm volatile("ecall" : "+r"(a0), "+r"(a1) : "r"(a2), "r"(a6), "r"(a7) : "memory");
    return (long)a0;   // SBI error code (0=성공)
}

// 보조 하트 진입점(entry.S가 호출). 자기 satp만 잡고 온라인을 알린 뒤 대기.
// (다음 단계에서 스케줄러에 합류시킬 예정)
void hart_main(void) {
    kvminithart();   // 이 코어의 satp에 (하트 0이 만든) 커널 페이지 테이블 적재
    acquire(&pr_lock);
    uart_puts("[ok] hart ");
    uart_dec(r_tp());
    uart_puts(" online (secondary core)\n");
    release(&pr_lock);
    for (;;)
        asm volatile("wfi");   // 일단 대기
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

    virtio_disk_init(); // virtio-blk 디스크
    fs_init();          // 파일시스템 마운트

    // SMP: 스핀락 준비 후 보조 하트(코어)들을 SBI HSM으로 깨운다.
    initlock(&pr_lock);
    uart_puts("[ok] hart 0 online (boot core), starting ");
    uart_dec(NHART - 1);
    uart_puts(" secondary core(s)\n");
    for (int h = 1; h < NHART; h++)
        sbi_hart_start(h, (uint64)_entry);   // 반환값 0=성공

    // Stage 7+: 유저공간 셸을 첫 유저 프로세스로 띄운다(커널에 임베드된 init=셸).
    procinit();
    make_user_proc("sh");
    uart_puts("[ok] starting userspace shell\n");

    scheduler();    // RUNNABLE 프로세스를 선점형으로 실행 (돌아오지 않음)
}
