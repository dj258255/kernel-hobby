// console.h — 콘솔 입력 (라인 버퍼 + 블로킹 read)
#ifndef CONSOLE_H
#define CONSOLE_H

void console_intr(char c);          // UART 인터럽트가 글자 하나를 넘김
int  console_read(char *dst, int n); // 한 줄을 읽는다(없으면 sleep). 읽은 바이트 수.

#endif
