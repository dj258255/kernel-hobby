// fs.c — 읽기 전용 파일시스템 (fsformat.h 포맷)
//
// 마운트: 블록0(슈퍼블록)에서 magic/파일수 확인, 블록1(디렉터리) 적재.
// ls: 디렉터리 출력. cat: 파일의 start 블록부터 size만큼 읽어 출력.
//
// 주의: 디스크 버퍼는 static(커널 .bss=식별 매핑)이어야 virtio DMA 주소가 맞다.

#include "fs.h"
#include "fsformat.h"
#include "virtio.h"
#include "uart.h"
#include "types.h"

static struct fs_dirent dir[MAXFILES];
static int nfiles;
static int mounted;
static uint32 next_free;      // 다음 빈 데이터 블록(파일 생성 시 할당)
static uint8 fsbuf[BSIZE];   // 디스크 I/O 버퍼(커널 메모리)

static int streq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

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
    nfiles = (int)sb->nfiles;
    if (nfiles > MAXFILES) nfiles = MAXFILES;
    next_free = sb->next_free;

    virtio_disk_rw(DIRBLOCK, fsbuf, 0);
    struct fs_dirent *d = (struct fs_dirent *)fsbuf;
    for (int i = 0; i < MAXFILES; i++)
        dir[i] = d[i];

    mounted = 1;
    uart_puts("[ok] filesystem mounted: ");
    uart_dec(nfiles);
    uart_puts(" files\n");
}

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

// 파일 전체를 buf(커널 메모리)로 읽는다. 크기 반환, 없거나 너무 크면 -1.
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

// 파일 메타데이터(시작 블록, 크기). 찾으면 0, 없으면 -1.
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

// 파일의 offset(페이지 정렬)부터 한 페이지(BSIZE*8=4096B)를 buf로. 파일 끝 너머는 0.
void fs_read_page(unsigned start_block, unsigned size, unsigned offset, unsigned char *buf) {
    for (int i = 0; i < 4096; i++)
        buf[i] = 0;                       // 0으로 채우고
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

// 파일 생성(write-once): 데이터 블록 할당+쓰기 → 디렉터리/슈퍼블록 디스크 갱신.
// 0=성공, -1=실패(공간 없음/중복/마운트 안 됨).
int fs_create(const char *name, const char *data, int size) {
    if (!mounted || nfiles >= MAXFILES || size < 0)
        return -1;
    for (int i = 0; i < nfiles; i++)
        if (streq(dir[i].name, name))
            return -1;                      // 이미 존재(덮어쓰기 미지원)

    int nblocks = (size + BSIZE - 1) / BSIZE;
    uint32 start = next_free;

    // 1) 데이터 블록 쓰기
    int off = 0;
    for (int b = 0; b < nblocks; b++) {
        for (int j = 0; j < BSIZE; j++) fsbuf[j] = 0;
        int n = size - off; if (n > BSIZE) n = BSIZE;
        for (int j = 0; j < n; j++) fsbuf[j] = (uint8)data[off + j];
        virtio_disk_rw(start + b, fsbuf, 1);   // WRITE
        off += BSIZE;
    }

    // 2) 디렉터리 항목 추가(메모리)
    int slot = nfiles;
    int k = 0;
    for (; name[k] && k < NAMELEN - 1; k++) dir[slot].name[k] = name[k];
    dir[slot].name[k] = 0;
    dir[slot].size = (unsigned)size;
    dir[slot].start = start;
    nfiles++;
    next_free += nblocks;

    // 3) 디렉터리 블록(1)을 디스크에 다시 쓰기
    uint8 *db = (uint8 *)dir;
    for (int j = 0; j < BSIZE; j++) fsbuf[j] = (j < (int)sizeof(dir)) ? db[j] : 0;
    virtio_disk_rw(DIRBLOCK, fsbuf, 1);

    // 4) 슈퍼블록(0)을 디스크에 다시 쓰기
    struct fs_superblock sb = { FS_MAGIC, (unsigned)nfiles, next_free };
    uint8 *sp = (uint8 *)&sb;
    for (int j = 0; j < BSIZE; j++) fsbuf[j] = (j < (int)sizeof(sb)) ? sp[j] : 0;
    virtio_disk_rw(0, fsbuf, 1);

    return 0;
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
