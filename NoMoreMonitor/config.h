#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#else
#include <stdbool.h>
#endif

/*
 * NoMoreMonitor 统一配置。
 *
 * 配置文件为与可执行文件同目录的 NoMoreMonitor.ini。
 * 宿主程序(NoMoreMonitor.exe) 与注入 DLL(NoMoreMonitor_Dll.dll) 位于同一目录，
 * 二者都通过本模块读取同一份配置：
 *   - EXE 读取 OSD 文字/颜色、气泡通知、图片、控制台文字；
 *   - DLL 读取 暂停秒数(pause_seconds) 与 倒计时文字(countdown_text)。
 */

typedef struct AppConfig {
    /* ---- 通用 ---- */
    int      pause_seconds;      /* 捕获开始前的倒计时秒数 */
    bool     show_image;         /* 是否显示图片 */
    bool     image_replace;      /* true: 图片替代文字; false: 图片叠加在文字下 */
    int      image_scale;        /* 图片显示缩放百分比，100=适配屏幕 */
    bool     advanced_mode;      /* 配置面板是否打开专业模式 */
    wchar_t  data_dir[MAX_PATH]; /* spy 数据目录，空串=与程序同目录 */

    /* ---- OSD 文字 ---- */
    wchar_t  start_text[128];
    wchar_t  watching_text[128];
    wchar_t  stop_text[128];
    wchar_t  countdown_text[128];  /* 倒计时文字，可用 %d 表示剩余秒数 */

    /* ---- OSD 文字位置：0~8 九宫格(0左上 1上 2右上 3左 4中 5右 6左下 7下 8右下) ---- */
    int      text_position;
    int      text_offset_x;      /* 像素微调 */
    int      text_offset_y;

    /* ---- OSD 颜色 ---- */
    COLORREF start_color;
    COLORREF watching_color;
    COLORREF stop_color;
    COLORREF log_color;

    /* ---- 气泡通知 + 托盘 ---- */
    wchar_t  start_title[64];
    wchar_t  start_body[256];
    wchar_t  watching_title[64];
    wchar_t  watching_body[256];
    wchar_t  stop_title[64];
    wchar_t  stop_body[256];
    wchar_t  tray_tip[128];

    /* ---- 图片路径(空串表示不显示) ---- */
    wchar_t  start_image[MAX_PATH];
    wchar_t  watching_image[MAX_PATH];
    wchar_t  stop_image[MAX_PATH];

    /* ---- 控制台 ---- */
    bool     show_console;         /* 是否显示控制台窗口 */
    wchar_t  console_color[8];     /* 控制台颜色(16 色属性，如 "0E") */
    wchar_t  console_title[256];
    wchar_t  console_mode_on[128];
    wchar_t  console_mode_off[128];
    wchar_t  console_waiting[128];
    wchar_t  console_target_appeared[128];
    wchar_t  console_running[128];
    wchar_t  console_target_closed[128];
    wchar_t  console_mission_completed[128];
    wchar_t  console_spy_start[128];
    wchar_t  console_spy_stop[128];
} AppConfig;

/* 宿主程序运行时使用的全局配置(EXE 启动时加载一次) */
extern AppConfig g_config;

/* 填入默认值 */
void config_defaults(AppConfig* c);

/* 生成配置文件完整路径(mod 同目录下 NoMoreMonitor\NoMoreMonitor.ini)；mod 传 NULL 用当前模块 */
void config_get_path(wchar_t* buf, size_t len, HMODULE mod);

/* 程序数据目录：<模块目录>\NoMoreMonitor（末尾无反斜杠） */
void config_get_data_dir(wchar_t* buf, size_t len, HMODULE mod);

/* 确保数据目录存在并设为隐藏 */
void config_ensure_data_dir(HMODULE mod);

/* 从配置文件读取到 c(先填默认值再覆盖)。mod 为 NULL 时使用 EXE 模块。 */
bool config_load(AppConfig* c, HMODULE mod);

/* 将 c 写入配置文件。mod 为 NULL 时使用 EXE 模块。 */
bool config_save(const AppConfig* c, HMODULE mod);

/* 立即把 g_config 中的控制台设置应用到当前控制台窗口（标题/颜色/显隐） */
void config_apply_console(void);

/* 颜色 <-> "RRGGBB" 字符串 */
void color_to_hex(COLORREF color, wchar_t* out, size_t out_len);
COLORREF hex_to_color(const wchar_t* hex);

#ifdef __cplusplus
}
#endif
