# hobby-kernel 학습 로드맵 — "작은 유닉스"로 키우기

지금까지(부팅·VGA·인터럽트·키보드·페이징·힙·async 셸)는 OS의 **골격**이다.
여기서부터는 OS의 **핵심 of 핵심** — 유저/커널 경계, 시스템콜, 프로세스, 파일시스템 — 을 직접 구현한다.

> **참고서**: 막히면 [xv6 (MIT 6.S081)](https://pdos.csail.mit.edu/6.828/2023/xv6.html)의 해당 파일을 본다.
> xv6는 RISC-V라 어셈블리/트랩 디테일은 다르지만 **구조·개념은 그대로 매핑**된다.
> x86_64 구체 구현은 [OSDev Wiki](https://wiki.osdev.org/)를 본다.

현재 코드 기준(GDT에 커널 코드 + TSS(더블폴트 IST)만 있음, 모든 코드가 ring0, async executor는 협력적).

---

## Stage 1 — 유저모드 + 시스템콜 ⭐ (키스톤, 제일 큰 깨달음)

**목표:** ring3(유저모드)에서 도는 코드가 `syscall`로 커널에 "부탁"한다.

세부 단계:
1. **GDT 확장** — 유저 코드(ring3)·유저 데이터 세그먼트 추가. (지금은 커널 코드 + TSS만)
2. **TSS 커널 스택** — `tss.privilege_stack_table[0]`에 커널 스택(RSP0)을 넣는다. 유저모드에서 인터럽트/syscall이 나면 CPU가 이 스택으로 전환한다. (지금 TSS엔 더블폴트용 IST만 설정됨)
3. **syscall 메커니즘** — 둘 중 하나:
   - 쉬운 길: `int 0x80` IDT 엔트리를 **DPL=3**으로 등록(유저가 호출 가능).
   - 현대적 길: `syscall`/`sysret` 명령어 + MSR 설정(`EFER.SCE`, `STAR`, `LSTAR`, `SFMASK`).
   → 처음엔 `int 0x80`로 시작하고 나중에 `syscall`로 바꿔도 된다.
4. **유저 페이지 매핑** — 유저 코드/스택 페이지에 `USER_ACCESSIBLE` 플래그를 줘서 ring3가 접근 가능하게.
5. **ring3 진입** — `iretq`용 프레임(유저 SS, RSP, RFLAGS, 유저 CS, RIP)을 스택에 쌓고 `iretq` → 유저 코드로 점프.
6. **syscall 디스패치** — 핸들러에서 번호로 분기. 우선 `sys_write`(화면 출력) + `sys_exit` 두 개.

**배우는 것:** 권한 분리(ring0 vs ring3)가 왜 OS의 본질인지, syscall이 정확히 뭔지(유저→커널 통제된 진입).
**완료 기준:** ring3 유저 함수가 `sys_write`로 화면에 글자를 찍고 `sys_exit`로 커널에 복귀.
**xv6 참고:** `kernel/trap.c`, `kernel/trampoline.S`, `kernel/syscall.c`, `kernel/sysproc.c`
**OSDev:** "Getting to Ring 3", "System Calls", "SYSENTER"

---

## Stage 2 — 선점형 멀티태스킹 + 프로세스

**목표:** 여러 프로세스가 각자 주소공간을 갖고, 타이머로 **강제 전환**되며 돈다. (지금 async executor는 협력적 = 양보해야 전환)

세부 단계:
1. **프로세스 구조체** — PID, 저장된 레지스터 컨텍스트, 페이지테이블(CR3), 커널 스택, 상태(Ready/Running/Blocked).
2. **컨텍스트 스위치(어셈블리)** — callee-saved 레지스터 저장/복원 + RSP 교체 + CR3 교체.
3. **프로세스별 주소공간** — 각 프로세스가 자기 L4 페이지테이블(커널 매핑은 공유, 유저 매핑은 분리).
4. **선점형 스케줄러** — 타이머 인터럽트 → `schedule()` → 다음 Ready 프로세스로 컨텍스트 스위치. 라운드로빈부터.

**배우는 것:** "동시에 도는 것처럼 보이는" 마법의 진짜 구현, 프로세스 격리.
**완료 기준:** 두 유저 프로세스가 타이머에 의해 번갈아(자발적 양보 없이) 실행.
**xv6 참고:** `kernel/proc.c`(scheduler), `kernel/swtch.S`(context switch), `kernel/proc.h`

---

## Stage 3 — fork / exec + ELF 로더

**목표:** 프로세스가 자식을 낳고(fork), 프로그램을 불러 실행(exec)한다.

세부 단계:
1. **fork** — 부모 주소공간 복제(처음엔 전체 복사, 나중에 Copy-on-Write로 최적화).
2. **ELF 로더** — ELF 헤더 파싱, LOAD 세그먼트를 유저 주소공간에 매핑.
3. **exec** — 새 주소공간에 ELF 로드 + 유저 스택 셋업 + 진입점으로 점프.

**배우는 것:** 유닉스 프로세스 모델의 심장(fork/exec), 프로그램 적재.
**완료 기준:** 커널이 디스크(또는 내장)의 유저 ELF를 exec해서 실행.
**xv6 참고:** `kernel/proc.c`(fork), `kernel/exec.c`

---

## Stage 4 — 파일시스템 (영속성)

**목표:** 디스크에 파일을 저장하고 읽는다.

세부 단계:
1. **디스크 드라이버** — ATA PIO(512B 섹터 읽기/쓰기) 또는 virtio-blk. ATA PIO가 가장 단순.
2. **버퍼 캐시 + 블록 계층**.
3. **파일시스템** — 슈퍼블록·inode·디렉터리 엔트리·블록 할당. 간단한 자작 FS(또는 FAT 읽기).
4. **파일 syscall** — open/read/write/close.

**배우는 것:** 영속성, inode, 블록 할당, 버퍼 캐시.
**완료 기준:** 파일을 만들고 쓰고, 재부팅 후 읽기.
**xv6 참고:** `kernel/fs.c`, `kernel/bio.c`, `kernel/file.c`

---

## Stage 5 — 유저공간 셸 + 기본 프로그램

**목표:** 커널 셸이 아니라 **유저공간 셸**이 fork/exec로 프로그램을 돌린다(파이프·리다이렉션).

세부 단계:
1. **미니 libc** — syscall 래퍼(write/read/fork/exec/exit).
2. **유저 프로그램** — `sh`, `echo`, `cat`, `ls`.
3. **파이프/리다이렉션**.

**배우는 것:** 커널과 유저공간의 협업, 셸의 실제 동작.
**완료 기준:** 유저 셸에서 `ls`, `echo hi > f`, `cat f` 가 동작.
**xv6 참고:** `user/sh.c`, `user/ulib.c`, `user/usys.pl`

---

## 난이도/순서 노트

- **Stage 1이 가장 큰 벽이자 가장 큰 깨달음.** 여기만 넘으면 나머지는 "그 위에 쌓기".
- Stage 1~2는 강하게 결합(유저모드 없이 프로세스 의미 없음). 1→2는 붙여서 간다.
- Stage 4(FS)는 1~3과 비교적 독립적 — 중간에 끼워도 됨.
- **async executor(현재)** 는 커널 내부 협력 태스크용으로 남겨두고, Stage 2의 선점형 프로세스는 그 위/옆에 새로 만든다(둘은 다른 계층).

## 다음 액션

**Stage 1의 1~3단계(GDT 유저 세그먼트 + TSS 커널스택 + int 0x80 syscall)** 부터 시작.
첫 결과물: "ring3 유저 함수가 syscall로 화면에 글자를 찍는다."
