// net.c — virtio-net(MMIO) 드라이버 + 미니 네트워크 스택 (폴링 방식)
//
// QEMU virt의 virtio-mmio 슬롯(0x10001000~)을 훑어 device-id=1(net)을 찾는다.
// virtio-blk과 같은 전송 계층(디스크립터/avail/used 링)을 쓰되, 차이는:
//   (1) 큐가 둘 — 0=수신(RX), 1=송신(TX)
//   (2) 모든 패킷 앞에 12바이트 virtio_net_hdr가 붙는다(VERSION_1)
//
// 스택은 게이트웨이(10.0.2.2)/DNS(10.0.2.3)를 흉내내는 QEMU user 네트워킹(SLIRP)을
// 상대로 ARP·DNS까지 끝까지 동작한다. 인터럽트 없이 used 링을 폴링한다.

#include "net.h"
#include "types.h"
#include "kalloc.h"
#include "uart.h"

// ---- MMIO 레지스터(virtio.c와 동일 오프셋) ----
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
#define MMIO_CONFIG         0x100   // 디바이스별 설정공간(net: mac[6])

#define S_ACKNOWLEDGE 1
#define S_DRIVER      2
#define S_DRIVER_OK   4
#define S_FEATURES_OK 8

#define VIRTIO_NET_F_MAC   5    // config에 MAC이 있음
// 협상에서 끌 기능들(체크섬/오프로드/머지버퍼/제어큐 등)
#define VIRTIO_NET_F_CSUM        0
#define VIRTIO_NET_F_GUEST_CSUM  1
#define VIRTIO_NET_F_MRG_RXBUF   15
#define VIRTIO_NET_F_STATUS      16
#define VIRTIO_NET_F_CTRL_VQ     17
#define VIRTIO_RING_F_INDIRECT   28
#define VIRTIO_RING_F_EVENT_IDX  29

#define NUM 8          // 큐당 디스크립터 수
#define BUFSZ 2048     // RX 버퍼/프레임 버퍼 크기

struct virtq_desc { uint64 addr; uint32 len; uint16 flags; uint16 next; };
#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2
struct virtq_avail { uint16 flags; uint16 idx; uint16 ring[NUM]; uint16 unused; };
struct virtq_used_elem { uint32 id; uint32 len; };
struct virtq_used { uint16 flags; uint16 idx; struct virtq_used_elem ring[NUM]; };

// 12바이트 virtio_net_hdr(VERSION_1) — 송수신 버퍼 맨 앞에 붙는다.
struct virtio_net_hdr {
    uint8  flags;
    uint8  gso_type;
    uint16 hdr_len;
    uint16 gso_size;
    uint16 csum_start;
    uint16 csum_offset;
    uint16 num_buffers;
};
#define NETHDR 12

struct vq {
    struct virtq_desc  *desc;
    struct virtq_avail *avail;
    volatile struct virtq_used *used;
    uint16 used_seen;
};

static uint64 net_base;          // 발견한 virtio-net MMIO 베이스
static int    net_ok;
static struct vq rxq, txq;
static uint8 *rxbuf[NUM];        // RX 버퍼(디바이스가 채움)
static uint8 *txframe;           // TX 버퍼(hdr+프레임)
static uint8 my_mac[6];

#define NR32(off) (*(volatile uint32 *)(net_base + (off)))
#define mb() asm volatile("fence" ::: "memory")

// 우리 호스트 설정(SLIRP 기본값)
static const uint8 MY_IP[4] = {10, 0, 2, 15};
static const uint8 GW_IP[4] = {10, 0, 2, 2};
static const uint8 DNS_IP[4] = {10, 0, 2, 3};

static void zero(void *p, uint64 n) { char *d = p; while (n--) *d++ = 0; }
static void copy(void *d, const void *s, uint64 n) {
    uint8 *dd = d; const uint8 *ss = s; while (n--) *dd++ = *ss++;
}
static int eq4(const uint8 *a, const uint8 *b) {
    for (int i = 0; i < 4; i++) if (a[i] != b[i]) return 0;
    return 1;
}

// ---- 바이트오더/직렬화 헬퍼(네트워크=빅엔디안) ----
static void put16(uint8 *p, uint16 v) { p[0] = v >> 8; p[1] = v; }
static void put32(uint8 *p, uint32 v) { p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }
static uint16 get16(const uint8 *p) { return ((uint16)p[0] << 8) | p[1]; }
static uint32 get32(const uint8 *p) {
    return ((uint32)p[0]<<24)|((uint32)p[1]<<16)|((uint32)p[2]<<8)|p[3];
}

// 16비트 1의 보수 체크섬(IP/ICMP/UDP 공용)
static uint16 cksum(const uint8 *data, int len) {
    uint32 sum = 0;
    for (int i = 0; i + 1 < len; i += 2) sum += get16(data + i);
    if (len & 1) sum += (uint16)data[len - 1] << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return ~sum & 0xffff;
}

static void print_mac(const uint8 *m) {
    const char *hex = "0123456789abcdef";
    for (int i = 0; i < 6; i++) {
        if (i) uart_putc(':');
        uart_putc(hex[m[i] >> 4]); uart_putc(hex[m[i] & 0xf]);
    }
}
static void print_ip(const uint8 *ip) {
    for (int i = 0; i < 4; i++) { if (i) uart_putc('.'); uart_dec(ip[i]); }
}

// ---- 큐 하나 설정(QUEUE_SEL → 크기 → 링 주소 → READY) ----
static void setup_queue(int qidx, struct vq *q) {
    NR32(MMIO_QUEUE_SEL) = qidx;
    q->desc  = (struct virtq_desc *)kalloc();
    q->avail = (struct virtq_avail *)kalloc();
    q->used  = (struct virtq_used *)kalloc();
    zero(q->desc, 4096); zero(q->avail, 4096); zero((void *)q->used, 4096);
    NR32(MMIO_QUEUE_NUM) = NUM;
    NR32(MMIO_QUEUE_DESC_LO)  = (uint64)q->desc;
    NR32(MMIO_QUEUE_DESC_HI)  = (uint64)q->desc >> 32;
    NR32(MMIO_DRIVER_DESC_LO) = (uint64)q->avail;
    NR32(MMIO_DRIVER_DESC_HI) = (uint64)q->avail >> 32;
    NR32(MMIO_DEVICE_DESC_LO) = (uint64)q->used;
    NR32(MMIO_DEVICE_DESC_HI) = (uint64)q->used >> 32;
    NR32(MMIO_QUEUE_READY) = 1;
    q->used_seen = 0;
}

int net_init(void) {
    // virtio-mmio 8슬롯을 훑어 device-id=1(net)을 찾는다.
    for (int i = 0; i < 8; i++) {
        uint64 base = 0x10001000L + (uint64)i * 0x1000;
        if (*(volatile uint32 *)(base + MMIO_MAGIC) != 0x74726976) continue;
        if (*(volatile uint32 *)(base + MMIO_VERSION) != 2) continue;
        if (*(volatile uint32 *)(base + MMIO_DEVICE_ID) != 1) continue;
        net_base = base;
        break;
    }
    if (net_base == 0) { uart_puts("[net] no virtio-net device\n"); return -1; }

    uint32 status = 0;
    NR32(MMIO_STATUS) = status;
    status |= S_ACKNOWLEDGE; NR32(MMIO_STATUS) = status;
    status |= S_DRIVER;      NR32(MMIO_STATUS) = status;

    // 하위 32비트: MAC만 받고 체크섬/머지버퍼/제어큐/이벤트idx는 끈다.
    NR32(MMIO_DEVICE_FEAT_SEL) = 0;
    uint32 lo = NR32(MMIO_DEVICE_FEAT);
    lo &= (1u << VIRTIO_NET_F_MAC);  // MAC만 유지
    NR32(MMIO_DRIVER_FEAT_SEL) = 0;
    NR32(MMIO_DRIVER_FEAT) = lo;
    // 상위 32비트: VERSION_1(비트32)만 수락(헤더 12바이트 고정).
    NR32(MMIO_DEVICE_FEAT_SEL) = 1;
    uint32 hi = NR32(MMIO_DEVICE_FEAT) & 0x1;  // 비트0=VERSION_1
    NR32(MMIO_DRIVER_FEAT_SEL) = 1;
    NR32(MMIO_DRIVER_FEAT) = hi;

    status |= S_FEATURES_OK; NR32(MMIO_STATUS) = status;
    if (!(NR32(MMIO_STATUS) & S_FEATURES_OK)) {
        uart_puts("[net] FEATURES_OK rejected\n"); return -1;
    }

    // MAC 읽기(config 0x100)
    for (int i = 0; i < 6; i++)
        my_mac[i] = *(volatile uint8 *)(net_base + MMIO_CONFIG + i);

    setup_queue(0, &rxq);   // RX
    setup_queue(1, &txq);   // TX

    // RX 버퍼 NUM개를 디바이스에 제공(전부 device-writable로 avail에 넣는다)
    for (int i = 0; i < NUM; i++) {
        rxbuf[i] = (uint8 *)kalloc();
        rxq.desc[i].addr = (uint64)rxbuf[i];
        rxq.desc[i].len = BUFSZ;
        rxq.desc[i].flags = VRING_DESC_F_WRITE;
        rxq.desc[i].next = 0;
        rxq.avail->ring[i] = i;
    }
    mb();
    rxq.avail->idx = NUM;
    mb();

    txframe = (uint8 *)kalloc();

    status |= S_DRIVER_OK; NR32(MMIO_STATUS) = status;
    NR32(MMIO_QUEUE_NOTIFY) = 0;   // RX 큐: 버퍼 제공 알림

    net_ok = 1;
    uart_puts("[ok] virtio-net ready, mac ");
    print_mac(my_mac);
    uart_putc('\n');
    return 0;
}

// 이더넷 프레임 송신(payload=ethertype 이후 데이터). dst MAC + ethertype 지정.
// 반환: 0=성공.
static int eth_tx(const uint8 *dst, uint16 ethertype, const uint8 *payload, int plen) {
    if (!net_ok) return -1;
    zero(txframe, NETHDR);                 // virtio_net_hdr = 0 (오프로드 없음)
    uint8 *eth = txframe + NETHDR;
    copy(eth, dst, 6);
    copy(eth + 6, my_mac, 6);
    put16(eth + 12, ethertype);
    copy(eth + 14, payload, plen);
    int total = NETHDR + 14 + plen;

    txq.desc[0].addr = (uint64)txframe;
    txq.desc[0].len = total;
    txq.desc[0].flags = 0;                 // device가 읽음(read-only)
    txq.desc[0].next = 0;
    txq.avail->ring[txq.avail->idx % NUM] = 0;
    mb();
    txq.avail->idx += 1;
    mb();
    NR32(MMIO_QUEUE_NOTIFY) = 1;           // TX 큐
    // 송신 완료 폴링
    for (uint64 spin = 0; txq.used->idx == txq.used_seen; spin++)
        if (spin > 200000000ULL) return -1;
    txq.used_seen = txq.used->idx;
    return 0;
}

// RX 한 프레임 폴링. 도착하면 프레임(이더넷 헤더부터)을 out에 복사하고 길이 반환.
// 없으면 0, 디바이스 없음 -1. 처리한 버퍼는 디바이스에 되돌린다.
static int eth_rx(uint8 *out, int max) {
    if (!net_ok) return -1;
    if (rxq.used->idx == rxq.used_seen) return 0;   // 새 패킷 없음
    mb();
    struct virtq_used_elem *e = (struct virtq_used_elem *)
        &rxq.used->ring[rxq.used_seen % NUM];
    uint32 id = e->id;
    uint32 len = e->len;                  // hdr + 프레임 바이트 수
    int flen = 0;
    if (len > NETHDR) {
        flen = len - NETHDR;
        if (flen > max) flen = max;
        copy(out, rxbuf[id] + NETHDR, flen);   // virtio_net_hdr 건너뛰고 복사
    }
    rxq.used_seen += 1;
    // 버퍼를 다시 디바이스에 제공
    rxq.avail->ring[rxq.avail->idx % NUM] = id;
    mb();
    rxq.avail->idx += 1;
    mb();
    NR32(MMIO_QUEUE_NOTIFY) = 0;
    return flen;
}

// ---- ARP ----
#define ETH_ARP  0x0806
#define ETH_IP   0x0800

// ARP 요청 전송(누가 target_ip를 갖고 있나).
static void arp_request(const uint8 *target_ip) {
    uint8 bcast[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
    uint8 a[28];
    put16(a + 0, 1);            // htype=Ethernet
    put16(a + 2, ETH_IP);       // ptype=IPv4
    a[4] = 6; a[5] = 4;         // hlen, plen
    put16(a + 6, 1);            // oper=request
    copy(a + 8, my_mac, 6);
    copy(a + 14, MY_IP, 4);
    zero(a + 18, 6);            // target mac=0
    copy(a + 24, target_ip, 4);
    eth_tx(bcast, ETH_ARP, a, 28);
}

// 들어온 프레임이 우리 IP에 대한 ARP 요청이면 응답한다.
static void arp_maybe_reply(const uint8 *fr, int len) {
    if (len < 14 + 28 || get16(fr + 12) != ETH_ARP) return;
    const uint8 *a = fr + 14;
    if (get16(a + 6) != 1) return;          // request만
    if (!eq4(a + 24, MY_IP)) return;        // 우리 IP를 묻는 게 아님
    uint8 r[28];
    put16(r + 0, 1); put16(r + 2, ETH_IP);
    r[4] = 6; r[5] = 4; put16(r + 6, 2);    // oper=reply
    copy(r + 8, my_mac, 6); copy(r + 14, MY_IP, 4);
    copy(r + 18, a + 8, 6); copy(r + 24, a + 14, 4);  // 요청자에게
    eth_tx(a + 8, ETH_ARP, r, 28);
}

// target_ip의 MAC을 ARP로 알아낸다. 0=성공(out_mac 채움), -1=타임아웃.
static int arp_resolve(const uint8 *target_ip, uint8 *out_mac) {
    arp_request(target_ip);
    uint8 fr[BUFSZ];
    for (uint64 spin = 0; spin < 400000000ULL; spin++) {
        int len = eth_rx(fr, sizeof(fr));
        if (len <= 0) continue;
        if (get16(fr + 12) == ETH_ARP && len >= 14 + 28) {
            const uint8 *a = fr + 14;
            if (get16(a + 6) == 2 && eq4(a + 14, target_ip)) {  // reply from target
                copy(out_mac, a + 8, 6);
                return 0;
            }
            arp_maybe_reply(fr, len);   // 그 사이 우리에게 온 요청도 처리
        }
    }
    return -1;
}

// ---- IP/UDP 송신 ----
// payload를 UDP로 감싸 dst_ip:dst_port로 보낸다(이더넷 dst=dst_mac).
static int udp_tx(const uint8 *dst_mac, const uint8 *dst_ip,
                  uint16 src_port, uint16 dst_port,
                  const uint8 *payload, int plen) {
    uint8 pkt[BUFSZ];
    int udplen = 8 + plen;
    int iplen = 20 + udplen;
    // IP 헤더
    pkt[0] = 0x45; pkt[1] = 0;
    put16(pkt + 2, iplen);
    put16(pkt + 4, 0);                 // id
    put16(pkt + 6, 0);                 // flags/frag
    pkt[8] = 64;                       // ttl
    pkt[9] = 17;                       // proto=UDP
    put16(pkt + 10, 0);                // 체크섬 자리
    copy(pkt + 12, MY_IP, 4);
    copy(pkt + 16, dst_ip, 4);
    put16(pkt + 10, cksum(pkt, 20));   // IP 헤더 체크섬
    // UDP 헤더 + 의사헤더 체크섬(SLIRP DNS는 유효 체크섬을 요구할 수 있음)
    put16(pkt + 20, src_port);
    put16(pkt + 22, dst_port);
    put16(pkt + 24, udplen);
    put16(pkt + 26, 0);
    copy(pkt + 28, payload, plen);
    // 의사헤더(src+dst+0+proto+udplen) + UDP 세그먼트 합산
    uint32 sum = 0;
    sum += get16(MY_IP) + get16(MY_IP + 2);
    sum += get16(dst_ip) + get16(dst_ip + 2);
    sum += 17 + udplen;
    for (int i = 0; i + 1 < udplen; i += 2) sum += get16(pkt + 20 + i);
    if (udplen & 1) sum += (uint16)pkt[20 + udplen - 1] << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    uint16 c = ~sum & 0xffff;
    put16(pkt + 26, c ? c : 0xffff);
    return eth_tx(dst_mac, ETH_IP, pkt, iplen);
}

// IP/UDP 응답 한 개를 기다린다. UDP payload를 out에 복사, 길이 반환. -1=타임아웃.
static int udp_rx(uint16 want_port, uint8 *out, int max) {
    uint8 fr[BUFSZ];
    for (uint64 spin = 0; spin < 400000000ULL; spin++) {
        int len = eth_rx(fr, sizeof(fr));
        if (len <= 0) { if (len < 0) return -1; continue; }
        if (get16(fr + 12) == ETH_ARP) { arp_maybe_reply(fr, len); continue; }
        if (get16(fr + 12) != ETH_IP || len < 14 + 28) continue;
        const uint8 *ip = fr + 14;
        if ((ip[0] >> 4) != 4 || ip[9] != 17) continue;   // IPv4 + UDP
        int ihl = (ip[0] & 0xf) * 4;
        const uint8 *udp = ip + ihl;
        if (get16(udp + 2) != want_port) continue;        // 우리 포트로 온 것만
        int ulen = get16(udp + 4);
        int plen = ulen - 8;
        if (plen < 0) continue;
        if (plen > max) plen = max;
        copy(out, udp + 8, plen);
        return plen;
    }
    return -1;
}

// ---- DNS ----
// 도메인을 DNS 질의로 만들어 보내고 A 레코드(IPv4)를 받아 out_ip에 담는다. 0=성공.
static int dns_query(const uint8 *dns_mac, const char *name, uint8 *out_ip) {
    uint8 q[256];
    uint16 id = 0x1234;
    put16(q + 0, id);
    put16(q + 2, 0x0100);   // flags: 재귀 요청(RD)
    put16(q + 4, 1);        // qdcount=1
    put16(q + 6, 0); put16(q + 8, 0); put16(q + 10, 0);
    // QNAME: 라벨 길이+문자, 0으로 종료
    int p = 12;
    int i = 0;
    while (name[i]) {
        int start = i;
        while (name[i] && name[i] != '.') i++;
        int l = i - start;
        q[p++] = (uint8)l;
        for (int k = 0; k < l; k++) q[p++] = (uint8)name[start + k];
        if (name[i] == '.') i++;
    }
    q[p++] = 0;             // 루트 라벨
    put16(q + p, 1); p += 2;  // QTYPE=A
    put16(q + p, 1); p += 2;  // QCLASS=IN

    if (udp_tx(dns_mac, DNS_IP, 0x9999, 53, q, p) != 0) return -1;

    uint8 r[BUFSZ];
    int rlen = udp_rx(0x9999, r, sizeof(r));
    if (rlen < 12) return -1;
    int an = get16(r + 6);          // answer count
    if (an < 1) return -1;

    // 질문 섹션 건너뛰기
    int off = 12;
    while (off < rlen && r[off] != 0) off += r[off] + 1;  // QNAME
    off += 1 + 4;                   // 0 + QTYPE + QCLASS
    // 답변들에서 첫 A 레코드를 찾는다
    for (int a = 0; a < an && off + 12 <= rlen; a++) {
        // NAME: 압축 포인터(0xC0..)면 2바이트, 아니면 라벨열
        if ((r[off] & 0xc0) == 0xc0) off += 2;
        else { while (off < rlen && r[off] != 0) off += r[off] + 1; off += 1; }
        if (off + 10 > rlen) return -1;
        int type = get16(r + off);
        int rdlen = get16(r + off + 8);
        off += 10;
        if (type == 1 && rdlen == 4) {     // A 레코드
            copy(out_ip, r + off, 4);
            return 0;
        }
        off += rdlen;
    }
    return -1;
}

// ---- ICMP echo(ping) ----
// dst_ip로 echo request를 보내고 reply를 기다린다. 0=성공(왕복), -1=타임아웃.
// 게이트웨이 ping은 SLIRP가 내부에서 응답하므로 외부망 없이도 IP 계층을 검증한다.
static int icmp_ping(const uint8 *next_mac, const uint8 *dst_ip) {
    uint8 icmp[8 + 32];
    icmp[0] = 8; icmp[1] = 0;          // type=echo request, code=0
    put16(icmp + 2, 0);                // 체크섬 자리
    put16(icmp + 4, 0xabcd);           // id
    put16(icmp + 6, 1);                // seq
    for (int i = 0; i < 32; i++) icmp[8 + i] = (uint8)('a' + i % 26);
    int ilen = 8 + 32;
    put16(icmp + 2, cksum(icmp, ilen));

    uint8 pkt[BUFSZ];
    pkt[0] = 0x45; pkt[1] = 0;
    put16(pkt + 2, 20 + ilen);
    put16(pkt + 4, 0); put16(pkt + 6, 0);
    pkt[8] = 64; pkt[9] = 1;           // ttl, proto=ICMP
    put16(pkt + 10, 0);
    copy(pkt + 12, MY_IP, 4);
    copy(pkt + 16, dst_ip, 4);
    put16(pkt + 10, cksum(pkt, 20));
    copy(pkt + 20, icmp, ilen);
    if (eth_tx(next_mac, ETH_IP, pkt, 20 + ilen) != 0) return -1;

    uint8 fr[BUFSZ];
    for (uint64 spin = 0; spin < 400000000ULL; spin++) {
        int len = eth_rx(fr, sizeof(fr));
        if (len <= 0) { if (len < 0) return -1; continue; }
        if (get16(fr + 12) == ETH_ARP) { arp_maybe_reply(fr, len); continue; }
        if (get16(fr + 12) != ETH_IP || len < 14 + 28) continue;
        const uint8 *ip = fr + 14;
        if (ip[9] != 1) continue;                 // ICMP
        int ihl = (ip[0] & 0xf) * 4;
        const uint8 *ic = ip + ihl;
        if (ic[0] == 0 && eq4(ip + 12, dst_ip))   // echo reply from dst
            return 0;
    }
    return -1;
}

// ---- TCP (능동 개방 클라이언트) ----
#define IP_TCP 6
#define TH_FIN 0x01
#define TH_SYN 0x02
#define TH_RST 0x04
#define TH_PSH 0x08
#define TH_ACK 0x10

// TCP 세그먼트 하나 전송(IP+TCP 헤더 빌드, 의사헤더 체크섬).
static int tcp_send(const uint8 *mac, const uint8 *dip, uint16 sport, uint16 dport,
                    uint32 seq, uint32 ack, uint8 flags, const uint8 *data, int dlen) {
    uint8 pkt[BUFSZ];
    int tcplen = 20 + dlen;
    int iplen = 20 + tcplen;
    pkt[0] = 0x45; pkt[1] = 0;
    put16(pkt + 2, iplen);
    put16(pkt + 4, 0); put16(pkt + 6, 0);
    pkt[8] = 64; pkt[9] = IP_TCP; put16(pkt + 10, 0);
    copy(pkt + 12, MY_IP, 4); copy(pkt + 16, dip, 4);
    put16(pkt + 10, cksum(pkt, 20));
    uint8 *t = pkt + 20;
    put16(t + 0, sport); put16(t + 2, dport);
    put32(t + 4, seq); put32(t + 8, ack);
    t[12] = (5 << 4); t[13] = flags;       // data offset=5(20B), flags
    put16(t + 14, 64240);                  // window
    put16(t + 16, 0); put16(t + 18, 0);    // checksum, urgent
    for (int i = 0; i < dlen; i++) t[20 + i] = data[i];
    // TCP 체크섬(의사헤더 포함)
    uint32 sum = 0;
    sum += get16(MY_IP) + get16(MY_IP + 2) + get16(dip) + get16(dip + 2);
    sum += IP_TCP + tcplen;
    for (int i = 0; i + 1 < tcplen; i += 2) sum += get16(t + i);
    if (tcplen & 1) sum += (uint16)t[tcplen - 1] << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    put16(t + 16, ~sum & 0xffff);
    return eth_tx(mac, ETH_IP, pkt, iplen);
}

// 우리 포트(want_port)로 온 TCP 세그먼트 하나 수신. payload 길이 반환(-1=타임아웃).
// seq/ack/flags와 상대(MAC/IP/포트)를 채운다. 그 사이 온 ARP 요청은 응답.
static int tcp_recv(uint16 want_port, uint8 *pmac, uint8 *pip, uint16 *pport,
                    uint32 *seq, uint32 *ack, uint8 *flags, uint8 *out, int max) {
    uint8 fr[BUFSZ];
    for (uint64 spin = 0; spin < 800000000ULL; spin++) {
        int len = eth_rx(fr, sizeof(fr));
        if (len <= 0) { if (len < 0) return -1; continue; }
        if (get16(fr + 12) == ETH_ARP) { arp_maybe_reply(fr, len); continue; }
        if (get16(fr + 12) != ETH_IP) continue;
        const uint8 *ip = fr + 14;
        if ((ip[0] >> 4) != 4 || ip[9] != IP_TCP) continue;
        int ihl = (ip[0] & 0xf) * 4;
        const uint8 *t = ip + ihl;
        if (get16(t + 2) != want_port) continue;     // 우리 포트로 온 것만
        if (pmac) copy(pmac, fr + 6, 6);             // 상대 MAC(이더넷 src)
        if (pip) copy(pip, ip + 12, 4);              // 상대 IP(src)
        if (pport) *pport = get16(t + 0);            // 상대 포트(src)
        *seq = get32(t + 4); *ack = get32(t + 8); *flags = t[13];
        int doff = (t[12] >> 4) * 4;
        int dlen = get16(ip + 2) - ihl - doff;
        if (dlen < 0) dlen = 0;
        if (dlen > max) dlen = max;
        copy(out, t + doff, dlen);
        return dlen;
    }
    return -1;
}

// 데모(수동 개방=서버): :5599에서 한 연결을 받아 3-way 핸드셰이크 → 데이터 수신 →
// 에코 송신 → 종료. 호스트가 hostfwd로 접속한다(SLIRP가 호스트→게스트로 넘겨줌).
void net_tcp_demo(void) {
    if (!net_ok) return;
    uint16 myport = 5599;
    uint8 pmac[6], pip[4]; uint16 pport = 0;
    uint32 rseq, rack; uint8 fl; uint8 buf[BUFSZ];

    uart_puts("[tcp] listening on :5599 (waiting for host connect) ... ");

    // 1) SYN 대기
    int n;
    for (;;) {
        n = tcp_recv(myport, pmac, pip, &pport, &rseq, &rack, &fl, buf, sizeof(buf));
        if (n < 0) { uart_puts("timeout (no connect)\n"); return; }
        if (fl & TH_SYN) break;
    }
    uint32 cliseq = rseq;
    uart_puts("SYN from "); print_ip(pip); uart_putc('\n');

    // 2) SYN-ACK (우리 ISN=20000, SYN이 seq 1 소비)
    uint32 myseq = 20000;
    tcp_send(pmac, pip, myport, pport, myseq, cliseq + 1, TH_SYN | TH_ACK, 0, 0);
    myseq += 1;
    uint32 cliack = cliseq + 1;   // 상대로부터 다음에 기대하는 seq

    // 3) 핸드셰이크 ACK + 데이터 수신(데이터가 올 때까지)
    for (int tries = 0; tries < 12; tries++) {
        n = tcp_recv(myport, 0, 0, 0, &rseq, &rack, &fl, buf, sizeof(buf));
        if (n < 0) { uart_puts("[tcp] no data\n"); return; }
        if (n > 0) {
            uart_puts("[tcp] recv: ");
            for (int i = 0; i < n; i++) uart_putc((char)buf[i]);
            if (buf[n - 1] != '\n') uart_putc('\n');
            cliack = rseq + n;
            // 4) 에코 송신(받은 데이터를 그대로) + ACK
            tcp_send(pmac, pip, myport, pport, myseq, cliack, TH_PSH | TH_ACK, buf, n);
            myseq += n;
            break;
        }
        if (fl & TH_FIN) { cliack = rseq + 1; break; }
    }

    // 5) 종료: FIN 보내고 상대 ACK/FIN 처리
    tcp_send(pmac, pip, myport, pport, myseq, cliack, TH_FIN | TH_ACK, 0, 0);
    n = tcp_recv(myport, 0, 0, 0, &rseq, &rack, &fl, buf, sizeof(buf));
    if (n >= 0 && (fl & TH_FIN))
        tcp_send(pmac, pip, myport, pport, myseq + 1, rseq + 1, TH_ACK, 0, 0);
    uart_puts("[ok] tcp: accept + handshake + echo + close done\n");
}

// ---- 부팅 데모 ----
void net_demo(void) {
    if (!net_ok) return;

    // 1) ARP: 게이트웨이 MAC 해석(드라이버 TX/RX + 이더넷 + ARP 검증)
    uart_puts("[net] ARP who-has ");
    print_ip(GW_IP); uart_puts(" (gateway) ... ");
    uint8 gwmac[6];
    if (arp_resolve(GW_IP, gwmac) != 0) { uart_puts("timeout\n"); return; }
    uart_puts("is-at "); print_mac(gwmac); uart_putc('\n');

    // 2) ICMP: 게이트웨이 ping(IP 계층 + 체크섬 검증, SLIRP 내부 응답 → 오프라인 OK)
    uart_puts("[net] ping "); print_ip(GW_IP); uart_puts(" ... ");
    if (icmp_ping(gwmac, GW_IP) == 0) uart_puts("reply\n");
    else uart_puts("timeout\n");

    // 3) DNS: 도메인 해석(UDP 송수신 + DNS 파싱). 외부망 차단 환경에선 timeout일 수 있음.
    uint8 dnsmac[6];
    if (arp_resolve(DNS_IP, dnsmac) != 0) {
        uart_puts("[net] cannot reach DNS server\n");
    } else {
        const char *host = "example.com";
        uart_puts("[net] DNS A "); uart_puts(host); uart_puts(" ... ");
        uint8 ip[4];
        if (dns_query(dnsmac, host, ip) != 0)
            uart_puts("timeout (no outbound DNS?)\n");
        else { print_ip(ip); uart_putc('\n'); }
    }
    uart_puts("[ok] networking up: virtio-net + ARP + ICMP + IP/UDP\n");
}
