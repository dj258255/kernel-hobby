// init.c — 따로 컴파일되어 ELF로 커널에 임베드되는 첫 유저 프로그램.
// 커널이 ELF를 파싱해 USERVA(0x1000)에 적재하고 _start로 진입한다.
//
// 동작: 자신을 fork → 부모/자식이 각자 인사 → 틱을 돌며 잠깐 일함 → 종료.
// (전역 변수를 쓰지 않아 .data/.bss가 없고, 한 페이지에 담겨 fork와 호환된다)

#include "usys.h"

void _start(void) {
    long pid = sys_fork();   // 자신을 복제(부모=자식pid, 자식=0)

    if (pid == 0) {
        // 자식: 디스크의 hello 프로그램으로 자신을 교체(fork + exec 패턴)
        sys_exec("hello");
        sys_print(0);        // exec 실패 시에만 도달
        sys_exit();
    }

    // 부모: 인사 후 틱을 돌다 종료
    sys_print(pid);
    for (int i = 0; i < 25; i++) {
        sys_tick();
        for (volatile long d = 0; d < 0x2000000; d++)
            ;                // 바쁜 대기: 틱 간격 벌리기
    }

    sys_exit();
    for (;;)
        ;                    // 안전망
}
