// hello.c — 디스크에 올라가는 별도 유저 프로그램.
// init이 fork한 자식이 exec("hello")로 이 프로그램이 된다.
// 전역 변수 없이(.bss 없음) 한 페이지에 담기게 작성.

#include "usys.h"

void _start(void) {
    const char *msg = "  [hello] I am a separate program, exec'd from disk!\n";
    for (const char *p = msg; *p; p++)
        sys_putchar(*p);     // 한 글자씩 시스템콜로 출력
    sys_exit();
}
