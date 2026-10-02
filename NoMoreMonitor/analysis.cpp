#include "analysis.h"
#include "spy_data.h"
#include <gdiplus.h>
#include <math.h>
#include <vector>
#include <algorithm>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

#define ANAL_CLASS L"NoMoreMonitorAnalysis"
#define TWO_PI 6.28318530717958647692

struct AnalysisResult {
    std::vector<double> start_times;  /* 分钟(自午夜) */
    std::vector<DWORD>  durations;
    double peak_minutes;              /* von Mises 圆核 KDE 峰值 */
    double mean_minutes;              /* 圆均值 */
    double median_sec;
    double mean_sec;
    double p25_sec, p75_sec;          /* 时长四分位（稳健离散度） */
    double kappa;                     /* von Mises 集中度（用于画平滑曲线） */
    DWORD  min_sec, max_sec;
    int    count;
};

static double minutes_of_day(const SYSTEMTIME* st) {
    return st->wHour * 60.0 + st->wMinute + st->wSecond / 60.0;
}

static double percentile_of_sorted(const std::vector<DWORD>& v, double p) {
    if (v.empty()) return 0.0;
    double idx = p * (double)(v.size() - 1);
    size_t lo = (size_t)idx;
    size_t hi = lo + 1 < v.size() ? lo + 1 : lo;
    double frac = idx - (double)lo;
    return (double)v[lo] * (1.0 - frac) + (double)v[hi] * frac;
}

/* Banerjee et al. 近似：由平均合向量长度 R 估计 von Mises 集中度 κ */
static double kappa_from_R(double R) {
    if (R < 1e-6) return 0.05;
    if (R < 0.53) return 2.0 * R + R * R * R + 5.0 * pow(R, 5) / 6.0;
    if (R < 0.85) return -0.4 + 1.39 * R + 0.43 / (1.0 - R);
    double denom = R * R * R - 4.0 * R * R + 3.0 * R;
    if (denom < 1e-6) return 50.0;
    return 1.0 / denom;
}

/*
 * 圆核密度估计（von Mises 核）——比“圆距离上的高斯核”更贴合圆周数据。
 * 带宽：由数据集中度 κ 再按 Silverman 思路 n^(-2/5) 加宽，避免小样本过拟合。
 */
static double kde_kappa(const std::vector<double>& t) {
    int n = (int)t.size();
    if (n <= 1) return 0.15;   /* 约 1.5 小时平滑 */

    double sx = 0.0, sy = 0.0;
    for (int i = 0; i < n; i++) {
        double a = t[i] / 1440.0 * TWO_PI;
        sx += cos(a);
        sy += sin(a);
    }
    double R = sqrt(sx * sx + sy * sy) / n;
    double kappa = kappa_from_R(R) * pow((double)n, -0.4);
    if (kappa < 0.08) kappa = 0.08;
    if (kappa > 40.0) kappa = 40.0;
    return kappa;
}

static double kde_density(const std::vector<double>& t, double theta_min, double kappa) {
    double theta = theta_min / 1440.0 * TWO_PI;
    double sum = 0.0;
    for (size_t i = 0; i < t.size(); i++) {
        double a = t[i] / 1440.0 * TWO_PI;
        sum += exp(kappa * cos(theta - a));
    }
    return sum;
}

/* 圆核 KDE 峰值 + 抛物线插值细化（亚分钟精度） */
static double kde_peak_minutes(const std::vector<double>& t) {
    int n = (int)t.size();
    if (n == 0) return -1.0;
    if (n == 1) return t[0];

    double kappa = kde_kappa(t);
    double best = -1.0, bestT = 0.0;
    for (int m = 0; m < 1440; m++) {
        double d = kde_density(t, (double)m, kappa);
        if (d > best) { best = d; bestT = (double)m; }
    }

    /* 峰值附近二次插值，把 1 分钟网格细化到亚分钟 */
    double y0 = best;
    double ym = kde_density(t, bestT - 1.0, kappa);
    double yp = kde_density(t, bestT + 1.0, kappa);
    double denom = ym - 2.0 * y0 + yp;
    if (fabs(denom) > 1e-12) {
        double delta = 0.5 * (ym - yp) / denom;
        if (delta > -1.0 && delta < 1.0) bestT += delta;
    }
    if (bestT < 0.0) bestT += 1440.0;
    if (bestT >= 1440.0) bestT -= 1440.0;
    return bestT;
}

static double circ_mean_minutes(const std::vector<double>& t) {
    int n = (int)t.size();
    if (n == 0) return -1.0;
    double sx = 0.0, sy = 0.0;
    for (int i = 0; i < n; i++) {
        double a = t[i] / 1440.0 * TWO_PI;
        sx += cos(a);
        sy += sin(a);
    }
    sx /= n; sy /= n;
    double m = atan2(sy, sx);
    if (m < 0) m += TWO_PI;
    return m / TWO_PI * 1440.0;
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
    r->kappa = kde_kappa(r->start_times);

    std::vector<DWORD> d = r->durations;
    std::sort(d.begin(), d.end());
    r->min_sec = d.front();
    r->max_sec = d.back();
    r->median_sec = percentile_of_sorted(d, 0.50);
    r->p25_sec = percentile_of_sorted(d, 0.25);
    r->p75_sec = percentile_of_sorted(d, 0.75);
    double sum = 0.0;
    for (size_t i = 0; i < d.size(); i++) sum += d[i];
    r->mean_sec = sum / d.size();
    return true;
}

static void format_minutes(double m, wchar_t* buf, size_t len) {
    if (m < 0) { swprintf_s(buf, len, L"—"); return; }
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

/* 三个关键数字卡片 */
static void DrawKpiCard(Graphics* g, const RectF& box, const wchar_t* label, const wchar_t* value, Color accent) {
    SolidBrush bg(Color(255, 255, 255));
    g->FillRectangle(&bg, box);
    Pen border(Color(220, 220, 220), 1.0f);
    g->DrawRectangle(&border, box);

    SolidBrush accentBrush(accent);
    g->FillRectangle(&accentBrush, box.X, box.Y, 4.0f, box.Height);

    DrawTextAt(g, label, box.X + 16.0f, box.Y + 10.0f, 13.0f, Color(110, 110, 110));
    DrawTextAt(g, value, box.X + 16.0f, box.Y + 30.0f, 22.0f, Color(30, 30, 30));
}

/*
 * 起点分布：24 小时柱状图 + 峰值标记 + 可选轻量平滑曲线。
 * 刻度 0/6/12/18/24，一眼能对上钟点。
 */
static void DrawStartChart(Graphics* g, const AnalysisResult* r, const RectF& area) {
    /* 背景 */
    SolidBrush bg(Color(255, 255, 255));
    g->FillRectangle(&bg, area);
    Pen axisPen(Color(210, 210, 210), 1.0f);
    g->DrawRectangle(&axisPen, area);

    int bins[24] = { 0 };
    for (size_t i = 0; i < r->start_times.size(); i++) {
        int b = ((int)r->start_times[i]) / 60;
        if (b < 0) b = 0;
        if (b >= 24) b = 23;
        bins[b]++;
    }
    int maxB = 1;
    for (int i = 0; i < 24; i++) if (bins[i] > maxB) maxB = bins[i];

    REAL bottom = area.Y + area.Height - 22.0f;
    REAL plotH = bottom - area.Y - 8.0f;
    REAL bw = area.Width / 24.0f;

    SolidBrush barBrush(Color(90, 150, 220));
    int peakHour = (r->peak_minutes >= 0) ? (int)(r->peak_minutes / 60.0) : -1;

    for (int i = 0; i < 24; i++) {
        if (bins[i] == 0) continue;
        REAL bh = plotH * bins[i] / maxB;
        REAL x = area.X + i * bw + 3.0f;
        REAL y = bottom - bh;
        if (i == peakHour) {
            SolidBrush peakBar(Color(255, 120, 60));
            g->FillRectangle(&peakBar, x, y, bw - 6.0f, bh);
        }
        else {
            g->FillRectangle(&barBrush, x, y, bw - 6.0f, bh);
        }
    }

    /* 峰值竖线 + 标签 */
    if (r->peak_minutes >= 0) {
        REAL px = area.X + area.Width * (REAL)(r->peak_minutes / 1440.0);
        Pen peakPen(Color(200, 70, 30), 2.0f);
        g->DrawLine(&peakPen, px, area.Y + 4.0f, px, bottom);

        wchar_t tag[32];
        format_minutes(r->peak_minutes, tag, _countof(tag));
        SolidBrush tagBg(Color(255, 120, 60));
        RectF tagBox(px - 28.0f, area.Y + 2.0f, 56.0f, 18.0f);
        g->FillRectangle(&tagBg, tagBox);
        DrawTextAt(g, tag, tagBox.X + 10.0f, tagBox.Y + 1.0f, 12.0f, Color(255, 255, 255));
    }

    /* X 轴：0 / 6 / 12 / 18 / 24 */
    for (int hh = 0; hh <= 24; hh += 6) {
        REAL x = area.X + area.Width * hh / 24.0f;
        wchar_t lbl[8];
        swprintf_s(lbl, L"%d", hh);
        DrawTextAt(g, lbl, x - 6.0f, bottom + 4.0f, 12.0f, Color(100, 100, 100));
    }
    DrawTextAt(g, L"时", area.X + area.Width - 24.0f, bottom + 4.0f, 12.0f, Color(140, 140, 140));
}

/*
 * 时长分布：柱状图 + 中位数标记。
 * 上限用 P90，避免个别超长会话把柱子压扁。
 */
static void DrawDurationChart(Graphics* g, const AnalysisResult* r, const RectF& area) {
    SolidBrush bg(Color(255, 255, 255));
    g->FillRectangle(&bg, area);
    Pen axisPen(Color(210, 210, 210), 1.0f);
    g->DrawRectangle(&axisPen, area);

    std::vector<DWORD> d = r->durations;
    std::sort(d.begin(), d.end());
    DWORD displayMax = d.empty() ? 1 : d.back();
    if (d.size() >= 4) {
        size_t idx = (size_t)(d.size() * 0.90);
        if (idx >= d.size()) idx = d.size() - 1;
        displayMax = d[idx];
    }
    if (displayMax < 1) displayMax = 1;
    if ((double)r->median_sec > (double)displayMax) {
        displayMax = (DWORD)(r->median_sec + 0.5);
    }

    int nb = 16;
    double binW = (double)displayMax / nb;
    if (binW < 1.0) binW = 1.0;

    int bins[16] = { 0 };
    for (size_t i = 0; i < r->durations.size(); i++) {
        int b = (int)(r->durations[i] / binW);
        if (b >= nb) b = nb - 1;
        if (b < 0) b = 0;
        bins[b]++;
    }
    int maxB = 1;
    for (int i = 0; i < nb; i++) if (bins[i] > maxB) maxB = bins[i];

    REAL bottom = area.Y + area.Height - 22.0f;
    REAL plotH = bottom - area.Y - 8.0f;
    REAL bw = area.Width / nb;

    SolidBrush barBrush(Color(70, 170, 130));
    for (int i = 0; i < nb; i++) {
        if (bins[i] == 0) continue;
        REAL bh = plotH * bins[i] / maxB;
        REAL x = area.X + i * bw + 2.0f;
        REAL y = bottom - bh;
        g->FillRectangle(&barBrush, x, y, bw - 4.0f, bh);
    }

    /* 中位数竖线 */
    if (r->median_sec >= 0 && displayMax > 0) {
        REAL mx = area.X + area.Width * (REAL)(r->median_sec / (double)displayMax);
        if (mx > area.X + area.Width) mx = area.X + area.Width;
        Pen medPen(Color(230, 120, 20), 2.0f);
        g->DrawLine(&medPen, mx, area.Y + 4.0f, mx, bottom);

        wchar_t tag[32];
        format_duration(r->median_sec, tag, _countof(tag));
        SolidBrush tagBg(Color(230, 120, 20));
        RectF tagBox(mx - 22.0f, area.Y + 2.0f, 44.0f, 18.0f);
        g->FillRectangle(&tagBg, tagBox);
        DrawTextAt(g, L"中位", tagBox.X + 8.0f, tagBox.Y + 1.0f, 12.0f, Color(255, 255, 255));
    }

    /* X 轴：0 / ¼ / ½ / ¾ / max */
    for (int k = 0; k <= 4; k++) {
        REAL x = area.X + area.Width * k / 4.0f;
        double sec = (double)displayMax * k / 4.0;
        wchar_t lbl[16];
        format_duration(sec, lbl, _countof(lbl));
        DrawTextAt(g, lbl, x - 16.0f, bottom + 4.0f, 11.0f, Color(100, 100, 100));
    }
}

static AnalysisResult* s_result = NULL;

static LRESULT CALLBACK AnalProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;

        Graphics g(hdc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        g.Clear(Color(245, 246, 248));

        if (!s_result || s_result->count == 0) {
            DrawTextAt(&g, L"暂无 spy 数据", 40.0f, 40.0f, 22.0f, Color(120, 120, 120));
            DrawTextAt(&g, L"等捕获会话记录之后，这里会出现统计结果。", 40.0f, 78.0f, 14.0f, Color(140, 140, 140));
            EndPaint(hwnd, &ps);
            return 0;
        }

        DrawTextAt(&g, L"Spy 分析", 16.0f, 10.0f, 18.0f, Color(25, 25, 25));

        wchar_t pk[16], md[64], cnt[16];
        format_minutes(s_result->peak_minutes, pk, _countof(pk));
        format_duration(s_result->median_sec, md, _countof(md));
        swprintf_s(cnt, L"%d 次", s_result->count);

        /* KPI 卡片 */
        REAL pad = 16.0f;
        REAL cardW = ((REAL)w - pad * 4.0f) / 3.0f;
        REAL cardY = 36.0f;
        REAL cardH = 58.0f;
        DrawKpiCard(&g, RectF(pad, cardY, cardW, cardH), L"记录次数", cnt, Color(90, 150, 220));
        DrawKpiCard(&g, RectF(pad * 2 + cardW, cardY, cardW, cardH), L"最可能起点", pk, Color(255, 120, 60));
        DrawKpiCard(&g, RectF(pad * 3 + cardW * 2, cardY, cardW, cardH), L"典型时长（中位）", md, Color(70, 170, 130));

        /* 两块图 */
        REAL chartTop = cardY + cardH + 18.0f;
        REAL chartH = ((REAL)h - chartTop - 28.0f) / 2.0f;

        DrawTextAt(&g, L"什么时候最容易被盯上（按小时）", pad, chartTop - 2.0f, 13.0f, Color(80, 80, 80));
        DrawStartChart(&g, s_result, RectF(pad, chartTop + 18.0f, (REAL)w - pad * 2.0f, chartH - 18.0f));

        REAL durTop = chartTop + chartH + 6.0f;
        wchar_t durTitle[128];
        wchar_t q1[32], q3[32];
        format_duration(s_result->p25_sec, q1, _countof(q1));
        format_duration(s_result->p75_sec, q3, _countof(q3));
        swprintf_s(durTitle, L"一次大概持续多久（四分位 %s ~ %s）", q1, q3);
        DrawTextAt(&g, durTitle, pad, durTop, 13.0f, Color(80, 80, 80));
        DrawDurationChart(&g, s_result, RectF(pad, durTop + 18.0f, (REAL)w - pad * 2.0f, (REAL)h - durTop - 28.0f));

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
    return DefWindowProc(hwnd, msg, wp, lp);
}

void analysis_show(HINSTANCE hInstance) {
    AnalysisResult result;
    result.peak_minutes = -1.0;
    result.mean_minutes = -1.0;
    result.median_sec = 0.0;
    result.mean_sec = 0.0;
    result.p25_sec = 0.0;
    result.p75_sec = 0.0;
    result.kappa = 0.0;
    result.min_sec = 0;
    result.max_sec = 0;
    result.count = 0;
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
                                CW_USEDEFAULT, CW_USEDEFAULT, 720, 520,
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
