// trap.c — 트랩 처리 (타이머 인터럽트 + 예외)

#include "trap.h"
#include "types.h"
#include "csr.h"
#include "uart.h"

extern void kernelvec(void);  // kernelvec.S

// 타이머 주기. QEMU virt의 타이머는 10MHz이므로 1,000,000 = 0.1초.
#define TIMER_INTERVAL 1000000UL

static uint64 ticks = 0;

void trap_init(void) {
    w_stvec((uint64)kernelvec);  // 트랩 벡터 등록 (Direct 모드)
}

void timer_init(void) {
    w_stimecmp(r_time() + TIMER_INTERVAL);  // 첫 타이머 예약 (sstc)
    w_sie(r_sie() | SIE_STIE);              // S-mode 타이머 인터럽트 enable
    intr_on();                              // 글로벌 인터럽트 enable
}

// kernelvec.S가 레지스터를 저장한 뒤 호출한다.
void kerneltrap(void) {
    uint64 cause = r_scause();

    if (cause & SCAUSE_INTERRUPT) {
        // 인터럽트
        uint64 code = cause & 0xff;
        if (code == SCAUSE_S_TIMER) {
            ticks++;
            // 0.1초 간격이라 10틱마다 = 약 1초. 콘솔 안 넘치게 1초에 한 번만.
            if (ticks % 10 == 0) {
                uart_puts("[timer] ");
                uart_dec(ticks / 10);
                uart_puts(" sec\n");
            }
            w_stimecmp(r_time() + TIMER_INTERVAL);  // 다음 타이머 예약
        } else {
            uart_puts("[trap] unknown interrupt, scause=");
            uart_hex(cause);
            uart_putc('\n');
        }
    } else {
        // 예외 — 정보를 찍고 멈춘다 (디버깅용 안전망)
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
