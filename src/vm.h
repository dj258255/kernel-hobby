// vm.h — 가상 메모리(Sv39 페이지 테이블)
#ifndef VM_H
#define VM_H

#include "types.h"

typedef uint64  pte_t;
typedef uint64 *pagetable_t;

// PTE 플래그
#define PTE_V (1L << 0)
#define PTE_R (1L << 1)
#define PTE_W (1L << 2)
#define PTE_X (1L << 3)
#define PTE_U (1L << 4)  // U-mode 접근 가능

void kvminit(void);      // 커널 페이지 테이블 생성 + 매핑
void kvminithart(void);  // satp에 커널 페이지 테이블 적재(페이징 ON)
void kvm_map(uint64 va, uint64 pa, uint64 sz, int perm);  // 커널 페이지 테이블에 매핑(유저 페이지 등)

#endif
