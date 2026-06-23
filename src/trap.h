// trap.h — 트랩(인터럽트/예외) 처리
#ifndef TRAP_H
#define TRAP_H

#include "types.h"

// kernelvec.S가 스택에 저장하는 레지스터 레이아웃(오프셋과 정확히 일치).
struct regframe {
    uint64 ra, gp, tp, t0, t1, t2, s0, s1;       //   0 ..  56
    uint64 a0, a1, a2, a3, a4, a5, a6, a7;        //  64 .. 120
    uint64 s2, s3, s4, s5, s6, s7, s8, s9, s10, s11; // 128 .. 200
    uint64 t3, t4, t5, t6;                        // 208 .. 232
};

void   trap_init(void);     // stvec 등록 + 타이머/외부 인터럽트 enable
void   timer_init(void);    // 첫 타이머 예약
void   interrupts_on(void); // 글로벌 인터럽트 enable (모든 준비 후 마지막에)
void   kerneltrap(struct regframe *f); // kernelvec.S가 호출하는 C 핸들러
uint64 clock_ticks(void);   // 부팅 후 타이머 틱 수 (0.1초 단위)

#endif
