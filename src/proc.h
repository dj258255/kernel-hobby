// proc.h — 프로세스(커널 스레드) + 스케줄러
#ifndef PROC_H
#define PROC_H

#include "types.h"

// 컨텍스트 스위치 때 저장/복원하는 callee-saved 레지스터들.
// (swtch.S의 오프셋과 정확히 일치해야 한다)
struct context {
    uint64 ra;
    uint64 sp;
    uint64 s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
};

enum proc_state { UNUSED, RUNNABLE, RUNNING };

struct proc {
    enum proc_state state;
    struct context  context;
    char           *kstack;    // 커널 스택(한 페이지)
    void          (*fn)(void); // 스레드 본체
    uint64          counter;   // 데모용: 이 스레드가 얼마나 돌았는지
    char            name[8];
};

void         procinit(void);
struct proc *make_thread(void (*fn)(void), const char *name);
void         scheduler(void);       // 돌아오지 않음
void         yield(void);           // 타이머/자발적으로 스케줄러에 양보
struct proc *current_proc(void);    // 현재 실행 중인 proc(없으면 0)
void         proc_dump(void);       // ps 명령용

#endif
