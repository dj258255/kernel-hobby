// trap.h — 트랩(인터럽트/예외) 처리
#ifndef TRAP_H
#define TRAP_H

#include "types.h"

void   trap_init(void);     // stvec 등록 + 타이머/외부 인터럽트 enable
void   timer_init(void);    // 첫 타이머 예약
void   interrupts_on(void); // 글로벌 인터럽트 enable (모든 준비 후 마지막에)
void   kerneltrap(void);    // kernelvec.S가 호출하는 C 핸들러
uint64 clock_ticks(void);   // 부팅 후 타이머 틱 수 (0.1초 단위)

#endif
