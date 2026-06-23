// elf.c — 아주 작은 ELF64 로더
//
// ELF의 프로그램 헤더(Program Header)를 훑어 PT_LOAD 세그먼트를 적재한다.
// 각 세그먼트는 "파일의 [p_offset, p_offset+p_filesz)를 가상주소 p_vaddr에 두고,
// p_memsz까지 0으로 채우라"는 지시다(.bss 처리). e_entry가 진입점.

#include "elf.h"
#include "types.h"
#include "vm.h"     // USERVA
#include "uart.h"

#define PGSIZE  4096
#define PT_LOAD 1

struct elf64_ehdr {
    uint8  e_ident[16];
    uint16 e_type, e_machine;
    uint32 e_version;
    uint64 e_entry, e_phoff, e_shoff;
    uint32 e_flags;
    uint16 e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
};

struct elf64_phdr {
    uint32 p_type, p_flags;
    uint64 p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
};

static void copyb(char *d, const char *s, uint64 n) {
    while (n-- > 0) *d++ = *s++;
}

int load_elf(const char *img, char *codepage, uint64 *entry) {
    const struct elf64_ehdr *eh = (const struct elf64_ehdr *)img;

    // 매직 넘버 확인: 0x7F 'E' 'L' 'F'
    if (!(eh->e_ident[0] == 0x7f && eh->e_ident[1] == 'E' &&
          eh->e_ident[2] == 'L' && eh->e_ident[3] == 'F')) {
        uart_puts("[elf] bad magic\n");
        return -1;
    }
    *entry = eh->e_entry;

    for (int i = 0; i < eh->e_phnum; i++) {
        const struct elf64_phdr *ph =
            (const struct elf64_phdr *)(img + eh->e_phoff + (uint64)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD)
            continue;
        uint64 off = ph->p_vaddr - USERVA;   // 코드 페이지 내 오프셋
        if (off + ph->p_memsz > PGSIZE) {
            uart_puts("[elf] segment too big (>1 page)\n");
            return -1;
        }
        copyb(codepage + off, img + ph->p_offset, ph->p_filesz);
        // p_memsz > p_filesz 부분(.bss)은 codepage가 미리 0이라 자동 처리
    }
    return 0;
}
