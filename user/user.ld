/* user.ld — 유저 프로그램 링커 스크립트.
 * USERVA(0x1000)에 링크하고, 모든 섹션을 하나의 PT_LOAD 세그먼트로 묶는다.
 * (한 페이지에 담아 커널의 단순 ELF 로더 + fork(페이지 복사)와 호환되게)
 */
OUTPUT_ARCH(riscv)
ENTRY(_start)

PHDRS {
    load PT_LOAD;
}

SECTIONS {
    . = 0x1000;
    .text : {
        *(.text._start)   /* _start를 맨 앞에 두어 e_entry == 0x1000 */
        *(.text*)
        *(.rodata*)
        *(.data*)
        *(.bss*)
    } :load

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.riscv.attributes)
    }
}
