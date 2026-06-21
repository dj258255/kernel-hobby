// anime_os — 토이 커널
// 5단계: 페이징 + 힙 할당 (Box/Vec 같은 동적 할당 지원).
#![no_std]
#![no_main]
#![feature(abi_x86_interrupt)]

extern crate alloc; // 동적 할당(Box, Vec 등)을 쓰기 위한 alloc 크레이트

mod allocator;
mod gdt;
mod interrupts;
mod memory;
mod vga_buffer;

use alloc::{boxed::Box, vec::Vec};
use bootloader::{entry_point, BootInfo};
use core::panic::PanicInfo;
use x86_64::VirtAddr;

// entry_point!: 부트로더가 넘겨주는 BootInfo를 안전하게 받도록
// 진입점을 정의한다(이전의 #[no_mangle] _start를 대체).
entry_point!(kernel_main);

fn kernel_main(boot_info: &'static BootInfo) -> ! {
    println!("anime_os v0.1.0 booting...");

    gdt::init();
    interrupts::init();
    println!("  [ok] interrupts enabled");

    // 페이징 + 힙 초기화
    let phys_mem_offset = VirtAddr::new(boot_info.physical_memory_offset);
    let mut mapper = unsafe { memory::init(phys_mem_offset) };
    let mut frame_allocator =
        unsafe { memory::BootInfoFrameAllocator::init(&boot_info.memory_map) };

    allocator::init_heap(&mut mapper, &mut frame_allocator).expect("heap init failed");
    println!("  [ok] heap initialized (100 KiB)");
    println!();

    // 동적 할당 시연 — 이제 Box/Vec을 쓸 수 있다!
    let heap_value = Box::new(41);
    println!("  Box::new(41) -> value {} at {:p}", heap_value, heap_value);

    let mut vec = Vec::new();
    for i in 1..=10 {
        vec.push(i);
    }
    println!("  Vec {:?}  (sum = {})", vec, vec.iter().sum::<i32>());

    println!();
    println!("dynamic allocation works! type away:");
    print!("> ");

    loop {
        x86_64::instructions::hlt();
    }
}

#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    println!("{}", info);
    loop {
        x86_64::instructions::hlt();
    }
}
