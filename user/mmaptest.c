// mmaptest.c — mmap 시연(디스크에 적재).
// 파일을 주소공간에 매핑하고, read() 없이 메모리 접근만으로 내용을 읽는다.
// 각 페이지 접근이 폴트를 내고, 커널이 그 파일 블록을 디스크에서 적재한다.

#include "usys.h"

static void puts(const char *s) {
    for (; *s; s++)
        sys_putchar(*s);
}

void _start(void) {
    puts("mmaptest: mmap(\"motd.txt\")\n");
    char *p = (char *)sys_mmap("motd.txt");
    if ((long)p < 0) {
        puts("mmap failed\n");
        sys_exit();
    }
    puts("reading the file as memory (no read() -- page faults load it):\n");
    for (int i = 0; i < 4096 && p[i]; i++)   // 0 바이트(파일 끝)까지 출력
        sys_putchar(p[i]);
    sys_exit();
}
