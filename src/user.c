// user.c — 시스템콜 디스패치 (커널 측)
//
// 유저 프로그램은 이제 user/init.c로 따로 컴파일되어 ELF로 임베드된다.
// 여기는 그 프로그램이 ecall로 부른 시스템콜을 처리하는 커널 측 핸들러다.

#include "user.h"
#include "types.h"
#include "uart.h"
#include "proc.h"   // current_proc(), yield(), proc_fork()

#define SYS_putchar 1
#define SYS_print   2
#define SYS_tick    3
#define SYS_exit    4
#define SYS_fork    5
#define SYS_exec    6

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
    case SYS_exec: {
        // a0 = 유저 공간의 경로 문자열 포인터. SUM으로 읽어 커널 버퍼에 복사.
        static char path[64];
        const char *u = (const char *)f->a0;
        int i = 0;
        for (; i < 63 && u[i]; i++)
            path[i] = u[i];
        path[i] = 0;
        f->a0 = (uint64)proc_exec(path);  // 성공 시 돌아오지 않음, 실패 시 -1
        break;
    }
    default:
        uart_puts("[syscall] unknown number\n");
        f->a0 = (uint64)-1;
        break;
    }
}
