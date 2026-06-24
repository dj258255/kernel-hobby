// trap.c — 트랩 처리 (타이머 + 외부(UART) 인터럽트 + 예외)

#include "trap.h"
#include "types.h"
#include "csr.h"
#include "uart.h"
#include "plic.h"
#include "user.h"
#include "proc.h"

extern void kernelvec(void);  // kernelvec.S

// QEMU virt 타이머는 10MHz → 1,000,000 = 0.1초.
#define TIMER_INTERVAL 1000000UL
#define SCAUSE_S_EXTERNAL 9   // 인터럽트 코드 9 = S-mode 외부 인터럽트
#define SCAUSE_U_ECALL    8   // 예외 코드 8 = U-mode에서의 ecall(시스템콜)
#define SCAUSE_LOAD_FAULT 13  // 예외 13 = 로드 페이지 폴트
#define SCAUSE_STORE_FAULT 15 // 예외 15 = 스토어/AMO 페이지 폴트

static uint64 ticks = 0;

void trap_init(void) {
    w_stvec((uint64)kernelvec);              // 트랩 벡터 등록 (Direct 모드)
    w_sie(r_sie() | SIE_STIE | SIE_SEIE);    // 타이머 + 외부 인터럽트 enable
    w_sstatus(r_sstatus() | SSTATUS_SUM);    // SUM=1 고정: 커널이 항상 유저 페이지 접근 가능
                                             // (유저 프로세스 트랩을 유저 스택에서 처리하므로 필수)
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
void kerneltrap(struct regframe *f) {
    uint64 cause = r_scause();

    if (cause & SCAUSE_INTERRUPT) {
        uint64 code = cause & 0xff;
        if (code == SCAUSE_S_TIMER) {
            // 타이머: 틱 카운트 + 다음 타이머 예약 + 현재 스레드 선점.
            ticks++;
            w_stimecmp(r_time() + TIMER_INTERVAL);
            if (current_proc())
                yield();  // 실행 중인 스레드를 스케줄러에 양보(선점)
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
    } else if (cause == SCAUSE_U_ECALL) {
        // 유저 프로그램이 ecall로 커널에 '부탁'한 것 = 시스템콜.
        syscall(f);
        f->sepc += 4;  // ecall(4바이트) 다음 명령으로 복귀(프레임의 sepc 수정)
    } else if ((cause == SCAUSE_LOAD_FAULT || cause == SCAUSE_STORE_FAULT) &&
               proc_pagefault(r_stval(), cause == SCAUSE_STORE_FAULT)) {
        // 힙 페이지 폴트를 지연 할당으로 처리. sepc는 그대로 → 명령 재시도.
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
