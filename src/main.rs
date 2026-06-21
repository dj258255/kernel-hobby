// anime_os — 토이 커널
// 6단계: 키보드 입력 + 힙을 엮은 간단한 커널 셸.
#![no_std]
#![no_main]
#![feature(abi_x86_interrupt)]

extern crate alloc; // 동적 할당(String, Vec 등)을 쓰기 위한 alloc 크레이트

mod allocator;
mod gdt;
mod interrupts;
mod memory;
mod shell;
mod vga_buffer;

use bootloader::{entry_point, BootInfo};
use core::panic::PanicInfo;
use x86_64::VirtAddr;

// entry_point!: 부트로더가 넘겨주는 BootInfo를 안전하게 받는 진입점.
entry_point!(kernel_main);

fn kernel_main(boot_info: &'static BootInfo) -> ! {
    println!("anime_os v0.1.0 booting...");

    gdt::init();
    interrupts::init();

    // 페이징 + 힙 초기화 (셸의 줄 버퍼/명령 처리에 동적 할당이 필요)
    let phys_mem_offset = VirtAddr::new(boot_info.physical_memory_offset);
    let mut mapper = unsafe { memory::init(phys_mem_offset) };
    let mut frame_allocator =
        unsafe { memory::BootInfoFrameAllocator::init(&boot_info.memory_map) };
    allocator::init_heap(&mut mapper, &mut frame_allocator).expect("heap init failed");

    println!("  [ok] interrupts + heap ready");

    shell::init(); // 입력 큐를 미리 할당(인터럽트 컨텍스트 할당 방지)
    shell::run(); // 셸 진입 — 돌아오지 않는다
}

#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    println!("{}", info);
    loop {
        x86_64::instructions::hlt();
    }
}
