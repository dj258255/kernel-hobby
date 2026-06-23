// trap.c — 트랩 처리 (타이머 + 외부(UART) 인터럽트 + 예외)

#include "trap.h"
#include "types.h"
#include "csr.h"
#include "uart.h"
#include "plic.h"

extern void kernelvec(void);  // kernelvec.S

// QEMU virt 타이머는 10MHz → 1,000,000 = 0.1초.
#define TIMER_INTERVAL 1000000UL
#define SCAUSE_S_EXTERNAL 9   // 인터럽트 코드 9 = S-mode 외부 인터럽트

static uint64 ticks = 0;

void trap_init(void) {
    w_stvec((uint64)kernelvec);              // 트랩 벡터 등록 (Direct 모드)
    w_sie(r_sie() | SIE_STIE | SIE_SEIE);    // 타이머 + 외부 인터럽트 enable
}

void timer_init(void) {
    w_stimecmp(r_time() + TIMER_INTERVAL);   // 첫 타이머 예약 (sstc)
}

void interrupts_on(void) {
    intr_on();  // sstatus.SIE = 1
}

uint64 clock_ticks(void) {
    return ticks;
}

// kernelvec.S가 레지스터를 저장한 뒤 호출한다.
void kerneltrap(void) {
    uint64 cause = r_scause();

    if (cause & SCAUSE_INTERRUPT) {
        uint64 code = cause & 0xff;
        if (code == SCAUSE_S_TIMER) {
            // 타이머: 조용히 틱만 센다(uptime 명령이 읽음). 다음 타이머 예약.
            ticks++;
            w_stimecmp(r_time() + TIMER_INTERVAL);
        } else if (code == SCAUSE_S_EXTERNAL) {
            // 외부 장치 인터럽트: PLIC에서 누구인지 받아 처리.
            int irq = plic_claim();
            if (irq == UART0_IRQ)
                uart_intr();
            if (irq)
                plic_complete(irq);
        } else {
            uart_puts("[trap] unknown interrupt, scause=");
            uart_hex(cause);
            uart_putc('\n');
        }
    } else {
        // 예외 — 정보를 찍고 멈춘다(디버깅용 안전망)
        uart_puts("[trap] EXCEPTION  scause=");
        uart_hex(cause);
        uart_puts("  sepc=");
        uart_hex(r_sepc());
        uart_puts("  stval=");
        uart_hex(r_stval());
        uart_putc('\n');
        for (;;)
            ;
    }
}
