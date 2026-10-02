#include "notice.h"
#include "messager.h"
#include "config.h"
#include "osd_render.h"
#include "config_panel.h"
#include "position_panel.h"
#include "analysis.h"
#include "toast_sound.h"
#include "img_loader.h"
#include <shlobj.h>
#include "resource.h"
#pragma comment(lib, "msimg32.lib")

const wchar_t* notice_window_name = L"Lilith is all you need!";
const wchar_t* notice_class_name = L"YDS & Lilith";
HINSTANCE instacne;
HWND hwnd;
int current_ui_state = status_waiting;

bool g_use_osd = true;
wchar_t g_log_text[1024] = { 0 };

#define FADE_TIMER_ID 1
#define FADE_STEP     0.02f   /* 每次计时器滴答减少的透明度(约 1.5s 淡出) */

static float g_text_alpha = 1.0f;
static bool  g_fading = false;

/* ---- 桌面 Toast 弹窗（不依赖系统通知中心，保证可见） ---- */
#define TOAST_CLASS_NAME L"NoMoreMonitor.Toast"
#define TOAST_TIMER_ID   3
#define TOAST_SHOW_MS    5000
#define TOAST_W          360
#define TOAST_H          110
#define TOAST_IMG_SIZE   80

static HWND g_toast_hwnd = NULL;
static wchar_t g_toast_title[128] = { 0 };
static wchar_t g_toast_body[256] = { 0 };

/* Toast 小图缓存（预乘 BGRA） */
static wchar_t g_toast_img_path[MAX_PATH] = { 0 };
static unsigned char* g_toast_img_bgra = NULL;
static int g_toast_img_w = 0;
static int g_toast_img_h = 0;

static void toast_img_clear(void) {
    if (g_toast_img_bgra) { free(g_toast_img_bgra); g_toast_img_bgra = NULL; }
    g_toast_img_path[0] = L'\0';
    g_toast_img_w = 0;
    g_toast_img_h = 0;
}

static void toast_img_draw(HDC hdc, const wchar_t* path, int x, int y, int dw, int dh) {
    if (!path || path[0] == L'\0') return;

    if (wcscmp(g_toast_img_path, path) != 0 || !g_toast_img_bgra) {
        toast_img_clear();
        int iw = 0, ih = 0;
        unsigned char* px = img_load_file(path, &iw, &ih);
        if (!px || iw <= 0 || ih <= 0) { img_free(px); return; }

        unsigned char* scaled = img_scale_rgba(px, &iw, &ih, dw, dh);
        if (scaled == px) { dw = iw; dh = ih; }

        unsigned char* bgra = (unsigned char*)malloc((size_t)dw * dh * 4);
        if (!bgra) { img_free(scaled); return; }
        for (int i = 0; i < dw * dh; i++) {
            int a = scaled[i * 4 + 3];
            bgra[i * 4 + 0] = (BYTE)(scaled[i * 4 + 2] * a / 255);
            bgra[i * 4 + 1] = (BYTE)(scaled[i * 4 + 1] * a / 255);
            bgra[i * 4 + 2] = (BYTE)(scaled[i * 4 + 0] * a / 255);
            bgra[i * 4 + 3] = (BYTE)a;
        }
        img_free(scaled);

        wcsncpy_s(g_toast_img_path, _countof(g_toast_img_path), path, _TRUNCATE);
        g_toast_img_bgra = bgra;
        g_toast_img_w = dw;
        g_toast_img_h = dh;
    }

    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = g_toast_img_w;
    bmi.bmiHeader.biHeight = -g_toast_img_h; /* top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC mem = CreateCompatibleDC(hdc);
    void* bits = NULL;
    HBITMAP dib = CreateDIBSection(mem, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!dib || !bits) {
        if (dib) DeleteObject(dib);
        DeleteDC(mem);
        return;
    }
    memcpy(bits, g_toast_img_bgra, (size_t)g_toast_img_w * g_toast_img_h * 4);
    HGDIOBJ old = SelectObject(mem, dib);

    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(hdc, x, y, dw, dh, mem, 0, 0, g_toast_img_w, g_toast_img_h, bf);

    SelectObject(mem, old);
    DeleteObject(dib);
    DeleteDC(mem);
}

static void toast_hide(void) {
    if (!g_toast_hwnd) return;
    KillTimer(g_toast_hwnd, TOAST_TIMER_ID);
    ShowWindow(g_toast_hwnd, SW_HIDE);
}

static void toast_place(void) {
    if (!g_toast_hwnd) return;
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.right - TOAST_W - 16;
    int y = wa.bottom - TOAST_H - 16;
    SetWindowPos(g_toast_hwnd, HWND_TOPMOST, x, y, TOAST_W, TOAST_H,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static LRESULT CALLBACK toast_wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        toast_hide();
        return 0;
    case WM_TIMER:
        if (wp == TOAST_TIMER_ID) {
            toast_hide();
            return 0;
        }
        break;
    case WM_ERASEBKGND: {
        /* 背景必须在这里画实，否则 class 无画刷时会出现“透明空窗” */
        HDC hdc = (HDC)wp;
        RECT rc;
        GetClientRect(h, &rc);
        HBRUSH bg = CreateSolidBrush(g_config.toast_bg_color);
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);

        COLORREF bg = g_config.toast_bg_color;
        COLORREF accent = g_config.toast_accent_color;

        HBRUSH bgbrush = CreateSolidBrush(bg);
        FillRect(hdc, &rc, bgbrush);
        DeleteObject(bgbrush);

        RECT accent_rc = { 0, 0, 6, rc.bottom };
        HBRUSH ab = CreateSolidBrush(accent);
        FillRect(hdc, &accent_rc, ab);
        DeleteObject(ab);

        int text_left = 18;
        if (g_config.toast_image[0]) {
            int ih = TOAST_IMG_SIZE;
            int iy = (rc.bottom - ih) / 2;
            toast_img_draw(hdc, g_config.toast_image, 14, iy, ih, ih);
            text_left = 14 + ih + 12;
        }

        SetBkMode(hdc, TRANSPARENT);

        HFONT title_font = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
        HFONT body_font = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");

        HGDIOBJ old = SelectObject(hdc, title_font);
        SetTextColor(hdc, RGB(255, 255, 255));
        RECT tr = { text_left, 12, rc.right - 12, 40 };
        DrawTextW(hdc, g_toast_title, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

        SelectObject(hdc, body_font);
        SetTextColor(hdc, RGB(210, 210, 210));
        RECT br = { text_left, 44, rc.right - 12, rc.bottom - 10 };
        DrawTextW(hdc, g_toast_body, -1, &br, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);

        SelectObject(hdc, old);
        DeleteObject(title_font);
        DeleteObject(body_font);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CLOSE:
        toast_hide();
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static bool toast_ensure_window(void) {
    if (g_toast_hwnd) return true;

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = toast_wnd_proc;
    wc.hInstance = instacne;
    wc.lpszClassName = TOAST_CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);

    g_toast_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        TOAST_CLASS_NAME, L"",
        WS_POPUP,
        0, 0, TOAST_W, TOAST_H,
        NULL, NULL, instacne, NULL);
    return g_toast_hwnd != NULL;
}

static void toast_show(const wchar_t* title, const wchar_t* body) {
    if (!toast_ensure_window()) return;

    wcsncpy_s(g_toast_title, _countof(g_toast_title), title ? title : L"", _TRUNCATE);
    wcsncpy_s(g_toast_body, _countof(g_toast_body), body ? body : L"", _TRUNCATE);

    toast_place();
    /* 强制置顶并重绘，避免被其它 TOPMOST 窗口（如全屏 OSD）盖住 */
    SetWindowPos(g_toast_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_toast_hwnd, NULL, TRUE);
    UpdateWindow(g_toast_hwnd);
    SetTimer(g_toast_hwnd, TOAST_TIMER_ID, TOAST_SHOW_MS, NULL);
}

/* 托盘右键菜单项(固定文字，不进配置) */
#define IDM_SETTINGS 0x1001  /* 设置... 完整面板 */
#define IDM_POSITION 0x1002  /* 配置    文字位置 */
#define IDM_ANALYSIS 0x1003  /* 分析    GDI+ 图表 */
#define IDM_EXIT     0x1004  /* 退出 */

/* NOTIFYICON_VERSION_4 通知事件（部分旧 SDK 未定义时兜底） */
#ifndef NIN_SELECT
#define NIN_SELECT     (WM_USER + 0)
#define NINF_KEY       0x1
#define NIN_KEYSELECT  (NIN_SELECT | NINF_KEY)
#endif
#ifndef NIN_POPUPMENU
#define NIN_POPUPMENU  (WM_USER + 6)
#endif

static void notice_render(void) {
    if (!hwnd) return;
    /* 仅通知模式：OSD 不画任何文字/图片，提示只走 Toast */
    if (!g_use_osd) {
        osd_render(hwnd, status_waiting, &g_config, NULL, 0.0f);
        return;
    }
    osd_render(hwnd, current_ui_state, &g_config, g_log_text, g_text_alpha);
}

void notice_refresh_osd(void) {
    notice_render();
}

void send_balloon_notification(const wchar_t* title, const wchar_t* text) {
    /* 桌面 Toast：不依赖系统通知中心/专注助手，一定能看见 */
    toast_show(title, text);

    /* 提示音：自定义音频或系统默认 */
    toast_sound_play(g_config.toast_sound);

    /* 同时尝试托盘气泡（Win10/11 可能进通知中心，作补充） */
    if (!hwnd) return;
    NOTIFYICONDATA nid = { 0 };
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;
    if (title) wcscpy_s(nid.szInfoTitle, _countof(nid.szInfoTitle), title);
    if (text)  wcscpy_s(nid.szInfo, _countof(nid.szInfo), text);
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

static void show_tray_menu(void) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"设置...");
    AppendMenuW(menu, MF_STRING, IDM_POSITION, L"配置");
    AppendMenuW(menu, MF_STRING, IDM_ANALYSIS, L"分析");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"退出");

    SetForegroundWindow(hwnd);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                             pt.x, pt.y, 0, hwnd, NULL);
    PostMessageW(hwnd, WM_NULL, 0, 0);   /* 让弹出菜单正确关闭 */
    DestroyMenu(menu);

    switch (cmd) {
    case IDM_SETTINGS:
        config_panel_show(instacne, &g_config);
        notice_render();

        /* 刷新托盘提示文字 */
        {
            NOTIFYICONDATA nid = { 0 };
            nid.cbSize = sizeof(NOTIFYICONDATA);
            nid.hWnd = hwnd;
            nid.uID = 1;
            nid.uFlags = NIF_TIP;
            wcscpy_s(nid.szTip, _countof(nid.szTip), g_config.tray_tip);
            Shell_NotifyIcon(NIM_MODIFY, &nid);
        }
        break;

    case IDM_POSITION:
        position_panel_show(instacne, &g_config);
        notice_render();
        break;

    case IDM_ANALYSIS:
        analysis_show(instacne);
        break;

    case IDM_EXIT:
        notice_cleanup_tray();
        PostQuitMessage(0);
        break;
    }
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_IPC_STATE: {
        int incoming_state = (int)wParam;

        if (incoming_state == status_log) {
            if (lParam != 0) {
                wcscpy_s(g_log_text, _countof(g_log_text), (const wchar_t*)lParam);
            }
        }
        else {
            current_ui_state = incoming_state;
            g_log_text[0] = L'\0';

            if (current_ui_state == status_start) {
                g_fading = false;
                g_text_alpha = 1.0f;
                KillTimer(hwnd, FADE_TIMER_ID);
                send_balloon_notification(g_config.start_title, g_config.start_body);
            }
            else if (current_ui_state == status_watching) {
                g_fading = false;
                g_text_alpha = 1.0f;
                KillTimer(hwnd, FADE_TIMER_ID);
                send_balloon_notification(g_config.watching_title, g_config.watching_body);
            }
            else if (current_ui_state == status_stop) {
                send_balloon_notification(g_config.stop_title, g_config.stop_body);
                g_fading = true;
                g_text_alpha = 1.0f;
                SetTimer(hwnd, FADE_TIMER_ID, 30, NULL);
            }
        }

        notice_render();
        return 0;
    }

    case WM_TIMER: {
        if (wParam == FADE_TIMER_ID && g_fading) {
            g_text_alpha -= FADE_STEP;
            if (g_text_alpha <= 0.0f) {
                g_text_alpha = 0.0f;
                g_fading = false;
                KillTimer(hwnd, FADE_TIMER_ID);
            }
            notice_render();
        }
        return 0;
    }

    case WM_APP + 2: {
        /* NOTIFYICON_VERSION_4：事件类型在 lParam 的 LOWORD 里，
           HIWORD 可能是鼠标坐标，必须用 LOWORD(lParam) 判断。 */
        UINT evt = LOWORD(lParam);
        if (evt == WM_LBUTTONUP || evt == WM_RBUTTONUP ||
            evt == WM_CONTEXTMENU || evt == NIN_SELECT || evt == NIN_POPUPMENU) {
            show_tray_menu();
        }
        return 0;
    }

    case WM_DESTROY: {
        KillTimer(hwnd, FADE_TIMER_ID);
        toast_sound_stop();
        toast_hide();
        toast_img_clear();
        if (g_toast_hwnd) {
            DestroyWindow(g_toast_hwnd);
            g_toast_hwnd = NULL;
        }
        NOTIFYICONDATA nid = { 0 };
        nid.cbSize = sizeof(NOTIFYICONDATA);
        nid.hWnd = hwnd;
        nid.uID = 1;
        Shell_NotifyIcon(NIM_DELETE, &nid);
        notice_cleanup_tray();
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void notice_cleanup_tray(void) {
    if (!hwnd) return;
    NOTIFYICONDATA nid = { 0 };
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 1;
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

bool notice_window_init() {
    SetCurrentProcessExplicitAppUserModelID(L"Lilith.Awesome.Notice.App");

    if (!osd_init()) {
        con_printf(L"[!]can't init GDI+ (OSD renderer)\n");
        return false;
    }

    instacne = GetModuleHandle(NULL);
    WNDCLASSW window_class = { 0 };
    window_class.hInstance = instacne;
    window_class.lpfnWndProc = wnd_proc;
    window_class.lpszClassName = notice_class_name;
    window_class.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassW(&window_class);

    hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        notice_class_name, notice_window_name,
        WS_POPUP | WS_VISIBLE,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) - 1,
        NULL, NULL, instacne, NULL);

    if (hwnd == NULL) {
        con_printf(L"[!]can't create a window which are used to notice user\n");
        return false;
    }

    NOTIFYICONDATA nid = { 0 };
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = WM_APP + 2;
    HICON hTrayIcon = (HICON)LoadImageW(
        instacne,
        MAKEINTRESOURCEW(IDI_ICON1),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR
    );

    if (hTrayIcon) {
        nid.hIcon = hTrayIcon;
    }
    else {
        nid.hIcon = LoadIcon(instacne, MAKEINTRESOURCE(IDI_ICON1));
    }

    wcscpy_s(nid.szTip, _countof(nid.szTip), g_config.tray_tip);

    if (!Shell_NotifyIcon(NIM_ADD, &nid)) {
        con_printf(L"[!]Shell_NotifyIcon NIM_ADD failed: %lu\n", GetLastError());
        return false;
    }

    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIcon(NIM_SETVERSION, &nid);

    notice_render();
    return true;
}
