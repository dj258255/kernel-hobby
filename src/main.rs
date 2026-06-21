// anime_os — 토이 커널
// 4단계(졸업): 하드웨어 인터럽트(타이머) + 키보드 입력.
#![no_std]
#![no_main]
#![feature(abi_x86_interrupt)]

mod gdt;
mod interrupts;
mod vga_buffer;

use core::panic::PanicInfo;

#[no_mangle]
pub extern "C" fn _start() -> ! {
    println!("anime_os v0.1.0 booting...");

    gdt::init(); // GDT/TSS (비상 스택)
    interrupts::init(); // IDT + PIC + 인터럽트 활성화

    println!("  [ok] interrupts enabled (timer + keyboard)");
    println!();
    println!("Type something on the keyboard:");
    print!("> ");

    // hlt 루프: 다음 인터럽트가 올 때까지 CPU를 재운다(바쁜 대기 대신 절전).
    // 키보드/타이머 인터럽트가 오면 깨어나 핸들러를 실행하고 다시 잠든다.
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
