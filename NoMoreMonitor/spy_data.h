#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#else
#include <stdbool.h>
#endif

/* 一次 spy 会话记录（本地时间） */
typedef struct {
    SYSTEMTIME start;
    SYSTEMTIME end;
    DWORD      duration_seconds;
} SpySession;

/* 追加一条 spy 记录（线程安全，原子追加 + flush 落盘） */
bool spy_data_append(const SpySession* s);

/* 读取全部记录到动态数组（*out 由调用方 free），*count 为条数 */
bool spy_data_load(SpySession** out, int* count);

/* 数据文件完整路径（spy_log.csv） */
void spy_data_file_path(wchar_t* buf, size_t len);

#ifdef __cplusplus
}
#endif
