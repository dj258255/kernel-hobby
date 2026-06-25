# entry.S — 커널 진입점 (S-mode), 멀티코어(하트) 대응
# OpenSBI가 M-mode 셋업 후 하트 0을 여기로 점프시킨다(a0=hartid, a1=dtb).
# 보조 하트는 하트 0이 SBI HSM(hart_start)으로 깨우며, 역시 여기로 a0=hartid로 들어온다.
# 할 일: tp에 hartid를 담고, 코어별 스택을 잡고, 하트 0이면 kmain, 아니면 hart_main으로.

        .section .text.entry
        .globl _entry
_entry:
        mv      tp, a0             # tp = hartid (SBI가 a0로 전달)
        # 코어별 스택: sp = stacks + (hartid + 1) * 4096   (스택 top)
        la      sp, stacks
        li      t0, 4096
        addi    t1, a0, 1
        mul     t0, t0, t1
        add     sp, sp, t0
        bnez    a0, secondary      # hartid != 0 → 보조 하트
        call    kmain              # 하트 0: 전체 초기화 (돌아오지 않음)
1:      wfi
        j       1b
secondary:
        call    hart_main          # 보조 하트
2:      wfi
        j       2b

        # 코어별 부팅 스택 (NCPU=4 × 4KB)
        .section .bss
        .align 16
        .globl stacks
stacks:
        .skip 4096 * 4
