// user.c — 유저 프로그램 + 시스템콜 디스패치
//
// Stage 5+: 유저 프로그램이 fork()로 자신을 복제한다. 부모와 자식은
// fork() 다음 줄부터 똑같이 실행하되, 반환값(a0)만 다르다(부모=자식pid, 자식=0).

#include "user.h"
#include "types.h"
#include "uart.h"
#include "proc.h"   // current_proc(), yield(), proc_fork()

#define SYS_putchar 1
#define SYS_print   2
#define SYS_tick    3
#define SYS_exit    4
#define SYS_fork    5

// 유저 프로그램(naked: 프롤로그/에필로그 없음).
// fork() → 부모/자식이 각자 인사 → 25회 (틱 + 바쁜 대기) → 종료.
// fork()의 반환값 a0(부모=pid, 자식=0)를 그대로 SYS_print의 선택자로 쓴다.
__attribute__((naked)) void user_program(void) {
    asm volatile(
        "li a7, 5\n"            // SYS_fork
        "ecall\n"               //   a0 = 자식 pid(부모) / 0(자식)
        "li a7, 2\n"            // SYS_print: a0=0이면 자식 메시지, 아니면 부모 메시지
        "ecall\n"
        "li s1, 25\n"           // 바깥 루프: 25회 틱 후 종료
        "1:\n"
        "  li a7, 3\n"          // SYS_tick
        "  ecall\n"
        "  li s0, 0x2000000\n"  // 바쁜 대기(~33M: 틱 간격 벌리기)
        "2: addi s0, s0, -1\n"
        "   bnez s0, 2b\n"
        "  addi s1, s1, -1\n"
        "  bnez s1, 1b\n"
        "li a7, 4\n"            // SYS_exit
        "ecall\n"
        "3: j 3b\n"             // 안전망
    );
}

void syscall(struct regframe *f) {
    switch (f->a7) {
    case SYS_putchar:
        uart_putc((char)f->a0);
        f->a0 = 0;
        break;
    case SYS_print:
        if (f->a0 != 0)
            uart_puts("  [parent] fork() returned a child; we are two now.\n");
        else
            uart_puts("  [child]  hello -- I was created by fork().\n");
        f->a0 = 0;
        break;
    case SYS_tick:
        current_proc()->counter++;  // ps에서 이 프로세스가 도는 게 보인다
        break;
    case SYS_exit:
        current_proc()->state = UNUSED;  // 스케줄러가 다시는 안 고름
        yield();                         // 스케줄러로 영구 양보(돌아오지 않음)
        break;
    case SYS_fork:
        f->a0 = proc_fork(f);  // 부모 반환값 = 자식 pid (자식은 proc_fork가 0으로 세팅)
        break;
    default:
        uart_puts("[syscall] unknown number\n");
        f->a0 = (uint64)-1;
        break;
    }
}
