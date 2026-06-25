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
#include "spinlock.h"

#define NPROC  8
#define PGSIZE 4096

extern void swtch(struct context *old, struct context *new);
extern void trapret_from(uint64 frame);  // kernelvec.S — sp=frame 후 트랩 복귀
extern char initcode[];     // initcode.S — 임베드된 유저 프로그램 ELF

static struct proc    proctable[NPROC];
static struct proc   *cpu_proc[NCPU];      // 코어별 현재 proc
static struct context cpu_sched[NCPU];     // 코어별 스케줄러 컨텍스트
struct spinlock       pt_lock;             // proctable + 스케줄링 보호(콘솔도 공유)
static int            next_pid = 1;        // 다음 프로세스 ID

static void zero(void *p, uint64 n) {
    char *d = p;
    while (n-- > 0) *d++ = 0;
}

// 현재 코어가 실행 중인 proc.
struct proc *current_proc(void) { return cpu_proc[r_tp()]; }

// fork된 자식의 첫 실행 진입점: 스케줄러가 잡은 락을 놓고 트랩 프레임으로 복귀.
void forkret(void) {
    release(&pt_lock);
    struct proc *p = cpu_proc[r_tp()];
    trapret_from(p->tf_va);   // sp=프레임 VA; trapret (돌아오지 않음)
}

static void copybytes(void *dst, const void *src, uint64 n) {
    char *d = dst;
    const char *s = src;
    while (n-- > 0) *d++ = *s++;
}

// 새 커널 스레드가 처음 실행될 때 거치는 래퍼: 스케줄러 락을 놓고 본체 호출.
static void thread_start(void) {
    release(&pt_lock);    // 스케줄러가 잡은 락을 놓는다(첫 실행)
    intr_on();            // 이 스레드부터 인터럽트(=선점) 받기 시작
    current_proc()->fn(); // 본체 실행
    proc_exit();          // 본체가 끝나면 종료(돌아오지 않음)
    for (;;) ;
}

// 유저 프로세스가 처음 실행될 때 거치는 래퍼: S-mode→U-mode로 진입.
// 스케줄러가 swtch로 여기 진입시키며, satp은 이미 이 프로세스 테이블이다.
static void enter_user(void) {
    release(&pt_lock);   // 스케줄러가 잡은 락을 놓는다(첫 실행)
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
    initlock(&pt_lock);
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
        p->heap_top = HEAPBASE;
        p->mmap_base = 0;
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
    struct proc *parent = current_proc();
    acquire(&pt_lock);
    for (int i = 0; i < NPROC; i++) {
        struct proc *child = &proctable[i];
        if (child->state != UNUSED)
            continue;

        // 코드 페이지: 복사하지 않고 COW로 공유(읽기전용 R|X라 쓰기 폴트가 없음).
        //   → fork가 가벼워지고 메모리를 아낀다. refcount로 수명 관리.
        // 스택 페이지: 사적 복사 유지. 아래에서 자식의 트랩 프레임(a0/sepc)을 직접
        //   고쳐 써야 하는데, 공유하면 부모 프레임까지 망가지기 때문.
        char *ustack = kalloc();
        copybytes(ustack, parent->ustack, PGSIZE);
        kref_inc(parent->ucode);
        child->ucode = parent->ucode;
        child->ustack = ustack;
        child->pagetable = proc_pagetable((uint64)parent->ucode, (uint64)ustack);

        // 힙 페이지: COW로 공유(쓰기 시 비로소 복제). 부모가 이미 건드린 힙만 대상.
        uvm_cow_share(parent->pagetable, child->pagetable, HEAPBASE, parent->heap_top);

        // 자식 스택에 복사된 트랩 프레임을 손본다:
        //  - 부모 프레임은 유저 VA (uint64)f 에 있고, 같은 VA가 자식에도 매핑됨.
        //  - 물리적으로는 복사본 ustack 안의 같은 오프셋에 들어 있다.
        uint64 off = (uint64)f - USERSTACK;
        struct regframe *cf = (struct regframe *)(ustack + off);
        cf->a0 = 0;             // 자식의 fork() 반환값 = 0
        cf->sepc = f->sepc + 4; // ecall 다음 명령부터(부모와 동일 지점)

        // 자식은 forkret로 진입 → tf_va(프레임 VA)로 sp 잡고 공통 복귀 경로
        zero(&child->context, sizeof(child->context));
        child->context.ra = (uint64)forkret;
        child->context.sp = (uint64)USERSTACKTOP;  // forkret이 release() 동안 쓸 스택
        child->tf_va = (uint64)f;                  // 프레임 VA(자식에서도 동일)

        child->is_user = 1;
        child->counter = 0;
        child->fn = 0;
        child->kstack = 0;        // forkret은 유저 스택에서 진입(커널 스택 불필요)
        child->parent = parent;   // wait/exit용
        child->heap_top = parent->heap_top;  // (힙 페이지 복사는 생략 — 셸은 힙 미사용)
        child->mmap_base = 0;                 // mmap은 자식이 exec 후 자기 걸 설정
        child->pid = next_pid++;
        int j = 0;
        for (; parent->name[j] && j < 6; j++) child->name[j] = parent->name[j];
        child->name[j++] = '+';   // 자식 표시
        child->name[j] = 0;
        child->state = RUNNABLE;
        int pid = child->pid;
        release(&pt_lock);
        return pid;
    }
    release(&pt_lock);
    return -1;  // 빈 슬롯 없음
}

// 현재 프로세스를 디스크의 ELF 프로그램으로 교체한다(exec).
// 페이지 테이블과 스택은 재사용하고 코드 페이지만 갈아끼운다 → satp 전환이
// 없어 스택이 안 바뀌고, 옛 코드 페이지는 회수되어 누수가 없다.
// 성공하면 새 프로그램으로 진입해 돌아오지 않는다. 실패 시 -1.
int proc_exec(const char *path) {
    static uint8 elfbuf[32768];  // 커널 메모리(식별 매핑). ELF는 -g라 다소 큼.

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

    struct proc *p = current_proc();
    char *oldcode = p->ucode;
    vm_free_range(p->pagetable, HEAPBASE, p->heap_top);  // 옛 프로그램 힙 회수
    p->heap_top = HEAPBASE;                          // 새 프로그램은 빈 힙
    if (p->mmap_base) {                              // 옛 mmap 회수
        uint64 mend = p->mmap_base + ((p->mmap_size + PGSIZE - 1) & ~(uint64)(PGSIZE - 1));
        vm_free_range(p->pagetable, p->mmap_base, mend);
        p->mmap_base = 0;
    }
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

// 프로세스의 자원을 회수한다(유저 페이지 + 힙 + 커널 스택 + 페이지 테이블 구조).
void proc_freeimage(struct proc *p) {
    if (p->pagetable) {
        vm_free_range(p->pagetable, HEAPBASE, p->heap_top);  // 지연 할당된 힙 페이지
        if (p->mmap_base) {                                  // mmap 페이지
            uint64 mend = p->mmap_base + ((p->mmap_size + PGSIZE - 1) & ~(uint64)(PGSIZE - 1));
            vm_free_range(p->pagetable, p->mmap_base, mend);
        }
    }
    if (p->ucode)  kfree(p->ucode);
    if (p->ustack) kfree(p->ustack);
    if (p->kstack) kfree(p->kstack);
    if (p->pagetable) free_pagetable(p->pagetable);
    p->ucode = p->ustack = p->kstack = 0;
    p->pagetable = 0;
    p->heap_top = HEAPBASE;
    p->mmap_base = 0;
}

// sbrk: 힙을 n바이트 키운다. 물리 페이지는 할당하지 않는다(지연 할당).
// 이전 heap_top을 반환(새로 얻은 영역의 시작 주소).
uint64 proc_sbrk(int n) {
    struct proc *p = current_proc();
    uint64 old = p->heap_top;
    if (n > 0)
        p->heap_top += (uint64)n;   // 성장만 지원(축소 생략)
    return old;
}

// 파일을 주소공간에 매핑(mmap). 페이지는 폴트 시 파일에서 적재(지연).
// 베이스 VA를 반환, 파일 없으면 -1.
uint64 proc_mmap(const char *path) {
    struct proc *p = current_proc();
    unsigned start, size;
    if (fs_stat(path, &start, &size) != 0)
        return (uint64)-1;
    p->mmap_base = MMAPBASE;
    p->mmap_start = start;
    p->mmap_size = size;
    return MMAPBASE;
}

// 페이지 폴트 처리: 폴트 주소가
//  - 힙 영역이면 → 빈 페이지 할당(demand paging)
//  - mmap 영역이면 → 그 파일 오프셋의 블록을 디스크에서 읽어 채움
// 처리하면 1(명령 재시도), 우리 영역이 아니면 0.
int proc_pagefault(uint64 va, int store) {
    struct proc *p = current_proc();
    if (!p || !p->is_user)
        return 0;
    uint64 a = va & ~(uint64)(PGSIZE - 1);          // 페이지 정렬

    // --- COW: 공유 페이지에 쓰기 폴트 → 복제 ---
    // (이미 매핑된 페이지이므로 아래 demand 분기보다 먼저 처리해야 한다.)
    if (store && uvm_cow_fault(p->pagetable, a)) {
        uart_puts("[cow] copied a shared page on write at va=");
        uart_hex(a); uart_putc('\n');
        return 1;
    }

    if (a >= HEAPBASE && a < p->heap_top) {          // --- 힙: 빈 페이지 ---
        char *mem = kalloc();
        if (!mem) return 0;
        zero(mem, PGSIZE);
        if (uvm_map(p->pagetable, a, (uint64)mem, PTE_R | PTE_W | PTE_U) != 0) {
            kfree(mem);
            return 0;
        }
        uart_puts("[pagefault] demand-allocated a heap page at va=");
        uart_hex(a); uart_putc('\n');
        return 1;
    }

    if (p->mmap_base) {                              // --- mmap: 파일 블록 ---
        uint64 mend = p->mmap_base + ((p->mmap_size + PGSIZE - 1) & ~(uint64)(PGSIZE - 1));
        if (a >= p->mmap_base && a < mend) {
            char *mem = kalloc();
            if (!mem) return 0;
            fs_read_page(p->mmap_start, p->mmap_size,
                         (unsigned)(a - p->mmap_base), (uint8 *)mem);
            if (uvm_map(p->pagetable, a, (uint64)mem, PTE_R | PTE_U) != 0) {  // 읽기 전용
                kfree(mem);
                return 0;
            }
            uart_puts("[pagefault] mmap loaded a file page at va=");
            uart_hex(a); uart_putc('\n');
            return 1;
        }
    }
    return 0;                                         // 우리 영역 아님 → 진짜 폴트
}

// 스케줄러: 각 코어가 무한 루프로 RUNNABLE proc을 골라 실행.
// pt_lock을 잡고 스캔→점유(RUNNING)하고, swtch를 가로질러 락을 들고 들어간다.
// proc은 첫 실행/yield에서 그 락을 놓는다(baton). proc이 양보하면 락을 들고 복귀.
void scheduler(void) {
    int id = r_tp();
    cpu_proc[id] = 0;
    for (;;) {
        intr_on();   // 이 코어가 인터럽트/wakeup을 받을 수 있게
        acquire(&pt_lock);
        for (int i = 0; i < NPROC; i++) {
            struct proc *p = &proctable[i];
            if (p->state == RUNNABLE) {
                p->state = RUNNING;
                cpu_proc[id] = p;
                switch_satp(p->pagetable);
                swtch(&cpu_sched[id], &p->context);  // pt_lock 든 채로 진입(proc이 놓음)
                switch_satp(kernel_pt());
                cpu_proc[id] = 0;                    // 복귀 시 pt_lock 다시 들고 있음
            }
        }
        release(&pt_lock);
    }
}

// 현재 proc이 CPU를 스케줄러에 양보(타이머 선점/자발적). pt_lock을 들고 swtch.
void yield(void) {
    int id = r_tp();
    struct proc *p = cpu_proc[id];
    if (!p)
        return;
    acquire(&pt_lock);
    if (p->state == RUNNING)
        p->state = RUNNABLE;
    swtch(&p->context, &cpu_sched[id]);  // pt_lock 든 채로 스케줄러로(스케줄러가 놓음)
    release(&pt_lock);                   // 다시 스케줄되면 여기서 락 해제
}

// chan에서 잠든다. 호출자가 pt_lock을 쥐고 들어온다(조건검사↔sleep 원자성 보장).
// swtch를 가로질러 락을 든 채로 스케줄러에 넘기고, 깨어나면 다시 든 채로 복귀.
void sleep(void *chan) {
    int id = r_tp();
    struct proc *p = cpu_proc[id];
    if (!p)
        return;
    p->chan = chan;
    p->state = SLEEPING;
    swtch(&p->context, &cpu_sched[id]);  // pt_lock held
    p->chan = 0;                         // 깨어나면(pt_lock held) 여기서 재개
}

// chan에서 자는 모든 프로세스를 RUNNABLE로. 호출자가 pt_lock을 쥐고 있어야 한다.
void wakeup(void *chan) {
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &proctable[i];
        if (p->state == SLEEPING && p->chan == chan)
            p->state = RUNNABLE;
    }
}

// 현재 프로세스를 종료(ZOMBIE)하고 부모를 깨운다. 돌아오지 않는다.
void proc_exit(void) {
    int id = r_tp();
    struct proc *p = cpu_proc[id];
    acquire(&pt_lock);
    if (p->parent)
        wakeup(p->parent);   // wait 중인 부모 깨우기(pt_lock held)
    p->state = ZOMBIE;
    swtch(&p->context, &cpu_sched[id]);  // pt_lock 든 채로(스케줄러가 놓음). 안 돌아옴.
    for (;;) ;
}

// 자식 하나가 종료할 때까지 기다린다. 종료한 자식을 회수하고 그 pid 반환.
int proc_wait(void) {
    int id = r_tp();
    struct proc *p = cpu_proc[id];
    acquire(&pt_lock);
    for (;;) {
        int kids = 0;
        for (int i = 0; i < NPROC; i++) {
            struct proc *q = &proctable[i];
            if (q->parent != p)
                continue;
            kids = 1;
            if (q->state == ZOMBIE) {
                int pid = q->pid;
                proc_freeimage(q);  // 유저 페이지 + 페이지 테이블 회수(pt_lock held)
                q->parent = 0;
                q->state = UNUSED;
                release(&pt_lock);
                return pid;
            }
        }
        if (!kids) {
            release(&pt_lock);
            return -1;        // 자식 없음
        }
        sleep(p);             // pt_lock 든 채로 잠듦(자식 exit가 wakeup). 깨면 락 유지.
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
