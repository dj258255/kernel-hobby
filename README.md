# hobby-kernel

**한국어** | [English](README.en.md)

![hobby-kernel 유저공간 셸 데모](docs/demo.svg)

C로 바닥부터 만드는 **RISC-V 학습용 커널**. OS 내부(유저/커널 경계·시스템콜·프로세스·파일시스템)를 직접 구현하며 공부하는 게 목표. 참고서로 [xv6(MIT 6.S081)](https://pdos.csail.mit.edu/6.828/2023/xv6.html)를 본다.

## 빌드 & 실행

```bash
# 사전: riscv64 크로스 컴파일러 + QEMU
brew install riscv64-elf-gcc qemu

make          # build/kernel.elf
make run      # QEMU virt + OpenSBI로 부팅 (UART → stdout). 종료: Ctrl-A 다음 X
```

## 현재 상태

- **Step 0**: S-mode 부팅 + NS16550 UART 출력
- **Stage 1**: 트랩/인터럽트 + 타이머 (stvec, sstc)
- **키보드 입력**: PLIC 외부 인터럽트 + UART RX
- **커널 셸**: help / about / uptime / mem / clear / whoami / echo
- **페이지 할당기**: kalloc/kfree (물리 메모리 ~125MB)
- **Stage 2**: 페이징(Sv39 3단계 페이지 테이블, 커널 식별 매핑)
- **Stage 3**: 유저모드 + 시스템콜 (U-mode 진입, `ecall` → putchar/print 디스패치)
- **Stage 4**: 프로세스 + 선점형 스케줄러 (swtch.S 컨텍스트 스위치, 타이머 선점, 셸 복귀 + `ps`)
- **Stage 5**: 유저 프로세스 (프로세스별 페이지 테이블=주소공간 격리, 스케줄러가 U-mode로 실행, `SYS_exit` 생명주기)
- **Stage 5+**: `fork()` — 주소공간 복사로 프로세스 복제(부모=자식 pid, 자식=0), pid
- **Stage 5++**: ELF 로더 — 따로 컴파일한 유저 프로그램(`user/init.c`)을 ELF로 임베드, 커널이 프로그램 헤더를 파싱해 적재
- **Stage 6**: 파일시스템 — virtio-blk 디스크 드라이버(모던 MMIO, 폴링) + 읽기 전용 FS(슈퍼블록/디렉터리/데이터), 셸 `ls`/`cat`
- **Stage 7**: 런타임 `exec()` — 디스크의 ELF 프로그램으로 현재 프로세스를 교체. `fork` + `exec("hello")`로 디스크의 별도 프로그램 실행
- **Stage 7+**: 유저공간 셸 — sleep/wakeup으로 블로킹 `read`, `wait`(부모가 자식 종료 대기), 셸이 U-mode에서 돌며 `ls`/`cat`/`mem`(내장) + 디스크 프로그램(fork+exec)을 실행
- **자원 회수**: exec는 페이지 테이블/스택을 재사용(코드만 교체)해 누수 없음, `wait`가 종료한 자식의 유저 페이지+페이지 테이블을 회수 — `hello`를 반복 실행해도 `mem`이 일정
- **Lazy allocation (demand paging)**: `sbrk`는 힙만 키우고 페이지는 안 줌 → 처음 접근 시 페이지 폴트로 할당. 예외 핸들러가 실제로 메모리를 만든다(디스크 프로그램 `lazytest`로 시연)

> 트랩 프레임에 `sepc`/`sstatus`를 저장해 여러 프로세스의 트랩이 인터리빙돼도 안전. 유저 프로세스 트랩은 SUM 비트로 유저 스택에서 처리(트램폴린 단순화). fork는 부모의 트랩 프레임이 유저 스택에 있다는 점을 이용 — 스택 페이지를 복사하면 같은 VA에 프레임이 그대로 들어가, `forkret`이 그 프레임으로 복귀. exec는 주소공간을 바꾸면 유저 스택이 통째로 바뀌므로, satp 전환 후 sret까지를 스택을 건드리지 않는 어셈블리(`userret_to`)로 처리. 유저 프로그램은 `user/`에서 별도 컴파일 → `init`은 `.incbin` 임베드, `hello`는 디스크에 적재. virtio used 링은 비동기 갱신이라 `volatile`로 읽는다.

## 학습 로드맵 (xv6 6.1810 랩 기준)

| 랩 | 주제 | 상태 |
|---|---|---|
| util / syscall / pgtbl / traps | 유틸·시스템콜·페이지테이블·트랩 | 완료 |
| lazy | 지연 할당(demand paging) | 완료 |
| cow | Copy-on-Write fork | 예정 |
| mmap | 메모리 맵 파일 | 예정 |
| thread | 멀티스레딩 | 예정 |
| lock | 병렬성·락(멀티코어 SMP) | 예정 |
| fs | 쓰기 가능 FS + 로깅(크래시 복구) | 읽기 전용만 |
| net | TCP/IP 네트워크 스택 | 예정 |

## 구조

```
hobby-kernel/
├── src/
│   ├── entry.S        # S-mode 진입점 (스택 설정 → kmain)
│   ├── kernelvec.S    # 트랩 진입/복귀 (+ forkret)
│   ├── swtch.S        # 컨텍스트 스위치
│   ├── uart/plic      # 장치 드라이버
│   ├── trap.c         # 트랩/인터럽트/타이머
│   ├── kalloc.c       # 물리 페이지 할당기
│   ├── vm.c           # Sv39 페이징 + 프로세스별 페이지 테이블
│   ├── proc.c         # 프로세스/스케줄러/fork/exec/wait/sleep
│   ├── elf.c          # ELF64 로더
│   ├── virtio.c       # virtio-blk 디스크 드라이버
│   ├── fs.c           # 읽기 전용 파일시스템
│   ├── fsformat.h     # 온디스크 포맷(커널+mkfs 공유)
│   ├── console.c      # 콘솔 입력(라인 버퍼 + 블로킹 read)
│   ├── user.c         # 시스템콜 디스패치
│   ├── initcode.S     # 셸 ELF 임베드(.incbin)
│   └── main.c         # kmain
├── user/              # 따로 컴파일되는 유저 공간
│   ├── init.c         # 유저공간 셸(임베드)
│   ├── hello.c        # 디스크에 올라가는 예제 프로그램
│   ├── usys.h         # 시스템콜 래퍼
│   └── user.ld        # USERVA(0x1000)에 링크
├── tools/mkfs.c       # 디스크 이미지 빌더(호스트)
├── fs/                # 디스크에 담을 파일들
├── kernel.ld          # 0x8020_0000에 링크
└── Makefile           # make → 커널 + fs.img,  make run → QEMU(디스크 첨부)
```

## 스택

C · RISC-V (rv64imac) · riscv64-elf-gcc · QEMU(virt) · OpenSBI
