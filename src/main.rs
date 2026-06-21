// hobby_kernel — 토이 커널
// 7단계: async/await 협력적 멀티태스킹 (셸 태스크 + 동시 태스크).
#![no_std]
#![no_main]
#![feature(abi_x86_interrupt)]

extern crate alloc; // 동적 할당 + async 태스크(Box<dyn Future>)에 필요

mod allocator;
mod gdt;
mod interrupts;
mod memory;
mod shell;
mod task;
mod vga_buffer;

use bootloader::{entry_point, BootInfo};
use core::panic::PanicInfo;
use task::{executor::Executor, Task};
use x86_64::VirtAddr;

entry_point!(kernel_main);

fn kernel_main(boot_info: &'static BootInfo) -> ! {
    println!("hobby_kernel v0.1.0 booting...");

    gdt::init();
    interrupts::init();

    // 페이징 + 힙 (async 태스크는 Box로 힙에 할당된다)
    let phys_mem_offset = VirtAddr::new(boot_info.physical_memory_offset);
    let mut mapper = unsafe { memory::init(phys_mem_offset) };
    let mut frame_allocator =
        unsafe { memory::BootInfoFrameAllocator::init(&boot_info.memory_map) };
    allocator::init_heap(&mut mapper, &mut frame_allocator).expect("heap init failed");

    println!("  [ok] interrupts + heap ready");

    // 실행기에 두 태스크를 올려 동시에 진행시킨다.
    let mut executor = Executor::new();
    executor.spawn(Task::new(example_task())); // 먼저 한 번 실행되고 끝남
    executor.spawn(Task::new(shell::run())); // 키 입력을 await하는 셸
    executor.run();
}

// 멀티태스킹 증명용 태스크 — async 연산을 await해 결과를 한 번 출력하고 끝난다.
// 셸 태스크와 같은 실행기에서 동시에 돌아간다.
async fn example_task() {
    let n = async_number().await;
    println!("  [task] concurrent async task computed: {}", n);
}

async fn async_number() -> u32 {
    42
}

#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    println!("{}", info);
    loop {
        x86_64::instructions::hlt();
    }
}
