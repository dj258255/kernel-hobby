// spinlock.h — 멀티코어 상호배제(스핀락)
#ifndef SPINLOCK_H
#define SPINLOCK_H

#define NCPU 4   // 최대 코어 수

struct spinlock {
    unsigned int locked;   // 0=풀림, 1=잠김
};

void initlock(struct spinlock *lk);
void acquire(struct spinlock *lk);  // 잠길 때까지 스핀 (인터럽트 끔)
void release(struct spinlock *lk);
void push_off(void);   // 인터럽트 끄기(중첩 카운트)
void pop_off(void);    // 인터럽트 복원

#endif
