// vm.h — 가상 메모리(Sv39 페이지 테이블)
#ifndef VM_H
#define VM_H

#include "types.h"

typedef uint64  pte_t;
typedef uint64 *pagetable_t;

void kvminit(void);      // 커널 페이지 테이블 생성 + 매핑
void kvminithart(void);  // satp에 커널 페이지 테이블 적재(페이징 ON)

#endif
