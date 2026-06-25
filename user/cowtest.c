// cowtest.c — copy-on-write fork 시연 프로그램(디스크에 적재).
//
// 힙 페이지에 값을 쓴 뒤 fork한다. 부모/자식은 같은 물리 페이지를 "공유"하지만,
// 자식이 그 페이지에 쓰는 순간 커널이 복제(copy-on-write)해 격리된다.
//   기대 출력: child가 42를 보고 99를 쓴 뒤에도 parent는 여전히 42.
//   커널은 그 쓰기 시점에 "[cow] copied a shared page ..."를 찍는다.

#include "usys.h"

static void puts(const char *s) {
    for (; *s; s++)
        sys_putchar(*s);
}
static void put2(int v) {                // 두 자리 수 출력
    sys_putchar('0' + (v / 10) % 10);
    sys_putchar('0' + v % 10);
}

void _start(void) {
    puts("cowtest: heap에 42를 쓰고 fork\n");
    char *p = (char *)sys_sbrk(4096);    // 힙 한 페이지(폴트 시 할당)
    p[0] = 42;                           // 여기서 demand-alloc(쓰기 가능)

    long pid = sys_fork();               // 이 시점에 힙 페이지가 COW 공유된다
    if (pid == 0) {
        puts("  child: 공유된 p[0]="); put2(p[0]);   // 42 (복사 안 함, 공유)
        p[0] = 99;                                    // 쓰기 → COW 복제 발생
        puts(" -> 99 기록 후 p[0]="); put2(p[0]);     // 99 (자식만의 사본)
        sys_putchar('\n');
        sys_exit();
    } else {
        sys_wait();                                   // 자식 종료 대기
        puts("  parent: p[0]="); put2(p[0]);          // 42 (자식 쓰기에 영향 없음)
        puts(" (42여야 격리 성공)\n");
        sys_exit();
    }
}
