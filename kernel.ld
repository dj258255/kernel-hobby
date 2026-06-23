/* kernel.ld — ckernel 링커 스크립트
 * OpenSBI가 S-mode 페이로드를 0x8020_0000으로 점프시키므로 거기에 링크한다.
 */
OUTPUT_ARCH("riscv")
ENTRY(_entry)

SECTIONS
{
    . = 0x80200000;

    .text : {
        *(.text.entry)      /* _entry가 맨 앞에 오도록 */
        *(.text .text.*)
    }

    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) }

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }

    PROVIDE(end = .);
}
