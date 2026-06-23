# hobby-kernel

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

> 여기까지로 이전 Rust 버전의 기능(출력·인터럽트·키보드·셸·힙)을 C/RISC-V로 전부 재구현했다. 아래부터는 Rust 커널엔 없던 깊은 영역.

## 로드맵 (이 너머 — "작은 유닉스"로)

| 단계 | 내용 | xv6 참고 |
|---|---|---|
| Stage 2 | 페이지 테이블(Sv39) + 커널 가상메모리 | `kernel/vm.c` |
| Stage 3 | **유저모드 + 시스템콜** (U-mode 진입, `ecall` 디스패치) | `kernel/trampoline.S`, `syscall.c` |
| Stage 4 | 프로세스 + 선점형 스케줄러 (context switch) | `kernel/proc.c`, `swtch.S` |
| Stage 5 | fork/exec + ELF 로더 | `kernel/exec.c` |
| Stage 6 | 파일시스템 (virtio-blk 디스크 + inode) | `kernel/fs.c`, `bio.c` |
| Stage 7 | 유저공간 셸 + 기본 프로그램 | `user/sh.c` |

## 구조

```
hobby-kernel/
├── src/
│   ├── entry.S   # S-mode 진입점 (스택 설정 → kmain)
│   ├── uart.c/.h # NS16550 UART 드라이버
│   └── main.c    # kmain
├── kernel.ld     # 0x8020_0000에 링크
└── Makefile
```

## 스택

C · RISC-V (rv64imac) · riscv64-elf-gcc · QEMU(virt) · OpenSBI
