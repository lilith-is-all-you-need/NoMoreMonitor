#include "osd_render.h"
#include "messager.h"
#include "img_loader.h"
#include <gdiplus.h>
#include <stdlib.h>
#include <wchar.h>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

static ULONG_PTR g_gdiplusToken = 0;
static bool g_osd_ready = false;

/* OSD 图片缓存：路径/缩放不变时复用已缩放/预乘的位图，避免淡出期间反复读盘解码 */
static wchar_t g_img_cache_path[MAX_PATH] = { 0 };
static unsigned char* g_img_cache_bgra = NULL;   /* 预乘 BGRA */
static int g_img_cache_w = 0;
static int g_img_cache_h = 0;
static int g_img_cache_scale = 0;

static void clear_img_cache(void) {
    if (g_img_cache_bgra) { free(g_img_cache_bgra); g_img_cache_bgra = NULL; }
    g_img_cache_path[0] = L'\0';
    g_img_cache_w = 0;
    g_img_cache_h = 0;
    g_img_cache_scale = 0;
}

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
        clear_img_cache();
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

static bool draw_image_fit(Graphics* g, const wchar_t* path, int w, int h, int scale_percent, int alpha) {
    if (!path || path[0] == L'\0') return false;
    if (scale_percent < 10) scale_percent = 10;
    if (scale_percent > 400) scale_percent = 400;
    if (alpha <= 0) return false;

    /* 路径/缩放变化才重新加载+缩放（淡出期间反复渲染时直接复用缓存） */
    if (wcscmp(g_img_cache_path, path) != 0 || g_img_cache_scale != scale_percent ||
        g_img_cache_w <= 0 || g_img_cache_h <= 0) {
        clear_img_cache();

        int iw = 0, ih = 0;
        unsigned char* px = img_load_file(path, &iw, &ih);
        if (!px || iw <= 0 || ih <= 0) { img_free(px); return false; }

        /* 适配屏幕的基准缩放，再乘用户缩放比例 */
        double fit = ((double)w / iw) < ((double)h / ih) ? ((double)w / iw) : ((double)h / ih);
        double scale = fit * scale_percent / 100.0;
        if (scale < 1e-6) scale = 1e-6;
        int dw = (int)(iw * scale), dh = (int)(ih * scale);
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;

        /* Catmull-Rom 锐化缩放（stb_image_resize2），比 GDI+ 双三次更清晰 */
        unsigned char* scaled = img_scale_rgba(px, &iw, &ih, dw, dh);
        if (scaled == px) { dw = iw; dh = ih; }   /* 缩放失败则回退原尺寸 */

        /* RGBA(直通 alpha) → 预乘 BGRA，供 GDI+ PixelFormat32bppPARGB 使用 */
        unsigned char* bgra = (unsigned char*)malloc((size_t)dw * dh * 4);
        if (!bgra) { img_free(scaled); return false; }
        for (int i = 0; i < dw * dh; i++) {
            int a = scaled[i * 4 + 3];
            bgra[i * 4 + 0] = (BYTE)(scaled[i * 4 + 2] * a / 255);  /* B */
            bgra[i * 4 + 1] = (BYTE)(scaled[i * 4 + 1] * a / 255);  /* G */
            bgra[i * 4 + 2] = (BYTE)(scaled[i * 4 + 0] * a / 255);  /* R */
            bgra[i * 4 + 3] = (BYTE)a;
        }
        img_free(scaled);

        wcsncpy_s(g_img_cache_path, _countof(g_img_cache_path), path, _TRUNCATE);
        g_img_cache_bgra = bgra;
        g_img_cache_w = dw;
        g_img_cache_h = dh;
        g_img_cache_scale = scale_percent;
    }

    int dx = (w - g_img_cache_w) / 2;
    int dy = (h - g_img_cache_h) / 2;

    /* 已经缩放到目标尺寸，1:1 绘制，不再二次插值；按 alpha 整体淡入淡出 */
    Bitmap bmp(g_img_cache_w, g_img_cache_h, g_img_cache_w * 4, PixelFormat32bppPARGB, g_img_cache_bgra);
    if (bmp.GetLastStatus() != Ok) return false;
    g->SetInterpolationMode(InterpolationModeNearestNeighbor);

    if (alpha >= 255) {
        g->DrawImage(&bmp, dx, dy, g_img_cache_w, g_img_cache_h);
    }
    else {
        ColorMatrix cm = { 0 };
        cm.m[0][0] = 1.0f;
        cm.m[1][1] = 1.0f;
        cm.m[2][2] = 1.0f;
        cm.m[3][3] = (REAL)alpha / 255.0f;
        cm.m[4][4] = 1.0f;
        ImageAttributes ia;
        ia.SetColorMatrix(&cm, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
        Rect dest(dx, dy, g_img_cache_w, g_img_cache_h);
        g->DrawImage(&bmp, dest, 0, 0, g_img_cache_w, g_img_cache_h, UnitPixel, &ia);
    }
    return true;
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

    int alpha = (int)(text_alpha * 255.0f + 0.5f);
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;

    bool has_image = false;
    if (cfg->show_image && alpha > 0) {
        has_image = draw_image_fit(&g, image_path, w, h, cfg->image_scale, alpha);
    }

    bool draw_text = !(has_image && cfg->image_replace);
    if (draw_text && state_text && state_text[0] != L'\0' && state != status_waiting) {
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
