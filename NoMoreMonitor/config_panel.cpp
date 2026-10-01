#include "config_panel.h"
#include "img_loader.h"
#include "preview_window.h"
#include <commdlg.h>
#include <commctrl.h>
#include <windowsx.h>
#include <shobjidl.h>
#include <objbase.h>
#include <stdio.h>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")

#define PANEL_CLASS   L"NoMoreMonitorConfigPanel"
#define CONTENT_CLASS L"NoMoreMonitorContent"
#define PREVIEW_CLASS L"NoMoreMonitorPreview"

/* ---------- 控件 ID ---------- */
enum {
    IDC_CHK_ADVANCED = 2000,   /* 专业模式开关 */

    IDC_PAUSE,
    IDC_CHK_SHOW_IMAGE,
    IDC_CHK_REPLACE,
    IDC_DATA_DIR,
    IDC_DATA_DIR_BROWSE,

    IDC_START_TEXT,
    IDC_WATCHING_TEXT,
    IDC_STOP_TEXT,
    IDC_COUNTDOWN_TEXT,

    IDC_COLOR_START,
    IDC_COLOR_WATCHING,
    IDC_COLOR_STOP,
    IDC_COLOR_LOG,

    IDC_START_TITLE,
    IDC_START_BODY,
    IDC_WATCHING_TITLE,
    IDC_WATCHING_BODY,
    IDC_STOP_TITLE,
    IDC_STOP_BODY,
    IDC_TRAY_TIP,

    IDC_START_IMAGE,
    IDC_START_IMAGE_BROWSE,
    IDC_WATCHING_IMAGE,
    IDC_WATCHING_IMAGE_BROWSE,
    IDC_STOP_IMAGE,
    IDC_STOP_IMAGE_BROWSE,
    IDC_PREVIEW_STATE,
    IDC_PREVIEW,
    IDC_BTN_PREVIEW,
    IDC_IMAGE_SCALE,
    IDC_IMAGE_SCALE_LBL,

    IDC_CHK_SHOW_CONSOLE,
    IDC_CONSOLE_COLOR,

    IDC_CONSOLE_TITLE,
    IDC_CONSOLE_MODE_ON,
    IDC_CONSOLE_MODE_OFF,
    IDC_CONSOLE_WAITING,
    IDC_CONSOLE_APPEARED,
    IDC_CONSOLE_RUNNING,
    IDC_CONSOLE_CLOSED,
    IDC_CONSOLE_MISSION,
    IDC_CONSOLE_SPY_START,
    IDC_CONSOLE_SPY_STOP,

    IDC_BTN_RESET = 2501,
    IDC_BTN_SAVE,
    IDC_BTN_CANCEL,
};

/* ---------- 布局常量 ---------- */
static const int LX = 12;
static const int LABEL_W = 150;
static const int EDIT_W = 420;
static const int ROW_H = 26;
static const int GAP = 4;
static const int CONTENT_W = 680;
static const int PREVIEW_W = 500;
static const int PREVIEW_H = 240;

struct PanelData {
    AppConfig* target;      /* 指向 g_config */
    AppConfig  work;        /* 编辑副本 */
    HWND hPanel;
    HWND hContent;
    HWND hPreview;
    WNDPROC oldPreviewProc;
    HINSTANCE hInst;
    int scrollY;
    int contentHeight;
    int previewState;       /* 0 开始 1 监视中 2 结束 */
    bool advanced;          /* 专业模式 */
    wchar_t previewPath[MAX_PATH];
    HBRUSH swatchBrush[4];
    COLORREF swatchColor[4];
    bool swatchColorSet[4];
    HFONT hFont;
    bool hFontCreated;
};

/* ---------- 控件工厂 ---------- */
/* 用系统消息字体(支持中文/IME)，避免 DEFAULT_GUI_FONT 位图字体显示异常 */
static HFONT create_msg_font(void) {
    NONCLIENTMETRICSW ncm = { 0 };
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        return CreateFontIndirectW(&ncm.lfMessageFont);
    }
    return NULL;
}

static HWND make_ctl(HWND parent, const wchar_t* cls, const wchar_t* text,
                     DWORD style, int x, int y, int w, int h, int id, HFONT font) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             x, y, w, h, parent, (HMENU)(INT_PTR)id,
                             GetModuleHandleW(NULL), NULL);
    if (c && font) SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
    return c;
}

static HWND make_label(HWND parent, const wchar_t* text, int x, int y, HFONT font) {
    return make_ctl(parent, L"STATIC", text, 0, x, y + 4, LABEL_W, 20, -1, font);
}

static HWND make_hint(HWND parent, const wchar_t* text, int x, int y, HFONT font) {
    return make_ctl(parent, L"STATIC", text, 0, x, y, CONTENT_W - 2 * LX, 22, -1, font);
}

static HWND make_edit(HWND parent, int id, const wchar_t* text, int x, int y, HFONT font, DWORD extra) {
    return make_ctl(parent, L"EDIT", text, WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | extra,
                    x, y, EDIT_W, ROW_H, id, font);
}

static HWND make_check(HWND parent, int id, const wchar_t* text, bool checked, int x, int y, HFONT font) {
    HWND c = make_ctl(parent, L"BUTTON", text, WS_TABSTOP | BS_AUTOCHECKBOX,
                      x, y, CONTENT_W - 2 * LX, 22, id, font);
    if (c) SendMessageW(c, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    return c;
}

static HWND make_button(HWND parent, int id, const wchar_t* text, int x, int y, int w, HFONT font) {
    return make_ctl(parent, L"BUTTON", text, WS_TABSTOP | BS_PUSHBUTTON, x, y, w, 28, id, font);
}

static HWND make_swatch(HWND parent, int id, int x, int y, HFONT font) {
    return make_ctl(parent, L"STATIC", L"", SS_NOTIFY | SS_SUNKEN, x, y, 52, 22, id, font);
}

static HWND make_group(HWND parent, const wchar_t* text, int x, int y, HFONT font) {
    return make_ctl(parent, L"BUTTON", text, BS_GROUPBOX, x, y, CONTENT_W - 2 * LX, 24, -1, font);
}

static HWND make_combo(HWND parent, int id, int x, int y, int w, HFONT font) {
    return make_ctl(parent, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                    x, y, w, 200, id, font);
}

/* ---------- 颜色映射 ---------- */
static COLORREF get_color_by_id(const PanelData* pd, int id) {
    switch (id) {
    case IDC_COLOR_START:    return pd->work.start_color;
    case IDC_COLOR_WATCHING: return pd->work.watching_color;
    case IDC_COLOR_STOP:     return pd->work.stop_color;
    case IDC_COLOR_LOG:      return pd->work.log_color;
    }
    return RGB(255, 255, 255);
}

static void set_color_by_id(PanelData* pd, int id, COLORREF c) {
    switch (id) {
    case IDC_COLOR_START:    pd->work.start_color = c; break;
    case IDC_COLOR_WATCHING: pd->work.watching_color = c; break;
    case IDC_COLOR_STOP:     pd->work.stop_color = c; break;
    case IDC_COLOR_LOG:      pd->work.log_color = c; break;
    }
}

static int swatch_index(int id) {
    switch (id) {
    case IDC_COLOR_START:    return 0;
    case IDC_COLOR_WATCHING: return 1;
    case IDC_COLOR_STOP:     return 2;
    case IDC_COLOR_LOG:      return 3;
    }
    return -1;
}

/* ---------- 预览 ---------- */
static void refresh_preview(PanelData* pd) {
    if (!pd->hPreview) return;
    HWND hEdit = NULL;
    switch (pd->previewState) {
    case 0: hEdit = GetDlgItem(pd->hContent, IDC_START_IMAGE); break;
    case 1: hEdit = GetDlgItem(pd->hContent, IDC_WATCHING_IMAGE); break;
    case 2: hEdit = GetDlgItem(pd->hContent, IDC_STOP_IMAGE); break;
    }
    if (hEdit) {
        GetWindowTextW(hEdit, pd->previewPath, MAX_PATH);
    }
    else {
        pd->previewPath[0] = L'\0';
    }
    InvalidateRect(pd->hPreview, NULL, TRUE);
}

static LRESULT CALLBACK PreviewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PanelData* pd = (PanelData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        HBRUSH bg = CreateSolidBrush(RGB(230, 230, 230));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        DWORD pattr = (pd && pd->previewPath[0]) ? GetFileAttributesW(pd->previewPath) : INVALID_FILE_ATTRIBUTES;
        if (pattr != INVALID_FILE_ATTRIBUTES && !(pattr & FILE_ATTRIBUTE_DIRECTORY)) {
            /* stb_image 纯 C 解码（无 WIC/COM/WinRT），参照 NoMoreCapture */
            int iw = 0, ih = 0;
            unsigned char* px = img_load_file(pd->previewPath, &iw, &ih);
            if (px && iw > 0 && ih > 0) {
                int cw = rc.right - rc.left;
                int chh = rc.bottom - rc.top;
                double scale = ((double)cw / iw) < ((double)chh / ih) ? ((double)cw / iw) : ((double)chh / ih);
                int dw = (int)(iw * scale), dh = (int)(ih * scale);
                if (dw < 1) dw = 1;
                if (dh < 1) dh = 1;
                int dx = (cw - dw) / 2, dy = (chh - dh) / 2;

                BITMAPINFO bmi = { 0 };
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = iw;
                bmi.bmiHeader.biHeight = -ih;   /* 自上而下 */
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;
                void* bits = NULL;
                HBITMAP hbm = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
                if (hbm && bits) {
                    BYTE* src = px;
                    BYTE* dst = (BYTE*)bits;
                    for (int i = 0; i < iw * ih; i++) {
                        dst[i * 4 + 0] = src[i * 4 + 2];  /* B */
                        dst[i * 4 + 1] = src[i * 4 + 1];  /* G */
                        dst[i * 4 + 2] = src[i * 4 + 0];  /* R */
                        dst[i * 4 + 3] = src[i * 4 + 3];  /* A */
                    }
                    HDC memdc = CreateCompatibleDC(hdc);
                    HGDIOBJ old = SelectObject(memdc, hbm);
                    SetStretchBltMode(hdc, HALFTONE);
                    StretchBlt(hdc, dx, dy, dw, dh, memdc, 0, 0, iw, ih, SRCCOPY);
                    SelectObject(memdc, old);
                    DeleteDC(memdc);
                    DeleteObject(hbm);
                }
                img_free(px);
            }
            else {
                img_free(px);
                SetTextColor(hdc, RGB(120, 120, 120));
                SetBkMode(hdc, TRANSPARENT);
                DrawTextW(hdc, L"无法加载图片", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
        else {
            SetTextColor(hdc, RGB(140, 140, 140));
            SetBkMode(hdc, TRANSPARENT);
            DrawTextW(hdc, L"无图片", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    if (pd && pd->oldPreviewProc) {
        return CallWindowProcW(pd->oldPreviewProc, hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* ---------- 读写控件 ---------- */
static void write_controls(PanelData* pd) {
    HWND c = pd->hContent;
    wchar_t buf[64];

    swprintf_s(buf, L"%d", pd->work.pause_seconds);
    SetDlgItemTextW(c, IDC_PAUSE, buf);
    SetDlgItemTextW(c, IDC_START_TEXT, pd->work.start_text);
    SetDlgItemTextW(c, IDC_WATCHING_TEXT, pd->work.watching_text);
    SetDlgItemTextW(c, IDC_STOP_TEXT, pd->work.stop_text);

    SendDlgItemMessageW(c, IDC_CHK_ADVANCED, BM_SETCHECK, pd->advanced ? BST_CHECKED : BST_UNCHECKED, 0);

    if (pd->advanced) {
        SetDlgItemTextW(c, IDC_COUNTDOWN_TEXT, pd->work.countdown_text);

        SendDlgItemMessageW(c, IDC_CHK_SHOW_IMAGE, BM_SETCHECK, pd->work.show_image ? BST_CHECKED : BST_UNCHECKED, 0);
        SendDlgItemMessageW(c, IDC_CHK_REPLACE, BM_SETCHECK, pd->work.image_replace ? BST_CHECKED : BST_UNCHECKED, 0);

        SendDlgItemMessageW(c, IDC_IMAGE_SCALE, TBM_SETPOS, TRUE, pd->work.image_scale);
        {
            wchar_t buf[32];
            swprintf_s(buf, L"%d%%", pd->work.image_scale);
            SetDlgItemTextW(c, IDC_IMAGE_SCALE_LBL, buf);
        }

        SetDlgItemTextW(c, IDC_DATA_DIR, pd->work.data_dir);

        SetDlgItemTextW(c, IDC_START_TITLE, pd->work.start_title);
        SetDlgItemTextW(c, IDC_START_BODY, pd->work.start_body);
        SetDlgItemTextW(c, IDC_WATCHING_TITLE, pd->work.watching_title);
        SetDlgItemTextW(c, IDC_WATCHING_BODY, pd->work.watching_body);
        SetDlgItemTextW(c, IDC_STOP_TITLE, pd->work.stop_title);
        SetDlgItemTextW(c, IDC_STOP_BODY, pd->work.stop_body);
        SetDlgItemTextW(c, IDC_TRAY_TIP, pd->work.tray_tip);

        SetDlgItemTextW(c, IDC_START_IMAGE, pd->work.start_image);
        SetDlgItemTextW(c, IDC_WATCHING_IMAGE, pd->work.watching_image);
        SetDlgItemTextW(c, IDC_STOP_IMAGE, pd->work.stop_image);

        SendDlgItemMessageW(c, IDC_CHK_SHOW_CONSOLE, BM_SETCHECK, pd->work.show_console ? BST_CHECKED : BST_UNCHECKED, 0);
        SetDlgItemTextW(c, IDC_CONSOLE_COLOR, pd->work.console_color);
        SetDlgItemTextW(c, IDC_CONSOLE_TITLE, pd->work.console_title);
        SetDlgItemTextW(c, IDC_CONSOLE_MODE_ON, pd->work.console_mode_on);
        SetDlgItemTextW(c, IDC_CONSOLE_MODE_OFF, pd->work.console_mode_off);
        SetDlgItemTextW(c, IDC_CONSOLE_WAITING, pd->work.console_waiting);
        SetDlgItemTextW(c, IDC_CONSOLE_APPEARED, pd->work.console_target_appeared);
        SetDlgItemTextW(c, IDC_CONSOLE_RUNNING, pd->work.console_running);
        SetDlgItemTextW(c, IDC_CONSOLE_CLOSED, pd->work.console_target_closed);
        SetDlgItemTextW(c, IDC_CONSOLE_MISSION, pd->work.console_mission_completed);
        SetDlgItemTextW(c, IDC_CONSOLE_SPY_START, pd->work.console_spy_start);
        SetDlgItemTextW(c, IDC_CONSOLE_SPY_STOP, pd->work.console_spy_stop);

        SendDlgItemMessageW(c, IDC_PREVIEW_STATE, CB_SETCURSEL, pd->previewState, 0);
        refresh_preview(pd);
    }

    InvalidateRect(c, NULL, TRUE);
}

static void read_controls(PanelData* pd) {
    HWND c = pd->hContent;
    BOOL translated;

    pd->work.pause_seconds = GetDlgItemInt(c, IDC_PAUSE, &translated, TRUE);
    if (pd->work.pause_seconds < 1) pd->work.pause_seconds = 1;

    GetDlgItemTextW(c, IDC_START_TEXT, pd->work.start_text, _countof(pd->work.start_text));
    GetDlgItemTextW(c, IDC_WATCHING_TEXT, pd->work.watching_text, _countof(pd->work.watching_text));
    GetDlgItemTextW(c, IDC_STOP_TEXT, pd->work.stop_text, _countof(pd->work.stop_text));

    if (pd->advanced) {
        GetDlgItemTextW(c, IDC_COUNTDOWN_TEXT, pd->work.countdown_text, _countof(pd->work.countdown_text));

        pd->work.show_image = (SendDlgItemMessageW(c, IDC_CHK_SHOW_IMAGE, BM_GETCHECK, 0, 0) == BST_CHECKED);
        pd->work.image_replace = (SendDlgItemMessageW(c, IDC_CHK_REPLACE, BM_GETCHECK, 0, 0) == BST_CHECKED);
        pd->work.image_scale = (int)SendDlgItemMessageW(c, IDC_IMAGE_SCALE, TBM_GETPOS, 0, 0);
        if (pd->work.image_scale < 10) pd->work.image_scale = 10;
        if (pd->work.image_scale > 400) pd->work.image_scale = 400;

        GetDlgItemTextW(c, IDC_DATA_DIR, pd->work.data_dir, _countof(pd->work.data_dir));

        GetDlgItemTextW(c, IDC_START_TITLE, pd->work.start_title, _countof(pd->work.start_title));
        GetDlgItemTextW(c, IDC_START_BODY, pd->work.start_body, _countof(pd->work.start_body));
        GetDlgItemTextW(c, IDC_WATCHING_TITLE, pd->work.watching_title, _countof(pd->work.watching_title));
        GetDlgItemTextW(c, IDC_WATCHING_BODY, pd->work.watching_body, _countof(pd->work.watching_body));
        GetDlgItemTextW(c, IDC_STOP_TITLE, pd->work.stop_title, _countof(pd->work.stop_title));
        GetDlgItemTextW(c, IDC_STOP_BODY, pd->work.stop_body, _countof(pd->work.stop_body));
        GetDlgItemTextW(c, IDC_TRAY_TIP, pd->work.tray_tip, _countof(pd->work.tray_tip));

        GetDlgItemTextW(c, IDC_START_IMAGE, pd->work.start_image, _countof(pd->work.start_image));
        GetDlgItemTextW(c, IDC_WATCHING_IMAGE, pd->work.watching_image, _countof(pd->work.watching_image));
        GetDlgItemTextW(c, IDC_STOP_IMAGE, pd->work.stop_image, _countof(pd->work.stop_image));

        pd->work.show_console = (SendDlgItemMessageW(c, IDC_CHK_SHOW_CONSOLE, BM_GETCHECK, 0, 0) == BST_CHECKED);
        GetDlgItemTextW(c, IDC_CONSOLE_COLOR, pd->work.console_color, _countof(pd->work.console_color));
        GetDlgItemTextW(c, IDC_CONSOLE_TITLE, pd->work.console_title, _countof(pd->work.console_title));
        GetDlgItemTextW(c, IDC_CONSOLE_MODE_ON, pd->work.console_mode_on, _countof(pd->work.console_mode_on));
        GetDlgItemTextW(c, IDC_CONSOLE_MODE_OFF, pd->work.console_mode_off, _countof(pd->work.console_mode_off));
        GetDlgItemTextW(c, IDC_CONSOLE_WAITING, pd->work.console_waiting, _countof(pd->work.console_waiting));
        GetDlgItemTextW(c, IDC_CONSOLE_APPEARED, pd->work.console_target_appeared, _countof(pd->work.console_target_appeared));
        GetDlgItemTextW(c, IDC_CONSOLE_RUNNING, pd->work.console_running, _countof(pd->work.console_running));
        GetDlgItemTextW(c, IDC_CONSOLE_CLOSED, pd->work.console_target_closed, _countof(pd->work.console_target_closed));
        GetDlgItemTextW(c, IDC_CONSOLE_MISSION, pd->work.console_mission_completed, _countof(pd->work.console_mission_completed));
        GetDlgItemTextW(c, IDC_CONSOLE_SPY_START, pd->work.console_spy_start, _countof(pd->work.console_spy_start));
        GetDlgItemTextW(c, IDC_CONSOLE_SPY_STOP, pd->work.console_spy_stop, _countof(pd->work.console_spy_stop));
    }
}

/* ---------- 浏览 ---------- */
static void browse_image(HWND hwnd, PanelData* pd, int editId) {
    wchar_t path[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"图片文件\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff\0所有文件\0*.*\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) {
        SetDlgItemTextW(pd->hContent, editId, path);
    }
}

static void browse_data_dir(HWND hwnd, PanelData* pd) {
    HRESULT hrInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool need_uninit = SUCCEEDED(hrInit);

    IFileOpenDialog* dlg = NULL;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                   IID_IFileOpenDialog, (void**)&dlg)) && dlg) {
        DWORD opts = 0;
        if (SUCCEEDED(dlg->GetOptions(&opts))) {
            dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        }
        dlg->SetTitle(L"选择 spy 数据存储目录");

        if (SUCCEEDED(dlg->Show(hwnd))) {
            IShellItem* item = NULL;
            if (SUCCEEDED(dlg->GetResult(&item)) && item) {
                PWSTR path = NULL;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                    /* 立即做一次“可写”探测，避免选了个只读目录到 spy 时才发现写不进 */
                    bool writable = false;
                    wchar_t probe[MAX_PATH] = { 0 };
                    swprintf_s(probe, _countof(probe), L"%s\\.nm_write_test", path);
                    HANDLE hp = CreateFileW(probe, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                            CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
                    if (hp != INVALID_HANDLE_VALUE) {
                        CloseHandle(hp);
                        DeleteFileW(probe);
                        writable = true;
                    }

                    if (writable) {
                        SetDlgItemTextW(pd->hContent, IDC_DATA_DIR, path);
                    }
                    else {
                        wchar_t msg[160];
                        swprintf_s(msg, L"该目录不可写（err=%lu）。\n\n若是管理员仍被拒，请检查 Windows 安全中心 → 勒索软件防护 → 受控文件夹访问。", GetLastError());
                        MessageBoxW(hwnd, msg, L"NoMoreMonitor", MB_OK | MB_ICONWARNING);
                    }
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dlg->Release();
    }

    if (need_uninit) CoUninitialize();
}

/* ---------- 滚动 ---------- */
static void update_scroll(PanelData* pd) {
    RECT rc;
    GetClientRect(pd->hPanel, &rc);
    int clientH = rc.bottom - rc.top;

    SCROLLINFO si = { 0 };
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = pd->contentHeight - 1;
    si.nPage = clientH;
    si.nPos = pd->scrollY;
    SetScrollInfo(pd->hPanel, SB_VERT, &si, TRUE);
}

static void do_scroll(PanelData* pd, int newY) {
    RECT rc;
    GetClientRect(pd->hPanel, &rc);
    int clientH = rc.bottom - rc.top;
    int maxY = pd->contentHeight - clientH;
    if (maxY < 0) maxY = 0;
    if (newY < 0) newY = 0;
    if (newY > maxY) newY = maxY;
    pd->scrollY = newY;

    SetWindowPos(pd->hContent, NULL, 0, -pd->scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    update_scroll(pd);
}

/* ---------- 布局 ---------- */
static int add_edit_row(PanelData* pd, const wchar_t* label, int id, const wchar_t* value, int y, DWORD extra) {
    make_label(pd->hContent, label, LX, y, pd->hFont);
    make_edit(pd->hContent, id, value, LX + LABEL_W, y, pd->hFont, extra);
    return y + ROW_H + GAP;
}

static int add_path_row(PanelData* pd, const wchar_t* label, int editId, int btnId, const wchar_t* value, int y) {
    make_label(pd->hContent, label, LX, y, pd->hFont);
    make_edit(pd->hContent, editId, value, LX + LABEL_W, y, pd->hFont, 0);
    make_button(pd->hContent, btnId, L"浏览...", LX + LABEL_W + EDIT_W + 8, y, 72, pd->hFont);
    return y + ROW_H + GAP;
}

static void build_content(PanelData* pd) {
    if (pd->hContent) { DestroyWindow(pd->hContent); pd->hContent = NULL; pd->hPreview = NULL; }
    for (int i = 0; i < 4; i++) {
        if (pd->swatchBrush[i]) { DeleteObject(pd->swatchBrush[i]); pd->swatchBrush[i] = NULL; }
        pd->swatchColorSet[i] = false;
        pd->swatchColor[i] = RGB(0, 0, 0);
    }

    pd->hContent = CreateWindowExW(0, CONTENT_CLASS, L"", WS_CHILD | WS_VISIBLE,
                                   0, 0, CONTENT_W, 2000, pd->hPanel, NULL, pd->hInst, NULL);

    int y = 8;

    /* 专业模式开关 */
    make_check(pd->hContent, IDC_CHK_ADVANCED, L"专业模式（显示全部选项）", pd->advanced, LX, y, pd->hFont);
    y += 28;

    /* 通用 */
    make_group(pd->hContent, L"通用", LX, y, pd->hFont);
    y += 30;
    y = add_edit_row(pd, L"暂停秒数", IDC_PAUSE, L"5", y, ES_NUMBER);
    y += 4;

    /* OSD 文字 */
    make_group(pd->hContent, L"OSD 文字", LX, y, pd->hFont);
    y += 30;
    y = add_edit_row(pd, L"开始文字", IDC_START_TEXT, pd->work.start_text, y, 0);
    y = add_edit_row(pd, L"监视中文字", IDC_WATCHING_TEXT, pd->work.watching_text, y, 0);
    y = add_edit_row(pd, L"结束文字", IDC_STOP_TEXT, pd->work.stop_text, y, 0);
    y += 4;

    /* OSD 颜色 */
    make_group(pd->hContent, L"OSD 颜色", LX, y, pd->hFont);
    y += 30;
    {
        struct { int id; const wchar_t* lbl; } rows[] = {
            { IDC_COLOR_START, L"开始颜色" },
            { IDC_COLOR_WATCHING, L"监视中颜色" },
            { IDC_COLOR_STOP, L"结束颜色" },
            { IDC_COLOR_LOG, L"日志颜色" },
        };
        for (int i = 0; i < 4; i++) {
            make_label(pd->hContent, rows[i].lbl, LX, y, pd->hFont);
            make_swatch(pd->hContent, rows[i].id, LX + LABEL_W, y, pd->hFont);
            y += ROW_H + GAP;
        }
    }
    y += 4;

    if (pd->advanced) {
        /* 倒计时文字 */
        make_group(pd->hContent, L"倒计时", LX, y, pd->hFont);
        y += 30;
        y = add_edit_row(pd, L"倒计时文字", IDC_COUNTDOWN_TEXT, pd->work.countdown_text, y, 0);
        y += 4;

        /* 图像 */
        make_group(pd->hContent, L"图像（三状态各一张，PNG/JPG 等）", LX, y, pd->hFont);
        y += 30;
        make_check(pd->hContent, IDC_CHK_SHOW_IMAGE, L"显示图像", pd->work.show_image, LX, y, pd->hFont);
        y += 26;
        make_check(pd->hContent, IDC_CHK_REPLACE, L"图片替代文字（否则叠加）", pd->work.image_replace, LX, y, pd->hFont);
        y += 28;
        {
            make_label(pd->hContent, L"显示缩放", LX, y, pd->hFont);
            HWND scale = make_ctl(pd->hContent, TRACKBAR_CLASSW, L"",
                                  WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS,
                                  LX + LABEL_W, y, 220, 28, IDC_IMAGE_SCALE, pd->hFont);
            SendMessageW(scale, TBM_SETRANGE, TRUE, MAKELPARAM(10, 400));
            SendMessageW(scale, TBM_SETTICFREQ, 10, 0);
            SendMessageW(scale, TBM_SETPOS, TRUE, pd->work.image_scale);
            make_ctl(pd->hContent, L"STATIC", L"", 0, LX + LABEL_W + 232, y + 4, 80, 20, IDC_IMAGE_SCALE_LBL, pd->hFont);
            y += ROW_H + GAP;
        }
        y = add_path_row(pd, L"开始图片", IDC_START_IMAGE, IDC_START_IMAGE_BROWSE, pd->work.start_image, y);
        y = add_path_row(pd, L"监视中图片", IDC_WATCHING_IMAGE, IDC_WATCHING_IMAGE_BROWSE, pd->work.watching_image, y);
        y = add_path_row(pd, L"结束图片", IDC_STOP_IMAGE, IDC_STOP_IMAGE_BROWSE, pd->work.stop_image, y);
        {
            make_label(pd->hContent, L"预览", LX, y, pd->hFont);
            HWND combo = make_combo(pd->hContent, IDC_PREVIEW_STATE, LX + LABEL_W, y, 200, pd->hFont);
            SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"开始图片");
            SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"监视中图片");
            SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"结束图片");
            SendMessageW(combo, CB_SETCURSEL, 0, 0);
            make_button(pd->hContent, IDC_BTN_PREVIEW, L"预览", LX + LABEL_W + 210, y, 60, pd->hFont);
            y += 30;

            pd->hPreview = CreateWindowExW(WS_EX_STATICEDGE, PREVIEW_CLASS, L"",
                                           WS_CHILD | WS_VISIBLE,
                                           LX + LABEL_W, y, PREVIEW_W, PREVIEW_H,
                                           pd->hContent, NULL, pd->hInst, NULL);
            pd->oldPreviewProc = (WNDPROC)SetWindowLongPtrW(pd->hPreview, GWLP_WNDPROC, (LONG_PTR)PreviewProc);
            SetWindowLongPtrW(pd->hPreview, GWLP_USERDATA, (LONG_PTR)pd);
            y += PREVIEW_H + 8;
        }

        /* 气泡通知 + 托盘 */
        make_group(pd->hContent, L"气泡通知 + 托盘", LX, y, pd->hFont);
        y += 30;
        y = add_edit_row(pd, L"开始标题", IDC_START_TITLE, pd->work.start_title, y, 0);
        y = add_edit_row(pd, L"开始正文", IDC_START_BODY, pd->work.start_body, y, 0);
        y = add_edit_row(pd, L"监视中标题", IDC_WATCHING_TITLE, pd->work.watching_title, y, 0);
        y = add_edit_row(pd, L"监视中正文", IDC_WATCHING_BODY, pd->work.watching_body, y, 0);
        y = add_edit_row(pd, L"结束标题", IDC_STOP_TITLE, pd->work.stop_title, y, 0);
        y = add_edit_row(pd, L"结束正文", IDC_STOP_BODY, pd->work.stop_body, y, 0);
        y = add_edit_row(pd, L"托盘提示", IDC_TRAY_TIP, pd->work.tray_tip, y, 0);
        y += 4;

        /* 数据目录 */
        make_group(pd->hContent, L"数据存储", LX, y, pd->hFont);
        y += 30;
        y = add_path_row(pd, L"数据目录", IDC_DATA_DIR, IDC_DATA_DIR_BROWSE, pd->work.data_dir, y);
        y += 4;

        /* 控制台 */
        make_group(pd->hContent, L"控制台", LX, y, pd->hFont);
        y += 30;
        make_check(pd->hContent, IDC_CHK_SHOW_CONSOLE, L"显示控制台窗口", pd->work.show_console, LX, y, pd->hFont);
        y += 26;
        y = add_edit_row(pd, L"控制台颜色(如 0E)", IDC_CONSOLE_COLOR, pd->work.console_color, y, 0);
        y = add_edit_row(pd, L"窗口标题", IDC_CONSOLE_TITLE, pd->work.console_title, y, 0);
        y = add_edit_row(pd, L"模式-有通知", IDC_CONSOLE_MODE_ON, pd->work.console_mode_on, y, 0);
        y = add_edit_row(pd, L"模式-仅通知", IDC_CONSOLE_MODE_OFF, pd->work.console_mode_off, y, 0);
        y = add_edit_row(pd, L"等待文字", IDC_CONSOLE_WAITING, pd->work.console_waiting, y, 0);
        y = add_edit_row(pd, L"发现目标文字", IDC_CONSOLE_APPEARED, pd->work.console_target_appeared, y, 0);
        y = add_edit_row(pd, L"运行文字", IDC_CONSOLE_RUNNING, pd->work.console_running, y, 0);
        y = add_edit_row(pd, L"目标关闭文字", IDC_CONSOLE_CLOSED, pd->work.console_target_closed, y, 0);
        y = add_edit_row(pd, L"完成文字", IDC_CONSOLE_MISSION, pd->work.console_mission_completed, y, 0);
        y = add_edit_row(pd, L"开始日志", IDC_CONSOLE_SPY_START, pd->work.console_spy_start, y, 0);
        y = add_edit_row(pd, L"停止日志", IDC_CONSOLE_SPY_STOP, pd->work.console_spy_stop, y, 0);
        y += 8;
    }
    else {
        /* 简单模式提示 */
        make_hint(pd->hContent, L"提示：图片、气泡通知、数据目录、控制台文字等高级选项，请勾选上方「专业模式」后设置。", LX, y, pd->hFont);
        y += 32;
    }

    /* 底部按钮 */
    make_button(pd->hContent, IDC_BTN_RESET, L"恢复默认", LX, y, 100, pd->hFont);
    make_button(pd->hContent, IDC_BTN_SAVE, L"保存", LX + 110, y, 90, pd->hFont);
    make_button(pd->hContent, IDC_BTN_CANCEL, L"取消", LX + 210, y, 90, pd->hFont);
    y += 40;

    pd->contentHeight = y;
    pd->scrollY = 0;
    SetWindowPos(pd->hContent, NULL, 0, 0, CONTENT_W, y, SWP_NOMOVE | SWP_NOZORDER);
    update_scroll(pd);
}

/* ---------- 内容窗口：把控件通知转发给面板 ---------- */
static LRESULT CALLBACK ContentProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    HWND parent = GetParent(hwnd);
    if (parent && (msg == WM_COMMAND || msg == WM_CTLCOLORSTATIC || msg == WM_DRAWITEM ||
                   msg == WM_NOTIFY || msg == WM_HSCROLL || msg == WM_VSCROLL ||
                   msg == WM_MOUSEWHEEL || msg == WM_MEASUREITEM)) {
        return SendMessageW(parent, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* ---------- 面板窗口过程 ---------- */
static LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PanelData* pd = (PanelData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        struct CreateInfo { HINSTANCE hInst; AppConfig* cfg; };
        CreateInfo* ci = (CreateInfo*)cs->lpCreateParams;

        pd = new PanelData();
        ZeroMemory(pd, sizeof(*pd));
        pd->target = ci->cfg;
        pd->work = *(ci->cfg);
        pd->hPanel = hwnd;
        pd->hInst = ci->hInst;
        pd->scrollY = 0;
        pd->previewState = 0;
        pd->previewPath[0] = L'\0';
        pd->advanced = pd->work.advanced_mode;   /* 记住上次的专业模式状态 */
        pd->hFont = create_msg_font();
        pd->hFontCreated = (pd->hFont != NULL);
        if (!pd->hFont) pd->hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pd);

        build_content(pd);
        write_controls(pd);
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        HWND ctl = (HWND)lParam;
        int id = GetDlgCtrlID(ctl);
        int idx = swatch_index(id);
        if (idx >= 0 && pd) {
            COLORREF c = get_color_by_id(pd, id);
            /* 仅当颜色变化时才重建画刷，避免每次重绘都创建/删除 GDI 对象 */
            if (!pd->swatchBrush[idx] || !pd->swatchColorSet[idx] || pd->swatchColor[idx] != c) {
                if (pd->swatchBrush[idx]) { DeleteObject(pd->swatchBrush[idx]); pd->swatchBrush[idx] = NULL; }
                pd->swatchBrush[idx] = CreateSolidBrush(c);
                pd->swatchColor[idx] = c;
                pd->swatchColorSet[idx] = true;
            }
            SetBkColor(hdc, c);
            return (LRESULT)pd->swatchBrush[idx];
        }
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        /* 注意：STN_CLICKED 与 BN_CLICKED 都是 0，必须按控件 ID 区分，
           否则会把普通按钮点击也吞掉 */
        if (code == STN_CLICKED && swatch_index(id) >= 0) {
            int idx = swatch_index(id);
            if (idx >= 0 && pd) {
                static COLORREF cust[16] = { 0 };
                CHOOSECOLORW cc = { 0 };
                cc.lStructSize = sizeof(cc);
                cc.hwndOwner = hwnd;
                cc.rgbResult = get_color_by_id(pd, id);
                cc.lpCustColors = cust;
                cc.Flags = CC_RGBINIT | CC_FULLOPEN;
                if (ChooseColorW(&cc)) {
                    set_color_by_id(pd, id, cc.rgbResult);
                    InvalidateRect(GetDlgItem(pd->hContent, id), NULL, TRUE);
                }
            }
            return 0;
        }

        if (code == EN_CHANGE && (id == IDC_START_IMAGE || id == IDC_WATCHING_IMAGE || id == IDC_STOP_IMAGE)) {
            if (pd) {
                pd->previewState = (id == IDC_START_IMAGE) ? 0 : (id == IDC_WATCHING_IMAGE ? 1 : 2);
                refresh_preview(pd);
            }
            return 0;
        }

        if (code == CBN_SELCHANGE && id == IDC_PREVIEW_STATE) {
            if (pd) {
                pd->previewState = (int)SendDlgItemMessageW(pd->hContent, IDC_PREVIEW_STATE, CB_GETCURSEL, 0, 0);
                refresh_preview(pd);
            }
            return 0;
        }

        if (code == BN_CLICKED) {
            switch (id) {
            case IDC_CHK_ADVANCED:
                if (pd) {
                    read_controls(pd);
                    pd->advanced = (SendDlgItemMessageW(pd->hContent, IDC_CHK_ADVANCED, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    build_content(pd);
                    write_controls(pd);
                }
                return 0;

            case IDC_START_IMAGE_BROWSE: if (pd) { browse_image(hwnd, pd, IDC_START_IMAGE); pd->previewState = 0; refresh_preview(pd); } return 0;
            case IDC_WATCHING_IMAGE_BROWSE: if (pd) { browse_image(hwnd, pd, IDC_WATCHING_IMAGE); pd->previewState = 1; refresh_preview(pd); } return 0;
            case IDC_STOP_IMAGE_BROWSE: if (pd) { browse_image(hwnd, pd, IDC_STOP_IMAGE); pd->previewState = 2; refresh_preview(pd); } return 0;
            case IDC_DATA_DIR_BROWSE: if (pd) { browse_data_dir(hwnd, pd); } return 0;

            case IDC_BTN_PREVIEW:
                if (pd) {
                    int sel = (int)SendDlgItemMessageW(pd->hContent, IDC_PREVIEW_STATE, CB_GETCURSEL, 0, 0);
                    int editId = (sel == 1) ? IDC_WATCHING_IMAGE : (sel == 2) ? IDC_STOP_IMAGE : IDC_START_IMAGE;
                    wchar_t path[MAX_PATH] = { 0 };
                    GetDlgItemTextW(pd->hContent, editId, path, MAX_PATH);
                    if (path[0]) {
                        if (!show_image_preview(path, pd->work.image_scale)) {
                            MessageBoxW(hwnd, L"无法加载该图片文件，请确认路径与文件格式。", L"预览", MB_OK | MB_ICONWARNING);
                        }
                    }
                    else {
                        MessageBoxW(hwnd, L"请先设置该状态的图片路径", L"预览", MB_OK | MB_ICONINFORMATION);
                    }
                }
                return 0;

            case IDC_BTN_SAVE:
                if (pd) {
                    read_controls(pd);
                    pd->work.advanced_mode = pd->advanced;   /* 记住专业模式开关 */
                    if (config_save(&pd->work, NULL)) {
                        *(pd->target) = pd->work;
                        /* 控制台颜色/标题/显隐立即生效，无需重启 */
                        config_apply_console();
                        MessageBoxW(hwnd, L"设置已保存。", L"NoMoreMonitor", MB_OK | MB_ICONINFORMATION);
                        DestroyWindow(hwnd);
                    }
                    else {
                        MessageBoxW(hwnd, L"保存失败：无法写入配置文件。请确认程序目录可写。", L"NoMoreMonitor", MB_OK | MB_ICONERROR);
                    }
                }
                return 0;

            case IDC_BTN_CANCEL:
                DestroyWindow(hwnd);
                return 0;

            case IDC_BTN_RESET:
                if (pd) {
                    config_defaults(&pd->work);
                    write_controls(pd);
                }
                return 0;
            }
        }
        break;
    }

    case WM_HSCROLL: {
        /* 图片显示缩放滑动条 */
        if (pd && (HWND)lParam == GetDlgItem(pd->hContent, IDC_IMAGE_SCALE)) {
            int pos = (int)SendDlgItemMessageW(pd->hContent, IDC_IMAGE_SCALE, TBM_GETPOS, 0, 0);
            pd->work.image_scale = pos;
            wchar_t buf[32];
            swprintf_s(buf, L"%d%%", pos);
            SetDlgItemTextW(pd->hContent, IDC_IMAGE_SCALE_LBL, buf);
            return 0;
        }
        break;
    }

    case WM_VSCROLL: {
        if (!pd) break;
        int action = LOWORD(wParam);
        int newY = pd->scrollY;
        int line = 40;
        switch (action) {
        case SB_LINEUP: newY -= line; break;
        case SB_LINEDOWN: newY += line; break;
        case SB_PAGEUP: newY -= 300; break;
        case SB_PAGEDOWN: newY += 300; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: newY = HIWORD(wParam); break;
        }
        do_scroll(pd, newY);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        if (!pd) break;
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        do_scroll(pd, pd->scrollY - (delta / 120) * 40);
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY: {
        if (pd) {
            for (int i = 0; i < 4; i++) {
                if (pd->swatchBrush[i]) DeleteObject(pd->swatchBrush[i]);
            }
            if (pd->hFontCreated && pd->hFont) DeleteObject(pd->hFont);
            delete pd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        PostQuitMessage(0);
        return 0;
    }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void config_panel_show(HINSTANCE hInstance, AppConfig* cfg) {
    if (!cfg) return;

    /* 注册 trackbar 等通用控件（滑动条） */
    {
        static bool cc_ready = false;
        if (!cc_ready) {
            INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };
            InitCommonControlsEx(&icc);
            cc_ready = true;
        }
    }

    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = { 0 };
        wc.lpfnWndProc = PanelProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = PANEL_CLASS;
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        RegisterClassW(&wc);

        WNDCLASSW ct = { 0 };
        ct.lpfnWndProc = ContentProc;
        ct.hInstance = hInstance;
        ct.lpszClassName = CONTENT_CLASS;
        ct.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        ct.hCursor = LoadCursorW(NULL, IDC_ARROW);
        RegisterClassW(&ct);

        WNDCLASSW pv = { 0 };
        pv.lpfnWndProc = DefWindowProcW;
        pv.hInstance = hInstance;
        pv.lpszClassName = PREVIEW_CLASS;
        pv.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        pv.hCursor = LoadCursorW(NULL, IDC_ARROW);
        RegisterClassW(&pv);

        registered = true;
    }

    struct CreateInfo { HINSTANCE hInst; AppConfig* cfg; };
    CreateInfo ci = { hInstance, cfg };

    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, PANEL_CLASS, L"设置 - NoMoreMonitor",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VSCROLL,
                                CW_USEDEFAULT, CW_USEDEFAULT, 720, 720,
                                NULL, NULL, hInstance, &ci);
    if (!hwnd) return;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        if (IsDialogMessageW(hwnd, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}
