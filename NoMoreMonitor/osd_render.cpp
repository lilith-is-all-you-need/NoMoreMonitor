#include "osd_render.h"
#include "messager.h"
#include "img_loader.h"
#include <gdiplus.h>
#include <stdlib.h>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

static ULONG_PTR g_gdiplusToken = 0;
static bool g_osd_ready = false;

bool osd_init(void) {
    GdiplusStartupInput si;
    si.GdiplusVersion = 1;
    if (GdiplusStartup(&g_gdiplusToken, &si, NULL) != Ok) {
        return false;
    }
    g_osd_ready = true;
    return true;
}

void osd_shutdown(void) {
    if (g_osd_ready) {
        GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
        g_osd_ready = false;
    }
}

/*
 * 按九宫格位置绘制文字。
 * pos: 0左上 1上 2右上 3左 4中 5右 6左下 7下 8右下
 * ox/oy: 像素微调；vshift: 额外向下偏移（日志行用）
 */
static void draw_text_at(Graphics* g, const wchar_t* text, int w, int h,
                         COLORREF color, int alpha, bool big,
                         int pos, int ox, int oy, int vshift) {
    if (!text || text[0] == L'\0') return;
    if (alpha <= 0) return;

    FontFamily ff(L"Microsoft YaHei");
    REAL size = big ? 120.0f : 40.0f;
    Font font(&ff, size, FontStyleBold, UnitPixel);
    SolidBrush brush(Color((BYTE)alpha, GetRValue(color), GetGValue(color), GetBValue(color)));

    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentNear);
    fmt.SetLineAlignment(StringAlignmentNear);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsNoClip);

    RectF layout(0.0f, 0.0f, (REAL)w, (REAL)h);
    RectF bounds;
    g->MeasureString(text, -1, &font, layout, &fmt, &bounds);
    REAL bw = bounds.Width;
    REAL bh = bounds.Height;
    if (bw <= 0.0f) bw = 1.0f;
    if (bh <= 0.0f) bh = 1.0f;

    if (pos < 0 || pos > 8) pos = 4;
    int hpos = pos % 3;   /* 0左 1中 2右 */
    int vpos = pos / 3;   /* 0上 1中 2下 */

    REAL x, y;
    if (hpos == 0)      x = (REAL)ox;
    else if (hpos == 1) x = ((REAL)w - bw) / 2.0f + (REAL)ox;
    else                x = (REAL)w - bw + (REAL)ox;

    if (vpos == 0)      y = (REAL)oy;
    else if (vpos == 1) y = ((REAL)h - bh) / 2.0f + (REAL)oy;
    else                y = (REAL)h - bh + (REAL)oy;

    y += (REAL)vshift;

    /* 收敛到可视区域 */
    if (x < 0.0f) x = 0.0f;
    if (y < 0.0f) y = 0.0f;
    if (x + bw > (REAL)w) x = (REAL)w - bw;
    if (y + bh > (REAL)h) y = (REAL)h - bh;

    RectF dest(x, y, bw, bh);
    g->DrawString(text, -1, &font, dest, &fmt, &brush);
}

static bool draw_image_fit(Graphics* g, const wchar_t* path, int w, int h) {
    if (!path || path[0] == L'\0') return false;

    /* stb_image 纯 C 解码（无 WIC/COM/WinRT），参照 NoMoreCapture */
    int iw = 0, ih = 0;
    unsigned char* px = img_load_file(path, &iw, &ih);
    if (!px || iw <= 0 || ih <= 0) { img_free(px); return false; }

    /* stbi RGBA(直通 alpha) → 预乘 BGRA，供 GDI+ PixelFormat32bppPARGB 使用 */
    unsigned char* pargb = (unsigned char*)malloc((size_t)iw * ih * 4);
    if (!pargb) { img_free(px); return false; }
    for (int i = 0; i < iw * ih; i++) {
        int a = px[i * 4 + 3];
        pargb[i * 4 + 0] = (BYTE)(px[i * 4 + 2] * a / 255);  /* B */
        pargb[i * 4 + 1] = (BYTE)(px[i * 4 + 1] * a / 255);  /* G */
        pargb[i * 4 + 2] = (BYTE)(px[i * 4 + 0] * a / 255);  /* R */
        pargb[i * 4 + 3] = (BYTE)a;
    }
    img_free(px);

    REAL scale = ((REAL)w / (REAL)iw) < ((REAL)h / (REAL)ih)
        ? ((REAL)w / (REAL)iw)
        : ((REAL)h / (REAL)ih);
    int dw = (int)(iw * scale);
    int dh = (int)(ih * scale);
    int dx = (w - dw) / 2;
    int dy = (h - dh) / 2;

    bool ok = false;
    {
        Bitmap bmp(iw, ih, iw * 4, PixelFormat32bppPARGB, pargb);
        if (bmp.GetLastStatus() == Ok) {
            g->SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g->DrawImage(&bmp, dx, dy, dw, dh);
            ok = true;
        }
    }
    free(pargb);
    return ok;
}

void osd_render(HWND hwnd, int state, const AppConfig* cfg, const wchar_t* log_text, float text_alpha) {
    if (!g_osd_ready || !hwnd || !cfg) return;

    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);
    if (w <= 0 || h <= 0) return;

    const wchar_t* state_text = NULL;
    COLORREF state_color = RGB(255, 255, 255);
    const wchar_t* image_path = NULL;

    switch (state) {
    case status_start:
        state_text = cfg->start_text;
        state_color = cfg->start_color;
        image_path = cfg->start_image;
        break;
    case status_watching:
        state_text = cfg->watching_text;
        state_color = cfg->watching_color;
        image_path = cfg->watching_image;
        break;
    case status_stop:
        state_text = cfg->stop_text;
        state_color = cfg->stop_color;
        image_path = cfg->stop_image;
        break;
    default:
        break;
    }

    Bitmap bmp(w, h, PixelFormat32bppPARGB);
    Graphics g(&bmp);
    g.SetCompositingMode(CompositingModeSourceOver);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.Clear(Color(0, 0, 0, 0));

    bool has_image = false;
    if (cfg->show_image) {
        has_image = draw_image_fit(&g, image_path, w, h);
    }

    bool draw_text = !(has_image && cfg->image_replace);
    if (draw_text && state_text && state_text[0] != L'\0' && state != status_waiting) {
        int alpha = (int)(text_alpha * 255.0f + 0.5f);
        if (alpha < 0) alpha = 0;
        if (alpha > 255) alpha = 255;
        draw_text_at(&g, state_text, w, h, state_color, alpha, true,
                     cfg->text_position, cfg->text_offset_x, cfg->text_offset_y, 0);
    }

    if (log_text && log_text[0] != L'\0') {
        draw_text_at(&g, log_text, w, h, cfg->log_color, 255, false,
                     cfg->text_position, cfg->text_offset_x, cfg->text_offset_y, 250);
    }

    /* 通过 UpdateLayeredWindow 呈现(逐像素 alpha) */
    HBITMAP hbm = NULL;
    if (bmp.GetHBITMAP(Color(0, 0, 0, 0), &hbm) != Ok || hbm == NULL) {
        return;
    }

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HGDIOBJ hOld = SelectObject(hdcMem, hbm);

    POINT ptSrc = { 0, 0 };
    POINT ptDst = { 0, 0 };
    SIZE sz = { w, h };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    UpdateLayeredWindow(hwnd, hdcScreen, &ptDst, &sz, hdcMem, &ptSrc, 0, &bf, ULW_ALPHA);

    SelectObject(hdcMem, hOld);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
}
