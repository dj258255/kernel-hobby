// init.c — 따로 컴파일되어 ELF로 커널에 임베드되는 첫 유저 프로그램.
// 커널이 ELF를 파싱해 USERVA(0x1000)에 적재하고 _start로 진입한다.
//
// 동작: 자신을 fork → 부모/자식이 각자 인사 → 틱을 돌며 잠깐 일함 → 종료.
// (전역 변수를 쓰지 않아 .data/.bss가 없고, 한 페이지에 담겨 fork와 호환된다)

#include "usys.h"

void _start(void) {
    long pid = sys_fork();   // 자신을 복제(부모=자식pid, 자식=0)
    sys_print(pid);          // pid를 선택자로: 부모/자식 메시지

    for (int i = 0; i < 25; i++) {
        sys_tick();          // 커널 측 카운터 증가(ps에서 보임)
        for (volatile long d = 0; d < 0x2000000; d++)
            ;                // 바쁜 대기: 틱 간격 벌리기
    }

    sys_exit();              // 종료(돌아오지 않음)
    for (;;)
        ;                    // 안전망
}
