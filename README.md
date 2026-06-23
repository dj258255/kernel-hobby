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
- **Stage 2**: 페이징(Sv39 3단계 페이지 테이블, 커널 식별 매핑)
- **Stage 3**: 유저모드 + 시스템콜 (U-mode 진입, `ecall` → putchar/print 디스패치)
- **Stage 4**: 프로세스 + 선점형 스케줄러 (swtch.S 컨텍스트 스위치, 타이머 선점, 셸 복귀 + `ps`)
- **Stage 5**: 유저 프로세스 (프로세스별 페이지 테이블=주소공간 격리, 스케줄러가 U-mode로 실행, `SYS_exit` 생명주기)

> 트랩 프레임에 `sepc`/`sstatus`를 저장해 여러 프로세스의 트랩이 인터리빙돼도 안전. 유저 프로세스 트랩은 SUM 비트로 유저 스택에서 처리(트램폴린 단순화).

## 로드맵 ("작은 유닉스"로)

| 단계 | 내용 | xv6 참고 |
|---|---|---|
| Stage 5+ | fork/exec + ELF 로더 (현재는 임베드된 프로그램) | `kernel/exec.c` |
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
