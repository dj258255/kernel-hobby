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
#define PTE_COW (1L << 8)  // 소프트웨어 예약(RSW) 비트: copy-on-write 공유 페이지

// 유저 프로세스 주소 배치
#define USERVA        0x1000L   // 유저 코드 진입점
#define USERSTACK     0x4000L   // 유저 스택(1페이지)
#define USERSTACKTOP  (USERSTACK + 4096)
#define HEAPBASE      0x10000L   // 힙 시작(sbrk로 위로 성장, 페이지는 폴트 시 할당)
#define MMAPBASE      0x100000L  // mmap 영역 시작(파일을 폴트 시 적재)

void kvminit(void);      // 커널 페이지 테이블 생성 + 매핑
void kvminithart(void);  // satp에 커널 페이지 테이블 적재(페이징 ON)
void kvm_map(uint64 va, uint64 pa, uint64 sz, int perm);  // 커널 페이지 테이블에 매핑

// 프로세스별 페이지 테이블: 커널을 식별 매핑한 새 테이블 + 유저 코드/스택(PTE_U).
pagetable_t proc_pagetable(uint64 ucode_pa, uint64 ustack_pa);
pagetable_t kernel_pt(void);             // 커널 페이지 테이블
void        switch_satp(pagetable_t pt); // satp 전환 + TLB flush
uint64      satp_for(pagetable_t pt);    // 페이지 테이블의 satp 값(Sv39)
void        remap_user_code(pagetable_t pt, uint64 newcode_pa);  // exec: 코드 페이지 교체
void        free_pagetable(pagetable_t pt);  // 페이지 테이블 구조 회수
int         uvm_map(pagetable_t pt, uint64 va, uint64 pa, int perm);  // 유저 1페이지 매핑
void        vm_free_range(pagetable_t pt, uint64 start, uint64 end);  // VA 범위의 매핑 페이지 해제
// COW: 부모의 [start,end) 매핑 페이지를 자식과 읽기전용 공유로 잇는다(쓰기가능 페이지는 COW 표시).
int         uvm_cow_share(pagetable_t parent, pagetable_t child, uint64 start, uint64 end);
// COW 쓰기 폴트 처리: va가 COW 페이지면 복제해 쓰기가능으로 만든다(1=처리, 0=COW 아님).
int         uvm_cow_fault(pagetable_t pt, uint64 va);

#endif
