#include "preview_window.h"
#include "img_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#define PV_CLS L"[Lilith]NoMoreMonitorPreviewWindow"

struct pv_data {
    HBITMAP bmp;
    HDC hdc;
};

static LRESULT CALLBACK pv_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    struct pv_data* pd = (struct pv_data*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_KEYDOWN:
        DestroyWindow(hwnd);
        return 0;
    case WM_NCDESTROY:
        if (pd) {
            if (pd->hdc) DeleteDC(pd->hdc);
            if (pd->bmp) DeleteObject(pd->bmp);
            free(pd);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool show_image_preview(const wchar_t* path) {
    int w = 0, h = 0;
    unsigned char* px = img_load_file(path, &w, &h);
    if (!px || w <= 0 || h <= 0) {
        img_free(px);
        return false;
    }

    /* 超过屏幕 80% 则等比缩小到能放下 */
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int max_w = sw * 80 / 100;
    int max_h = sh * 80 / 100;
    if (w > max_w || h > max_h) {
        double s = ((double)max_w / w) < ((double)max_h / h) ? ((double)max_w / w) : ((double)max_h / h);
        int nw = (int)(w * s), nh = (int)(h * s);
        if (nw < 1) nw = 1;
        if (nh < 1) nh = 1;
        px = img_scale_rgba(px, &w, &h, nw, nh);
        if (!px) return false;
    }

    HINSTANCE inst = GetModuleHandleW(NULL);
    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = pv_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_HAND);
    wc.lpszClassName = PV_CLS;
    RegisterClassExW(&wc);   /* 已注册则失败，忽略即可 */

    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;   /* 自上而下 */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp || !bits) {
        img_free(px);
        return false;
    }

    /* RGBA → 预乘 BGRA */
    BYTE* src = px;
    BYTE* dst = (BYTE*)bits;
    for (int i = 0; i < w * h; i++) {
        int a = src[i * 4 + 3];
        dst[i * 4 + 0] = (BYTE)(src[i * 4 + 2] * a / 255);
        dst[i * 4 + 1] = (BYTE)(src[i * 4 + 1] * a / 255);
        dst[i * 4 + 2] = (BYTE)(src[i * 4 + 0] * a / 255);
        dst[i * 4 + 3] = (BYTE)a;
    }
    img_free(px);

    HDC hdc = CreateCompatibleDC(NULL);
    SelectObject(hdc, bmp);

    int x = (sw - w) / 2, y = (sh - h) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST, PV_CLS,
                                L"NoMoreMonitor 预览（点击关闭）",
                                WS_POPUP, x, y, w, h, NULL, NULL, inst, NULL);
    if (!hwnd) {
        DeleteDC(hdc);
        DeleteObject(bmp);
        return false;
    }

    struct pv_data* pd = (struct pv_data*)malloc(sizeof(struct pv_data));
    if (!pd) {
        DestroyWindow(hwnd);
        DeleteDC(hdc);
        DeleteObject(bmp);
        return false;
    }
    pd->bmp = bmp;
    pd->hdc = hdc;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pd);

    POINT window_pt = { x, y };
    SIZE window_size = { w, h };
    POINT start_pt = { 0, 0 };
    BLENDFUNCTION blend_opt = { 0 };
    blend_opt.AlphaFormat = AC_SRC_ALPHA;
    blend_opt.SourceConstantAlpha = 255;
    blend_opt.BlendOp = AC_SRC_OVER;
    UpdateLayeredWindow(hwnd, NULL, &window_pt, &window_size, hdc, &start_pt, 0, &blend_opt, ULW_ALPHA);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    return true;
}
