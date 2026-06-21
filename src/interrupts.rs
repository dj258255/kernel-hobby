// interrupts — CPU 예외(IDT) + 하드웨어 인터럽트(타이머/키보드)
//
// IDT는 "각 예외/인터럽트 번호마다 어떤 함수를 부를지" 적어둔 표다.
// CPU가 예외/인터럽트를 만나면 이 표를 보고 핸들러로 점프한다.

use crate::gdt;
use crate::println;
use core::sync::atomic::{AtomicU64, Ordering};
use lazy_static::lazy_static;
use pic8259::ChainedPics;
use spin::Mutex;
use x86_64::structures::idt::{InterruptDescriptorTable, InterruptStackFrame};

// 타이머 인터럽트가 발생할 때마다 1씩 증가. 셸 uptime 명령이 읽는다.
pub static TICKS: AtomicU64 = AtomicU64::new(0);

pub fn ticks() -> u64 {
    TICKS.load(Ordering::Relaxed)
}

// 8259 PIC 두 개를 32, 40번부터 매핑한다(0~31은 CPU 예외가 쓰므로 피함).
pub const PIC_1_OFFSET: u8 = 32;
pub const PIC_2_OFFSET: u8 = PIC_1_OFFSET + 8;

pub static PICS: Mutex<ChainedPics> =
    Mutex::new(unsafe { ChainedPics::new(PIC_1_OFFSET, PIC_2_OFFSET) });

// 하드웨어 인터럽트 번호. 타이머=32, 키보드=33.
#[derive(Debug, Clone, Copy)]
#[repr(u8)]
pub enum InterruptIndex {
    Timer = PIC_1_OFFSET,
    Keyboard,
}

impl InterruptIndex {
    fn as_u8(self) -> u8 {
        self as u8
    }
    fn as_usize(self) -> usize {
        usize::from(self.as_u8())
    }
}

lazy_static! {
    static ref IDT: InterruptDescriptorTable = {
        let mut idt = InterruptDescriptorTable::new();
        // --- CPU 예외 ---
        idt.breakpoint.set_handler_fn(breakpoint_handler);
        unsafe {
            idt.double_fault
                .set_handler_fn(double_fault_handler)
                .set_stack_index(gdt::DOUBLE_FAULT_IST_INDEX);
        }
        // --- 하드웨어 인터럽트 ---
        idt[InterruptIndex::Timer.as_usize()].set_handler_fn(timer_interrupt_handler);
        idt[InterruptIndex::Keyboard.as_usize()].set_handler_fn(keyboard_interrupt_handler);
        idt
    };
}

// IDT 로드 + PIC 초기화 + 인터럽트 활성화(sti)까지 한 번에.
pub fn init() {
    IDT.load();
    unsafe { PICS.lock().initialize() };
    x86_64::instructions::interrupts::enable();
}

// ===== CPU 예외 핸들러 =====

extern "x86-interrupt" fn breakpoint_handler(stack_frame: InterruptStackFrame) {
    println!("EXCEPTION: BREAKPOINT\n{:#?}", stack_frame);
}

extern "x86-interrupt" fn double_fault_handler(
    stack_frame: InterruptStackFrame,
    _error_code: u64,
) -> ! {
    panic!("EXCEPTION: DOUBLE FAULT\n{:#?}", stack_frame);
}

// ===== 하드웨어 인터럽트 핸들러 =====

// 타이머: 주기적으로 발생. 지금은 조용히 EOI만 보낸다(화면 어지럽힘 방지).
// 이 핸들러가 안 죽고 도는 것 자체가 "인터럽트가 켜져 있다"는 증거.
extern "x86-interrupt" fn timer_interrupt_handler(_stack_frame: InterruptStackFrame) {
    TICKS.fetch_add(1, Ordering::Relaxed);
    unsafe {
        PICS.lock()
            .notify_end_of_interrupt(InterruptIndex::Timer.as_u8());
    }
}

// 키보드: 포트 0x60에서 스캔코드를 읽어 비동기 스트림 큐에 넣기만 한다.
// 실제 디코딩/처리는 셸 태스크가 한다(핸들러는 짧게 끝낸다).
extern "x86-interrupt" fn keyboard_interrupt_handler(_stack_frame: InterruptStackFrame) {
    use x86_64::instructions::port::Port;

    let mut port = Port::new(0x60);
    let scancode: u8 = unsafe { port.read() };
    crate::task::keyboard::add_scancode(scancode);

    unsafe {
        PICS.lock()
            .notify_end_of_interrupt(InterruptIndex::Keyboard.as_u8());
    }
}
