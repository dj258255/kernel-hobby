// shell.c — 커널 셸
// UART 인터럽트가 shell_input(c)로 글자를 하나씩 넘긴다. 줄을 모아 Enter에서 실행.

#include "shell.h"
#include "uart.h"
#include "trap.h"  // clock_ticks()

static char line[128];
static int  len = 0;

// --- 작은 문자열 헬퍼 (libc 없음) ---
static int str_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}
static int starts_with(const char *s, const char *p) {
    while (*p) {
        if (*s != *p) return 0;
        s++; p++;
    }
    return 1;
}

static void prompt(void) { uart_puts("hobby> "); }

static void execute(const char *cmd) {
    if (cmd[0] == '\0') {
        return;
    } else if (str_eq(cmd, "help")) {
        uart_puts("commands: help, about, uptime, clear, whoami, echo <text>\n");
    } else if (str_eq(cmd, "about")) {
        uart_puts("hobby-kernel (C / RISC-V) -- a from-scratch learning kernel\n");
    } else if (str_eq(cmd, "uptime")) {
        uart_puts("uptime: ");
        uart_dec(clock_ticks() / 10);  // 타이머 0.1초 간격 → /10 = 초
        uart_puts(" sec\n");
    } else if (str_eq(cmd, "clear")) {
        uart_puts("\033[2J\033[H");  // ANSI: 화면 지우고 커서 홈으로
    } else if (str_eq(cmd, "whoami")) {
        uart_puts("a kernel hacker building their own OS\n");
    } else if (starts_with(cmd, "echo ")) {
        uart_puts(cmd + 5);
        uart_putc('\n');
    } else {
        uart_puts("unknown command: ");
        uart_puts(cmd);
        uart_puts("  (try 'help')\n");
    }
}

void shell_init(void) {
    uart_puts("\ntype 'help' for commands.\n");
    prompt();
}

void shell_input(char c) {
    if (c == '\r' || c == '\n') {
        uart_putc('\n');
        line[len] = '\0';
        execute(line);
        len = 0;
        prompt();
    } else if (c == 0x7f || c == 0x08) {  // DEL / Backspace
        if (len > 0) {
            len--;
            uart_puts("\b \b");  // 화면에서도 한 글자 지움
        }
    } else if (len < (int)sizeof(line) - 1) {
        line[len++] = c;
        uart_putc(c);  // 입력 에코
    }
}
