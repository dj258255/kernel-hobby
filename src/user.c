// user.c — 유저모드 진입 + 시스템콜
//
// 트램폴린 없이 단순화한 방식:
//  - 커널 페이지 테이블에 유저 코드/스택을 PTE_U로 추가 매핑(한 주소공간)
//  - sstatus.SUM=1 → 트랩 시 커널이 유저 스택에 레지스터를 저장 가능
//  - 트랩 진입점은 기존 kernelvec 재사용(sp가 유저 스택이어도 동작)
// (xv6의 트램폴린/프로세스별 페이지 테이블은 Stage 4에서 도입)

#include "user.h"
#include "types.h"
#include "csr.h"
#include "vm.h"
#include "kalloc.h"
#include "uart.h"

#define PGSIZE    4096
#define USERVA    0x1000L   // 유저 코드 가상주소
#define USERSTACK 0x4000L   // 유저 스택 가상주소(1페이지)

#define SYS_putchar 1
#define SYS_print   2

// 유저 프로그램. naked라 프롤로그/에필로그가 없고, 메모리 접근 없이
// ecall만 하므로 USERVA로 바이트 복사해도 그대로 위치독립 실행된다.
__attribute__((naked)) void user_program(void);
__attribute__((naked)) void user_program(void) {
    asm volatile(
        "li a7, 1\n"   // SYS_putchar
        "li a0, 85\n"  // 'U'
        "ecall\n"
        "li a7, 1\n"
        "li a0, 10\n"  // '\n'
        "ecall\n"
        "li a7, 2\n"   // SYS_print
        "ecall\n"
        "1: j 1b\n"    // 이후 U-mode에서 대기(타이머 인터럽트는 계속 처리됨)
    );
}

static void copy_page(void *dst, const void *src, uint64 n) {
    char *d = dst;
    const char *s = src;
    while (n-- > 0)
        *d++ = *s++;
}

void user_init(void) {
    // 유저 코드 페이지: 새 페이지에 프로그램을 복사해 USERVA에 U+R+X로 매핑
    void *code = kalloc();
    copy_page(code, (const void *)user_program, PGSIZE);
    kvm_map(USERVA, (uint64)code, PGSIZE, PTE_U | PTE_R | PTE_X);

    // 유저 스택 페이지: U+R+W
    void *stack = kalloc();
    kvm_map(USERSTACK, (uint64)stack, PGSIZE, PTE_U | PTE_R | PTE_W);
}

void user_run(void) {
    uint64 s = r_sstatus();
    s &= ~SSTATUS_SPP;   // SPP=0 → sret 시 U-mode로
    s |= SSTATUS_SPIE;   // U-mode에서 인터럽트 enable
    s |= SSTATUS_SUM;    // S-mode가 유저(U) 페이지에 트랩 프레임 저장 가능
    s &= ~SSTATUS_SIE;   // sret 직전 S-mode 인터럽트는 꺼둔다(SPIE가 U에서 켬)
    w_sstatus(s);
    w_sepc(USERVA);      // 유저 진입점

    // 유저 스택 top으로 sp를 잡고 sret. 이 함수는 돌아오지 않는다.
    asm volatile(
        "mv sp, %0\n"
        "sret\n"
        :: "r"(USERSTACK + PGSIZE)
    );
}

void syscall(struct regframe *f) {
    switch (f->a7) {
    case SYS_putchar:
        uart_putc((char)f->a0);   // 유저가 a0로 넘긴 글자 출력
        f->a0 = 0;
        break;
    case SYS_print:
        uart_puts("Hello from user mode! (printed by the kernel, requested via ecall)\n");
        f->a0 = 0;
        break;
    default:
        uart_puts("[syscall] unknown number\n");
        f->a0 = (uint64)-1;
        break;
    }
}
