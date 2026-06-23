// virtio.h — virtio-blk(MMIO) 디스크 드라이버
#ifndef VIRTIO_H
#define VIRTIO_H

#include "types.h"

void virtio_disk_init(void);                       // 디바이스 초기화(0이면 성공/존재)
int  virtio_disk_rw(uint64 block, uint8 *buf, int write);  // 한 블록(512B) read/write, 0=성공

#endif
