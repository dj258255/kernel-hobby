// kalloc.c — 물리 페이지 할당기
//
// 커널 끝(end) ~ RAM 끝(PHYSTOP) 사이의 빈 메모리를 4KB 페이지로 쪼개
// free list로 관리한다. Rust 커널의 '힙'에 해당하는 토대. (xv6의 kalloc과 동일)

#include "kalloc.h"
#include "types.h"
#include "spinlock.h"

#define PGSIZE 4096
#define PHYSTOP 0x88000000UL   // QEMU virt 기본 RAM(128MB): 0x8000_0000~0x8800_0000

#define PGROUNDUP(a) (((a) + PGSIZE - 1) & ~((uint64)PGSIZE - 1))

extern char end[];  // 링커가 정의: 커널 이미지의 끝 주소

// free 페이지를 연결 리스트로 잇는다(페이지 첫 8바이트에 next 포인터 저장).
struct run {
    struct run *next;
};

static struct run    *freelist = 0;
static uint64         freecnt = 0;
static struct spinlock kmem_lock;   // 여러 코어가 동시 할당/해제하므로 보호

// 물리 페이지 참조 카운트(COW용). 인덱스 = (pa - RAMBASE) / PGSIZE.
// "이 페이지를 몇 개의 주소공간이 공유하나"를 센다. 0이 될 때만 진짜 반납.
#define RAMBASE 0x80000000UL
#define NPAGES  ((PHYSTOP - RAMBASE) / PGSIZE)
static uint8 refcnt[NPAGES];
static int refidx(void *pa) { return (int)(((uint64)pa - RAMBASE) >> 12); }

void kfree(void *pa) {
    acquire(&kmem_lock);
    int i = refidx(pa);
    if (refcnt[i] > 1) {           // 아직 다른 주소공간이 공유 중 → 반납하지 않음
        refcnt[i]--;
        release(&kmem_lock);
        return;
    }
    refcnt[i] = 0;
    struct run *r = (struct run *)pa;
    r->next = freelist;
    freelist = r;
    freecnt++;
    release(&kmem_lock);
}

// 공유 시작: 이 페이지를 가리키는 주소공간이 하나 늘었다(fork의 COW 공유).
void kref_inc(void *pa) {
    acquire(&kmem_lock);
    refcnt[refidx(pa)]++;
    release(&kmem_lock);
}

void kinit(void) {
    initlock(&kmem_lock);
    uint64 p = PGROUNDUP((uint64)end);
    for (; p + PGSIZE <= PHYSTOP; p += PGSIZE) {
        refcnt[refidx((void *)p)] = 1;   // 1로 두고 kfree(→0, 반납)해 경로 일관화
        kfree((void *)p);
    }
}

void *kalloc(void) {
    acquire(&kmem_lock);
    struct run *r = freelist;
    if (r) {
        freelist = r->next;
        freecnt--;
        refcnt[refidx(r)] = 1;     // 새로 할당된 페이지는 소유자 1명
    }
    release(&kmem_lock);
    return (void *)r;  // 0이면 메모리 부족
}

uint64 kfreecount(void) {
    return freecnt;
}
