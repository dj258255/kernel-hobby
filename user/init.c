// init.c — 유저공간 셸. 커널에 임베드되어 첫 유저 프로세스로 실행된다.
//
// 루프: 프롬프트 출력 → 한 줄 읽기 → 명령 처리.
//   내장: ls / cat <file> / help
//   그 외: fork + exec(디스크의 프로그램) + wait  ← 유닉스가 명령을 실행하는 방식

#include "usys.h"

static void puts(const char *s) {
    for (; *s; s++)
        sys_putchar(*s);
}

static int streq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

static int startswith(const char *s, const char *p) {
    while (*p) {
        if (*s != *p) return 0;
        s++; p++;
    }
    return 1;
}

void _start(void) {
    char line[128];

    puts("\nhobby-kernel userspace shell. try: ls, cat motd.txt, hello\n");

    for (;;) {
        puts("$ ");
        long n = sys_read(line, sizeof(line) - 1);
        if (n <= 0)
            continue;
        if (line[n - 1] == '\n')
            line[n - 1] = 0;   // 개행 제거
        else
            line[n] = 0;
        if (line[0] == 0)
            continue;

        // 내장 명령
        if (streq(line, "help")) {
            puts("builtins: ls, cat <file>, write <file> <text>, mem, help.  others = disk programs.\n");
            continue;
        }
        if (streq(line, "ls")) {
            sys_ls();
            continue;
        }
        if (streq(line, "mem")) {
            sys_mem();
            continue;
        }
        if (startswith(line, "cat ")) {
            sys_cat(line + 4);
            continue;
        }
        if (startswith(line, "write ")) {
            // write <name> <text...>  → 파일 생성
            char *arg = line + 6;
            char *sp = arg;
            while (*sp && *sp != ' ') sp++;
            if (*sp == ' ') { *sp = 0; sp++; } else { sp = arg + 0; while (*sp) sp++; }
            if (sys_create(arg, sp) != 0)
                puts("write: failed (exists? full?)\n");
            continue;
        }

        // 외부 명령: 자식을 fork해서 디스크 프로그램으로 exec, 끝날 때까지 wait
        long pid = sys_fork();
        if (pid == 0) {
            sys_exec(line);            // 성공하면 돌아오지 않음
            puts(line);
            puts(": command not found\n");
            sys_exit();
        }
        sys_wait();                    // 자식이 끝날 때까지 대기
    }
}
