#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 用 stb_image 解码图片文件为 RGBA 像素（宽*高*4 字节，自上而下）。
   成功返回像素缓冲（img_free 释放），失败返回 NULL。 */
unsigned char* img_load_file(const wchar_t* path, int* out_w, int* out_h);

/* 缩放 RGBA 像素（stb_image_resize2，Catmull-Rom），返回新缓冲（自动释放传入的 px） */
unsigned char* img_scale_rgba(unsigned char* px, int* w, int* h, int new_w, int new_h);

/* 释放 img_load_file 返回的缓冲 */
void img_free(unsigned char* px);

#ifdef __cplusplus
}
#endif
