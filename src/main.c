// main.c — C 커널 시작점
// entry.S가 스택을 잡고 여기로 넘어온다.

#include "uart.h"
#include "trap.h"
#include "plic.h"
#include "kalloc.h"
#include "vm.h"
#include "proc.h"
#include "virtio.h"
#include "net.h"
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

// 보조 하트 진입점(entry.S가 호출). satp + 트랩을 잡고 스케줄러에 합류한다.
void hart_main(void) {
    kvminithart();   // 이 코어의 satp에 (하트 0이 만든) 커널 페이지 테이블 적재
    trap_init();     // 이 코어의 stvec + 타이머/외부 인터럽트 enable
    timer_init();    // 이 코어의 타이머 시작(선점)
    acquire(&pr_lock);
    uart_puts("[ok] hart ");
    uart_dec(r_tp());
    uart_puts(" online -> joining scheduler\n");
    release(&pr_lock);
    scheduler();     // 공유 proctable에서 RUNNABLE을 골라 실행(돌아오지 않음)
}

static int boot_taken = 0;   // 0이면 아직 부팅 하트 없음

void kmain(void) {
    // 먼저 진입한 하트가 부팅 하트(부팅 하트 id는 OpenSBI가 정하며 0이 아닐 수 있다).
    // 나머지(SBI로 깨운)는 보조 경로로.
    if (__sync_lock_test_and_set(&boot_taken, 1) != 0) {
        hart_main();             // 보조 하트 (돌아오지 않음)
        for (;;) asm volatile("wfi");
    }

    uart_init();
    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("  kernel-hobby v0.1  (C / RISC-V)\n");
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

    // Stage 8: 네트워킹 — virtio-net 초기화 후 ARP/DNS 연결성 데모(폴링).
    if (net_init() == 0)
        net_demo();

    // Stage 7+: 유저공간 셸을 첫 유저 프로세스로 띄운다(커널에 임베드된 init=셸).
    procinit();
    make_user_proc("sh");

    // SMP: 스핀락 준비 후, 자신을 제외한 모든 하트를 SBI HSM으로 깨워 스케줄러에 합류.
    initlock(&pr_lock);
    uart_puts("[ok] boot hart ");
    uart_dec(r_tp());
    uart_puts(" up; waking other cores + starting shell\n");
    for (int h = 0; h < NHART; h++)
        if ((uint64)h != r_tp())
            sbi_hart_start(h, (uint64)_entry);

    scheduler();    // 부팅 하트도 스케줄러에 합류 (돌아오지 않음)
}
