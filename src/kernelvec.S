# kernelvec.S — S-mode 트랩 진입점
# 트랩(인터럽트/예외)이 나면 CPU가 stvec(=kernelvec)로 점프한다.
# 트랩 당한 코드의 레지스터를 전부 스택에 저장 → C의 kerneltrap() 호출 →
# 복원 → sret로 원래 자리로 복귀. (xv6의 kernelvec과 같은 패턴)

        .globl kernelvec
        .p2align 2          # stvec(Direct 모드)는 4바이트 정렬 필요
kernelvec:
        addi    sp, sp, -256
        sd      ra,    0(sp)
        sd      gp,    8(sp)
        sd      tp,   16(sp)
        sd      t0,   24(sp)
        sd      t1,   32(sp)
        sd      t2,   40(sp)
        sd      s0,   48(sp)
        sd      s1,   56(sp)
        sd      a0,   64(sp)
        sd      a1,   72(sp)
        sd      a2,   80(sp)
        sd      a3,   88(sp)
        sd      a4,   96(sp)
        sd      a5,  104(sp)
        sd      a6,  112(sp)
        sd      a7,  120(sp)
        sd      s2,  128(sp)
        sd      s3,  136(sp)
        sd      s4,  144(sp)
        sd      s5,  152(sp)
        sd      s6,  160(sp)
        sd      s7,  168(sp)
        sd      s8,  176(sp)
        sd      s9,  184(sp)
        sd      s10, 192(sp)
        sd      s11, 200(sp)
        sd      t3,  208(sp)
        sd      t4,  216(sp)
        sd      t5,  224(sp)
        sd      t6,  232(sp)
        csrr    t0, sepc        # 복귀 PC와 모드 비트도 프레임에 저장
        sd      t0,  240(sp)    #  (트랩마다 독립 → 여러 프로세스 인터리빙 안전)
        csrr    t0, sstatus
        sd      t0,  248(sp)

        csrr    tp, sscratch    # tp = 이 코어의 hartid 복구(유저가 tp를 덮었을 수 있음).
                                # 커널 코드의 r_tp()가 올바른 코어 번호를 보게 한다.

        mv      a0, sp          # 저장된 프레임 포인터를 kerneltrap의 인자로
        call    kerneltrap

# 프레임에서 복원하고 sret하는 공통 경로. forkret(아래)도 여기로 진입한다.
# 진입 시 sp는 복원할 트랩 프레임을 가리켜야 한다.
        .globl  trapret
trapret:
        ld      t0,  240(sp)    # 프레임의 sepc/sstatus를 CSR로 복원(sret 전)
        csrw    sepc, t0
        ld      t0,  248(sp)
        csrw    sstatus, t0

        ld      ra,    0(sp)
        ld      gp,    8(sp)
        ld      tp,   16(sp)
        ld      t0,   24(sp)
        ld      t1,   32(sp)
        ld      t2,   40(sp)
        ld      s0,   48(sp)
        ld      s1,   56(sp)
        ld      a0,   64(sp)
        ld      a1,   72(sp)
        ld      a2,   80(sp)
        ld      a3,   88(sp)
        ld      a4,   96(sp)
        ld      a5,  104(sp)
        ld      a6,  112(sp)
        ld      a7,  120(sp)
        ld      s2,  128(sp)
        ld      s3,  136(sp)
        ld      s4,  144(sp)
        ld      s5,  152(sp)
        ld      s6,  160(sp)
        ld      s7,  168(sp)
        ld      s8,  176(sp)
        ld      s9,  184(sp)
        ld      s10, 192(sp)
        ld      s11, 200(sp)
        ld      t3,  208(sp)
        ld      t4,  216(sp)
        ld      t5,  224(sp)
        ld      t6,  232(sp)
        addi    sp, sp, 256

        sret                # sepc로 복귀 (인터럽트면 원래 자리로 재개)

# trapret_from(uint64 frame) — sp를 프레임 VA로 잡고 공통 복귀 경로로.
# fork된 자식의 진입(C forkret)이 락을 놓은 뒤 호출한다.
        .globl  trapret_from
trapret_from:
        mv      sp, a0
        j       trapret

# userret_to(uint64 entry, uint64 sp, uint64 satp)  [a0, a1, a2]
# exec 전용: satp(주소공간)를 바꾸고 곧장 U-mode로 진입한다.
# satp를 바꾸면 유저 스택이 통째로 바뀌므로, 전환 후엔 스택을 절대 건드리지
# 않고 레지스터만으로 sret한다. (sstatus는 호출 전에 C에서 미리 세팅)
        .globl  userret_to
userret_to:
        csrw    satp, a2
        sfence.vma zero, zero
        csrw    sepc, a0
        mv      sp, a1
        sret
