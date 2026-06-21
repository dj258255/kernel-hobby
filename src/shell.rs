// shell — 아주 단순한 커널 셸
//
// 키보드 인터럽트 핸들러는 글자를 INPUT_QUEUE에 넣기만 하고(짧게 끝),
// 메인 루프(run)가 큐에서 꺼내 한 줄을 조립한 뒤 Enter에서 명령을 실행한다.

use crate::vga_buffer;
use crate::{print, println};
use alloc::string::String;
use crossbeam_queue::ArrayQueue;
use lazy_static::lazy_static;

lazy_static! {
    // 락-프리 고정 크기 큐(256칸). 인터럽트 핸들러와 메인 루프가
    // 안전하게(락 없이) 글자를 주고받는다.
    static ref INPUT_QUEUE: ArrayQueue<char> = ArrayQueue::new(256);
}

// 큐를 미리 할당해 둔다(인터럽트 컨텍스트에서 첫 할당이 일어나지 않게).
pub fn init() {
    lazy_static::initialize(&INPUT_QUEUE);
}

// 키보드 인터럽트 핸들러가 호출. 큐에 넣기만 한다(가득 차면 버림).
pub fn push_char(c: char) {
    let _ = INPUT_QUEUE.push(c);
}

// 셸 메인 루프. 다시 돌아오지 않는다.
pub fn run() -> ! {
    print_banner();
    print_prompt();

    let mut line = String::new();
    loop {
        match INPUT_QUEUE.pop() {
            Some('\n') => {
                println!();
                execute(line.trim());
                line.clear();
                print_prompt();
            }
            Some('\u{8}') => {
                // 백스페이스: 줄 버퍼와 화면에서 한 글자 지움
                if line.pop().is_some() {
                    vga_buffer::backspace();
                }
            }
            Some(c) => {
                line.push(c);
                print!("{}", c); // 입력 에코
            }
            None => x86_64::instructions::hlt(), // 큐가 비면 다음 키까지 잔다
        }
    }
}

fn print_prompt() {
    print!("anime_os> ");
}

fn print_banner() {
    println!();
    println!("  welcome to the anime_os shell");
    println!("  type 'help' to see commands");
    println!();
}

// 한 줄을 명령어 + 인자로 나눠 실행한다.
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
            println!("  clear        clear the screen");
            println!("  whoami       who is running this");
        }
        "echo" => println!("{}", args),
        "about" => {
            println!("anime_os v0.1.0  --  a hobby OS written in Rust");
            println!("path: boot -> vga -> interrupts -> keyboard -> heap -> shell");
        }
        "clear" => vga_buffer::clear_screen(),
        "whoami" => println!("a proud weeb running their own OS :)"),
        _ => println!("unknown command: '{}'  (try 'help')", cmd),
    }
}
