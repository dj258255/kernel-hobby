// virtio.c — virtio-blk(MMIO) 디스크 드라이버 (폴링 방식)
//
// QEMU virt의 첫 virtio-mmio 디바이스(0x10001000)에 블록 디바이스를 붙인다.
// 게스트는 메모리에 디스크립터 체인(요청헤더+버퍼+상태)을 만들어 available 링에
// 넣고, QUEUE_NOTIFY로 알린 뒤 used 링이 갱신될 때까지 폴링한다.
// (xv6의 virtio_disk.c를 폴링 전용으로 단순화)

#include "virtio.h"
#include "types.h"
#include "kalloc.h"
#include "uart.h"
#include "fsformat.h"  // BSIZE

#define VIRTIO0 0x10001000L

// MMIO 레지스터 오프셋
#define MMIO_MAGIC          0x000
#define MMIO_VERSION        0x004
#define MMIO_DEVICE_ID      0x008
#define MMIO_VENDOR_ID      0x00c
#define MMIO_DEVICE_FEAT    0x010
#define MMIO_DEVICE_FEAT_SEL 0x014
#define MMIO_DRIVER_FEAT    0x020
#define MMIO_DRIVER_FEAT_SEL 0x024
#define MMIO_QUEUE_SEL      0x030
#define MMIO_QUEUE_NUM_MAX  0x034
#define MMIO_QUEUE_NUM      0x038
#define MMIO_QUEUE_READY    0x044
#define MMIO_QUEUE_NOTIFY   0x050
#define MMIO_STATUS         0x070
#define MMIO_QUEUE_DESC_LO  0x080
#define MMIO_QUEUE_DESC_HI  0x084
#define MMIO_DRIVER_DESC_LO 0x090
#define MMIO_DRIVER_DESC_HI 0x094
#define MMIO_DEVICE_DESC_LO 0x0a0
#define MMIO_DEVICE_DESC_HI 0x0a4

// status 비트
#define S_ACKNOWLEDGE 1
#define S_DRIVER      2
#define S_DRIVER_OK   4
#define S_FEATURES_OK 8

// 끌 기능 비트
#define VIRTIO_BLK_F_RO         5
#define VIRTIO_BLK_F_SCSI       7
#define VIRTIO_BLK_F_CONFIG_WCE 11
#define VIRTIO_BLK_F_MQ         12
#define VIRTIO_F_ANY_LAYOUT     27
#define VIRTIO_RING_F_INDIRECT  28
#define VIRTIO_RING_F_EVENT_IDX 29

#define NUM 8  // 링 크기

struct virtq_desc { uint64 addr; uint32 len; uint16 flags; uint16 next; };
#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2
struct virtq_avail { uint16 flags; uint16 idx; uint16 ring[NUM]; uint16 unused; };
struct virtq_used_elem { uint32 id; uint32 len; };
struct virtq_used { uint16 flags; uint16 idx; struct virtq_used_elem ring[NUM]; };

struct virtio_blk_req { uint32 type; uint32 reserved; uint64 sector; };
#define VIRTIO_BLK_T_IN  0
#define VIRTIO_BLK_T_OUT 1

#define R32(off) (*(volatile uint32 *)(VIRTIO0 + (off)))
#define mb()     asm volatile("fence" ::: "memory")

static struct virtq_desc  *desc;
static struct virtq_avail *avail;
static volatile struct virtq_used *used;  // 디바이스가 비동기로 갱신 → volatile
static uint16 used_seen;             // 마지막으로 본 used->idx
static int    disk_ok;               // 초기화 성공 여부
static struct virtio_blk_req breq;   // 요청 헤더(한 번에 하나)
static volatile uint8 bstatus;       // 디바이스가 쓰는 상태 바이트

static void zero(void *p, uint64 n) {
    char *d = p;
    while (n-- > 0) *d++ = 0;
}

void virtio_disk_init(void) {
    if (R32(MMIO_MAGIC) != 0x74726976 || R32(MMIO_VERSION) != 2 ||
        R32(MMIO_DEVICE_ID) != 2 || R32(MMIO_VENDOR_ID) != 0x554d4551) {
        uart_puts("[virtio] no disk at 0x10001000\n");
        return;
    }

    uint32 status = 0;
    R32(MMIO_STATUS) = status;                  // reset
    status |= S_ACKNOWLEDGE; R32(MMIO_STATUS) = status;
    status |= S_DRIVER;      R32(MMIO_STATUS) = status;

    // 기능 협상(하위 32비트): 쓰지 않을 비트를 끈다
    R32(MMIO_DEVICE_FEAT_SEL) = 0;
    uint32 feat_lo = R32(MMIO_DEVICE_FEAT);
    feat_lo &= ~(1u << VIRTIO_BLK_F_RO);
    feat_lo &= ~(1u << VIRTIO_BLK_F_SCSI);
    feat_lo &= ~(1u << VIRTIO_BLK_F_CONFIG_WCE);
    feat_lo &= ~(1u << VIRTIO_BLK_F_MQ);
    feat_lo &= ~(1u << VIRTIO_F_ANY_LAYOUT);
    feat_lo &= ~(1u << VIRTIO_RING_F_EVENT_IDX);
    feat_lo &= ~(1u << VIRTIO_RING_F_INDIRECT);
    R32(MMIO_DRIVER_FEAT_SEL) = 0;
    R32(MMIO_DRIVER_FEAT) = feat_lo;

    // 상위 32비트(비트 32~): VIRTIO_F_VERSION_1(비트 32)을 수락해야 모던 동작.
    R32(MMIO_DEVICE_FEAT_SEL) = 1;
    uint32 feat_hi = R32(MMIO_DEVICE_FEAT);  // 비트 0 = VERSION_1
    R32(MMIO_DRIVER_FEAT_SEL) = 1;
    R32(MMIO_DRIVER_FEAT) = feat_hi;          // 그대로 수락(VERSION_1 포함)

    status |= S_FEATURES_OK; R32(MMIO_STATUS) = status;
    if (!(R32(MMIO_STATUS) & S_FEATURES_OK)) {
        uart_puts("[virtio] FEATURES_OK rejected\n");
        return;
    }

    // 큐 0 설정
    R32(MMIO_QUEUE_SEL) = 0;
    uint32 max = R32(MMIO_QUEUE_NUM_MAX);
    if (max == 0 || max < NUM) {
        uart_puts("[virtio] bad queue size\n");
        return;
    }
    R32(MMIO_QUEUE_NUM) = NUM;

    desc  = (struct virtq_desc *)kalloc();
    avail = (struct virtq_avail *)kalloc();
    used  = (struct virtq_used *)kalloc();
    zero(desc, 4096); zero(avail, 4096); zero((void *)used, 4096);

    R32(MMIO_QUEUE_DESC_LO)   = (uint64)desc;
    R32(MMIO_QUEUE_DESC_HI)   = (uint64)desc >> 32;
    R32(MMIO_DRIVER_DESC_LO)  = (uint64)avail;
    R32(MMIO_DRIVER_DESC_HI)  = (uint64)avail >> 32;
    R32(MMIO_DEVICE_DESC_LO)  = (uint64)used;
    R32(MMIO_DEVICE_DESC_HI)  = (uint64)used >> 32;

    R32(MMIO_QUEUE_READY) = 1;
    status |= S_DRIVER_OK; R32(MMIO_STATUS) = status;

    used_seen = 0;
    disk_ok = 1;
    uart_puts("[ok] virtio-blk disk ready\n");
}

// 한 블록(512B) read/write. write=0이면 읽기. 0=성공, -1=실패.
int virtio_disk_rw(uint64 block, uint8 *buf, int write) {
    if (!disk_ok)
        return -1;  // 디스크 없음/초기화 실패
    breq.type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    breq.reserved = 0;
    breq.sector = block;  // BSIZE==섹터(512)라 블록==섹터

    // 디스크립터 체인: [0]요청헤더 → [1]데이터버퍼 → [2]상태바이트
    desc[0].addr = (uint64)&breq;
    desc[0].len = sizeof(breq);
    desc[0].flags = VRING_DESC_F_NEXT;
    desc[0].next = 1;

    desc[1].addr = (uint64)buf;
    desc[1].len = BSIZE;
    desc[1].flags = (write ? 0 : VRING_DESC_F_WRITE) | VRING_DESC_F_NEXT;
    desc[1].next = 2;

    bstatus = 0xff;
    desc[2].addr = (uint64)&bstatus;
    desc[2].len = 1;
    desc[2].flags = VRING_DESC_F_WRITE;  // 디바이스가 여기 결과를 쓴다
    desc[2].next = 0;

    // available 링에 체인 헤드(0)를 넣고 알림
    avail->ring[avail->idx % NUM] = 0;
    mb();
    avail->idx += 1;
    mb();
    R32(MMIO_QUEUE_NOTIFY) = 0;

    // used 링이 갱신될 때까지 폴링(used는 volatile이라 매번 메모리에서 재로드)
    while (used->idx == used_seen)
        ;
    mb();
    used_seen = used->idx;

    return (bstatus == 0) ? 0 : -1;
}
