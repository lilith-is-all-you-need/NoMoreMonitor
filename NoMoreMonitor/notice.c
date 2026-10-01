#include "notice.h"
#include "messager.h"
#include "config.h"
#include "osd_render.h"
#include "config_panel.h"
#include "position_panel.h"
#include "analysis.h"
#include <shlobj.h>
#include "resource.h"

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
    int st = g_use_osd ? current_ui_state : status_waiting;
    osd_render(hwnd, st, &g_config, g_log_text, g_text_alpha);
}

void notice_refresh_osd(void) {
    notice_render();
}

void send_balloon_notification(const wchar_t* title, const wchar_t* text) {
    NOTIFYICONDATA nid = { 0 };
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 1;

    nid.uFlags = NIF_INFO | NIF_ICON;
    nid.hIcon = LoadIcon(instacne, MAKEINTRESOURCE(IDI_ICON1));

    wcscpy_s(nid.szInfoTitle, _countof(nid.szInfoTitle), title);
    wcscpy_s(nid.szInfo, _countof(nid.szInfo), text);
    nid.dwInfoFlags = NIIF_USER;

    if (!Shell_NotifyIcon(NIM_MODIFY, &nid)) {
        wprintf(L"[!]Shell_NotifyIcon NIM_MODIFY failed: %lu\n", GetLastError());
    }
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
        wprintf(L"[!]can't init GDI+ (OSD renderer)\n");
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
        wprintf(L"[!]can't create a window which are used to notice user\n");
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
        wprintf(L"[!]Shell_NotifyIcon NIM_ADD failed: %lu\n", GetLastError());
        return false;
    }

    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIcon(NIM_SETVERSION, &nid);

    notice_render();
    return true;
}
