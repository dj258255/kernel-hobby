# anime_os

Rust로 바닥부터 만든 x86_64 토이 커널. 부팅·화면 출력·예외 처리·키보드 입력·메모리 관리·비동기 멀티태스킹·간단한 셸까지 OS의 핵심 골격을 직접 구현한 학습 프로젝트입니다.

## 구현한 것

| 단계 | 내용 |
|---|---|
| 0 | `no_std` 베어메탈 환경 + 커스텀 x86_64 타겟 |
| 1 | 부팅 + VGA 텍스트 버퍼 직접 출력 |
| 2 | `println!` 매크로 + 스크롤/색상 출력 시스템 |
| 3 | CPU 예외 처리(IDT) + GDT 더블폴트 비상 스택 |
| 4 | 하드웨어 인터럽트(PIC) + 키보드 입력 |
| 5 | 페이징 + 힙 할당 (`Box`/`Vec` 동적 할당) |
| 6 | 간단한 커널 셸 (help/echo/about/uptime/mem/clear/whoami) |
| 7 | async/await 협력적 멀티태스킹 (executor + 비동기 키보드 스트림) |

## 빌드 & 실행

```bash
# 사전 준비
rustup toolchain install nightly
rustup component add rust-src llvm-tools-preview --toolchain nightly
cargo install bootimage
brew install qemu   # 또는 각 OS의 qemu 설치

# 빌드 + QEMU 실행
cargo run
```

## 스택

Rust (nightly) · `bootloader` 0.9 · `x86_64` · `pic8259` · `pc-keyboard` · `linked_list_allocator` · `futures-util` · QEMU

## 참고

[Writing an OS in Rust (Philipp Oppermann)](https://os.phil-opp.com/) 커리큘럼을 기반으로 하되, 최신 nightly에 맞춰 직접 수정하며 진행했습니다.

제작 과정은 블로그 연재 "취미 OS 만들기"에 정리되어 있습니다.
