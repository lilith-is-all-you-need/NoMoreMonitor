#include "spy_data.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <stdlib.h>
#include <shlobj.h>

#define CSV_FILENAME L"spy_log.csv"
#define CSV_HEADER   "start,end,duration_seconds\r\n"

static CRITICAL_SECTION g_spy_cs;
static volatile LONG g_spy_cs_init = 0;

static void ensure_cs(void) {
    if (InterlockedCompareExchange(&g_spy_cs_init, 1, 0) == 0) {
        InitializeCriticalSection(&g_spy_cs);
    }
}

/* 数据目录：data_dir 为空时用程序同目录 */
static void spy_data_get_dir(wchar_t* dir, size_t len) {
    if (!dir || len == 0) return;
    dir[0] = L'\0';

    if (g_config.data_dir[0] != L'\0') {
        wcscpy_s(dir, len, g_config.data_dir);
        size_t l = wcslen(dir);
        if (l > 0 && dir[l - 1] != L'\\' && dir[l - 1] != L'/') {
            wcscat_s(dir, len, L"\\");
        }
    }
    else {
        /* 默认：程序同目录下的 NoMoreMonitor\ 隐藏文件夹 */
        config_get_data_dir(dir, len, NULL);
        wcscat_s(dir, len, L"\\");
    }
}

void spy_data_file_path(wchar_t* buf, size_t len) {
    if (!buf || len == 0) return;
    wchar_t dir[MAX_PATH] = { 0 };
    spy_data_get_dir(dir, _countof(dir));
    swprintf_s(buf, len, L"%s%s", dir, CSV_FILENAME);
}

/* 默认目录（程序同目录 NoMoreMonitor\）下的 CSV 完整路径 */
static void spy_data_default_path(wchar_t* buf, size_t len) {
    if (!buf || len == 0) return;
    wchar_t dir[MAX_PATH] = { 0 };
    config_get_data_dir(dir, _countof(dir), NULL);
    size_t l = wcslen(dir);
    if (l > 0 && dir[l - 1] != L'\\' && dir[l - 1] != L'/') wcscat_s(dir, _countof(dir), L"\\");
    swprintf_s(buf, len, L"%s%s", dir, CSV_FILENAME);
}

/* 确保数据目录存在（自定义目录可能是多级） */
static bool spy_data_ensure_dir(void) {
    if (g_config.data_dir[0] == L'\0') {
        config_ensure_data_dir(NULL);   /* 创建并隐藏默认文件夹 */
        return true;
    }

    int r = SHCreateDirectoryExW(NULL, g_config.data_dir, NULL);
    if (r == ERROR_SUCCESS) return true;

    /* SHCreateDirectoryExW 对已存在的目录返回 ERROR_ALREADY_EXISTS(183)，
       因此再按属性兜底判断一次（并确认它确实是目录） */
    DWORD attr = GetFileAttributesW(g_config.data_dir);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        return true;
    }

    wprintf(L"[!]spy data dir create FAILED: %s (SHCreateDirectoryEx=%d, GetLastError=%lu)\n",
            g_config.data_dir, r, GetLastError());
    return false;
}

/* 向指定 CSV 文件追加一行（文件为空时先写表头）。成功返回 true */
static bool spy_write_line(const wchar_t* path, const char* line) {
    HANDLE f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER sz;
    sz.QuadPart = 0;
    GetFileSizeEx(f, &sz);
    if (sz.QuadPart == 0) {
        DWORD hw = 0;
        WriteFile(f, CSV_HEADER, (DWORD)strlen(CSV_HEADER), &hw, NULL);
    }

    DWORD written = 0;
    DWORD len = (DWORD)strlen(line);
    bool ok = (WriteFile(f, line, len, &written, NULL) && written == len);
    if (ok) FlushFileBuffers(f);
    CloseHandle(f);
    return ok;
}

bool spy_data_append(const SpySession* s) {
    if (!s) return false;

    ensure_cs();

    char line[128];
    sprintf_s(line, _countof(line),
              "%04d-%02d-%02d %02d:%02d:%02d,%04d-%02d-%02d %02d:%02d:%02d,%u\r\n",
              s->start.wYear, s->start.wMonth, s->start.wDay,
              s->start.wHour, s->start.wMinute, s->start.wSecond,
              s->end.wYear, s->end.wMonth, s->end.wDay,
              s->end.wHour, s->end.wMinute, s->end.wSecond,
              s->duration_seconds);

    EnterCriticalSection(&g_spy_cs);

    bool ok = false;

    /* 优先写自定义目录 */
    if (g_config.data_dir[0] != L'\0') {
        wchar_t path[MAX_PATH] = { 0 };
        spy_data_file_path(path, _countof(path));
        if (spy_data_ensure_dir()) {
            ok = spy_write_line(path, line);
            if (!ok) {
                wprintf(L"[!]spy data write to custom dir FAILED (%s, err=%lu), falling back to default dir\n",
                        g_config.data_dir, GetLastError());
            }
        }
        else {
            wprintf(L"[!]custom spy dir unusable (%s), falling back to default dir\n", g_config.data_dir);
        }
    }

    /* 回退到默认目录（自定义未设置或写失败） */
    if (!ok) {
        config_ensure_data_dir(NULL);
        wchar_t def_path[MAX_PATH] = { 0 };
        spy_data_default_path(def_path, _countof(def_path));
        ok = spy_write_line(def_path, line);
        if (!ok) {
            wprintf(L"[!]spy data write to default dir FAILED (%s, err=%lu)\n", def_path, GetLastError());
        }
    }

    LeaveCriticalSection(&g_spy_cs);
    return ok;
}

static bool parse_csv_line(const char* line, SpySession* out) {
    int y1, mo1, d1, h1, mi1, s1, y2, mo2, d2, h2, mi2, s2;
    unsigned dur;
    if (sscanf_s(line, "%d-%d-%d %d:%d:%d,%d-%d-%d %d:%d:%d,%u",
                 &y1, &mo1, &d1, &h1, &mi1, &s1,
                 &y2, &mo2, &d2, &h2, &mi2, &s2, &dur) != 13) {
        return false;
    }
    if (y1 < 1970 || y2 < 1970 || mo1 < 1 || mo1 > 12 || mo2 < 1 || mo2 > 12) return false;

    ZeroMemory(out, sizeof(*out));
    out->start.wYear = (WORD)y1; out->start.wMonth = (WORD)mo1; out->start.wDay = (WORD)d1;
    out->start.wHour = (WORD)h1; out->start.wMinute = (WORD)mi1; out->start.wSecond = (WORD)s1;
    out->end.wYear = (WORD)y2; out->end.wMonth = (WORD)mo2; out->end.wDay = (WORD)d2;
    out->end.wHour = (WORD)h2; out->end.wMinute = (WORD)mi2; out->end.wSecond = (WORD)s2;
    out->duration_seconds = dur;
    return true;
}

bool spy_data_load(SpySession** out, int* count) {
    if (out) *out = NULL;
    if (count) *count = 0;

    wchar_t path[MAX_PATH] = { 0 };
    spy_data_file_path(path, _countof(path));

    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE && g_config.data_dir[0] != L'\0') {
        /* 自定义目录里没有数据，回退到默认目录再找一次 */
        spy_data_default_path(path, _countof(path));
        f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    if (f == INVALID_HANDLE_VALUE) return false;

    DWORD size = GetFileSize(f, NULL);
    if (size == INVALID_FILE_SIZE || size == 0 || size > (64u << 20)) {
        CloseHandle(f);
        return false;
    }

    char* buf = (char*)malloc(size + 1);
    if (!buf) { CloseHandle(f); return false; }

    DWORD read = 0;
    ReadFile(f, buf, size, &read, NULL);
    CloseHandle(f);
    buf[read] = '\0';

    /* 预分配一个较大数组，逐行解析 */
    int cap = 256, n = 0;
    SpySession* arr = (SpySession*)malloc(sizeof(SpySession) * cap);
    if (!arr) { free(buf); return false; }

    char* line = buf;
    while (*line) {
        char* nl = strchr(line, '\n');
        if (nl) { *nl = '\0'; nl++; }
        else { nl = line + strlen(line); }

        /* 去掉 \r 和首尾空白 */
        char* s = line;
        while (*s == ' ' || *s == '\t' || *s == '\r') s++;
        char* e = s + strlen(s);
        while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) e--;
        *e = '\0';

        if (s[0] != '\0' && s[0] != '#' && s[0] != 's' && s[0] != 'S') {
            SpySession tmp;
            if (parse_csv_line(s, &tmp)) {
                if (n >= cap) {
                    cap *= 2;
                    SpySession* na = (SpySession*)realloc(arr, sizeof(SpySession) * cap);
                    if (!na) { free(arr); free(buf); return false; }
                    arr = na;
                }
                arr[n++] = tmp;
            }
        }

        line = nl;
    }

    free(buf);
    if (out) *out = arr; else free(arr);
    if (count) *count = n;
    return true;
}
