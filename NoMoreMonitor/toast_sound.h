#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Toast 提示音。
 * path 为空/NULL → 系统默认通知音；
 * 否则播放该音频文件（wav / mp3 / flac 等常见格式）。
 */
bool toast_sound_play(const wchar_t* path);
void toast_sound_stop(void);

#ifdef __cplusplus
}
#endif
