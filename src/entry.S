# entry.S — 커널 진입점 (S-mode)
# OpenSBI(QEMU 기본 펌웨어)가 M-mode 셋업을 끝낸 뒤 이 _entry로 점프한다.
# 진입 시: a0 = hartid, a1 = device tree(dtb) 포인터.
# 할 일: 스택 포인터를 잡고 C의 kmain()으로 넘어간다.

        .section .text.entry
        .globl _entry
_entry:
        la      sp, stack_top      # 스택 설정 (RISC-V 스택은 높은 주소→낮은 주소)
        call    kmain              # C 커널 시작
1:      wfi                        # kmain이 돌아오면 영원히 대기
        j       1b

        # 부팅용 커널 스택 (4KB)
        .section .bss
        .align 16
        .globl stack_bottom
stack_bottom:
        .skip 4096
        .globl stack_top
stack_top:
