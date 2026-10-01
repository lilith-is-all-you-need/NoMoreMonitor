#include "preview_window.h"
#include "img_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#define PV_CLS L"[Lilith]NoMoreMonitorPreviewWindow"

struct pv_data {
    HBITMAP bmp;
    HDC hdc;
    HGDIOBJ old_bmp;
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
            /* 先取消选中的位图再删除 DC，避免 GDI 对象泄漏/句柄耗尽 */
            if (pd->hdc && pd->old_bmp) SelectObject(pd->hdc, pd->old_bmp);
            if (pd->bmp) DeleteObject(pd->bmp);
            if (pd->hdc) DeleteDC(pd->hdc);
            free(pd);
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool show_image_preview(const wchar_t* path, int scale_percent) {
    int w = 0, h = 0;
    unsigned char* px = img_load_file(path, &w, &h);
    if (!px || w <= 0 || h <= 0) {
        img_free(px);
        return false;
    }

    /* 与 OSD 一致的缩放：适配屏幕基准 × 用户缩放比例（小图放大、大图缩小），预览即实际显示尺寸 */
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    if (sw <= 0 || sh <= 0) {
        img_free(px);
        return false;
    }
    if (scale_percent < 10) scale_percent = 10;
    if (scale_percent > 400) scale_percent = 400;
    double fit = ((double)sw / w) < ((double)sh / h) ? ((double)sw / w) : ((double)sh / h);
    double s = fit * scale_percent / 100.0;
    if (s < 1e-6) s = 1e-6;
    int nw = (int)(w * s), nh = (int)(h * s);
    if (nw < 1) nw = 1;
    if (nh < 1) nh = 1;
    px = img_scale_rgba(px, &w, &h, nw, nh);   /* Catmull-Rom 锐化缩放；失败时保持原尺寸 */

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
    if (!hdc) {
        DeleteObject(bmp);
        return false;
    }
    HGDIOBJ old_bmp = SelectObject(hdc, bmp);

    int x = (sw - w) / 2, y = (sh - h) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST, PV_CLS,
                                L"NoMoreMonitor 预览（点击关闭）",
                                WS_POPUP, x, y, w, h, NULL, NULL, inst, NULL);
    if (!hwnd) {
        SelectObject(hdc, old_bmp);
        DeleteObject(bmp);
        DeleteDC(hdc);
        return false;
    }

    struct pv_data* pd = (struct pv_data*)malloc(sizeof(struct pv_data));
    if (!pd) {
        DestroyWindow(hwnd);
        SelectObject(hdc, old_bmp);
        DeleteObject(bmp);
        DeleteDC(hdc);
        return false;
    }
    pd->bmp = bmp;
    pd->hdc = hdc;
    pd->old_bmp = old_bmp;
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
