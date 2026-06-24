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
#include "trap.h"   // struct regframe
#include "elf.h"    // load_elf
#include "fs.h"     // fs_read

#define NPROC  8
#define PGSIZE 4096

extern void swtch(struct context *old, struct context *new);
extern void forkret(void);  // kernelvec.S — fork된 자식 진입점
extern void userret_to(uint64 entry, uint64 sp, uint64 satp);  // kernelvec.S — exec 진입
extern char initcode[];     // initcode.S — 임베드된 유저 프로그램 ELF

static struct proc    proctable[NPROC];
static struct context sched_context;   // 스케줄러 자신의 컨텍스트
static struct proc   *cur = 0;         // 현재 실행 중인 proc
static int            next_pid = 1;    // 다음 프로세스 ID

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
        // 유저 코드 페이지: 임베드된 ELF를 파싱해 적재
        char *code = kalloc();
        zero(code, PGSIZE);                            // .bss(0 채움) 대비 미리 0
        uint64 entry;
        if (load_elf(initcode, code, &entry) != 0)
            return 0;                                  // ELF 적재 실패
        if (entry != USERVA)                           // _start는 USERVA에 링크됨
            uart_puts("[warn] elf entry != USERVA\n");
        char *ustack = kalloc();                       // 유저 스택 1페이지
        p->ucode = code;
        p->ustack = ustack;
        p->pagetable = proc_pagetable((uint64)code, (uint64)ustack);
        p->is_user = 1;
        p->counter = 0;
        p->fn = 0;
        p->pid = next_pid++;
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

// 현재 유저 프로세스를 복제한다(fork). 부모에겐 자식 pid를, 자식에겐 0을 반환.
//   f = 부모의 트랩 프레임(유저 스택 위, ecall 시점 상태).
int proc_fork(struct regframe *f) {
    struct proc *parent = cur;
    for (int i = 0; i < NPROC; i++) {
        struct proc *child = &proctable[i];
        if (child->state != UNUSED)
            continue;

        // 부모의 코드/스택 페이지를 새 물리 페이지로 통째 복사
        char *code = kalloc();
        char *ustack = kalloc();
        copybytes(code, parent->ucode, PGSIZE);
        copybytes(ustack, parent->ustack, PGSIZE);
        child->ucode = code;
        child->ustack = ustack;
        child->pagetable = proc_pagetable((uint64)code, (uint64)ustack);

        // 자식 스택에 복사된 트랩 프레임을 손본다:
        //  - 부모 프레임은 유저 VA (uint64)f 에 있고, 같은 VA가 자식에도 매핑됨.
        //  - 물리적으로는 복사본 ustack 안의 같은 오프셋에 들어 있다.
        uint64 off = (uint64)f - USERSTACK;
        struct regframe *cf = (struct regframe *)(ustack + off);
        cf->a0 = 0;             // 자식의 fork() 반환값 = 0
        cf->sepc = f->sepc + 4; // ecall 다음 명령부터(부모와 동일 지점)

        // 자식은 forkret로 진입 → s0(프레임 VA)로 sp 잡고 공통 복귀 경로
        zero(&child->context, sizeof(child->context));
        child->context.ra = (uint64)forkret;
        child->context.s0 = (uint64)f;             // 프레임 VA(자식에서도 동일)
        child->context.sp = (uint64)USERSTACKTOP;  // forkret 첫 명령이 덮어씀

        child->is_user = 1;
        child->counter = 0;
        child->fn = 0;
        child->kstack = 0;        // forkret은 유저 스택에서 진입(커널 스택 불필요)
        child->parent = parent;   // wait/exit용
        child->pid = next_pid++;
        int j = 0;
        for (; parent->name[j] && j < 6; j++) child->name[j] = parent->name[j];
        child->name[j++] = '+';   // 자식 표시
        child->name[j] = 0;
        child->state = RUNNABLE;
        return child->pid;
    }
    return -1;  // 빈 슬롯 없음
}

// 현재 프로세스를 디스크의 ELF 프로그램으로 교체한다(exec).
// 페이지 테이블과 스택은 재사용하고 코드 페이지만 갈아끼운다 → satp 전환이
// 없어 스택이 안 바뀌고, 옛 코드 페이지는 회수되어 누수가 없다.
// 성공하면 새 프로그램으로 진입해 돌아오지 않는다. 실패 시 -1.
int proc_exec(const char *path) {
    static uint8 elfbuf[8192];   // 커널 메모리(식별 매핑)

    int sz = fs_read(path, elfbuf, sizeof(elfbuf));
    if (sz < 0)
        return -1;

    char *newcode = kalloc();
    zero(newcode, PGSIZE);
    uint64 entry;
    if (load_elf((const char *)elfbuf, newcode, &entry) != 0) {
        kfree(newcode);
        return -1;
    }

    struct proc *p = cur;
    char *oldcode = p->ucode;
    remap_user_code(p->pagetable, (uint64)newcode);  // USERVA → 새 코드
    p->ucode = newcode;
    if (oldcode)
        kfree(oldcode);                              // 옛 코드 회수(누수 없음)

    // U-mode 진입. satp(주소공간)는 그대로라 스택이 안 바뀐다 → 스택 재사용.
    uint64 s = r_sstatus();
    s &= ~SSTATUS_SPP;   // U-mode로
    s |= SSTATUS_SPIE;   // 인터럽트 enable
    s |= SSTATUS_SUM;
    w_sstatus(s);
    w_sepc(entry);
    asm volatile("mv sp, %0\n sret\n" :: "r"((uint64)USERSTACKTOP));
    return -1;  // 도달하지 않음
}

// 프로세스의 자원을 회수한다(유저 페이지 + 커널 스택 + 페이지 테이블 구조).
void proc_freeimage(struct proc *p) {
    if (p->ucode)  kfree(p->ucode);
    if (p->ustack) kfree(p->ustack);
    if (p->kstack) kfree(p->kstack);
    if (p->pagetable) free_pagetable(p->pagetable);
    p->ucode = p->ustack = p->kstack = 0;
    p->pagetable = 0;
}

// 스케줄러: RUNNABLE proc을 골라 실행. proc이 yield하면 여기로 돌아온다.
void scheduler(void) {
    for (;;) {
        int ran = 0;
        for (int i = 0; i < NPROC; i++) {
            struct proc *p = &proctable[i];
            if (p->state == RUNNABLE) {
                p->state = RUNNING;
                cur = p;
                switch_satp(p->pagetable);           // 이 프로세스 주소공간으로
                swtch(&sched_context, &p->context);  // p 실행 → yield 시 복귀
                cur = 0;
                switch_satp(kernel_pt());            // 커널 주소공간으로 복귀
                ran = 1;
            }
        }
        if (!ran) {
            // 돌릴 게 없으면(모두 SLEEPING 등) 인터럽트를 켜고 쉰다.
            // 콘솔/타이머 인터럽트가 누군가를 깨우면 다시 돈다.
            intr_on();
            asm volatile("wfi");
            intr_off();
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

// chan에서 잠든다: 상태를 SLEEPING으로 두고 스케줄러에 양보.
// 트랩 처리 중(SIE=0)이라 "검사→sleep"이 원자적 → 잃어버린 wakeup 없음.
void sleep(void *chan) {
    struct proc *p = cur;
    if (!p)
        return;
    p->chan = chan;
    p->state = SLEEPING;
    swtch(&p->context, &sched_context);  // 깨어날 때까지 스케줄러로
    p->chan = 0;                         // 다시 스케줄되면 여기서 재개
}

// chan에서 자는 모든 프로세스를 RUNNABLE로.
void wakeup(void *chan) {
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &proctable[i];
        if (p->state == SLEEPING && p->chan == chan)
            p->state = RUNNABLE;
    }
}

// 현재 프로세스를 종료(ZOMBIE)하고 부모를 깨운다. 돌아오지 않는다.
void proc_exit(void) {
    struct proc *p = cur;
    if (p->parent)
        wakeup(p->parent);   // wait 중인 부모 깨우기
    p->state = ZOMBIE;
    swtch(&p->context, &sched_context);  // 스케줄러로(다신 안 돌아옴)
}

// 자식 하나가 종료할 때까지 기다린다. 종료한 자식을 회수하고 그 pid 반환.
int proc_wait(void) {
    struct proc *p = cur;
    for (;;) {
        int kids = 0;
        for (int i = 0; i < NPROC; i++) {
            struct proc *q = &proctable[i];
            if (q->parent != p)
                continue;
            kids = 1;
            if (q->state == ZOMBIE) {
                int pid = q->pid;
                proc_freeimage(q);  // 유저 페이지 + 페이지 테이블 회수
                q->parent = 0;
                q->state = UNUSED;
                return pid;
            }
        }
        if (!kids)
            return -1;        // 자식 없음
        sleep(p);             // 자식이 exit하며 wakeup(p)할 때까지
    }
}

void proc_dump(void) {
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &proctable[i];
        if (p->state == UNUSED)
            continue;
        uart_puts("  ");
        uart_puts(p->name);
        uart_puts(p->is_user ? " (user pid=" : " (kernel pid=");
        uart_dec(p->pid);
        uart_puts("): ticks=");
        uart_dec(p->counter);
        uart_putc('\n');
    }
}
