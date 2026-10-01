#include "analysis.h"
#include "spy_data.h"
#include <gdiplus.h>
#include <math.h>
#include <vector>
#include <algorithm>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

#define ANAL_CLASS L"NoMoreMonitorAnalysis"

struct AnalysisResult {
    std::vector<double> start_times;  /* 分钟(自午夜) */
    std::vector<DWORD>  durations;
    double peak_minutes;              /* 圆统计 KDE 峰值 → 最可能起点 */
    double mean_minutes;              /* 圆均值(参考) */
    double median_sec;                /* 时长中位数(稳健) */
    double mean_sec;
    DWORD  min_sec, max_sec;
    int    count;
};

static double minutes_of_day(const SYSTEMTIME* st) {
    return st->wHour * 60.0 + st->wMinute + st->wSecond / 60.0;
}

static double circ_dist_min(double a, double b) {
    double d = fabs(a - b);
    if (d > 720.0) d = 1440.0 - d;
    return d;
}

/* 圆核密度估计，返回概率密度最大的时刻（分钟） */
static double kde_peak_minutes(const std::vector<double>& t) {
    int n = (int)t.size();
    if (n == 0) return -1.0;

    double h = (n < 5) ? 45.0 : 30.0;   /* 带宽（分钟） */
    double best = -1.0, bestT = 0.0;
    for (int m = 0; m < 1440; m++) {
        double sum = 0.0;
        for (int i = 0; i < n; i++) {
            double d = circ_dist_min((double)m, t[i]);
            sum += exp(-(d * d) / (2.0 * h * h));
        }
        if (sum > best) { best = sum; bestT = (double)m; }
    }
    return bestT;
}

static double circ_mean_minutes(const std::vector<double>& t) {
    int n = (int)t.size();
    if (n == 0) return -1.0;
    double sx = 0.0, sy = 0.0;
    for (int i = 0; i < n; i++) {
        double a = t[i] / 1440.0 * 2.0 * 3.14159265358979323846;
        sx += cos(a);
        sy += sin(a);
    }
    sx /= n; sy /= n;
    double m = atan2(sy, sx);
    if (m < 0) m += 2.0 * 3.14159265358979323846;
    return m / (2.0 * 3.14159265358979323846) * 1440.0;
}

static double median_of(std::vector<DWORD> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    if (n % 2) return (double)v[n / 2];
    return ((double)v[n / 2 - 1] + (double)v[n / 2]) / 2.0;
}

static bool compute(AnalysisResult* r) {
    SpySession* arr = NULL;
    int count = 0;
    if (!spy_data_load(&arr, &count) || count <= 0) {
        free(arr);
        r->count = 0;
        return false;
    }

    r->count = count;
    r->start_times.reserve(count);
    r->durations.reserve(count);

    for (int i = 0; i < count; i++) {
        r->start_times.push_back(minutes_of_day(&arr[i].start));
        r->durations.push_back(arr[i].duration_seconds);
    }
    free(arr);

    r->peak_minutes = kde_peak_minutes(r->start_times);
    r->mean_minutes = circ_mean_minutes(r->start_times);

    std::vector<DWORD> d = r->durations;
    std::sort(d.begin(), d.end());
    r->min_sec = d.front();
    r->max_sec = d.back();
    r->median_sec = median_of(d);
    double sum = 0.0;
    for (size_t i = 0; i < d.size(); i++) sum += d[i];
    r->mean_sec = sum / d.size();
    return true;
}

static void format_minutes(double m, wchar_t* buf, size_t len) {
    int mm = (int)(m + 0.5) % 1440;
    swprintf_s(buf, len, L"%02d:%02d", mm / 60, mm % 60);
}

static void format_duration(double sec, wchar_t* buf, size_t len) {
    if (sec < 60.0) swprintf_s(buf, len, L"%.0f 秒", sec);
    else if (sec < 3600.0) swprintf_s(buf, len, L"%d 分 %.0f 秒", (int)(sec / 60.0), fmod(sec, 60.0));
    else swprintf_s(buf, len, L"%d 时 %d 分", (int)(sec / 3600.0), ((int)sec % 3600) / 60);
}

static void DrawTextAt(Graphics* g, const wchar_t* text, REAL x, REAL y, REAL size, Color color) {
    FontFamily ff(L"Microsoft YaHei");
    Font font(&ff, size, FontStyleRegular, UnitPixel);
    SolidBrush brush(color);
    PointF pt(x, y);
    g->DrawString(text, -1, &font, pt, &brush);
}

static void DrawStartChart(Graphics* g, const AnalysisResult* r, const RectF& area) {
    Pen axisPen(Color(80, 80, 80), 1.0f);
    g->DrawRectangle(&axisPen, area);

    /* 48 个 30 分钟 bin */
    int bins[48] = { 0 };
    for (size_t i = 0; i < r->start_times.size(); i++) {
        int b = ((int)r->start_times[i]) / 30;
        if (b >= 48) b = 47;
        bins[b]++;
    }
    int maxB = 1;
    for (int i = 0; i < 48; i++) if (bins[i] > maxB) maxB = bins[i];

    REAL plotH = area.Height - 30.0f;
    REAL bw = area.Width / 48.0f;
    SolidBrush barBrush(Color(90, 140, 220));

    for (int i = 0; i < 48; i++) {
        if (bins[i] == 0) continue;
        REAL bh = plotH * bins[i] / maxB;
        REAL x = area.X + i * bw + 1.0f;
        REAL y = area.Y + area.Height - 12.0f - bh;
        g->FillRectangle(&barBrush, x, y, bw - 2.0f, bh);
    }

    /* KDE 曲线 */
    if (r->start_times.size() >= 3) {
        Pen kdePen(Color(220, 60, 60), 2.0f);
        double h = (r->start_times.size() < 5) ? 45.0 : 30.0;
        double maxD = 0.0;
        for (int m = 0; m < 1440; m += 2) {
            double s = 0.0;
            for (size_t i = 0; i < r->start_times.size(); i++)
                s += exp(-(circ_dist_min((double)m, r->start_times[i]) * circ_dist_min((double)m, r->start_times[i])) / (2.0 * h * h));
            if (s > maxD) maxD = s;
        }
        if (maxD > 0) {
            bool first = true;
            PointF prev;
            for (int m = 0; m <= 1440; m += 4) {
                double s = 0.0;
                for (size_t i = 0; i < r->start_times.size(); i++) {
                    double dd = circ_dist_min((double)m, r->start_times[i]);
                    s += exp(-(dd * dd) / (2.0 * h * h));
                }
                REAL x = area.X + area.Width * m / 1440.0f;
                REAL y = area.Y + area.Height - 12.0f - (REAL)(plotH * s / maxD);
                PointF pt(x, y);
                if (!first) g->DrawLine(&kdePen, prev, pt);
                prev = pt; first = false;
            }
        }
    }

    /* 峰值竖线 */
    if (r->peak_minutes >= 0) {
        REAL px = area.X + area.Width * (REAL)(r->peak_minutes / 1440.0);
        Pen peakPen(Color(20, 120, 20), 2.0f);
        g->DrawLine(&peakPen, px, area.Y, px, area.Y + area.Height - 12.0f);
    }

    /* X 轴刻度 */
    for (int hh = 0; hh <= 24; hh += 6) {
        REAL x = area.X + area.Width * hh / 24.0f;
        wchar_t lbl[8];
        swprintf_s(lbl, L"%dh", hh);
        DrawTextAt(g, lbl, x - 8.0f, area.Y + area.Height - 14.0f, 12.0f, Color(60, 60, 60));
    }
}

static void DrawDurationChart(Graphics* g, const AnalysisResult* r, const RectF& area) {
    Pen axisPen(Color(80, 80, 80), 1.0f);
    g->DrawRectangle(&axisPen, area);

    DWORD maxD = r->max_sec > 0 ? r->max_sec : 1;
    int nb = 24;
    double binW = (double)maxD / nb;
    if (binW < 1.0) binW = 1.0;

    int bins[32] = { 0 };
    for (size_t i = 0; i < r->durations.size(); i++) {
        int b = (int)(r->durations[i] / binW);
        if (b >= nb) b = nb - 1;
        bins[b]++;
    }
    int maxB = 1;
    for (int i = 0; i < nb; i++) if (bins[i] > maxB) maxB = bins[i];

    REAL plotH = area.Height - 30.0f;
    REAL bw = area.Width / nb;
    SolidBrush barBrush(Color(70, 160, 120));

    for (int i = 0; i < nb; i++) {
        if (bins[i] == 0) continue;
        REAL bh = plotH * bins[i] / maxB;
        REAL x = area.X + i * bw + 1.0f;
        REAL y = area.Y + area.Height - 12.0f - bh;
        g->FillRectangle(&barBrush, x, y, bw - 2.0f, bh);
    }

    /* 中位数竖线 */
    if (r->median_sec >= 0 && maxD > 0) {
        REAL mx = area.X + area.Width * (REAL)(r->median_sec / (double)maxD);
        if (mx > area.X + area.Width) mx = area.X + area.Width;
        Pen medPen(Color(200, 80, 0), 2.0f);
        g->DrawLine(&medPen, mx, area.Y, mx, area.Y + area.Height - 12.0f);
    }
}

static AnalysisResult* s_result = NULL;

static LRESULT CALLBACK AnalProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1; /* 由 WM_PAINT 全量绘制，避免闪烁 */

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;

        Graphics g(hdc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        g.Clear(Color(250, 250, 250));

        if (!s_result || s_result->count == 0) {
            DrawTextAt(&g, L"暂无 spy 数据", 40.0f, 40.0f, 24.0f, Color(120, 120, 120));
            DrawTextAt(&g, L"等待捕获会话被记录后，这里会显示统计图表。", 40.0f, 80.0f, 14.0f, Color(120, 120, 120));
            EndPaint(hwnd, &ps);
            return 0;
        }

        DrawTextAt(&g, L"Spy 数据分析", 14.0f, 6.0f, 16.0f, Color(30, 30, 30));

        wchar_t line[256];
        wchar_t pk[16], mn[16], md[64];
        format_minutes(s_result->peak_minutes, pk, _countof(pk));
        format_minutes(s_result->mean_minutes, mn, _countof(mn));
        format_duration(s_result->median_sec, md, _countof(md));

        swprintf_s(line, L"样本 %d    最可能起点 %s (圆均值 %s)    中位时长 %s    范围 %u~%u 秒    均值 %.0f 秒",
                   s_result->count, pk, mn, md,
                   s_result->min_sec, s_result->max_sec, s_result->mean_sec);
        DrawTextAt(&g, line, 14.0f, 26.0f, 13.0f, Color(40, 40, 40));

        REAL top = 48.0f;
        REAL half = ((REAL)h - top) / 2.0f;
        RectF startArea(26.0f, top + 16.0f, (REAL)w - 52.0f, half - 22.0f);
        RectF durArea(26.0f, top + half + 8.0f, (REAL)w - 52.0f, half - 22.0f);

        DrawTextAt(&g, L"起点分布 (0-24h)，绿线=最可能起点", 28.0f, top - 4.0f, 12.0f, Color(70, 70, 70));
        DrawStartChart(&g, s_result, startArea);

        DrawTextAt(&g, L"时长分布，橙线=中位数", 28.0f, top + half - 4.0f, 12.0f, Color(70, 70, 70));
        DrawDurationChart(&g, s_result, durArea);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void analysis_show(HINSTANCE hInstance) {
    AnalysisResult result;
    compute(&result);
    s_result = &result;

    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = { 0 };
        wc.lpfnWndProc = AnalProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = ANAL_CLASS;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        RegisterClassW(&wc);
        registered = true;
    }

    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, ANAL_CLASS, L"Spy 分析 - NoMoreMonitor",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                CW_USEDEFAULT, CW_USEDEFAULT, 660, 430,
                                NULL, NULL, hInstance, NULL);
    if (!hwnd) { s_result = NULL; return; }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    s_result = NULL;
}
