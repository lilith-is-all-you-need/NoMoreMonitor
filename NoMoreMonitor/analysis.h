#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 打开 GDI+ 分析窗口（基于已存储的 spy 数据做统计） */
void analysis_show(HINSTANCE hInstance);

#ifdef __cplusplus
}
#endif
