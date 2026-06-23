// fsformat.h — 아주 단순한 온디스크 파일시스템 포맷.
// 커널(fs.c)과 호스트 도구(tools/mkfs.c)가 함께 포함한다. 그래서 커널 타입에
// 의존하지 않도록 표준 정수 타입(unsigned int)만 쓴다.
//
// 레이아웃 (블록 = 512바이트):
//   블록 0      : 슈퍼블록 (magic, 파일 개수)
//   블록 1      : 디렉터리 (fs_dirent 8개)
//   블록 2..    : 파일 데이터 (각 파일은 start부터 연속된 블록들)
#ifndef FSFORMAT_H
#define FSFORMAT_H

#define FS_MAGIC   0x52465331u  // 'RFS1'
#define BSIZE      512          // 블록(=섹터) 크기
#define MAXFILES   8            // 디렉터리 한 블록에 8개
#define NAMELEN    56           // 파일명 최대 길이
#define DIRBLOCK   1            // 디렉터리 블록 번호
#define DATASTART  2            // 데이터 시작 블록

struct fs_superblock {
    unsigned int magic;
    unsigned int nfiles;
};

struct fs_dirent {              // 64바이트 → 한 블록(512)에 8개
    char         name[NAMELEN];
    unsigned int size;          // 파일 크기(바이트)
    unsigned int start;         // 시작 블록 번호
};

#endif
