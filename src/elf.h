// elf.h — 아주 작은 ELF64 로더
#ifndef ELF_H
#define ELF_H

#include "types.h"

// ELF 이미지(img)의 PT_LOAD 세그먼트를 코드 페이지(codepage)에 적재한다.
// 진입점을 *entry에 담고, 성공 시 0. (한 페이지에 담기는 프로그램 가정)
int load_elf(const char *img, char *codepage, uint64 *entry);

#endif
