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

void kfree(void *pa) {
    acquire(&kmem_lock);
    struct run *r = (struct run *)pa;
    r->next = freelist;
    freelist = r;
    freecnt++;
    release(&kmem_lock);
}

void kinit(void) {
    initlock(&kmem_lock);
    uint64 p = PGROUNDUP((uint64)end);
    for (; p + PGSIZE <= PHYSTOP; p += PGSIZE)
        kfree((void *)p);
}

void *kalloc(void) {
    acquire(&kmem_lock);
    struct run *r = freelist;
    if (r) {
        freelist = r->next;
        freecnt--;
    }
    release(&kmem_lock);
    return (void *)r;  // 0이면 메모리 부족
}

uint64 kfreecount(void) {
    return freecnt;
}
