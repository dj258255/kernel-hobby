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
LDFLAGS := -T kernel.ld -nostdlib

OBJS := build/entry.o build/kernelvec.o build/uart.o build/trap.o build/plic.o build/shell.o build/kalloc.o build/main.o

all: build/kernel.elf

build:
	mkdir -p build

build/%.o: src/%.S | build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel.elf: $(OBJS) kernel.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

# QEMU virt + OpenSBI(기본 펌웨어)로 실행. -nographic이면 UART가 stdout으로 나온다.
# 종료: Ctrl-A 다음 X
run: build/kernel.elf
	qemu-system-riscv64 -machine virt -bios default -nographic -kernel build/kernel.elf

clean:
	rm -rf build

.PHONY: all run clean
