// usys.h — 유저 공간 시스템콜 래퍼 (ecall)
// 커널과 공유하지 않는 독립 헤더(유저 프로그램은 따로 컴파일된다).
#ifndef USYS_H
#define USYS_H

#define SYS_putchar 1
#define SYS_print   2
#define SYS_tick    3
#define SYS_exit    4
#define SYS_fork    5

// a7=콜 번호, a0=인자/반환값. ecall로 S-mode 트랩을 일으킨다.
static inline long __syscall(long num, long arg0) {
    register long a0 asm("a0") = arg0;
    register long a7 asm("a7") = num;
    asm volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
    return a0;
}

static inline long sys_fork(void)      { return __syscall(SYS_fork, 0); }
static inline void sys_print(long who) { __syscall(SYS_print, who); }
static inline void sys_tick(void)      { __syscall(SYS_tick, 0); }
static inline void sys_exit(void)      { __syscall(SYS_exit, 0); }
static inline void sys_putchar(long c) { __syscall(SYS_putchar, c); }

#endif
