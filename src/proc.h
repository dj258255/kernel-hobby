// proc.h — 프로세스(커널 스레드) + 스케줄러
#ifndef PROC_H
#define PROC_H

#include "types.h"
#include "vm.h"   // pagetable_t

// 컨텍스트 스위치 때 저장/복원하는 callee-saved 레지스터들.
// (swtch.S의 오프셋과 정확히 일치해야 한다)
struct context {
    uint64 ra;
    uint64 sp;
    uint64 s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
};

enum proc_state { UNUSED, RUNNABLE, RUNNING, SLEEPING, ZOMBIE };

struct regframe;  // trap.h

struct proc {
    enum proc_state state;
    struct context  context;
    char           *kstack;     // 커널 스택(한 페이지)
    void          (*fn)(void);  // 스레드 본체(커널 스레드)
    uint64          counter;    // 데모용: 이 스레드/프로세스가 얼마나 돌았는지
    char            name[8];
    pagetable_t     pagetable;  // 이 프로세스의 페이지 테이블(커널 스레드는 커널 테이블)
    int             is_user;    // U-mode 프로세스면 1
    int             pid;        // 프로세스 ID
    char           *ucode;      // 유저 코드 페이지(물리) — fork가 복사
    char           *ustack;     // 유저 스택 페이지(물리) — fork가 복사
    void           *chan;       // SLEEPING일 때 기다리는 채널
    struct proc    *parent;     // 부모(wait/exit용)
};

void         procinit(void);
struct proc *make_thread(void (*fn)(void), const char *name);
struct proc *make_user_proc(const char *name);  // 유저 프로세스 생성(자체 주소공간)
int          proc_fork(struct regframe *f);      // 현재 유저 프로세스를 복제(자식 pid 반환)
int          proc_exec(const char *path);        // 현재 프로세스를 디스크의 ELF로 교체(실패 시 -1)
void         proc_exit(void);       // 현재 프로세스 종료(ZOMBIE) + 부모 깨움
int          proc_wait(void);       // 자식 하나가 종료할 때까지 대기(자식 pid 반환, 없으면 -1)
void         sleep(void *chan);     // chan에서 잠들고 스케줄러에 양보
void         wakeup(void *chan);    // chan에서 자는 모든 프로세스를 깨움
void         scheduler(void);       // 돌아오지 않음
void         yield(void);           // 타이머/자발적으로 스케줄러에 양보
struct proc *current_proc(void);    // 현재 실행 중인 proc(없으면 0)
void         proc_dump(void);       // ps 명령용

#endif
