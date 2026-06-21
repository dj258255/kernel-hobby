// memory — 페이징(가상 메모리) 관리
//
// 부트로더가 미리 매핑해둔 물리 메모리를 바탕으로, 현재 활성 페이지 테이블에
// 접근하고(OffsetPageTable), 빈 물리 프레임을 나눠주는 할당기를 제공한다.

use bootloader::bootinfo::{MemoryMap, MemoryRegionType};
use x86_64::{
    structures::paging::{FrameAllocator, OffsetPageTable, PageTable, PhysFrame, Size4KiB},
    PhysAddr, VirtAddr,
};

// 현재 활성 페이지 테이블을 OffsetPageTable로 감싸 돌려준다.
// physical_memory_offset: 부트로더가 "물리 주소 + 이 값 = 가상 주소"로
// 전체 물리 메모리를 매핑해둔 그 오프셋.
pub unsafe fn init(physical_memory_offset: VirtAddr) -> OffsetPageTable<'static> {
    let level_4_table = active_level_4_table(physical_memory_offset);
    OffsetPageTable::new(level_4_table, physical_memory_offset)
}

// CPU의 Cr3 레지스터가 가리키는 최상위(4단계) 페이지 테이블을 참조한다.
unsafe fn active_level_4_table(physical_memory_offset: VirtAddr) -> &'static mut PageTable {
    use x86_64::registers::control::Cr3;

    let (level_4_table_frame, _) = Cr3::read();
    let phys = level_4_table_frame.start_address();
    let virt = physical_memory_offset + phys.as_u64();
    let page_table_ptr: *mut PageTable = virt.as_mut_ptr();

    &mut *page_table_ptr
}

// 부트로더가 준 메모리 지도에서 "사용 가능(Usable)" 영역의 프레임을
// 순서대로 하나씩 나눠주는 단순 할당기.
pub struct BootInfoFrameAllocator {
    memory_map: &'static MemoryMap,
    next: usize,
}

impl BootInfoFrameAllocator {
    pub unsafe fn init(memory_map: &'static MemoryMap) -> Self {
        BootInfoFrameAllocator {
            memory_map,
            next: 0,
        }
    }

    // 사용 가능한 모든 4KiB 프레임을 순회하는 이터레이터.
    fn usable_frames(&self) -> impl Iterator<Item = PhysFrame> {
        let regions = self.memory_map.iter();
        let usable = regions.filter(|r| r.region_type == MemoryRegionType::Usable);
        let addr_ranges = usable.map(|r| r.range.start_addr()..r.range.end_addr());
        let frame_addresses = addr_ranges.flat_map(|r| r.step_by(4096));
        frame_addresses.map(|addr| PhysFrame::containing_address(PhysAddr::new(addr)))
    }
}

unsafe impl FrameAllocator<Size4KiB> for BootInfoFrameAllocator {
    fn allocate_frame(&mut self) -> Option<PhysFrame> {
        let frame = self.usable_frames().nth(self.next);
        self.next += 1;
        frame
    }
}
