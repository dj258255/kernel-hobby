// kalloc.h — 물리 페이지 할당기 (4KB 단위)
#ifndef KALLOC_H
#define KALLOC_H

#include "types.h"

void   kinit(void);        // 빈 물리 메모리를 free list에 등록
void  *kalloc(void);       // 4KB 페이지 1개 할당 (없으면 0)
void   kfree(void *pa);    // 페이지 반납
uint64 kfreecount(void);   // 남은 빈 페이지 수

#endif
