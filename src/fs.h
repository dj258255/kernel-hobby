// fs.h — 파일시스템(읽기 + 생성/삭제 + 저널링 + 블록 재사용)
#ifndef FS_H
#define FS_H

void fs_init(void);              // 마운트(슈퍼블록+디렉터리, 로그 복구, 비트맵 구성)
void fs_ls(void);                // 파일 목록
void fs_cat(const char *name);   // 파일 내용 출력
int  fs_read(const char *name, unsigned char *buf, int max);  // 파일 전체를 buf로(크기 반환, 실패 -1)
int  fs_stat(const char *name, unsigned *start_block, unsigned *size);  // 파일 메타(시작블록/크기)
int  fs_create(const char *name, const char *data, int size);  // 파일 생성(트랜잭션)
int  fs_delete(const char *name);  // 파일 삭제(블록 해제 + 트랜잭션). 0=성공, -1=없음
void fs_read_page(unsigned start_block, unsigned size, unsigned offset, unsigned char *buf);  // 파일 offset의 한 페이지

#endif
