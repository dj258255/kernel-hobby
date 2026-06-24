// user.c — 시스템콜 디스패치 (커널 측)
//
// 유저 프로그램(셸 user/init.c, 디스크의 hello 등)이 ecall로 부른 시스템콜을
// 처리하는 커널 측 핸들러.

#include "user.h"
#include "types.h"
#include "uart.h"
#include "proc.h"     // current_proc(), proc_fork/exec/exit/wait
#include "console.h"  // console_read
#include "fs.h"       // fs_ls, fs_cat
#include "kalloc.h"   // kfreecount

#define SYS_putchar 1
#define SYS_print   2
#define SYS_tick    3
#define SYS_exit    4
#define SYS_fork    5
#define SYS_exec    6
#define SYS_read    7
#define SYS_wait    8
#define SYS_ls      9
#define SYS_cat    10
#define SYS_mem    11

// 유저 공간의 문자열(a0이 가리키는)을 커널 버퍼로 복사(SUM으로 읽기).
static void copy_path(uint64 uptr, char *dst, int max) {
    const char *u = (const char *)uptr;
    int i = 0;
    for (; i < max - 1 && u[i]; i++)
        dst[i] = u[i];
    dst[i] = 0;
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
        current_proc()->counter++;
        break;
    case SYS_exit:
        proc_exit();   // ZOMBIE + 부모 깨움 (돌아오지 않음)
        break;
    case SYS_fork:
        f->a0 = proc_fork(f);
        break;
    case SYS_exec: {
        static char path[64];
        copy_path(f->a0, path, sizeof(path));
        f->a0 = (uint64)proc_exec(path);  // 성공 시 돌아오지 않음
        break;
    }
    case SYS_read:
        // a0 = 유저 버퍼(VA, SUM으로 씀), a1 = 최대 길이
        f->a0 = (uint64)console_read((char *)f->a0, (int)f->a1);
        break;
    case SYS_wait:
        f->a0 = (uint64)proc_wait();
        break;
    case SYS_ls:
        fs_ls();
        f->a0 = 0;
        break;
    case SYS_mem:
        uart_puts("free pages: ");
        uart_dec(kfreecount());
        uart_puts("  (~");
        uart_dec(kfreecount() * 4096 / 1024 / 1024);
        uart_puts(" MB)\n");
        f->a0 = 0;
        break;
    case SYS_cat: {
        static char path[64];
        copy_path(f->a0, path, sizeof(path));
        fs_cat(path);
        f->a0 = 0;
        break;
    }
    default:
        uart_puts("[syscall] unknown number\n");
        f->a0 = (uint64)-1;
        break;
    }
}
