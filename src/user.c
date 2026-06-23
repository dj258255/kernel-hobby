// user.c — 유저 프로그램 + 시스템콜 디스패치
//
// Stage 5: 유저 프로그램은 이제 '프로세스'다. 자기 페이지 테이블(주소공간)을
// 갖고, 스케줄러가 U-mode로 실행한다. ecall로 커널에 일을 부탁한다(시스템콜).

#include "user.h"
#include "types.h"
#include "uart.h"
#include "proc.h"   // current_proc(), yield(), proc 상태

#define SYS_putchar 1
#define SYS_print   2
#define SYS_tick    3
#define SYS_exit    4

// 유저 프로그램(naked: 프롤로그/에필로그 없음).
// 인사 한 번(SYS_print) → 30회 동안 (틱 + 바쁜 대기) → 종료(SYS_exit).
// 바쁜 대기 루프가 틱 사이 간격을 벌려, ps에서 카운터가 천천히 오르는 게 보인다.
// 메모리 접근이 없어(레지스터만 사용) USERVA로 바이트 복사해도 위치독립 실행된다.
__attribute__((naked)) void user_program(void) {
    asm volatile(
        "li a7, 2\n"            // SYS_print: 인사 한 번
        "ecall\n"
        "li s1, 40\n"           // 바깥 루프: 40회 틱 후 종료
        "1:\n"
        "  li a7, 3\n"          // SYS_tick
        "  ecall\n"
        "  li s0, 0x4000000\n"  // 바쁜 대기(~67M 카운트다운: 틱 간격 벌리기)
        "2: addi s0, s0, -1\n"
        "   bnez s0, 2b\n"
        "  addi s1, s1, -1\n"
        "  bnez s1, 1b\n"
        "li a7, 4\n"            // SYS_exit
        "ecall\n"
        "3: j 3b\n"             // 안전망(여기 도달하면 그냥 대기)
    );
}

void syscall(struct regframe *f) {
    switch (f->a7) {
    case SYS_putchar:
        uart_putc((char)f->a0);  // 유저가 a0로 넘긴 글자 출력
        f->a0 = 0;
        break;
    case SYS_print:
        uart_puts("Hello from a user PROCESS! (own page table, U-mode)\n");
        f->a0 = 0;
        break;
    case SYS_tick:
        current_proc()->counter++;  // ps에서 이 유저 프로세스가 도는 게 보인다
        break;
    case SYS_exit:
        current_proc()->state = UNUSED;  // 스케줄러가 다시는 안 고름
        yield();                         // 스케줄러로 영구 양보(돌아오지 않음)
        break;
    default:
        uart_puts("[syscall] unknown number\n");
        f->a0 = (uint64)-1;
        break;
    }
}
