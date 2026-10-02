#include "config.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <stdlib.h>
#include <stdarg.h>
#include <locale.h>

#define INI_NAME      L"NoMoreMonitor.ini"
#define DATA_DIR_NAME L"NoMoreMonitor"

AppConfig g_config;

/* ================= UTF-8 文件读写(显式处理 BOM，避免代码页问题) ================= */

static wchar_t* read_file_utf8(const wchar_t* path) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;

    DWORD size = GetFileSize(f, NULL);
    if (size == INVALID_FILE_SIZE || size == 0 || size > (1u << 20)) {
        CloseHandle(f);
        return NULL;
    }

    char* raw = (char*)malloc(size + 1);
    if (!raw) { CloseHandle(f); return NULL; }

    DWORD read = 0;
    ReadFile(f, raw, size, &read, NULL);
    CloseHandle(f);
    raw[read] = '\0';

    char* p = raw;
    if (read >= 3 && (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) {
        p = raw + 3; /* 跳过 UTF-8 BOM */
    }

    int wlen = MultiByteToWideChar(CP_UTF8, 0, p, -1, NULL, 0);
    wchar_t* wbuf = (wchar_t*)malloc((size_t)wlen * sizeof(wchar_t));
    if (wbuf) {
        MultiByteToWideChar(CP_UTF8, 0, p, -1, wbuf, wlen);
    }
    free(raw);
    return wbuf;
}

static bool write_file_utf8(const wchar_t* path, const wchar_t* content) {
    int wlen = (int)wcslen(content);
    int bytes = WideCharToMultiByte(CP_UTF8, 0, content, wlen, NULL, 0, NULL, NULL);
    if (bytes <= 0) return false;

    char* buf = (char*)malloc((size_t)bytes + 3);
    if (!buf) return false;

    buf[0] = (char)0xEF; buf[1] = (char)0xBB; buf[2] = (char)0xBF; /* BOM */
    WideCharToMultiByte(CP_UTF8, 0, content, wlen, buf + 3, bytes, NULL, NULL);

    HANDLE f = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        free(buf);
        return false;
    }
    DWORD written = 0;
    WriteFile(f, buf, (DWORD)bytes + 3, &written, NULL);
    CloseHandle(f);
    free(buf);
    return true;
}

/* ================= 颜色 ================= */

void color_to_hex(COLORREF color, wchar_t* out, size_t out_len) {
    if (!out || out_len < 7) return;
    swprintf_s(out, out_len, L"%02X%02X%02X",
               GetRValue(color), GetGValue(color), GetBValue(color));
}

COLORREF hex_to_color(const wchar_t* hex) {
    if (!hex) return RGB(255, 255, 255);
    unsigned int v = 0;
    if (swscanf_s(hex, L"%x", &v) == 1) {
        return RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    }
    return RGB(255, 255, 255);
}

/* ================= 默认值 ================= */

void config_defaults(AppConfig* c) {
    if (!c) return;
    memset(c, 0, sizeof(*c));

    c->pause_seconds = 5;
    c->show_image = false;
    c->image_replace = false;
    c->image_scale = 100;
    c->advanced_mode = false;
    c->data_dir[0] = L'\0';

    c->text_position = 4;   /* 居中 */
    c->text_offset_x = 0;
    c->text_offset_y = 0;

    wcscpy_s(c->start_text,    _countof(c->start_text),    L"起风了");
    wcscpy_s(c->watching_text, _countof(c->watching_text), L"风好大");
    wcscpy_s(c->stop_text,     _countof(c->stop_text),     L"风停了");
    wcscpy_s(c->countdown_text,_countof(c->countdown_text),L"Delay %d s");
    c->text_font_size = 120;
    c->letter_spacing = 0;

    c->start_color    = RGB(255, 0, 0);
    c->watching_color = RGB(255, 165, 0);
    c->stop_color     = RGB(255, 105, 180);
    c->log_color      = RGB(255, 255, 0);

    wcscpy_s(c->start_title,    _countof(c->start_title),    L"起风了");
    wcscpy_s(c->start_body,     _countof(c->start_body),     L"Ready to face challenge");
    wcscpy_s(c->watching_title, _countof(c->watching_title), L"风好大");
    wcscpy_s(c->watching_body,  _countof(c->watching_body),  L"Storming");
    wcscpy_s(c->stop_title,     _countof(c->stop_title),     L"风停了");
    wcscpy_s(c->stop_body,      _countof(c->stop_body),      L"We are safe... temporarily");
    wcscpy_s(c->tray_tip,       _countof(c->tray_tip),       L"Lilith Status");
    c->toast_sound[0] = L'\0';   /* 空 = 系统默认提示音 */
    c->toast_bg_color     = RGB(36, 36, 36);
    c->toast_accent_color = RGB(255, 105, 180);
    c->toast_image[0]     = L'\0';

    c->start_image[0] = L'\0';
    c->watching_image[0] = L'\0';
    c->stop_image[0] = L'\0';

    c->show_console = true;
    wcscpy_s(c->console_color, _countof(c->console_color), L"0E");

    wcscpy_s(c->console_title,             _countof(c->console_title), L"[Lilith]NoMoreMonitor --dev:YDS inspiration from LHX");
    wcscpy_s(c->console_mode_on,           _countof(c->console_mode_on), L"mode: text with notice");
    wcscpy_s(c->console_mode_off,          _countof(c->console_mode_off), L"mode: notice only");
    wcscpy_s(c->console_waiting,           _countof(c->console_waiting), L"Waiting for media_capture.exe...");
    wcscpy_s(c->console_target_appeared,   _countof(c->console_target_appeared), L"Target appeared");
    wcscpy_s(c->console_running,           _countof(c->console_running), L"running...");
    wcscpy_s(c->console_target_closed,     _countof(c->console_target_closed), L"Target process closed. Restarting monitor...");
    wcscpy_s(c->console_mission_completed, _countof(c->console_mission_completed), L"mission completed!");
    wcscpy_s(c->console_spy_start,         _countof(c->console_spy_start), L"Spy start!");
    wcscpy_s(c->console_spy_stop,          _countof(c->console_spy_stop), L"Spy stop!");
}

void config_get_data_dir(wchar_t* buf, size_t len, HMODULE mod) {
    if (!buf || len == 0) return;
    buf[0] = L'\0';

    HMODULE m = mod ? mod : GetModuleHandleW(NULL);
    wchar_t mod_path[MAX_PATH] = { 0 };
    GetModuleFileNameW(m, mod_path, MAX_PATH);

    wchar_t* last = wcsrchr(mod_path, L'\\');
    if (last) last[1] = L'\0';
    swprintf_s(buf, len, L"%s%s", mod_path, DATA_DIR_NAME);
}

void config_ensure_data_dir(HMODULE mod) {
    wchar_t dir[MAX_PATH] = { 0 };
    config_get_data_dir(dir, _countof(dir), mod);

    CreateDirectoryW(dir, NULL);   /* 已存在时返回失败，可忽略 */

    DWORD attr = GetFileAttributesW(dir);
    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_HIDDEN)) {
        SetFileAttributesW(dir, attr | FILE_ATTRIBUTE_HIDDEN);
    }
}

void config_get_path(wchar_t* buf, size_t len, HMODULE mod) {
    if (!buf || len == 0) return;
    wchar_t dir[MAX_PATH] = { 0 };
    config_get_data_dir(dir, _countof(dir), mod);
    swprintf_s(buf, len, L"%s\\%s", dir, INI_NAME);
}

/* ================= 解析 ================= */

static wchar_t* trim_line(wchar_t* s) {
    while (*s == L' ' || *s == L'\t') s++;
    wchar_t* end = s + wcslen(s);
    while (end > s && (end[-1] == L' ' || end[-1] == L'\t' ||
                       end[-1] == L'\r' || end[-1] == L'\n')) {
        end--;
    }
    *end = L'\0';
    return s;
}

static void set_str_field(wchar_t* dst, size_t cap, const wchar_t* v) {
    if (!dst || cap == 0) return;
    if (!v) { dst[0] = L'\0'; return; }
    wcsncpy_s(dst, cap, v, _TRUNCATE);
}

static void apply_key(AppConfig* c, const wchar_t* sec, const wchar_t* key, const wchar_t* val) {
    if (wcscmp(sec, L"general") == 0) {
        if (wcscmp(key, L"pause_seconds") == 0) { c->pause_seconds = _wtoi(val); if (c->pause_seconds < 1) c->pause_seconds = 1; }
        else if (wcscmp(key, L"show_image") == 0) { c->show_image = (wcscmp(val, L"1") == 0 || _wcsicmp(val, L"true") == 0); }
        else if (wcscmp(key, L"image_replace") == 0) { c->image_replace = (wcscmp(val, L"1") == 0 || _wcsicmp(val, L"true") == 0); }
        else if (wcscmp(key, L"image_scale") == 0) { c->image_scale = _wtoi(val); if (c->image_scale < 10) c->image_scale = 10; if (c->image_scale > 400) c->image_scale = 400; }
        else if (wcscmp(key, L"advanced_mode") == 0) { c->advanced_mode = (wcscmp(val, L"1") == 0 || _wcsicmp(val, L"true") == 0); }
        else if (wcscmp(key, L"data_dir") == 0) set_str_field(c->data_dir, _countof(c->data_dir), val);
    }
    else if (wcscmp(sec, L"text") == 0) {
        if (wcscmp(key, L"start_text") == 0) set_str_field(c->start_text, _countof(c->start_text), val);
        else if (wcscmp(key, L"watching_text") == 0) set_str_field(c->watching_text, _countof(c->watching_text), val);
        else if (wcscmp(key, L"stop_text") == 0) set_str_field(c->stop_text, _countof(c->stop_text), val);
        else if (wcscmp(key, L"countdown_text") == 0) set_str_field(c->countdown_text, _countof(c->countdown_text), val);
        else if (wcscmp(key, L"text_font_size") == 0) {
            c->text_font_size = _wtoi(val);
            if (c->text_font_size < 16) c->text_font_size = 16;
            if (c->text_font_size > 240) c->text_font_size = 240;
        }
        else if (wcscmp(key, L"letter_spacing") == 0) {
            c->letter_spacing = _wtoi(val);
            if (c->letter_spacing < -20) c->letter_spacing = -20;
            if (c->letter_spacing > 80) c->letter_spacing = 80;
        }
        else if (wcscmp(key, L"text_position") == 0) { c->text_position = _wtoi(val); if (c->text_position < 0 || c->text_position > 8) c->text_position = 4; }
        else if (wcscmp(key, L"text_offset_x") == 0) c->text_offset_x = _wtoi(val);
        else if (wcscmp(key, L"text_offset_y") == 0) c->text_offset_y = _wtoi(val);
    }
    else if (wcscmp(sec, L"color") == 0) {
        if (wcscmp(key, L"start_color") == 0) c->start_color = hex_to_color(val);
        else if (wcscmp(key, L"watching_color") == 0) c->watching_color = hex_to_color(val);
        else if (wcscmp(key, L"stop_color") == 0) c->stop_color = hex_to_color(val);
        else if (wcscmp(key, L"log_color") == 0) c->log_color = hex_to_color(val);
    }
    else if (wcscmp(sec, L"notify") == 0) {
        if (wcscmp(key, L"start_title") == 0) set_str_field(c->start_title, _countof(c->start_title), val);
        else if (wcscmp(key, L"start_body") == 0) set_str_field(c->start_body, _countof(c->start_body), val);
        else if (wcscmp(key, L"watching_title") == 0) set_str_field(c->watching_title, _countof(c->watching_title), val);
        else if (wcscmp(key, L"watching_body") == 0) set_str_field(c->watching_body, _countof(c->watching_body), val);
        else if (wcscmp(key, L"stop_title") == 0) set_str_field(c->stop_title, _countof(c->stop_title), val);
        else if (wcscmp(key, L"stop_body") == 0) set_str_field(c->stop_body, _countof(c->stop_body), val);
        else if (wcscmp(key, L"tray_tip") == 0) set_str_field(c->tray_tip, _countof(c->tray_tip), val);
        else if (wcscmp(key, L"toast_sound") == 0) set_str_field(c->toast_sound, _countof(c->toast_sound), val);
        else if (wcscmp(key, L"toast_bg_color") == 0) c->toast_bg_color = hex_to_color(val);
        else if (wcscmp(key, L"toast_accent_color") == 0) c->toast_accent_color = hex_to_color(val);
        else if (wcscmp(key, L"toast_image") == 0) set_str_field(c->toast_image, _countof(c->toast_image), val);
    }
    else if (wcscmp(sec, L"image") == 0) {
        if (wcscmp(key, L"start_image") == 0) set_str_field(c->start_image, _countof(c->start_image), val);
        else if (wcscmp(key, L"watching_image") == 0) set_str_field(c->watching_image, _countof(c->watching_image), val);
        else if (wcscmp(key, L"stop_image") == 0) set_str_field(c->stop_image, _countof(c->stop_image), val);
    }
    else if (wcscmp(sec, L"console") == 0) {
        if (wcscmp(key, L"show_console") == 0) { c->show_console = (wcscmp(val, L"1") == 0 || _wcsicmp(val, L"true") == 0); }
        else if (wcscmp(key, L"color") == 0) set_str_field(c->console_color, _countof(c->console_color), val);
        else if (wcscmp(key, L"title") == 0) set_str_field(c->console_title, _countof(c->console_title), val);
        else if (wcscmp(key, L"mode_on") == 0) set_str_field(c->console_mode_on, _countof(c->console_mode_on), val);
        else if (wcscmp(key, L"mode_off") == 0) set_str_field(c->console_mode_off, _countof(c->console_mode_off), val);
        else if (wcscmp(key, L"waiting") == 0) set_str_field(c->console_waiting, _countof(c->console_waiting), val);
        else if (wcscmp(key, L"target_appeared") == 0) set_str_field(c->console_target_appeared, _countof(c->console_target_appeared), val);
        else if (wcscmp(key, L"running") == 0) set_str_field(c->console_running, _countof(c->console_running), val);
        else if (wcscmp(key, L"target_closed") == 0) set_str_field(c->console_target_closed, _countof(c->console_target_closed), val);
        else if (wcscmp(key, L"mission_completed") == 0) set_str_field(c->console_mission_completed, _countof(c->console_mission_completed), val);
        else if (wcscmp(key, L"spy_start") == 0) set_str_field(c->console_spy_start, _countof(c->console_spy_start), val);
        else if (wcscmp(key, L"spy_stop") == 0) set_str_field(c->console_spy_stop, _countof(c->console_spy_stop), val);
    }
}

bool config_load(AppConfig* c, HMODULE mod) {
    if (!c) return false;
    config_defaults(c);

    wchar_t path[MAX_PATH] = { 0 };
    config_get_path(path, _countof(path), mod);

    wchar_t* text = read_file_utf8(path);
    if (!text) return true; /* 文件不存在或读取失败 → 使用默认值 */

    wchar_t sec[64] = L"";
    wchar_t* line = text;
    while (*line) {
        wchar_t* nl = wcschr(line, L'\n');
        if (nl) { *nl = L'\0'; nl++; }
        else { nl = line + wcslen(line); }

        wchar_t* s = trim_line(line);

        if (*s == L'[') {
            wchar_t* close = wcschr(s, L']');
            if (close) {
                *close = L'\0';
                wcsncpy_s(sec, _countof(sec), s + 1, _TRUNCATE);
            }
        }
        else if (*s != L'\0' && *s != L';' && *s != L'#') {
            wchar_t* eq = wcschr(s, L'=');
            if (eq) {
                *eq = L'\0';
                wchar_t* key = trim_line(s);
                wchar_t* val = trim_line(eq + 1);
                apply_key(c, sec, key, val);
            }
        }

        line = nl;
    }

    free(text);
    return true;
}

/* ================= 写出 ================= */

static void buf_append(wchar_t* buf, size_t cap, size_t* pos, const wchar_t* fmt, ...) {
    if (!buf || *pos >= cap - 1) return;
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnwprintf_s(buf + *pos, cap - *pos, _TRUNCATE, fmt, ap);
    va_end(ap);
    if (n > 0) *pos += (size_t)n;
}

bool config_save(const AppConfig* c, HMODULE mod) {
    if (!c) return false;

    config_ensure_data_dir(mod);

    wchar_t path[MAX_PATH] = { 0 };
    config_get_path(path, _countof(path), mod);

    wchar_t buf[16384];
    size_t pos = 0;
    wchar_t tmp[64];

    buf_append(buf, _countof(buf), &pos, L"[general]\r\n");
    buf_append(buf, _countof(buf), &pos, L"pause_seconds=%d\r\n", c->pause_seconds);
    buf_append(buf, _countof(buf), &pos, L"show_image=%d\r\n", c->show_image ? 1 : 0);
    buf_append(buf, _countof(buf), &pos, L"image_replace=%d\r\n", c->image_replace ? 1 : 0);
    buf_append(buf, _countof(buf), &pos, L"image_scale=%d\r\n", c->image_scale);
    buf_append(buf, _countof(buf), &pos, L"advanced_mode=%d\r\n", c->advanced_mode ? 1 : 0);
    buf_append(buf, _countof(buf), &pos, L"data_dir=%s\r\n", c->data_dir);
    buf_append(buf, _countof(buf), &pos, L"\r\n");

    buf_append(buf, _countof(buf), &pos, L"[text]\r\n");
    buf_append(buf, _countof(buf), &pos, L"start_text=%s\r\n", c->start_text);
    buf_append(buf, _countof(buf), &pos, L"watching_text=%s\r\n", c->watching_text);
    buf_append(buf, _countof(buf), &pos, L"stop_text=%s\r\n", c->stop_text);
    buf_append(buf, _countof(buf), &pos, L"countdown_text=%s\r\n", c->countdown_text);
    buf_append(buf, _countof(buf), &pos, L"text_font_size=%d\r\n", c->text_font_size);
    buf_append(buf, _countof(buf), &pos, L"letter_spacing=%d\r\n", c->letter_spacing);
    buf_append(buf, _countof(buf), &pos, L"text_position=%d\r\n", c->text_position);
    buf_append(buf, _countof(buf), &pos, L"text_offset_x=%d\r\n", c->text_offset_x);
    buf_append(buf, _countof(buf), &pos, L"text_offset_y=%d\r\n", c->text_offset_y);
    buf_append(buf, _countof(buf), &pos, L"\r\n");

    buf_append(buf, _countof(buf), &pos, L"[color]\r\n");
    color_to_hex(c->start_color, tmp, _countof(tmp));
    buf_append(buf, _countof(buf), &pos, L"start_color=%s\r\n", tmp);
    color_to_hex(c->watching_color, tmp, _countof(tmp));
    buf_append(buf, _countof(buf), &pos, L"watching_color=%s\r\n", tmp);
    color_to_hex(c->stop_color, tmp, _countof(tmp));
    buf_append(buf, _countof(buf), &pos, L"stop_color=%s\r\n", tmp);
    color_to_hex(c->log_color, tmp, _countof(tmp));
    buf_append(buf, _countof(buf), &pos, L"log_color=%s\r\n", tmp);
    buf_append(buf, _countof(buf), &pos, L"\r\n");

    buf_append(buf, _countof(buf), &pos, L"[notify]\r\n");
    buf_append(buf, _countof(buf), &pos, L"start_title=%s\r\n", c->start_title);
    buf_append(buf, _countof(buf), &pos, L"start_body=%s\r\n", c->start_body);
    buf_append(buf, _countof(buf), &pos, L"watching_title=%s\r\n", c->watching_title);
    buf_append(buf, _countof(buf), &pos, L"watching_body=%s\r\n", c->watching_body);
    buf_append(buf, _countof(buf), &pos, L"stop_title=%s\r\n", c->stop_title);
    buf_append(buf, _countof(buf), &pos, L"stop_body=%s\r\n", c->stop_body);
    buf_append(buf, _countof(buf), &pos, L"tray_tip=%s\r\n", c->tray_tip);
    buf_append(buf, _countof(buf), &pos, L"toast_sound=%s\r\n", c->toast_sound);
    color_to_hex(c->toast_bg_color, tmp, _countof(tmp));
    buf_append(buf, _countof(buf), &pos, L"toast_bg_color=%s\r\n", tmp);
    color_to_hex(c->toast_accent_color, tmp, _countof(tmp));
    buf_append(buf, _countof(buf), &pos, L"toast_accent_color=%s\r\n", tmp);
    buf_append(buf, _countof(buf), &pos, L"toast_image=%s\r\n", c->toast_image);
    buf_append(buf, _countof(buf), &pos, L"\r\n");

    buf_append(buf, _countof(buf), &pos, L"[image]\r\n");
    buf_append(buf, _countof(buf), &pos, L"start_image=%s\r\n", c->start_image);
    buf_append(buf, _countof(buf), &pos, L"watching_image=%s\r\n", c->watching_image);
    buf_append(buf, _countof(buf), &pos, L"stop_image=%s\r\n", c->stop_image);
    buf_append(buf, _countof(buf), &pos, L"\r\n");

    buf_append(buf, _countof(buf), &pos, L"[console]\r\n");
    buf_append(buf, _countof(buf), &pos, L"show_console=%d\r\n", c->show_console ? 1 : 0);
    buf_append(buf, _countof(buf), &pos, L"color=%s\r\n", c->console_color);
    buf_append(buf, _countof(buf), &pos, L"title=%s\r\n", c->console_title);
    buf_append(buf, _countof(buf), &pos, L"mode_on=%s\r\n", c->console_mode_on);
    buf_append(buf, _countof(buf), &pos, L"mode_off=%s\r\n", c->console_mode_off);
    buf_append(buf, _countof(buf), &pos, L"waiting=%s\r\n", c->console_waiting);
    buf_append(buf, _countof(buf), &pos, L"target_appeared=%s\r\n", c->console_target_appeared);
    buf_append(buf, _countof(buf), &pos, L"running=%s\r\n", c->console_running);
    buf_append(buf, _countof(buf), &pos, L"target_closed=%s\r\n", c->console_target_closed);
    buf_append(buf, _countof(buf), &pos, L"mission_completed=%s\r\n", c->console_mission_completed);
    buf_append(buf, _countof(buf), &pos, L"spy_start=%s\r\n", c->console_spy_start);
    buf_append(buf, _countof(buf), &pos, L"spy_stop=%s\r\n", c->console_spy_stop);

    return write_file_utf8(path, buf);
}

/* ================= 控制台运行态应用 ================= */

void config_apply_console(void) {
    SetConsoleTitleW(g_config.console_title);

    HWND hc = GetConsoleWindow();
    if (!g_config.show_console) {
        if (hc) ShowWindow(hc, SW_HIDE);
        return;
    }
    if (hc) ShowWindow(hc, SW_SHOW);

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;

    int vals[2] = { 0, 7 };   /* 默认黑底、浅灰字 */
    size_t n = wcslen(g_config.console_color);
    for (size_t i = 0; i < n && i < 2; i++) {
        wchar_t ch = g_config.console_color[i];
        int v = -1;
        if (ch >= L'0' && ch <= L'9') v = ch - L'0';
        else if (ch >= L'a' && ch <= L'f') v = ch - L'a' + 10;
        else if (ch >= L'A' && ch <= L'F') v = ch - L'A' + 10;
        if (v >= 0) vals[i] = v;
    }
    WORD attr = (WORD)((vals[0] << 4) | vals[1]);
    SetConsoleTextAttribute(hOut, attr);
}

/* ================= 控制台中文输出 ================= */

void con_init(void) {
    /* UTF-8 代码页：与 INI 的 UTF-8 一致 */
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    /* CRT 侧也切到 UTF-8（UCRT 支持 ".UTF8"；失败则用系统默认） */
    if (!setlocale(LC_ALL, ".UTF8")) {
        setlocale(LC_ALL, "");
    }

    /* 换成带 CJK 字形的等宽字体，避免 UTF-8 中文显示成方框 */
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == NULL || hOut == INVALID_HANDLE_VALUE) return;

    CONSOLE_FONT_INFOEX cfi;
    ZeroMemory(&cfi, sizeof(cfi));
    cfi.cbSize = sizeof(cfi);
    if (!GetCurrentConsoleFontEx(hOut, FALSE, &cfi)) return;

    static const wchar_t* fonts[] = {
        L"新宋体", L"NSimSun", L"微软雅黑",
        L"Microsoft YaHei", L"Consolas",
    };
    for (int i = 0; i < 5; i++) {
        wcscpy_s(cfi.FaceName, _countof(cfi.FaceName), fonts[i]);
        if (SetCurrentConsoleFontEx(hOut, FALSE, &cfi)) break;
    }
}

void con_printf(const wchar_t* fmt, ...) {
    if (!fmt) return;

    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);

    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == NULL || h == INVALID_HANDLE_VALUE) return;

    DWORD written = 0;
    DWORD len = (DWORD)wcslen(buf);
    if (WriteConsoleW(h, buf, len, &written, NULL)) {
        return;
    }

    /* 输出被重定向到文件/管道时 WriteConsoleW 会失败，改写 UTF-8 */
    int bytes = WideCharToMultiByte(CP_UTF8, 0, buf, (int)len, NULL, 0, NULL, NULL);
    if (bytes <= 0) return;
    char* utf8 = (char*)malloc((size_t)bytes + 1);
    if (!utf8) return;
    WideCharToMultiByte(CP_UTF8, 0, buf, (int)len, utf8, bytes, NULL, NULL);
    WriteFile(h, utf8, (DWORD)bytes, &written, NULL);
    free(utf8);
}
