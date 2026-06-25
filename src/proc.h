// proc.h — 프로세스(커널 스레드) + 스케줄러
#ifndef PROC_H
#define PROC_H

#include "types.h"
#include "vm.h"   // pagetable_t
#include "spinlock.h"

extern struct spinlock pt_lock;   // proctable + 스케줄링 보호(sleep/wakeup 호출 시 보유)

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
    uint64          tf_va;      // fork된 자식의 트랩 프레임 VA(forkret용)
    uint64          heap_top;   // 힙 끝(sbrk로 성장, 페이지는 폴트 시 지연 할당)
    uint64          mmap_base;  // mmap 영역 시작 VA(0이면 없음)
    uint32          mmap_start; // 매핑된 파일의 시작 블록
    uint32          mmap_size;  // 매핑된 파일 크기(바이트)
};

void         procinit(void);
struct proc *make_thread(void (*fn)(void), const char *name);
struct proc *make_user_proc(const char *name);  // 유저 프로세스 생성(자체 주소공간)
int          proc_fork(struct regframe *f);      // 현재 유저 프로세스를 복제(자식 pid 반환)
int          proc_exec(const char *path);        // 현재 프로세스를 디스크의 ELF로 교체(실패 시 -1)
void         proc_exit(void);       // 현재 프로세스 종료(ZOMBIE) + 부모 깨움
int          proc_wait(void);       // 자식 하나가 종료할 때까지 대기(자식 pid 반환, 없으면 -1)
void         proc_freeimage(struct proc *p);  // 프로세스 자원 회수(페이지/테이블)
uint64       proc_sbrk(int n);     // 힙을 n바이트 키운다(이전 heap_top 반환). 지연 할당.
uint64       proc_mmap(const char *path);  // 파일을 주소공간에 매핑(베이스 VA 반환, 실패 -1)
int          proc_pagefault(uint64 va, int store);  // 힙/mmap 폴트면 페이지 적재(1=처리됨)
void         sleep(void *chan);     // chan에서 잠들고 스케줄러에 양보
void         wakeup(void *chan);    // chan에서 자는 모든 프로세스를 깨움
void         scheduler(void);       // 돌아오지 않음
void         yield(void);           // 타이머/자발적으로 스케줄러에 양보
struct proc *current_proc(void);    // 현재 실행 중인 proc(없으면 0)
void         proc_dump(void);       // ps 명령용

#endif
