#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#else
#include <stdbool.h>
#endif

/* 弹出一个独立的分层预览窗口（stb_image + UpdateLayeredWindow），点击任意键关闭。
   参照 NoMoreCapture 的 show_preview_window。scale_percent 为显示缩放百分比(100=适配屏幕)。
   返回 false 表示图片加载失败。 */
bool show_image_preview(const wchar_t* path, int scale_percent);

#ifdef __cplusplus
}
#endif
