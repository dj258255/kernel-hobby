// lazytest.c — demand paging 시연 프로그램(디스크에 적재).
// sbrk로 힙을 키워도 페이지는 안 생기고, 처음 건드릴 때 폴트로 할당된다.

#include "usys.h"

static void puts(const char *s) {
    for (; *s; s++)
        sys_putchar(*s);
}

void _start(void) {
    puts("lazytest: sbrk(8192) -- grow heap by 2 pages\n");
    char *p = (char *)sys_sbrk(8192);   // 힙만 늘림(물리 페이지 0장)

    puts("touching page 0 and page 1 (each triggers a page fault)...\n");
    p[0]    = 42;     // 첫 페이지 첫 바이트 → 스토어 폴트 → 할당
    p[4096] = 99;     // 둘째 페이지 → 또 폴트 → 할당

    puts("read back: ");
    sys_putchar((p[0] == 42 && p[4096] == 99) ? 'O' : 'X');
    sys_putchar('K');
    sys_putchar('\n');
    sys_exit();
}
