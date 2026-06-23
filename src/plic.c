// plic.c — QEMU virt PLIC (base 0x0c00_0000)
// UART 같은 외부 장치의 인터럽트를 hart의 S-mode 외부 인터럽트로 전달한다.
// (xv6의 plic.c와 같은 레지스터 레이아웃)

#include "plic.h"
#include "types.h"

#define PLIC 0x0c000000L
#define PLIC_PRIORITY      (PLIC + 0x0)
#define PLIC_SENABLE(h)    (PLIC + 0x2080 + (h) * 0x100)
#define PLIC_STHRESHOLD(h) (PLIC + 0x201000 + (h) * 0x2000)
#define PLIC_SCLAIM(h)     (PLIC + 0x201004 + (h) * 0x2000)

#define REG(addr) (*(volatile uint32 *)(addr))

void plic_init(void) {
    // UART0 인터럽트의 우선순위를 0이 아닌 값으로 (0 = 비활성)
    REG(PLIC_PRIORITY + UART0_IRQ * 4) = 1;
}

void plic_init_hart(void) {
    int hart = 0;  // 단일 hart
    // 이 hart의 S-context에서 UART0 인터럽트를 enable
    REG(PLIC_SENABLE(hart)) = (1u << UART0_IRQ);
    // threshold 0 = 모든 우선순위 인터럽트 수락
    REG(PLIC_STHRESHOLD(hart)) = 0;
}

int plic_claim(void) {
    return (int)REG(PLIC_SCLAIM(0));
}

void plic_complete(int irq) {
    REG(PLIC_SCLAIM(0)) = (uint32)irq;
}
