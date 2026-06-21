// executor — 비동기 태스크 실행기
//
// 준비된(깨어난) 태스크들을 폴링한다. 태스크가 Pending이면 잠들고,
// 인터럽트(키보드 등)가 Waker를 통해 깨우면 다시 큐에 올린다.
// 할 일이 없으면 enable_and_hlt로 CPU를 재워 전력을 아낀다.

use super::{Task, TaskId};
use alloc::{collections::BTreeMap, sync::Arc, task::Wake};
use core::task::{Context, Poll, Waker};
use crossbeam_queue::ArrayQueue;

pub struct Executor {
    tasks: BTreeMap<TaskId, Task>,
    task_queue: Arc<ArrayQueue<TaskId>>,
    waker_cache: BTreeMap<TaskId, Waker>,
}

impl Executor {
    pub fn new() -> Self {
        Executor {
            tasks: BTreeMap::new(),
            task_queue: Arc::new(ArrayQueue::new(100)),
            waker_cache: BTreeMap::new(),
        }
    }

    pub fn spawn(&mut self, task: Task) {
        let task_id = task.id;
        if self.tasks.insert(task.id, task).is_some() {
            panic!("동일 ID 태스크가 이미 존재");
        }
        self.task_queue.push(task_id).expect("태스크 큐 가득 참");
    }

    fn run_ready_tasks(&mut self) {
        let Self {
            tasks,
            task_queue,
            waker_cache,
        } = self;

        while let Some(task_id) = task_queue.pop() {
            let task = match tasks.get_mut(&task_id) {
                Some(task) => task,
                None => continue, // 이미 끝난 태스크
            };
            let waker = waker_cache
                .entry(task_id)
                .or_insert_with(|| TaskWaker::new(task_id, task_queue.clone()));
            let mut context = Context::from_waker(waker);
            match task.poll(&mut context) {
                Poll::Ready(()) => {
                    tasks.remove(&task_id);
                    waker_cache.remove(&task_id);
                }
                Poll::Pending => {}
            }
        }
    }

    pub fn run(&mut self) -> ! {
        loop {
            self.run_ready_tasks();
            self.sleep_if_idle();
        }
    }

    fn sleep_if_idle(&self) {
        use x86_64::instructions::interrupts::{self, enable_and_hlt};
        // 큐 확인과 잠들기 사이에 인터럽트가 끼어들지 않도록 잠시 끈다.
        interrupts::disable();
        if self.task_queue.is_empty() {
            enable_and_hlt(); // 인터럽트 켜고 즉시 hlt (원자적)
        } else {
            interrupts::enable();
        }
    }
}

// 태스크를 깨우는 Waker. 깨우면 그 태스크 ID를 다시 실행 큐에 넣는다.
struct TaskWaker {
    task_id: TaskId,
    task_queue: Arc<ArrayQueue<TaskId>>,
}

impl TaskWaker {
    fn new(task_id: TaskId, task_queue: Arc<ArrayQueue<TaskId>>) -> Waker {
        Waker::from(Arc::new(TaskWaker {
            task_id,
            task_queue,
        }))
    }

    fn wake_task(&self) {
        self.task_queue.push(self.task_id).expect("태스크 큐 가득 참");
    }
}

impl Wake for TaskWaker {
    fn wake(self: Arc<Self>) {
        self.wake_task();
    }
    fn wake_by_ref(self: &Arc<Self>) {
        self.wake_task();
    }
}
