// fs.c — 파일시스템 (읽기 + 생성/삭제 + 저널링 + 블록 재사용)
//
// 마운트: 슈퍼블록 확인 → 로그 복구(replay) → 디렉터리 로드 → 빈 블록 비트맵 구성.
// 쓰기는 모두 "트랜잭션"으로 묶인다 — 데이터/디렉터리/슈퍼블록을 로그에 모은 뒤
//   커밋 표시를 찍고, 그제서야 제자리에 설치(install). 크래시가 나도 전부 반영되거나
//   전혀 반영되지 않아(원자성), 디스크가 절반만 쓰인 상태로 깨지지 않는다.
//
// 주의: 디스크 버퍼는 static(커널 .bss=식별 매핑)이어야 virtio DMA 주소가 맞다.

#include "fs.h"
#include "fsformat.h"
#include "virtio.h"
#include "uart.h"
#include "types.h"

static struct fs_dirent dir[MAXFILES];
static int      nfiles;
static int      mounted;
static uint32   next_free;          // 할당 힌트(실제 할당은 비트맵이 담당)
static uint32   total_blocks;       // 디스크 전체 블록 수
static uint8    fsbuf[BSIZE];       // 디스크 I/O 버퍼(커널 메모리)

#define MAXBLK 8192                  // 비트맵 상한(블록 수)
static uint8    used[MAXBLK];        // 빈 블록 비트맵(메모리). 1=사용중

// 진행 중 트랜잭션의 목적지 블록들(메모리). log_write가 채우고 log_commit이 설치.
static uint32   log_blk[LOGN];
static int      log_n;

static int streq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

// ---- 저널링(write-ahead log) ----

// 로그 헤더(블록 LOGHDR)를 쓴다. committed/n/blk[]을 한 블록에 직렬화.
static void write_loghdr(uint32 committed, uint32 n, const uint32 *blks) {
    struct fs_loghdr h;
    h.committed = committed;
    h.n = n;
    for (int i = 0; i < LOGN; i++) h.blk[i] = (i < (int)n) ? blks[i] : 0;
    uint8 *p = (uint8 *)&h;
    for (int j = 0; j < BSIZE; j++) fsbuf[j] = (j < (int)sizeof(h)) ? p[j] : 0;
    virtio_disk_rw(LOGHDR, fsbuf, 1);
}

static void log_begin(void) { log_n = 0; }

// 목적지 dst로 갈 데이터를 로그 데이터 블록에 먼저 적어둔다(아직 제자리엔 안 씀).
static void log_write(uint32 dst, const uint8 *data) {
    if (log_n >= LOGN) return;                  // 트랜잭션이 로그보다 크면 무시(상한)
    for (int j = 0; j < BSIZE; j++) fsbuf[j] = data[j];
    virtio_disk_rw(LOGSTART + log_n, fsbuf, 1); // 로그에 기록
    log_blk[log_n] = dst;
    log_n++;
}

// 로그 데이터 블록들을 각자의 목적지로 복사(install).
static void log_install(int n, const uint32 *blks) {
    for (int i = 0; i < n; i++) {
        virtio_disk_rw(LOGSTART + i, fsbuf, 0);  // 로그에서 읽어
        virtio_disk_rw(blks[i], fsbuf, 1);       // 제자리에 쓴다
    }
}

// 커밋: (1) 헤더에 committed=1 기록 → 원자적 커밋 포인트, (2) 설치, (3) 헤더 비움.
static void log_commit(void) {
    if (log_n == 0) return;
    write_loghdr(1, log_n, log_blk);   // 커밋(이 순간 이후엔 크래시 나도 복구가 완성)
    log_install(log_n, log_blk);       // 제자리에 설치
    write_loghdr(0, 0, 0);             // 로그 비움(설치 끝)
    log_n = 0;
}

// 마운트 시 복구: 커밋만 되고(=committed=1) 설치가 끝나지 않은 트랜잭션을 replay.
static void recover(void) {
    virtio_disk_rw(LOGHDR, fsbuf, 0);
    struct fs_loghdr *h = (struct fs_loghdr *)fsbuf;
    if (!h->committed) return;
    uint32 n = h->n;
    if (n > LOGN) n = LOGN;
    uint32 blks[LOGN];
    for (uint32 i = 0; i < n; i++) blks[i] = h->blk[i];   // fsbuf 재사용 전에 추출
    log_install((int)n, blks);
    write_loghdr(0, 0, 0);
    uart_puts("[fs] recovered a committed log transaction\n");
}

// ---- 빈 블록 비트맵 ----

static void build_bitmap(void) {
    for (uint32 i = 0; i < MAXBLK; i++) used[i] = 0;
    used[0] = used[DIRBLOCK] = used[LOGHDR] = 1;           // 메타데이터 블록
    for (int i = 0; i < LOGN; i++) used[LOGSTART + i] = 1; // 로그 영역
    for (int f = 0; f < nfiles; f++) {                     // 각 파일의 데이터 블록
        int nb = (dir[f].size + BSIZE - 1) / BSIZE;
        for (int k = 0; k < nb; k++) {
            uint32 b = dir[f].start + k;
            if (b < MAXBLK) used[b] = 1;
        }
    }
}

// 연속 nblocks개의 빈 블록을 first-fit으로 찾는다. 없으면 -1.
static int balloc_contig(int nblocks) {
    if (nblocks == 0) return DATASTART;
    for (uint32 s = DATASTART; s + nblocks <= total_blocks && s + nblocks <= MAXBLK; s++) {
        int ok = 1;
        for (int k = 0; k < nblocks; k++)
            if (used[s + k]) { ok = 0; break; }
        if (ok) return (int)s;
    }
    return -1;
}

// ---- 마운트 ----

void fs_init(void) {
    if (virtio_disk_rw(0, fsbuf, 0) != 0) {
        uart_puts("[fs] cannot read superblock\n");
        return;
    }
    struct fs_superblock *sb = (struct fs_superblock *)fsbuf;
    if (sb->magic != FS_MAGIC) {
        uart_puts("[fs] bad magic (no filesystem)\n");
        return;
    }
    total_blocks = sb->total_blocks;
    if (total_blocks == 0 || total_blocks > MAXBLK) total_blocks = MAXBLK;

    recover();                       // 크래시 복구(있으면 replay)

    virtio_disk_rw(0, fsbuf, 0);     // 복구 후 슈퍼블록 재로딩
    sb = (struct fs_superblock *)fsbuf;
    nfiles = (int)sb->nfiles;
    if (nfiles > MAXFILES) nfiles = MAXFILES;
    next_free = sb->next_free;

    virtio_disk_rw(DIRBLOCK, fsbuf, 0);
    struct fs_dirent *d = (struct fs_dirent *)fsbuf;
    for (int i = 0; i < MAXFILES; i++)
        dir[i] = d[i];

    build_bitmap();

    mounted = 1;
    uart_puts("[ok] filesystem mounted (journaled): ");
    uart_dec(nfiles);
    uart_puts(" files\n");
}

// ---- 읽기 경로 ----

void fs_ls(void) {
    if (!mounted) { uart_puts("ls: no filesystem\n"); return; }
    for (int i = 0; i < nfiles; i++) {
        uart_puts("  ");
        uart_puts(dir[i].name);
        uart_puts("  (");
        uart_dec(dir[i].size);
        uart_puts(" bytes)\n");
    }
}

int fs_read(const char *name, unsigned char *buf, int max) {
    if (!mounted) return -1;
    for (int i = 0; i < nfiles; i++) {
        if (!streq(dir[i].name, name))
            continue;
        uint32 size = dir[i].size;
        if ((int)size > max) return -1;
        uint32 blk = dir[i].start, off = 0, remaining = size;
        while (remaining > 0) {
            virtio_disk_rw(blk, fsbuf, 0);
            uint32 n = remaining > BSIZE ? BSIZE : remaining;
            for (uint32 j = 0; j < n; j++)
                buf[off + j] = fsbuf[j];
            off += n; remaining -= n; blk++;
        }
        return (int)size;
    }
    return -1;
}

int fs_stat(const char *name, unsigned *start_block, unsigned *size) {
    if (!mounted) return -1;
    for (int i = 0; i < nfiles; i++) {
        if (!streq(dir[i].name, name))
            continue;
        *start_block = dir[i].start;
        *size = dir[i].size;
        return 0;
    }
    return -1;
}

void fs_read_page(unsigned start_block, unsigned size, unsigned offset, unsigned char *buf) {
    for (int i = 0; i < 4096; i++)
        buf[i] = 0;
    unsigned blk = start_block + offset / BSIZE;
    unsigned done = 0;
    while (done < 4096 && offset + done < size) {
        virtio_disk_rw(blk, fsbuf, 0);
        unsigned n = size - (offset + done);
        if (n > BSIZE) n = BSIZE;
        for (unsigned j = 0; j < n; j++)
            buf[done + j] = fsbuf[j];
        done += BSIZE;
        blk++;
    }
}

void fs_cat(const char *name) {
    if (!mounted) { uart_puts("cat: no filesystem\n"); return; }
    for (int i = 0; i < nfiles; i++) {
        if (!streq(dir[i].name, name))
            continue;
        uint32 remaining = dir[i].size;
        uint32 blk = dir[i].start;
        while (remaining > 0) {
            virtio_disk_rw(blk, fsbuf, 0);
            uint32 n = remaining > BSIZE ? BSIZE : remaining;
            for (uint32 j = 0; j < n; j++)
                uart_putc((char)fsbuf[j]);
            remaining -= n;
            blk++;
        }
        return;
    }
    uart_puts("cat: no such file: ");
    uart_puts(name);
    uart_putc('\n');
}

// ---- 쓰기 경로(트랜잭션) ----

// 디렉터리 블록 이미지를 트랜잭션 로그에 추가.
static void log_dir(void) {
    uint8 *db = (uint8 *)dir;
    uint8 tmp[BSIZE];
    for (int j = 0; j < BSIZE; j++) tmp[j] = (j < (int)sizeof(dir)) ? db[j] : 0;
    log_write(DIRBLOCK, tmp);
}

// 슈퍼블록 이미지를 트랜잭션 로그에 추가.
static void log_super(void) {
    struct fs_superblock sb = { FS_MAGIC, (unsigned)nfiles, next_free, total_blocks };
    uint8 *sp = (uint8 *)&sb;
    uint8 tmp[BSIZE];
    for (int j = 0; j < BSIZE; j++) tmp[j] = (j < (int)sizeof(sb)) ? sp[j] : 0;
    log_write(0, tmp);
}

// 파일 생성: 데이터+디렉터리+슈퍼블록을 한 트랜잭션으로 원자적으로 쓴다.
// 0=성공, -1=실패(공간 없음/중복/마운트 안 됨).
int fs_create(const char *name, const char *data, int size) {
    if (!mounted || nfiles >= MAXFILES || size < 0)
        return -1;
    for (int i = 0; i < nfiles; i++)
        if (streq(dir[i].name, name))
            return -1;                      // 이미 존재(덮어쓰기 미지원)

    int nblocks = (size + BSIZE - 1) / BSIZE;
    int start = balloc_contig(nblocks);     // 비트맵에서 빈 자리 찾기(재사용 가능)
    if (start < 0) return -1;               // 공간 없음

    log_begin();
    // 1) 데이터 블록들 → 로그
    int off = 0;
    uint8 tmp[BSIZE];
    for (int b = 0; b < nblocks; b++) {
        for (int j = 0; j < BSIZE; j++) tmp[j] = 0;
        int n = size - off; if (n > BSIZE) n = BSIZE;
        for (int j = 0; j < n; j++) tmp[j] = (uint8)data[off + j];
        log_write((uint32)(start + b), tmp);
        off += BSIZE;
    }
    // 2) 디렉터리 항목 추가(메모리) + 비트맵 갱신
    int slot = nfiles, k = 0;
    for (; name[k] && k < NAMELEN - 1; k++) dir[slot].name[k] = name[k];
    dir[slot].name[k] = 0;
    dir[slot].size = (unsigned)size;
    dir[slot].start = (unsigned)start;
    nfiles++;
    for (int b = 0; b < nblocks; b++) used[start + b] = 1;
    if ((uint32)(start + nblocks) > next_free) next_free = start + nblocks;
    // 3) 메타데이터(디렉터리/슈퍼블록) → 로그, 커밋
    log_dir();
    log_super();
    log_commit();
    return 0;
}

// 파일 삭제: 블록을 비트맵에서 해제하고, 디렉터리/슈퍼블록을 트랜잭션으로 갱신.
// 0=성공, -1=없음/마운트 안 됨.
int fs_delete(const char *name) {
    if (!mounted) return -1;
    int idx = -1;
    for (int i = 0; i < nfiles; i++)
        if (streq(dir[i].name, name)) { idx = i; break; }
    if (idx < 0) return -1;

    int nblocks = (dir[idx].size + BSIZE - 1) / BSIZE;
    for (int b = 0; b < nblocks; b++) {            // 블록 해제(재사용 가능해짐)
        uint32 blk = dir[idx].start + b;
        if (blk < MAXBLK) used[blk] = 0;
    }
    for (int j = idx; j < nfiles - 1; j++)         // 디렉터리 배열 압축
        dir[j] = dir[j + 1];
    for (int j = 0; j < NAMELEN; j++) dir[nfiles - 1].name[j] = 0;
    dir[nfiles - 1].size = dir[nfiles - 1].start = 0;
    nfiles--;

    log_begin();
    log_dir();
    log_super();
    log_commit();
    return 0;
}
