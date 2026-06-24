# hobby-kernel

[한국어](README.md) | **English**

A **RISC-V learning kernel** written from scratch in C. The goal is to understand OS internals — the user/kernel boundary, system calls, processes, and a filesystem — by implementing them by hand. It follows [xv6 (MIT 6.S081)](https://pdos.csail.mit.edu/6.828/2023/xv6.html) as a reference.

## Build & Run

```bash
# Prerequisites: riscv64 cross-compiler + QEMU
brew install riscv64-elf-gcc qemu

make          # build/kernel.elf
make run      # boot on QEMU virt + OpenSBI (UART -> stdout). Quit: Ctrl-A then X
```

## Status

- **Step 0**: S-mode boot + NS16550 UART output
- **Stage 1**: traps/interrupts + timer (stvec, sstc)
- **Keyboard input**: PLIC external interrupts + UART RX
- **Kernel shell**: help / about / uptime / mem / clear / whoami / echo
- **Page allocator**: kalloc/kfree (physical memory ~125MB)
- **Stage 2**: paging (Sv39 3-level page tables, identity-mapped kernel)
- **Stage 3**: user mode + system calls (enter U-mode, `ecall` -> putchar/print dispatch)
- **Stage 4**: processes + preemptive scheduler (swtch.S context switch, timer preemption, shell back + `ps`)
- **Stage 5**: user processes (per-process page tables = address-space isolation, scheduler runs them in U-mode, `SYS_exit` lifecycle)
- **Stage 5+**: `fork()` — duplicate a process by copying its address space (parent gets child pid, child gets 0), pids
- **Stage 5++**: ELF loader — a separately compiled user program (`user/init.c`) embedded as an ELF; the kernel parses program headers and loads it
- **Stage 6**: filesystem — virtio-blk disk driver (modern MMIO, polling) + a read-only FS (superblock/directory/data), shell `ls`/`cat`
- **Stage 7**: runtime `exec()` — replace the current process with an ELF program from disk. `fork` + `exec("hello")` runs a separate on-disk program
- **Stage 7+**: userspace shell — blocking `read` via sleep/wakeup, `wait` (parent waits for a child to exit); the shell runs in U-mode and executes `ls`/`cat`/`mem` (built-ins) + disk programs (fork+exec)
- **Resource reclamation**: exec reuses the page table/stack (only swaps the code page) so it leaks nothing; `wait` reclaims an exited child's user pages + page table — `mem` stays constant even when `hello` is run repeatedly

> The trap frame saves `sepc`/`sstatus` so interleaved traps from multiple processes stay safe. User-process traps are handled on the user stack via the SUM bit (a trampoline-free simplification). fork exploits the fact that the parent's trap frame lives on the user stack — copying the stack page places the frame at the same VA, and `forkret` returns through it. exec changes the address space, which would swap the entire user stack, so the satp switch through `sret` is done in stack-free assembly (`userret_to`) — except the reclaiming exec reuses the page table/stack instead. User programs are compiled separately under `user/`: `init` is embedded via `.incbin`, `hello` lives on the disk. The virtio used ring is updated asynchronously by the device, so it is read as `volatile`.

## Roadmap (toward a "small Unix")

| Stage | Topic | xv6 ref |
|---|---|---|
| Polish | writable FS, multi-page programs, inodes, shell pipes/redirection | `kernel/exec.c`, `fs.c`, `user/sh.c` |

## Layout

```
hobby-kernel/
├── src/
│   ├── entry.S        # S-mode entry (set up stack -> kmain)
│   ├── kernelvec.S    # trap entry/return (+ forkret)
│   ├── swtch.S        # context switch
│   ├── uart/plic      # device drivers
│   ├── trap.c         # traps/interrupts/timer
│   ├── kalloc.c       # physical page allocator
│   ├── vm.c           # Sv39 paging + per-process page tables
│   ├── proc.c         # processes/scheduler/fork/exec/wait/sleep
│   ├── elf.c          # ELF64 loader
│   ├── virtio.c       # virtio-blk disk driver
│   ├── fs.c           # read-only filesystem
│   ├── fsformat.h     # on-disk format (shared by kernel + mkfs)
│   ├── console.c      # console input (line buffer + blocking read)
│   ├── user.c         # syscall dispatch
│   ├── initcode.S     # shell ELF embedded (.incbin)
│   └── main.c         # kmain
├── user/              # userspace, compiled separately
│   ├── init.c         # userspace shell (embedded)
│   ├── hello.c        # example program placed on disk
│   ├── usys.h         # syscall wrappers
│   └── user.ld        # linked at USERVA (0x1000)
├── tools/mkfs.c       # disk image builder (host)
├── fs/                # files placed on the disk
├── kernel.ld          # linked at 0x8020_0000
└── Makefile           # make -> kernel + fs.img,  make run -> QEMU (disk attached)
```

## Stack

C · RISC-V (rv64imac) · riscv64-elf-gcc · QEMU (virt) · OpenSBI

## Write-ups

A 4-part series (Korean): from boot to a userspace shell.

1. Boot to paging
2. User mode to processes
3. fork, ELF loader, filesystem
4. exec and a userspace shell
