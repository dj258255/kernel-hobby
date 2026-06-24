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
