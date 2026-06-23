// proc.c — 커널 스레드 + 선점형 라운드로빈 스케줄러
//
// 스레드는 S-mode에서 도는 커널 함수다. 각자 커널 스택과 컨텍스트를 갖고,
// 타이머 인터럽트가 yield()를 불러 강제로 전환된다(선점형).

#include "proc.h"
#include "types.h"
#include "kalloc.h"
#include "csr.h"
#include "uart.h"
#include "vm.h"
#include "user.h"   // user_program

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

static void copybytes(void *dst, const void *src, uint64 n) {
    char *d = dst;
    const char *s = src;
    while (n-- > 0) *d++ = *s++;
}

// 새 커널 스레드가 처음 실행될 때 거치는 래퍼: 인터럽트를 켜고 본체를 호출.
static void thread_start(void) {
    intr_on();            // 이 스레드부터 인터럽트(=선점) 받기 시작
    cur->fn();            // 본체 실행
    cur->state = UNUSED;  // 본체가 끝나면(보통 안 끝남) 종료 처리
    yield();              // 스케줄러로 (돌아오지 않음)
    for (;;) ;
}

// 유저 프로세스가 처음 실행될 때 거치는 래퍼: S-mode→U-mode로 진입.
// 스케줄러가 swtch로 여기 진입시키며, satp은 이미 이 프로세스 테이블이다.
static void enter_user(void) {
    uint64 s = r_sstatus();
    s &= ~SSTATUS_SPP;   // SPP=0 → sret 시 U-mode로
    s |= SSTATUS_SPIE;   // U-mode에서 인터럽트 enable(선점 가능)
    s |= SSTATUS_SUM;    // 트랩 시 커널이 유저 스택에 프레임 저장 가능
    w_sstatus(s);
    w_sepc(USERVA);      // 유저 진입점
    // sp를 유저 스택 top으로 잡고 sret. 돌아오지 않는다.
    asm volatile("mv sp, %0\n sret\n" :: "r"((uint64)USERSTACKTOP));
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
        p->pagetable = kernel_pt();           // 커널 스레드는 커널 주소공간
        p->is_user = 0;
        int j = 0;
        for (; name[j] && j < 7; j++) p->name[j] = name[j];
        p->name[j] = 0;
        p->state = RUNNABLE;
        return p;
    }
    return 0;
}

// 유저 프로세스 생성: 자체 페이지 테이블(주소공간) + 유저 코드/스택 + 커널 스택.
struct proc *make_user_proc(const char *name) {
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &proctable[i];
        if (p->state != UNUSED)
            continue;
        // 유저 코드 페이지: 프로그램을 새 물리 페이지로 복사
        char *code = kalloc();
        copybytes(code, (const void *)user_program, PGSIZE);
        char *ustack = kalloc();                       // 유저 스택 1페이지
        p->pagetable = proc_pagetable((uint64)code, (uint64)ustack);
        p->is_user = 1;
        p->counter = 0;
        p->fn = 0;
        p->kstack = kalloc();                          // 커널 스택(enter_user 실행용)
        zero(&p->context, sizeof(p->context));
        p->context.ra = (uint64)enter_user;            // 첫 swtch는 enter_user로
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
                switch_satp(p->pagetable);           // 이 프로세스 주소공간으로
                swtch(&sched_context, &p->context);  // p 실행 → yield 시 복귀
                cur = 0;
                switch_satp(kernel_pt());            // 커널 주소공간으로 복귀
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
