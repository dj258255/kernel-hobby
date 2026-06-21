// allocator — 커널 힙 할당기
//
// 가상 주소 공간의 한 영역(HEAP_START~)을 실제 물리 프레임에 매핑한 뒤,
// 그 위에 linked_list_allocator를 전역 할당기로 설치한다.
// 이게 끝나면 alloc 크레이트의 Box/Vec/String 등을 쓸 수 있다.

use linked_list_allocator::LockedHeap;
use x86_64::{
    structures::paging::{
        mapper::MapToError, FrameAllocator, Mapper, Page, PageTableFlags, Size4KiB,
    },
    VirtAddr,
};

// #[global_allocator]: Rust에게 "동적 할당은 이 객체에 맡겨라"고 등록.
#[global_allocator]
static ALLOCATOR: LockedHeap = LockedHeap::empty();

pub const HEAP_START: usize = 0x_4444_4444_0000; // 안 겹치는 임의의 가상 주소
pub const HEAP_SIZE: usize = 100 * 1024; // 100 KiB

// 힙 영역에 해당하는 가상 페이지들을 빈 물리 프레임에 매핑하고,
// 할당기를 그 영역으로 초기화한다.
pub fn init_heap(
    mapper: &mut impl Mapper<Size4KiB>,
    frame_allocator: &mut impl FrameAllocator<Size4KiB>,
) -> Result<(), MapToError<Size4KiB>> {
    let page_range = {
        let heap_start = VirtAddr::new(HEAP_START as u64);
        let heap_end = heap_start + HEAP_SIZE - 1u64;
        let heap_start_page = Page::containing_address(heap_start);
        let heap_end_page = Page::containing_address(heap_end);
        Page::range_inclusive(heap_start_page, heap_end_page)
    };

    for page in page_range {
        let frame = frame_allocator
            .allocate_frame()
            .ok_or(MapToError::FrameAllocationFailed)?;
        let flags = PageTableFlags::PRESENT | PageTableFlags::WRITABLE;
        unsafe { mapper.map_to(page, frame, flags, frame_allocator)?.flush() };
    }

    // 매핑이 끝났으니 할당기에 "이 영역을 힙으로 써라"고 알려준다.
    unsafe {
        ALLOCATOR.lock().init(HEAP_START as *mut u8, HEAP_SIZE);
    }

    Ok(())
}

// 힙 사용 통계 (used, free, total) — 셸 mem 명령에서 쓴다.
// 락을 짧게 잡고 숫자만 복사해 나온다(출력은 락 해제 후).
pub fn heap_stats() -> (usize, usize, usize) {
    let used;
    let free;
    {
        let heap = ALLOCATOR.lock();
        used = heap.used();
        free = heap.free();
    }
    (used, free, HEAP_SIZE)
}
