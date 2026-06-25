// spinlock.c — 스핀락 + 코어별 인터럽트 중첩 관리
//
// 멀티코어에서 여러 하트가 공유 자료구조(프리리스트, proctable 등)를 동시에
// 만지면 깨진다. acquire/release로 한 번에 하나만 들어가게 한다.
//
// 락을 쥔 동안엔 인터럽트를 꺼야 한다 — 안 그러면 같은 코어에서 인터럽트
// 핸들러가 같은 락을 다시 잡으려다 자기 자신과 데드락. push_off/pop_off가
// 중첩을 세서, 가장 바깥 acquire에서 끄고 가장 바깥 release에서 복원한다.

#include "spinlock.h"
#include "csr.h"
#include "types.h"

// 코어별 상태: 인터럽트 끔 중첩 횟수(noff)와, 끄기 전 원래 상태(intena).
struct cpu {
    int noff;
    int intena;
};
static struct cpu cpus[NCPU];

static struct cpu *mycpu(void) { return &cpus[r_tp()]; }

void push_off(void) {
    int old = intr_get();
    intr_off();
    struct cpu *c = mycpu();
    if (c->noff == 0)
        c->intena = old;     // 가장 바깥에서 원래 상태 기억
    c->noff += 1;
}

void pop_off(void) {
    struct cpu *c = mycpu();
    c->noff -= 1;
    if (c->noff == 0 && c->intena)
        intr_on();           // 가장 바깥 release에서 복원
}

void initlock(struct spinlock *lk) { lk->locked = 0; }

void acquire(struct spinlock *lk) {
    push_off();
    // amoswap: locked를 1로 바꾸고 이전 값을 본다. 0이었으면 내가 잡은 것.
    while (__sync_lock_test_and_set(&lk->locked, 1) != 0)
        ;
    __sync_synchronize();    // 임계구역 진입 — 이후 메모리 접근이 앞서가지 않게
}

void release(struct spinlock *lk) {
    __sync_synchronize();    // 임계구역의 쓰기가 락 해제보다 먼저 보이게
    __sync_lock_release(&lk->locked);  // amoswap으로 0
    pop_off();
}
