// vga_buffer — VGA 텍스트 모드 출력 모듈
//
// 1단계에서 _start에 직접 박았던 "0xb8000에 글자 쓰기"를 제대로 캡슐화한다.
// 줄바꿈, 스크롤, 색상, println! 매크로까지 지원한다.

use core::fmt;
use lazy_static::lazy_static;
use spin::Mutex;
use volatile::Volatile;

// VGA가 지원하는 16색. repr(u8)로 0~15 숫자값을 그대로 갖게 한다.
#[allow(dead_code)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum Color {
    Black = 0,
    Blue = 1,
    Green = 2,
    Cyan = 3,
    Red = 4,
    Magenta = 5,
    Brown = 6,
    LightGray = 7,
    DarkGray = 8,
    LightBlue = 9,
    LightGreen = 10,
    LightCyan = 11,
    LightRed = 12,
    Pink = 13,
    Yellow = 14,
    White = 15,
}

// 색상 1바이트 = 상위 4비트(배경) + 하위 4비트(글자색).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(transparent)]
struct ColorCode(u8);

impl ColorCode {
    fn new(foreground: Color, background: Color) -> ColorCode {
        ColorCode((background as u8) << 4 | (foreground as u8))
    }
}

// 화면 글자 1칸 = [아스키 1바이트][색상 1바이트]. repr(C)로 순서를 보장한다.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(C)]
struct ScreenChar {
    ascii_character: u8,
    color_code: ColorCode,
}

const BUFFER_HEIGHT: usize = 25;
const BUFFER_WIDTH: usize = 80;

// 화면 전체 = 25행 x 80열. 각 칸을 Volatile로 감싸 최적화 생략을 막는다.
#[repr(transparent)]
struct Buffer {
    chars: [[Volatile<ScreenChar>; BUFFER_WIDTH]; BUFFER_HEIGHT],
}

// 실제로 글자를 찍는 주체. 현재 커서 열, 색상, 화면 버퍼 참조를 들고 있다.
pub struct Writer {
    column_position: usize,
    color_code: ColorCode,
    buffer: &'static mut Buffer,
}

impl Writer {
    // 바이트 1개 출력. 줄바꿈이면 new_line, 아니면 현재 위치에 찍는다.
    pub fn write_byte(&mut self, byte: u8) {
        match byte {
            b'\n' => self.new_line(),
            byte => {
                if self.column_position >= BUFFER_WIDTH {
                    self.new_line();
                }
                let row = BUFFER_HEIGHT - 1; // 항상 맨 아랫줄에 쓰고 위로 스크롤
                let col = self.column_position;
                let color_code = self.color_code;
                self.buffer.chars[row][col].write(ScreenChar {
                    ascii_character: byte,
                    color_code,
                });
                self.column_position += 1;
            }
        }
    }

    pub fn write_string(&mut self, s: &str) {
        for byte in s.bytes() {
            match byte {
                // 출력 가능한 아스키(0x20~0x7e)와 줄바꿈만 그대로,
                0x20..=0x7e | b'\n' => self.write_byte(byte),
                // 나머지(한글 등 VGA가 모르는 문자)는 ■(0xfe)로 대체.
                _ => self.write_byte(0xfe),
            }
        }
    }

    // 줄바꿈 = 모든 줄을 한 칸 위로 옮기고(스크롤), 맨 아랫줄을 비운다.
    fn new_line(&mut self) {
        for row in 1..BUFFER_HEIGHT {
            for col in 0..BUFFER_WIDTH {
                let character = self.buffer.chars[row][col].read();
                self.buffer.chars[row - 1][col].write(character);
            }
        }
        self.clear_row(BUFFER_HEIGHT - 1);
        self.column_position = 0;
    }

    fn clear_row(&mut self, row: usize) {
        let blank = ScreenChar {
            ascii_character: b' ',
            color_code: self.color_code,
        };
        for col in 0..BUFFER_WIDTH {
            self.buffer.chars[row][col].write(blank);
        }
    }
}

// 이걸 구현하면 write!/writeln! 및 format_args!(숫자 포맷팅 등)를 쓸 수 있다.
impl fmt::Write for Writer {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        self.write_string(s);
        Ok(())
    }
}

lazy_static! {
    // 전역 Writer. 0xb8000을 가리키며, 기본 색은 검정 배경 + 노랑 글자.
    // Mutex로 감싸 동시 접근 시 안전하게(지금은 단일 스레드지만 습관) 한다.
    pub static ref WRITER: Mutex<Writer> = Mutex::new(Writer {
        column_position: 0,
        color_code: ColorCode::new(Color::Yellow, Color::Black),
        buffer: unsafe { &mut *(0xb8000 as *mut Buffer) },
    });
}

// println!/print! 매크로 — std의 그것을 우리 Writer용으로 직접 만든다.
#[macro_export]
macro_rules! print {
    ($($arg:tt)*) => ($crate::vga_buffer::_print(format_args!($($arg)*)));
}

#[macro_export]
macro_rules! println {
    () => ($crate::print!("\n"));
    ($($arg:tt)*) => ($crate::print!("{}\n", format_args!($($arg)*)));
}

#[doc(hidden)]
pub fn _print(args: fmt::Arguments) {
    use core::fmt::Write;
    WRITER.lock().write_fmt(args).unwrap();
}
