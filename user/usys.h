// usys.h — 유저 공간 시스템콜 래퍼 (ecall)
// 커널과 공유하지 않는 독립 헤더(유저 프로그램은 따로 컴파일된다).
#ifndef USYS_H
#define USYS_H

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
#define SYS_sbrk   12
#define SYS_mmap   13
#define SYS_create 14
#define SYS_rm     15

// a7=콜 번호, a0=인자/반환값. ecall로 S-mode 트랩을 일으킨다.
static inline long __syscall(long num, long arg0) {
    register long a0 asm("a0") = arg0;
    register long a7 asm("a7") = num;
    asm volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
    return a0;
}

// 인자 2개(a0, a1)
static inline long __syscall2(long num, long arg0, long arg1) {
    register long a0 asm("a0") = arg0;
    register long a1 asm("a1") = arg1;
    register long a7 asm("a7") = num;
    asm volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a7) : "memory");
    return a0;
}

static inline long sys_fork(void)               { return __syscall(SYS_fork, 0); }
static inline long sys_exec(const char *p)      { return __syscall(SYS_exec, (long)p); }
static inline long sys_read(char *buf, long n)  { return __syscall2(SYS_read, (long)buf, n); }
static inline long sys_wait(void)               { return __syscall(SYS_wait, 0); }
static inline void sys_ls(void)                 { __syscall(SYS_ls, 0); }
static inline void sys_cat(const char *p)       { __syscall(SYS_cat, (long)p); }
static inline void sys_mem(void)                { __syscall(SYS_mem, 0); }
static inline long sys_sbrk(long n)             { return __syscall(SYS_sbrk, n); }
static inline long sys_mmap(const char *p)      { return __syscall(SYS_mmap, (long)p); }
static inline long sys_create(const char *name, const char *data) { return __syscall2(SYS_create, (long)name, (long)data); }
static inline long sys_rm(const char *name)     { return __syscall(SYS_rm, (long)name); }
static inline void sys_print(long who)          { __syscall(SYS_print, who); }
static inline void sys_tick(void)               { __syscall(SYS_tick, 0); }
static inline void sys_exit(void)               { __syscall(SYS_exit, 0); }
static inline void sys_putchar(long c)          { __syscall(SYS_putchar, c); }

#endif
