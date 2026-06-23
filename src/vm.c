// vm.c — Sv39 가상 메모리
//
// Sv39: 39비트 가상주소 → 3단계 페이지 테이블(각 4KB, 512 PTE).
// 커널은 식별 매핑(va == pa)으로 매핑해, 페이징을 켜도 주소가 그대로다.
// (xv6의 vm.c를 우리 메모리 배치에 맞춰 단순화)

#include "vm.h"
#include "types.h"
#include "csr.h"
#include "kalloc.h"
#include "uart.h"

#define PGSIZE  4096
#define PGSHIFT 12
// PTE 플래그(PTE_V/R/W/X/U)는 vm.h에 정의됨

#define PA2PTE(pa)  ((((uint64)(pa)) >> 12) << 10)
#define PTE2PA(pte) (((pte) >> 10) << 12)

#define PXMASK 0x1FF
#define PXSHIFT(level) (PGSHIFT + (9 * (level)))
#define PX(level, va)  ((((uint64)(va)) >> PXSHIFT(level)) & PXMASK)

#define PGROUNDDOWN(a) (((a)) & ~((uint64)PGSIZE - 1))
#define MAKE_SATP(pt)  ((8L << 60) | (((uint64)(pt)) >> 12))  // 8 = Sv39

// 장치/메모리 주소
#define UART0    0x10000000L
#define PLIC     0x0c000000L
#define KERNBASE 0x80200000L   // 우리 커널이 적재되는 곳
#define PHYSTOP  0x88000000L   // RAM 끝(128MB)

extern char etext[];  // 링커: 커널 텍스트 끝(페이지 정렬)

static pagetable_t kernel_pagetable;

static void *memset(void *dst, int c, uint64 n) {
    char *d = dst;
    while (n-- > 0) *d++ = (char)c;
    return dst;
}

// va에 해당하는 최하위 PTE의 주소를 반환. alloc!=0이면 중간 테이블을 할당.
static pte_t *walk(pagetable_t pagetable, uint64 va, int alloc) {
    for (int level = 2; level > 0; level--) {
        pte_t *pte = &pagetable[PX(level, va)];
        if (*pte & PTE_V) {
            pagetable = (pagetable_t)PTE2PA(*pte);
        } else {
            if (!alloc)
                return 0;
            pagetable = (pagetable_t)kalloc();
            if (pagetable == 0)
                return 0;
            memset(pagetable, 0, PGSIZE);
            *pte = PA2PTE(pagetable) | PTE_V;
        }
    }
    return &pagetable[PX(0, va)];
}

// [va, va+size) → [pa, ...) 를 perm 권한으로 매핑.
static int mappages(pagetable_t pt, uint64 va, uint64 size, uint64 pa, int perm) {
    uint64 a = PGROUNDDOWN(va);
    uint64 last = PGROUNDDOWN(va + size - 1);
    for (;;) {
        pte_t *pte = walk(pt, a, 1);
        if (pte == 0)
            return -1;
        if (*pte & PTE_V) {
            uart_puts("[vm] remap!\n");
            return -1;
        }
        *pte = PA2PTE(pa) | perm | PTE_V;
        if (a == last)
            break;
        a += PGSIZE;
        pa += PGSIZE;
    }
    return 0;
}

static void kvmmap(pagetable_t kpt, uint64 va, uint64 pa, uint64 sz, int perm) {
    if (mappages(kpt, va, sz, pa, perm) != 0) {
        uart_puts("[vm] kvmmap failed\n");
        for (;;) ;
    }
}

// 커널 영역(UART/PLIC/텍스트/데이터)을 식별 매핑한다.
// 커널 페이지 테이블과 모든 프로세스 페이지 테이블이 똑같이 이 매핑을 갖는다.
static void map_kernel(pagetable_t pt) {
    kvmmap(pt, UART0, UART0, PGSIZE, PTE_R | PTE_W);              // UART
    kvmmap(pt, 0x10001000L, 0x10001000L, PGSIZE, PTE_R | PTE_W);  // virtio-blk MMIO
    kvmmap(pt, PLIC, PLIC, 0x400000, PTE_R | PTE_W);             // PLIC
    kvmmap(pt, KERNBASE, KERNBASE,                               // 커널 텍스트 R/X
           (uint64)etext - KERNBASE, PTE_R | PTE_X);
    kvmmap(pt, (uint64)etext, (uint64)etext,                     // 데이터+나머지 RAM R/W
           PHYSTOP - (uint64)etext, PTE_R | PTE_W);
}

void kvminit(void) {
    kernel_pagetable = (pagetable_t)kalloc();
    memset(kernel_pagetable, 0, PGSIZE);
    map_kernel(kernel_pagetable);
}

pagetable_t kernel_pt(void) { return kernel_pagetable; }

// satp를 pt로 전환하고 TLB를 비운다(스케줄러가 프로세스 전환 시 호출).
void switch_satp(pagetable_t pt) {
    w_satp(MAKE_SATP(pt));
    sfence_vma();
}

// 프로세스별 페이지 테이블: 커널 식별 매핑 + 유저 코드(U|R|X) + 유저 스택(U|R|W).
pagetable_t proc_pagetable(uint64 ucode_pa, uint64 ustack_pa) {
    pagetable_t pt = (pagetable_t)kalloc();
    if (pt == 0)
        return 0;
    memset(pt, 0, PGSIZE);
    map_kernel(pt);
    mappages(pt, USERVA, PGSIZE, ucode_pa, PTE_R | PTE_X | PTE_U);
    mappages(pt, USERSTACK, PGSIZE, ustack_pa, PTE_R | PTE_W | PTE_U);
    return pt;
}

void kvminithart(void) {
    sfence_vma();
    w_satp(MAKE_SATP(kernel_pagetable));  // 페이징 ON
    sfence_vma();
}

// 커널 페이지 테이블에 매핑을 추가한다(유저 코드/스택 페이지 등).
void kvm_map(uint64 va, uint64 pa, uint64 sz, int perm) {
    kvmmap(kernel_pagetable, va, pa, sz, perm);
    sfence_vma();  // 활성 페이지 테이블을 바꿨으니 TLB flush
}
