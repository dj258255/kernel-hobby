// mkfs.c — 호스트(맥)에서 도는 파일시스템 이미지 빌더.
//   사용법: mkfs out.img file1 [file2 ...]
//   슈퍼블록(블록0) + 디렉터리(블록1) + 파일 데이터(블록2..)를 써서 디스크 이미지 생성.
//
// 커널의 fs.c와 같은 fsformat.h를 공유한다(포맷 불일치 방지).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fsformat.h"

static char zero[BSIZE];

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s out.img file1 [file2 ...]\n", argv[0]);
        return 1;
    }
    int nfiles = argc - 2;
    if (nfiles > MAXFILES) {
        fprintf(stderr, "mkfs: too many files (max %d)\n", MAXFILES);
        return 1;
    }

    FILE *out = fopen(argv[1], "wb");
    if (!out) { perror("fopen out"); return 1; }

    struct fs_superblock sb = { FS_MAGIC, (unsigned)nfiles };
    struct fs_dirent dir[MAXFILES];
    memset(dir, 0, sizeof(dir));

    // 1) 각 파일을 읽어 디렉터리 항목을 채우고, 데이터 블록 위치를 배정한다.
    char *blobs[MAXFILES];
    unsigned sizes[MAXFILES];
    unsigned cur = DATASTART;  // 다음 빈 데이터 블록
    for (int i = 0; i < nfiles; i++) {
        const char *path = argv[2 + i];
        FILE *f = fopen(path, "rb");
        if (!f) { perror(path); return 1; }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *buf = malloc(sz ? sz : 1);
        if (sz > 0 && fread(buf, 1, sz, f) != (size_t)sz) { perror("fread"); return 1; }
        fclose(f);

        // 경로에서 파일명만(마지막 '/' 뒤)
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        strncpy(dir[i].name, base, NAMELEN - 1);
        dir[i].size  = (unsigned)sz;
        dir[i].start = cur;
        blobs[i] = buf;
        sizes[i] = (unsigned)sz;
        cur += (sz + BSIZE - 1) / BSIZE;  // 차지하는 블록 수만큼 전진
        if (sizes[i] == 0) cur += 0;       // 빈 파일은 0블록
    }

    // 2) 블록0: 슈퍼블록
    char sbblk[BSIZE];
    memset(sbblk, 0, BSIZE);
    memcpy(sbblk, &sb, sizeof(sb));
    fwrite(sbblk, 1, BSIZE, out);

    // 3) 블록1: 디렉터리
    char dirblk[BSIZE];
    memset(dirblk, 0, BSIZE);
    memcpy(dirblk, dir, sizeof(dir));
    fwrite(dirblk, 1, BSIZE, out);

    // 4) 블록2..: 파일 데이터(블록 단위로 패딩)
    for (int i = 0; i < nfiles; i++) {
        unsigned written = 0;
        while (written < sizes[i]) {
            char blk[BSIZE];
            memset(blk, 0, BSIZE);
            unsigned n = sizes[i] - written;
            if (n > BSIZE) n = BSIZE;
            memcpy(blk, blobs[i] + written, n);
            fwrite(blk, 1, BSIZE, out);
            written += n;
        }
        free(blobs[i]);
    }

    fclose(out);
    (void)zero;
    printf("mkfs: wrote %s (%d files, %u blocks)\n", argv[1], nfiles, cur);
    return 0;
}
