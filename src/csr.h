// csr.h — RISC-V 제어/상태 레지스터(CSR) 접근 + 비트 상수
#ifndef CSR_H
#define CSR_H

#include "types.h"

// sstatus (S-mode 상태)
#define SSTATUS_SIE (1UL << 1)   // S-mode 글로벌 인터럽트 enable

// sie (S-mode 인터럽트 enable)
#define SIE_SSIE (1UL << 1)      // 소프트웨어
#define SIE_STIE (1UL << 5)      // 타이머
#define SIE_SEIE (1UL << 9)      // 외부

// scause
#define SCAUSE_INTERRUPT (1UL << 63) // 최상위 비트: 1=인터럽트, 0=예외
#define SCAUSE_S_TIMER   5           // 인터럽트 코드 5 = S-mode 타이머

static inline uint64 r_sstatus(void) { uint64 x; asm volatile("csrr %0, sstatus" : "=r"(x)); return x; }
static inline void   w_sstatus(uint64 x) { asm volatile("csrw sstatus, %0" : : "r"(x)); }
static inline uint64 r_sie(void)     { uint64 x; asm volatile("csrr %0, sie" : "=r"(x)); return x; }
static inline void   w_sie(uint64 x) { asm volatile("csrw sie, %0" : : "r"(x)); }
static inline void   w_stvec(uint64 x) { asm volatile("csrw stvec, %0" : : "r"(x)); }
static inline uint64 r_scause(void)  { uint64 x; asm volatile("csrr %0, scause" : "=r"(x)); return x; }
static inline uint64 r_sepc(void)    { uint64 x; asm volatile("csrr %0, sepc" : "=r"(x)); return x; }
static inline uint64 r_stval(void)   { uint64 x; asm volatile("csrr %0, stval" : "=r"(x)); return x; }
static inline uint64 r_time(void)    { uint64 x; asm volatile("csrr %0, time" : "=r"(x)); return x; }

// stimecmp (sstc 확장, CSR 0x14D). 어셈블러가 이름을 모를 수 있어 번호로 접근.
static inline void w_stimecmp(uint64 x) { asm volatile("csrw 0x14d, %0" : : "r"(x)); }

// S-mode 글로벌 인터럽트 켜기
static inline void intr_on(void) { w_sstatus(r_sstatus() | SSTATUS_SIE); }

#endif
