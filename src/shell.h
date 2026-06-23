// shell.h — 아주 단순한 커널 셸
#ifndef SHELL_H
#define SHELL_H

void shell_init(void);        // 환영 메시지 + 프롬프트
void shell_input(char c);     // UART 인터럽트가 글자 하나씩 넣어준다

#endif
