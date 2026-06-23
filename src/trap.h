// trap.h — 트랩(인터럽트/예외) 처리
#ifndef TRAP_H
#define TRAP_H

void trap_init(void);    // stvec 등록
void timer_init(void);   // 타이머 인터럽트 시작
void kerneltrap(void);   // kernelvec.S가 호출하는 C 핸들러

#endif
