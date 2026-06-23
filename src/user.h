// user.h — 유저모드 + 시스템콜
#ifndef USER_H
#define USER_H

#include "trap.h"  // struct regframe

void syscall(struct regframe *f);  // ecall 디스패치 (유저 프로그램은 user/init.c)

#endif
