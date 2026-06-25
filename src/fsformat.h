// fsformat.h — 단순한 온디스크 파일시스템 포맷(저널링 + 삭제/재사용 지원).
// 커널(fs.c)과 호스트 도구(tools/mkfs.c)가 함께 포함한다. 그래서 커널 타입에
// 의존하지 않도록 표준 정수 타입(unsigned int)만 쓴다.
//
// 레이아웃 (블록 = 512바이트):
//   블록 0            : 슈퍼블록 (magic, 파일 수, next_free, total_blocks)
//   블록 1            : 디렉터리 (fs_dirent 8개)
//   블록 2            : 로그 헤더 (committed, n, blk[])  ← 저널링
//   블록 3..3+LOGN-1  : 로그 데이터 블록 (한 트랜잭션 분량)
//   블록 DATASTART..  : 파일 데이터 (각 파일은 start부터 연속된 블록들)
#ifndef FSFORMAT_H
#define FSFORMAT_H

#define FS_MAGIC   0x52465332u  // 'RFS2' (저널링 포맷)
#define BSIZE      512          // 블록(=섹터) 크기
#define MAXFILES   8            // 디렉터리 한 블록에 8개
#define NAMELEN    56           // 파일명 최대 길이
#define DIRBLOCK   1            // 디렉터리 블록 번호
#define LOGHDR     2            // 로그 헤더 블록
#define LOGSTART   3            // 로그 데이터 블록 시작
#define LOGN       16           // 로그 데이터 블록 수(한 트랜잭션 최대 16블록)
#define DATASTART  (LOGSTART + LOGN)  // 파일 데이터 시작 블록(= 19)

struct fs_superblock {
    unsigned int magic;
    unsigned int nfiles;
    unsigned int next_free;     // 다음 빈 데이터 블록(할당 힌트)
    unsigned int total_blocks;  // 디스크 전체 블록 수(빈 블록 비트맵 크기)
};

struct fs_dirent {              // 64바이트 → 한 블록(512)에 8개
    char         name[NAMELEN];
    unsigned int size;          // 파일 크기(바이트)
    unsigned int start;         // 시작 블록 번호
};

// 로그 헤더: 커밋된 트랜잭션이 어느 블록들을 바꾸는지 기록한다.
//   committed=1 이면 로그 데이터 블록 i를 blk[i]로 설치해야 한다(복구 시 replay).
struct fs_loghdr {
    unsigned int committed;     // 1=커밋됨(설치 대기), 0=비어 있음
    unsigned int n;             // 로그된 블록 수
    unsigned int blk[LOGN];     // 각 로그 블록의 목적지 블록 번호
};

#endif
