// fs.h — 아주 단순한 읽기 전용 파일시스템
#ifndef FS_H
#define FS_H

void fs_init(void);              // 디스크에서 슈퍼블록+디렉터리 적재(마운트)
void fs_ls(void);                // 파일 목록
void fs_cat(const char *name);   // 파일 내용 출력
int  fs_read(const char *name, unsigned char *buf, int max);  // 파일 전체를 buf로(크기 반환, 실패 -1)
int  fs_stat(const char *name, unsigned *start_block, unsigned *size);  // 파일 메타(시작블록/크기)
void fs_read_page(unsigned start_block, unsigned size, unsigned offset, unsigned char *buf);  // 파일 offset의 한 페이지

#endif
