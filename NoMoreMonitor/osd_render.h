#pragma once
#include <windows.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 GDI+，宿主程序启动时调用一次 */
bool osd_init(void);

/* 反初始化 GDI+ */
void osd_shutdown(void);

/*
 * 把当前状态渲染到 OSD 分层窗口。
 * state : status_waiting / status_start / status_watching / status_stop
 * log_text : 附加日志行(倒计时等)，可为 NULL/空串
 * text_alpha : 主状态文字的透明度 0.0f ~ 1.0f(用于 stop 淡出)
 */
void osd_render(HWND hwnd, int state, const AppConfig* cfg, const wchar_t* log_text, float text_alpha);

#ifdef __cplusplus
}
#endif
