// uthread.c — 협조적(cooperative) 유저 스레드 (디스크에 적재되는 프로그램).
//
// 핵심 제약과 해법:
//   우리 유저 프로그램은 코드가 단일 페이지(R|X)로만 매핑돼서, 전역 변수(.data/.bss)에
//   쓸 수 없다. 그래서 스레드 시스템 상태를 "전역"이 아니라 힙의 고정 주소(0x10000)에
//   둔다. (struct sys *)0x10000 은 .data가 아니라 포인터 "상수"라 R|X 페이지에서도 OK.
//   힙은 sbrk로 키우면 R|W로 demand 매핑되므로, 스레드 스택과 컨텍스트를 거기 둔다.
//
// 스레드 전환은 유저 공간의 uswitch(asm)로 한다(커널 swtch와 같은 패턴).
//   스레드는 uyield()로 스케줄러에 양보하고, 스케줄러가 다음 runnable 스레드로 전환한다.

#include "usys.h"

#define HEAPBASE 0x10000UL
#define NTHREAD  3
#define STACKSZ  2048

struct ctx { unsigned long ra, sp, s[12]; };     // ra, sp, s0..s11 = 14개(112바이트)
struct thread { struct ctx ctx; int state; };    // state: 0=종료/미사용, 1=runnable
struct sys {
    struct ctx     sched;                         // 스케줄러 컨텍스트
    struct thread  t[NTHREAD];
    int            current;                        // 현재 스레드 인덱스
    unsigned char  stacks[NTHREAD][STACKSZ];       // 스레드별 스택(힙)
};
#define S ((volatile struct sys *)HEAPBASE)

// 유저 컨텍스트 스위치: old에 callee-saved/ra/sp 저장, new에서 복원, ret.
asm(
".globl uswitch\n"
"uswitch:\n"
"  sd ra,0(a0)\n  sd sp,8(a0)\n"
"  sd s0,16(a0)\n sd s1,24(a0)\n sd s2,32(a0)\n sd s3,40(a0)\n"
"  sd s4,48(a0)\n sd s5,56(a0)\n sd s6,64(a0)\n sd s7,72(a0)\n"
"  sd s8,80(a0)\n sd s9,88(a0)\n sd s10,96(a0)\n sd s11,104(a0)\n"
"  ld ra,0(a1)\n  ld sp,8(a1)\n"
"  ld s0,16(a1)\n ld s1,24(a1)\n ld s2,32(a1)\n ld s3,40(a1)\n"
"  ld s4,48(a1)\n ld s5,56(a1)\n ld s6,64(a1)\n ld s7,72(a1)\n"
"  ld s8,80(a1)\n ld s9,88(a1)\n ld s10,96(a1)\n ld s11,104(a1)\n"
"  ret\n"
);
extern void uswitch(volatile struct ctx *old, volatile struct ctx *neu);

static void puts(const char *s) { for (; *s; s++) sys_putchar(*s); }

// 현재 스레드 → 스케줄러로 양보(나중에 스케줄러가 다시 들어온다).
static void uyield(void) {
    int c = S->current;
    uswitch(&S->t[c].ctx, &S->sched);
}

// 스레드 종료: 상태를 끄고 스케줄러로(다시 돌아오지 않는다).
static void texit(void) {
    int c = S->current;
    S->t[c].state = 0;
    uswitch(&S->t[c].ctx, &S->sched);
}

// 데모 스레드: 세 번 일하며 매번 양보 → 다른 스레드와 번갈아 실행된다.
static void worker(void) {
    int id = S->current;
    for (int i = 0; i < 3; i++) {
        puts("  [thread "); sys_putchar((char)('A' + id)); puts("] step ");
        sys_putchar((char)('1' + i)); sys_putchar('\n');
        uyield();
    }
    puts("  [thread "); sys_putchar((char)('A' + id)); puts("] done\n");
    texit();
}

static void tcreate(int i, void (*fn)(void)) {
    S->t[i].state = 1;
    S->t[i].ctx.ra = (unsigned long)fn;
    S->t[i].ctx.sp = ((unsigned long)&S->stacks[i][STACKSZ]) & ~15UL;  // 16바이트 정렬
    for (int k = 0; k < 12; k++) S->t[i].ctx.s[k] = 0;
}

void _start(void) {
    sys_sbrk(16384);                    // 힙 확보(struct sys + 스택들)
    // 힙 페이지를 미리 touch해 모두 매핑한다. 안 그러면 스레드가 ecall할 때 커널이
    // 트랩 프레임을 힙 스택(sp-256)에 저장하다 미매핑 페이지를 만나 커널 폴트가 난다.
    for (unsigned long a = 0; a < 16384; a += 4096)
        *((volatile unsigned char *)(HEAPBASE + a)) = 0;
    for (int i = 0; i < NTHREAD; i++) S->t[i].state = 0;
    S->current = -1;

    puts("uthread: cooperative user threads (state on heap, no kernel changes)\n");
    for (int i = 0; i < NTHREAD; i++) tcreate(i, worker);

    // 스케줄러: runnable 스레드를 한 바퀴씩 돌아가며 실행(round-robin).
    for (;;) {
        int ran = 0;
        for (int i = 0; i < NTHREAD; i++) {
            if (S->t[i].state == 1) {
                S->current = i;
                uswitch(&S->sched, &S->t[i].ctx);   // 스레드로 진입
                ran = 1;                             // 스레드가 양보/종료하면 여기로
            }
        }
        if (!ran) break;                             // 모두 종료
    }
    puts("uthread: all threads finished\n");
    sys_exit();
}
