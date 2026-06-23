// user.h — 유저모드 + 시스템콜
#ifndef USER_H
#define USER_H

#include "trap.h"  // struct regframe

void user_init(void);              // 유저 프로그램/스택을 페이지 테이블에 매핑
void user_run(void);               // U-mode로 진입 (돌아오지 않음)
void syscall(struct regframe *f);  // ecall 디스패치

#endif
