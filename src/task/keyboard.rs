// task::keyboard — 키보드 스캔코드를 비동기 Stream으로 제공
//
// 인터럽트 핸들러는 add_scancode로 큐에 넣고 Waker를 깨우기만 한다.
// 셸 태스크는 ScancodeStream을 .next().await로 소비한다.

use conquer_once::spin::OnceCell;
use core::{
    pin::Pin,
    task::{Context, Poll},
};
use crossbeam_queue::ArrayQueue;
use futures_util::{
    stream::Stream,
    task::AtomicWaker,
};

static SCANCODE_QUEUE: OnceCell<ArrayQueue<u8>> = OnceCell::uninit();
static WAKER: AtomicWaker = AtomicWaker::new();

// 키보드 인터럽트 핸들러가 호출. 큐에 넣고 대기 중인 태스크를 깨운다.
// 인터럽트 컨텍스트라 할당/락을 피한다(ArrayQueue는 락-프리, 미리 할당됨).
pub(crate) fn add_scancode(scancode: u8) {
    if let Ok(queue) = SCANCODE_QUEUE.try_get() {
        if queue.push(scancode).is_ok() {
            WAKER.wake();
        }
        // 큐가 가득 차면 조용히 버린다.
    }
}

pub struct ScancodeStream {
    _private: (),
}

impl ScancodeStream {
    pub fn new() -> Self {
        SCANCODE_QUEUE
            .try_init_once(|| ArrayQueue::new(128))
            .expect("ScancodeStream::new은 한 번만 호출");
        ScancodeStream { _private: () }
    }
}

impl Stream for ScancodeStream {
    type Item = u8;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context) -> Poll<Option<u8>> {
        let queue = SCANCODE_QUEUE.try_get().expect("큐가 초기화되지 않음");

        // 빠른 경로: 이미 값이 있으면 바로 반환.
        if let Some(scancode) = queue.pop() {
            return Poll::Ready(Some(scancode));
        }

        // 비어 있으면 Waker를 등록하고, 등록과 pop 사이의 경합을 한 번 더 확인.
        WAKER.register(cx.waker());
        match queue.pop() {
            Some(scancode) => {
                WAKER.take();
                Poll::Ready(Some(scancode))
            }
            None => Poll::Pending,
        }
    }
}
