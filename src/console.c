// console.c — 콘솔 입력: 라인 버퍼 + 블로킹 read
//
// UART 인터럽트(console_intr)가 글자를 버퍼에 쌓고 에코한다. 개행이 오면
// wakeup으로 read를 깨운다. console_read는 입력이 없으면 sleep으로 블록된다.

#include "console.h"
#include "uart.h"
#include "proc.h"
#include "types.h"

#define INBUF 128
static char   inbuf[INBUF];
static uint64 in_w;   // 인터럽트가 쓴 위치(단조 증가)
static uint64 in_r;   // read가 소비한 위치

// 트랩 핸들러에서 호출. 에코하고 버퍼에 넣고, 개행이면 깨운다.
// 버퍼/wakeup은 pt_lock으로 보호(read와 경쟁, 멀티코어 대비). 잃어버린 wakeup 방지.
void console_intr(char c) {
    if (c == '\r')
        c = '\n';
    acquire(&pt_lock);
    if (c == 0x7f || c == 0x08) {  // DEL / Backspace
        if (in_w > in_r && inbuf[(in_w - 1) % INBUF] != '\n') {
            in_w--;
            uart_puts("\b \b");
        }
        release(&pt_lock);
        return;
    }
    if (in_w - in_r >= INBUF) {     // 버퍼 가득 → 버림
        release(&pt_lock);
        return;
    }
    inbuf[in_w % INBUF] = c;
    in_w++;
    uart_putc(c);                   // 입력 에코(개행 포함)
    if (c == '\n')
        wakeup(&inbuf);             // 줄 완성 → read를 깨움
    release(&pt_lock);
}

// 한 줄(개행까지)을 dst로. 입력이 없으면 sleep으로 블록. 읽은 바이트 수 반환.
// pt_lock을 잡고 검사·sleep을 원자적으로(잃어버린 wakeup 방지). sleep은 락을 든
// 채로 swtch하고, 깨어나면 다시 든 채로 복귀한다.
int console_read(char *dst, int n) {
    int i = 0;
    acquire(&pt_lock);
    while (i < n) {
        while (in_r == in_w)
            sleep(&inbuf);          // 입력 없으면 잠든다(pt_lock held; 인터럽트가 깨움)
        char c = inbuf[in_r % INBUF];
        in_r++;
        dst[i++] = c;
        if (c == '\n')
            break;                  // 줄 끝
    }
    release(&pt_lock);
    return i;
}
