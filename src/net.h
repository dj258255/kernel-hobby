// net.h — virtio-net(MMIO) 드라이버 + 미니 네트워크 스택(이더넷/ARP/IP/UDP/DNS)
#ifndef NET_H
#define NET_H

#include "types.h"

// virtio-net 디바이스를 찾아 초기화한다(RX/TX 큐 설정, MAC 읽기). 0=성공, -1=없음.
int  net_init(void);

// 부팅 시 연결성 데모: 게이트웨이 MAC을 ARP로 알아내고, DNS로 도메인을 해석한다.
void net_demo(void);

#endif
