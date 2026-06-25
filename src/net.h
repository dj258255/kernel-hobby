// net.h — virtio-net(MMIO) 드라이버 + 미니 네트워크 스택(이더넷/ARP/IP/UDP/DNS)
#ifndef NET_H
#define NET_H

#include "types.h"

// virtio-net 디바이스를 찾아 초기화한다(RX/TX 큐 설정, MAC 읽기). 0=성공, -1=없음.
int  net_init(void);

// 부팅 시 연결성 데모: 게이트웨이 MAC을 ARP로 알아내고, DNS로 도메인을 해석한다.
void net_demo(void);

// TCP 데모: 10.0.2.2:5599에 접속해 핸드셰이크→데이터→에코 수신→종료(셸 tcp 명령).
void net_tcp_demo(void);

#endif
