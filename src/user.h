// user.h — 유저모드 + 시스템콜
#ifndef USER_H
#define USER_H

#include "trap.h"  // struct regframe

void user_program(void);           // 유저 프로그램(naked) — 코드 페이지로 복사됨
void syscall(struct regframe *f);  // ecall 디스패치

#endif
