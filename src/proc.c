// proc.c — 커널 스레드 + 선점형 라운드로빈 스케줄러
//
// 스레드는 S-mode에서 도는 커널 함수다. 각자 커널 스택과 컨텍스트를 갖고,
// 타이머 인터럽트가 yield()를 불러 강제로 전환된다(선점형).

#include "proc.h"
#include "types.h"
#include "kalloc.h"
#include "csr.h"
#include "uart.h"

#define NPROC  8
#define PGSIZE 4096

extern void swtch(struct context *old, struct context *new);

static struct proc    proctable[NPROC];
static struct context sched_context;   // 스케줄러 자신의 컨텍스트
static struct proc   *cur = 0;         // 현재 실행 중인 proc

static void zero(void *p, uint64 n) {
    char *d = p;
    while (n-- > 0) *d++ = 0;
}

struct proc *current_proc(void) { return cur; }

// 새 스레드가 처음 실행될 때 거치는 래퍼: 인터럽트를 켜고 본체를 호출.
static void thread_start(void) {
    intr_on();            // 이 스레드부터 인터럽트(=선점) 받기 시작
    cur->fn();            // 본체 실행
    cur->state = UNUSED;  // 본체가 끝나면(보통 안 끝남) 종료 처리
    yield();              // 스케줄러로 (돌아오지 않음)
    for (;;) ;
}

void procinit(void) {
    for (int i = 0; i < NPROC; i++)
        proctable[i].state = UNUSED;
}

struct proc *make_thread(void (*fn)(void), const char *name) {
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &proctable[i];
        if (p->state != UNUSED)
            continue;
        p->fn = fn;
        p->counter = 0;
        p->kstack = kalloc();                 // 커널 스택 1페이지
        zero(&p->context, sizeof(p->context));
        p->context.ra = (uint64)thread_start; // 첫 swtch는 thread_start로 진입
        p->context.sp = (uint64)(p->kstack + PGSIZE);
        int j = 0;
        for (; name[j] && j < 7; j++) p->name[j] = name[j];
        p->name[j] = 0;
        p->state = RUNNABLE;
        return p;
    }
    return 0;
}

// 스케줄러: RUNNABLE proc을 골라 실행. proc이 yield하면 여기로 돌아온다.
void scheduler(void) {
    for (;;) {
        for (int i = 0; i < NPROC; i++) {
            struct proc *p = &proctable[i];
            if (p->state == RUNNABLE) {
                p->state = RUNNING;
                cur = p;
                swtch(&sched_context, &p->context);  // p 실행 → yield 시 복귀
                cur = 0;
            }
        }
    }
}

// 현재 스레드가 CPU를 스케줄러에 양보한다(타이머 인터럽트나 자발적으로).
void yield(void) {
    struct proc *p = cur;
    if (!p)
        return;
    if (p->state == RUNNING)
        p->state = RUNNABLE;
    swtch(&p->context, &sched_context);  // 스케줄러로
}

void proc_dump(void) {
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &proctable[i];
        if (p->state == UNUSED)
            continue;
        uart_puts("  ");
        uart_puts(p->name);
        uart_puts(": ticks=");
        uart_dec(p->counter);
        uart_putc('\n');
    }
}
