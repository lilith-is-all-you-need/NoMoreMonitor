#include "notice.h"
#include "messager.h"
#include <shlobj.h>
#include "resource.h"

const wchar_t* notice_window_name = L"Lilith is all you need!";
const wchar_t* notice_class_name = L"YDS & Lilith";
HINSTANCE instacne;
HWND hwnd;
int current_ui_state = status_waiting;

bool g_use_osd = true;
wchar_t g_log_text[1024] = { 0 };

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
                send_balloon_notification(L"起风了", L"Ready to face challenge");
            }
            else if (current_ui_state == status_watching) {
                send_balloon_notification(L"风好大", L"Storming");
            }
            else if (current_ui_state == status_stop) {
                send_balloon_notification(L"风停了", L"We are safe... temporarily");
            }
        }

        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        if (!g_use_osd) {
            EndPaint(hwnd, &ps);
            return 0;
        }

        SetBkMode(hdc, TRANSPARENT);
        HFONT hFont = CreateFontW(120, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            VARIABLE_PITCH, L"Microsoft YaHei");

        HGDIOBJ hOldFont = SelectObject(hdc, hFont);
        RECT rect;
        GetClientRect(hwnd, &rect);

        if (current_ui_state == status_start) {
            SetTextColor(hdc, RGB(255, 0, 0));
            DrawTextW(hdc, L"起风了", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        else if (current_ui_state == status_watching) {
            SetTextColor(hdc, RGB(255, 165, 0));
            DrawTextW(hdc, L"风好大", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        else if (current_ui_state == status_stop) {
            SetTextColor(hdc, RGB(255, 105, 180));
            DrawTextW(hdc, L"风停了", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        if (g_log_text[0] != L'\0') {
            RECT log_rect = rect;
            log_rect.top += 250;
            SetTextColor(hdc, RGB(255, 255, 0));
            DrawTextW(hdc, g_log_text, -1, &log_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        SelectObject(hdc, hOldFont);
        DeleteObject(hFont);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY: {
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
    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

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

    wcscpy_s(nid.szTip, _countof(nid.szTip), L"Lilith Status");

    if (!Shell_NotifyIcon(NIM_ADD, &nid)) {
        wprintf(L"[!]Shell_NotifyIcon NIM_ADD failed: %lu\n", GetLastError());
        return false;
    }

    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIcon(NIM_SETVERSION, &nid);

    return true;
}