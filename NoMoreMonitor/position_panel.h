#pragma once
#include <windows.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 文字位置对话框（LilithTimer 风格：九宫格 + 偏移 + 实时预览，取消还原） */
void position_panel_show(HINSTANCE hInstance, AppConfig* cfg);

#ifdef __cplusplus
}
#endif
