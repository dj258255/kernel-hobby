// shell — 비동기 커널 셸
//
// executor가 돌리는 async 태스크. 키보드 스트림을 await로 받아 한 줄을
// 조립하고, Enter에서 명령을 실행한다. 다른 태스크와 동시에 진행된다.

use crate::task::keyboard::ScancodeStream;
use crate::vga_buffer;
use crate::{allocator, interrupts, print, println};
use alloc::string::String;
use futures_util::stream::StreamExt;
use pc_keyboard::{layouts, DecodedKey, HandleControl, Keyboard, ScancodeSet1};

pub async fn run() {
    let mut scancodes = ScancodeStream::new();
    let mut keyboard = Keyboard::new(ScancodeSet1::new(), layouts::Us104Key, HandleControl::Ignore);
    let mut line = String::new();

    print_banner();
    print_prompt();

    while let Some(scancode) = scancodes.next().await {
        if let Ok(Some(event)) = keyboard.add_byte(scancode) {
            if let Some(DecodedKey::Unicode(c)) = keyboard.process_keyevent(event) {
                match c {
                    '\n' => {
                        println!();
                        execute(line.trim());
                        line.clear();
                        print_prompt();
                    }
                    '\u{8}' => {
                        if line.pop().is_some() {
                            vga_buffer::backspace();
                        }
                    }
                    c => {
                        line.push(c);
                        print!("{}", c); // 입력 에코
                    }
                }
            }
        }
    }
}

fn print_prompt() {
    print!("hobby_kernel> ");
}

fn print_banner() {
    println!();
    println!("  welcome to the hobby_kernel shell");
    println!("  type 'help' to see commands");
    println!();
}

fn execute(line: &str) {
    if line.is_empty() {
        return;
    }
    let mut parts = line.splitn(2, ' ');
    let cmd = parts.next().unwrap_or("");
    let args = parts.next().unwrap_or("");

    match cmd {
        "help" => {
            println!("commands:");
            println!("  help         show this help");
            println!("  echo <text>  print text back");
            println!("  about        about this OS");
            println!("  uptime       timer ticks since boot");
            println!("  mem          heap usage");
            println!("  clear        clear the screen");
            println!("  whoami       who is running this");
        }
        "echo" => println!("{}", args),
        "about" => {
            println!("hobby_kernel v0.1.0  --  a hobby OS written in Rust");
            println!("boot -> vga -> interrupts -> heap -> async tasks -> shell");
        }
        "uptime" => {
            // 타이머 인터럽트 발생 횟수. 실제 주파수는 설정에 따라 달라
            // 정확한 초 환산은 하지 않고 raw 틱을 그대로 보여준다.
            println!("timer ticks since boot: {}", interrupts::ticks());
        }
        "mem" => {
            let (used, free, total) = allocator::heap_stats();
            println!("heap: {} used / {} free / {} total (bytes)", used, free, total);
        }
        "clear" => vga_buffer::clear_screen(),
        "whoami" => println!("a proud weeb running their own OS :)"),
        _ => println!("unknown command: '{}'  (try 'help')", cmd),
    }
}
