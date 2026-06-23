# swtch.S — 컨텍스트 스위치
# void swtch(struct context *old, struct context *new)
#   현재 callee-saved 레지스터/ra/sp를 old에 저장하고, new에서 복원한 뒤 ret.
#   ret은 new->ra로 점프하므로, "다른 실행 흐름"으로 갈아탄다. (xv6의 swtch와 동일)

        .globl swtch
swtch:
        sd      ra,   0(a0)
        sd      sp,   8(a0)
        sd      s0,  16(a0)
        sd      s1,  24(a0)
        sd      s2,  32(a0)
        sd      s3,  40(a0)
        sd      s4,  48(a0)
        sd      s5,  56(a0)
        sd      s6,  64(a0)
        sd      s7,  72(a0)
        sd      s8,  80(a0)
        sd      s9,  88(a0)
        sd      s10, 96(a0)
        sd      s11,104(a0)

        ld      ra,   0(a1)
        ld      sp,   8(a1)
        ld      s0,  16(a1)
        ld      s1,  24(a1)
        ld      s2,  32(a1)
        ld      s3,  40(a1)
        ld      s4,  48(a1)
        ld      s5,  56(a1)
        ld      s6,  64(a1)
        ld      s7,  72(a1)
        ld      s8,  80(a1)
        ld      s9,  88(a1)
        ld      s10, 96(a1)
        ld      s11,104(a1)

        ret
