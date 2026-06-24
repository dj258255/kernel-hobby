# ckernel Makefile — C / RISC-V 학습 커널

CROSS   := riscv64-elf-
CC      := $(CROSS)gcc
LD      := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy

# rv64imac + lp64(소프트플로트 ABI): 부팅 시 FPU(sstatus.FS)가 꺼져 있어도
# float 명령이 안 나가 안전. medany: 어느 주소에서도 동작하는 코드 모델.
CFLAGS  := -march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany \
           -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
           -Wall -Wextra -O2 -g -Isrc
# 유저 프로그램: 작게(-Os), _start를 맨 앞에 두려고 함수별 섹션 분리
UCFLAGS := -march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany \
           -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
           -ffunction-sections -Os -g -Iuser
LDFLAGS := -T kernel.ld -nostdlib

OBJS := build/entry.o build/kernelvec.o build/uart.o build/trap.o build/plic.o build/console.o build/kalloc.o build/vm.o build/elf.o build/virtio.o build/fs.o build/user.o build/proc.o build/swtch.o build/initcode.o build/main.o

# 호스트(맥) 컴파일러로 빌드하는 도구 + 디스크에 담을 파일들
HOSTCC  := cc
FSFILES := fs/motd.txt fs/readme.txt build/hello

all: build/kernel.elf build/fs.img

build:
	mkdir -p build

build/%.o: src/%.S | build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

# --- 유저 프로그램(별도 컴파일 → ELF) ---
build/user_init.o: user/init.c user/usys.h | build
	$(CC) $(UCFLAGS) -c user/init.c -o $@

build/user_init.elf: build/user_init.o user/user.ld
	$(LD) -T user/user.ld build/user_init.o -o $@

# 디스크에 올라가는 별도 프로그램(hello). exec("hello")로 적재됨.
build/user_hello.o: user/hello.c user/usys.h | build
	$(CC) $(UCFLAGS) -c user/hello.c -o $@

build/hello: build/user_hello.o user/user.ld
	$(LD) -T user/user.ld build/user_hello.o -o $@

# initcode.S는 위에서 만든 ELF를 .incbin으로 임베드하므로 의존성 추가
build/initcode.o: build/user_init.elf

build/kernel.elf: $(OBJS) kernel.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

# --- 디스크 이미지(호스트에서 빌드) ---
build/mkfs: tools/mkfs.c src/fsformat.h | build
	$(HOSTCC) -Isrc -Wall -o $@ tools/mkfs.c

build/fs.img: build/mkfs $(FSFILES)
	build/mkfs $@ $(FSFILES)

# QEMU virt + OpenSBI(기본 펌웨어)로 실행. -nographic이면 UART가 stdout으로 나온다.
# virtio-blk 디스크로 fs.img를 붙인다. 종료: Ctrl-A 다음 X
# force-legacy=false → virtio-mmio를 모던(version 2)으로. 우리 드라이버는 모던 전용.
QEMU_DISK := -global virtio-mmio.force-legacy=false \
             -drive file=build/fs.img,if=none,format=raw,id=x0 \
             -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
run: build/kernel.elf build/fs.img
	qemu-system-riscv64 -machine virt -bios default -nographic -kernel build/kernel.elf $(QEMU_DISK)

clean:
	rm -rf build

.PHONY: all run clean
