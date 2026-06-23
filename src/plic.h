// plic.h — Platform-Level Interrupt Controller (외부 장치 인터럽트 라우팅)
#ifndef PLIC_H
#define PLIC_H

#define UART0_IRQ 10   // QEMU virt에서 UART0의 인터럽트 번호

void plic_init(void);       // 장치 우선순위 설정 (전역 1회)
void plic_init_hart(void);  // 이 hart의 S-context에 인터럽트 enable
int  plic_claim(void);      // 발생한 IRQ 번호를 받아온다
void plic_complete(int irq);// 처리 완료를 PLIC에 알린다

#endif
