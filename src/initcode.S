# initcode.S — 따로 컴파일된 첫 유저 프로그램(ELF)을 커널 이미지에 임베드.
# 커널은 initcode/initcode_end 사이의 바이트를 ELF로 파싱해 적재한다.

        .section .rodata
        .globl initcode
        .globl initcode_end
initcode:
        .incbin "build/user_init.elf"
initcode_end:
