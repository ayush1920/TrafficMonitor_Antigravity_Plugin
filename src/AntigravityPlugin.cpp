#include "AntigravityPlugin.h"
#include "QuotaSync.h"
#include "json.hpp"
#include <fstream>
#include <objidl.h>
#ifdef NOMINMAX
#ifndef min
#define _AG_DEFINED_MIN
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define _AG_DEFINED_MAX
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#endif
#include <gdiplus.h>
#ifdef _AG_DEFINED_MIN
#undef min
#undef _AG_DEFINED_MIN
#endif
#ifdef _AG_DEFINED_MAX
#undef max
#undef _AG_DEFINED_MAX
#endif
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")

using json = nlohmann::json;

static std::once_flag g_gdiplusOnce;
static ULONG_PTR g_gdiplusToken = 0;

static void EnsureGdiPlus()
{
    std::call_once(g_gdiplusOnce, []() {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr);
    });
}

static std::wstring Utf8ToWide(const std::string& utf8Str)
{
    if (utf8Str.empty()) return std::wstring();
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), (int)utf8Str.size(), nullptr, 0);
    if (sizeNeeded <= 0) return std::wstring();
    std::wstring wstr(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), (int)utf8Str.size(), &wstr[0], sizeNeeded);
    return wstr;
}

static double SafeGetDouble(const json& j, const char* key, double defVal = 0.0)
{
    if (!j.contains(key)) return defVal;
    const auto& val = j[key];
    if (val.is_number()) return val.get<double>();
    return defVal;
}

static std::string SafeGetString(const json& j, const char* key, const std::string& defVal = "")
{
    if (!j.contains(key)) return defVal;
    const auto& val = j[key];
    if (val.is_string()) return val.get<std::string>();
    return defVal;
}

static bool SafeGetBool(const json& j, const char* key, bool defVal = false)
{
    if (!j.contains(key)) return defVal;
    const auto& val = j[key];
    if (val.is_boolean()) return val.get<bool>();
    return defVal;
}

static std::wstring FormatResetTime(const std::string& raw, bool shortForm)
{
    if (raw.empty() || raw == "--") return L"";

    if (!shortForm)
    {
        // Long form: keep/normalize a single space between time units (e.g. "4h 50m", "6d 23h")
        std::wstring result;
        bool lastWasSpace = true; // trim leading spaces
        for (size_t idx = 0; idx < raw.size(); ++idx)
        {
            unsigned char ch = static_cast<unsigned char>(raw[idx]);
            if (ch == ' ' || ch == '\t')
            {
                if (!lastWasSpace)
                {
                    result.push_back(L' ');
                    lastWasSpace = true;
                }
                continue;
            }
            if (!result.empty() && !lastWasSpace && isdigit(ch))
            {
                wchar_t prev = result.back();
                if (prev == L'd' || prev == L'D' || prev == L'h' || prev == L'H' || prev == L'm' || prev == L'M')
                {
                    result.push_back(L' ');
                }
            }
            result.push_back((wchar_t)ch);
            lastWasSpace = false;
        }
        while (!result.empty() && result.back() == L' ') result.pop_back();
        return result;
    }

    // Short form: truncate to largest non-zero unit (e.g. 3h 35m -> 3h, 4h 20m -> 4h, 2h 40m -> 2h, 0h 50m -> 50m, 6d 23h -> 6d)
    int days = -1, hours = -1, mins = -1;
    size_t i = 0;
    while (i < raw.size())
    {
        while (i < raw.size() && (raw[i] == ' ' || raw[i] == '\t')) i++;
        if (i >= raw.size()) break;
        if (isdigit((unsigned char)raw[i]))
        {
            int val = 0;
            while (i < raw.size() && isdigit((unsigned char)raw[i]))
            {
                val = val * 10 + (raw[i] - '0');
                i++;
            }
            while (i < raw.size() && raw[i] == ' ') i++;
            if (i < raw.size())
            {
                char unit = raw[i];
                if (unit == 'd' || unit == 'D') days = val;
                else if (unit == 'h' || unit == 'H') hours = val;
                else if (unit == 'm' || unit == 'M') mins = val;
                i++;
            }
        }
        else
        {
            i++;
        }
    }

    if (days > 0)
    {
        return std::to_wstring(days) + L"d";
    }
    if (hours > 0)
    {
        return std::to_wstring(hours) + L"h";
    }
    if (mins >= 0)
    {
        return std::to_wstring(mins) + L"m";
    }
    if (days == 0 || hours == 0)
    {
        return L"0m";
    }

    std::wstring fallback;
    for (char c : raw) if (c != ' ') fallback.push_back((wchar_t)c);
    return fallback;
}

static COLORREF GetThresholdColor(int pct, bool darkMode)
{
    if (pct > 60)
        return darkMode ? RGB(74, 222, 128) : RGB(22, 163, 74);   // Green (> 60%)
    if (pct > 20)
        return darkMode ? RGB(251, 191, 36) : RGB(217, 119, 6);   // Yellow (21-60%, <= 60% inclusive)
    return darkMode ? RGB(248, 113, 113) : RGB(220, 38, 38);      // Red (<= 20% inclusive)
}

static COLORREF ComputeSecondaryColor(COLORREF baseColor, bool darkMode, bool insidePill, bool dimEnabled)
{
    if (!dimEnabled) return baseColor;

    BYTE r = GetRValue(baseColor);
    BYTE g = GetGValue(baseColor);
    BYTE b = GetBValue(baseColor);

    // Compute luminance of the base text color to determine whether the text is dark or light
    int lum = (r * 299 + g * 587 + b * 114) / 1000;
    bool textIsDark = (lum < 128);

    if (textIsDark)
    {
        // For dark text (e.g. black RGB(0,0,0)), dimming means lightening towards a crisp
        // dark-charcoal tone (~65% contrast) that remains sharp on translucent backgrounds/pills.
        BYTE dr = static_cast<BYTE>(r + (255 - r) * 0.30f + 0.5f);
        BYTE dg = static_cast<BYTE>(g + (255 - g) * 0.30f + 0.5f);
        BYTE db = static_cast<BYTE>(b + (255 - b) * 0.30f + 0.5f);
        return RGB(dr, dg, db);
    }
    else
    {
        // For light text (e.g. white RGB(255,255,255)), dimming means dimming by ~30% towards dark
        // while preserving the host's configured tint.
        BYTE dr = static_cast<BYTE>(r * 0.70f + 0.5f);
        BYTE dg = static_cast<BYTE>(g * 0.70f + 0.5f);
        BYTE db = static_cast<BYTE>(b * 0.70f + 0.5f);
        return RGB(dr, dg, db);
    }
}

static int ComputeDigitSlotWidth(HDC hdc)
{
    int maxW = 0;
    for (wchar_t d = L'0'; d <= L'9'; ++d)
    {
        SIZE sz = { 0, 0 };
        GetTextExtentPoint32W(hdc, &d, 1, &sz);
        if (sz.cx > maxW) maxW = sz.cx;
    }
    return (maxW > 0) ? maxW : 8;
}

static int MeasureTextWidth(HDC hdc, const std::wstring& text, bool tabularDigits, int digitSlotW)
{
    if (text.empty()) return 0;
    if (!tabularDigits)
    {
        SIZE sz = { 0, 0 };
        GetTextExtentPoint32W(hdc, text.c_str(), (int)text.size(), &sz);
        return sz.cx;
    }

    int total = 0;
    std::wstring run;
    auto flushRun = [&]() {
        if (!run.empty())
        {
            SIZE sz = { 0, 0 };
            GetTextExtentPoint32W(hdc, run.c_str(), (int)run.size(), &sz);
            total += sz.cx;
            run.clear();
        }
    };

    for (wchar_t ch : text)
    {
        if (ch >= L'0' && ch <= L'9')
        {
            flushRun();
            total += digitSlotW;
        }
        else
        {
            run.push_back(ch);
        }
    }
    flushRun();
    return total;
}

// Renders GDI text with proper premultiplied alpha onto hdc.
// This bypasses host DrawTextW hooks that perform non-premultiplied AlphaBlend
// (which can cause channel overflow / red artifacts or hollow glyphs on mid-tone colors).
static void DrawTextPremultiplied(HDC hdc, const wchar_t* text, int len, LPRECT lprc, UINT format, COLORREF color)
{
    if (!hdc || !text || len == 0 || !lprc) return;
    int w = lprc->right - lprc->left;
    int h = lprc->bottom - lprc->top;
    if (w <= 0 || h <= 0) return;

    HDC memDC = CreateCompatibleDC(NULL);
    if (!memDC)
    {
        DrawTextW(hdc, text, len, lprc, format);
        return;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; // top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (!hBmp || !pBits)
    {
        if (hBmp) DeleteObject(hBmp);
        DeleteDC(memDC);
        DrawTextW(hdc, text, len, lprc, format);
        return;
    }

    HGDIOBJ hOldBmp = SelectObject(memDC, hBmp);
    HFONT hFont = (HFONT)GetCurrentObject(hdc, OBJ_FONT);
    HGDIOBJ hOldFont = hFont ? SelectObject(memDC, hFont) : nullptr;

    SetTextColor(memDC, RGB(255, 255, 255));
    SetBkColor(memDC, RGB(0, 0, 0));
    SetBkMode(memDC, OPAQUE);

    RECT rcLocal = { 0, 0, w, h };
    DrawTextW(memDC, text, len, &rcLocal, format);
    GdiFlush();

    BYTE r = GetRValue(color);
    BYTE g = GetGValue(color);
    BYTE b = GetBValue(color);

    BYTE* px = static_cast<BYTE*>(pBits);
    const size_t count = static_cast<size_t>(w) * static_cast<size_t>(h);
    for (size_t i = 0; i < count; ++i)
    {
        uint16_t sum = static_cast<uint16_t>(px[0]) + static_cast<uint16_t>(px[1]) + static_cast<uint16_t>(px[2]);
        if (sum == 0)
        {
            px[0] = 0;
            px[1] = 0;
            px[2] = 0;
            px[3] = 0;
        }
        else
        {
            uint32_t a = (sum + 1u) / 3u;
            if (a > 255u) a = 255u;
            px[0] = static_cast<BYTE>((static_cast<uint32_t>(b) * a + 127u) / 255u);
            px[1] = static_cast<BYTE>((static_cast<uint32_t>(g) * a + 127u) / 255u);
            px[2] = static_cast<BYTE>((static_cast<uint32_t>(r) * a + 127u) / 255u);
            px[3] = static_cast<BYTE>(a);
        }
        px += 4;
    }

    BLENDFUNCTION bf{};
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;
    if (!AlphaBlend(hdc, lprc->left, lprc->top, w, h, memDC, 0, 0, w, h, bf))
    {
        DrawTextW(hdc, text, len, lprc, format);
    }

    if (hOldFont) SelectObject(memDC, hOldFont);
    SelectObject(memDC, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(memDC);
}

static void DrawStyledText(HDC hdc, int x, int y, int w, int h, const std::wstring& text,
                           COLORREF color, UINT alignH, bool tabularDigits, int digitSlotW)
{
    if (text.empty() || w <= 0 || h <= 0) return;
    COLORREF oldClr = SetTextColor(hdc, color);
    if (!tabularDigits)
    {
        RECT rc = { x, y, x + w, y + h };
        DrawTextPremultiplied(hdc, text.c_str(), (int)text.size(), &rc, alignH | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, color);
        SetTextColor(hdc, oldClr);
        return;
    }

    int contentW = MeasureTextWidth(hdc, text, true, digitSlotW);
    int curX = x;
    if (alignH == DT_RIGHT) curX = x + w - contentW;
    else if (alignH == DT_CENTER) curX = x + (w - contentW) / 2;

    std::wstring run;
    auto flushRun = [&]() {
        if (!run.empty())
        {
            SIZE sz = { 0, 0 };
            GetTextExtentPoint32W(hdc, run.c_str(), (int)run.size(), &sz);
            RECT rc = { curX, y, curX + sz.cx + 2, y + h };
            DrawTextPremultiplied(hdc, run.c_str(), (int)run.size(), &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, color);
            curX += sz.cx;
            run.clear();
        }
    };

    for (wchar_t ch : text)
    {
        if (ch >= L'0' && ch <= L'9')
        {
            flushRun();
            RECT rc = { curX, y, curX + digitSlotW, y + h };
            DrawTextPremultiplied(hdc, &ch, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, color);
            curX += digitSlotW;
        }
        else
        {
            run.push_back(ch);
        }
    }
    flushRun();
    SetTextColor(hdc, oldClr);
}

static void AddRoundedRectPath(Gdiplus::GraphicsPath& path, Gdiplus::REAL x, Gdiplus::REAL y,
                               Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL r)
{
    if (w <= 0.0f || h <= 0.0f) return;
    Gdiplus::REAL d = r * 2.0f;
    if (d > w) d = w;
    if (d > h) d = h;
    if (d < 1.0f)
    {
        path.AddRectangle(Gdiplus::RectF(x, y, w, h));
        return;
    }
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

static inline Gdiplus::Color ToGpColor(COLORREF c, BYTE alpha = 255)
{
    return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

static void DrawClockIcon(Gdiplus::Graphics& g, Gdiplus::REAL cx, Gdiplus::REAL cy, Gdiplus::REAL radius, COLORREF color)
{
    Gdiplus::Pen pen(ToGpColor(color), 1.15f);
    g.DrawEllipse(&pen, cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
    g.DrawLine(&pen, cx, cy, cx, cy - radius * 0.58f);
    g.DrawLine(&pen, cx, cy, cx + radius * 0.52f, cy);
}

struct ColumnLayoutMetrics
{
    int fontH{16};
    int digitSlotW{8};
    int gapSmall{4};
    int gapMed{6};
    int padX{6};
    int labelW{0};
    int barW{0};
    int pctW{0};
    int sepW{0};
    int clockW{0};
    int clockGap{3};
    int resetW{0};
    bool hasAnyReset{false};
    int totalW{0};
};

static std::wstring FormatResetForStyle(const QuotaDisplayEntry& e, DisplayStyle style)
{
    if (e.resetText.empty() || style == STYLE_MINIMAL_DOTS) return L"";
    if (style == STYLE_CLASSIC) return L"(" + e.resetText + L")";
    return e.resetText;
}

static std::wstring FormatLabelForStyle(const QuotaDisplayEntry& e, DisplayStyle style)
{
    if (e.label.empty()) return L"";
    // Preserve host-configured Display Text Settings verbatim across all Visual Styles.
    if (e.labelFromHost) return e.label;
    if (style == STYLE_CLASSIC)
    {
        return (e.label.back() == L':') ? e.label : (e.label + L":");
    }
    std::wstring out = e.label;
    while (!out.empty() && out.back() == L':') out.pop_back();
    return out;
}

static ColumnLayoutMetrics ComputeColumnMetrics(HDC hdc, const std::vector<const QuotaDisplayEntry*>& rows,
                                                const PluginSettings& settings)
{
    ColumnLayoutMetrics m;
    SIZE szRef = { 0, 0 };
    GetTextExtentPoint32W(hdc, L"0", 1, &szRef);
    m.fontH = (szRef.cy > 0) ? szRef.cy : 16;
    m.digitSlotW = ComputeDigitSlotWidth(hdc);
    m.gapSmall = std::max(3, m.digitSlotW / 2);
    m.gapMed = std::max(5, (m.digitSlotW * 2) / 3);
    m.padX = std::max(5, m.fontH / 3);
    m.clockGap = std::max(3, m.gapSmall - 1);

    DisplayStyle style = settings.displayStyle;
    bool tab = settings.tabularDigits;

    if (style == STYLE_PROGRESS_BAR)
    {
        m.barW = std::max(28, m.fontH * 2);
    }
    if (style == STYLE_TYPOGRAPHIC)
    {
        m.sepW = MeasureTextWidth(hdc, L"\x00B7", false, m.digitSlotW); // middle dot '·'
    }
    if (style == STYLE_PILL_BADGES)
    {
        m.clockW = std::max(8, (m.fontH * 5) / 9);
    }

    int maxNaturalTotal = 0;
    for (const QuotaDisplayEntry* pe : rows)
    {
        std::wstring lbl = FormatLabelForStyle(*pe, style);
        std::wstring rst = FormatResetForStyle(*pe, style);
        int lw = lbl.empty() ? 0 : MeasureTextWidth(hdc, lbl, false, m.digitSlotW);
        int pw = MeasureTextWidth(hdc, pe->pctText, tab, m.digitSlotW);
        int rw = MeasureTextWidth(hdc, rst, tab, m.digitSlotW);

        if (lw > m.labelW) m.labelW = lw;
        if (pw > m.pctW) m.pctW = pw;
        if (rw > m.resetW) m.resetW = rw;
        if (!rst.empty()) m.hasAnyReset = true;

        int lblGap = (lw > 0) ? ((style == STYLE_CLASSIC) ? 2 : m.gapMed) : 0;
        int rowW = lw + lblGap;
        if (style == STYLE_CLASSIC)
        {
            rowW += pw + (rw > 0 ? m.gapSmall + rw : 0);
        }
        else if (style == STYLE_TYPOGRAPHIC)
        {
            rowW += pw + (rw > 0 ? m.gapSmall + m.sepW + m.gapSmall + rw : 0);
        }
        else if (style == STYLE_PROGRESS_BAR)
        {
            rowW += m.barW + m.gapMed + pw + (rw > 0 ? m.gapMed + rw : 0);
        }
        else if (style == STYLE_PILL_BADGES)
        {
            rowW = m.padX + lw + lblGap + pw + (rw > 0 ? m.gapMed + m.clockW + m.clockGap + rw : 0) + m.padX;
        }
        if (rowW > maxNaturalTotal) maxNaturalTotal = rowW;
    }

    if (tab && settings.alignColumns && !rows.empty())
    {
        bool hasPctChar = (rows[0]->pctText.find(L'%') != std::wstring::npos);
        int minThreeDigitW = MeasureTextWidth(hdc, hasPctChar ? L"100%" : L"100", true, m.digitSlotW);
        if (minThreeDigitW > m.pctW) m.pctW = minThreeDigitW;
    }

    if (!settings.alignColumns)
    {
        m.totalW = maxNaturalTotal;
    }
    else
    {
        int colLblGap = (m.labelW > 0) ? ((style == STYLE_CLASSIC) ? 2 : m.gapMed) : 0;
        if (style == STYLE_CLASSIC)
        {
            m.totalW = m.labelW + colLblGap + m.pctW + (m.hasAnyReset ? m.gapSmall + m.resetW : 0);
        }
        else if (style == STYLE_TYPOGRAPHIC)
        {
            m.totalW = m.labelW + colLblGap + m.pctW + (m.hasAnyReset ? m.gapSmall + m.sepW + m.gapSmall + m.resetW : 0);
        }
        else if (style == STYLE_PROGRESS_BAR)
        {
            m.totalW = m.labelW + colLblGap + m.barW + m.gapMed + m.pctW + (m.hasAnyReset ? m.gapMed + m.resetW : 0);
        }
        else if (style == STYLE_PILL_BADGES)
        {
            m.totalW = m.padX + m.labelW + colLblGap + m.pctW +
                       (m.hasAnyReset ? m.gapMed + m.clockW + m.clockGap + m.resetW : 0) + m.padX;
        }
        else
        {
            m.totalW = maxNaturalTotal;
        }
    }

    return m;
}

static void DrawSingleColumnEntry(HDC hdc, int rx, int ry, int rw, int rh,
                                  const QuotaDisplayEntry& e, const ColumnLayoutMetrics& m,
                                  const PluginSettings& settings, COLORREF labelColor, COLORREF valueColor, bool darkMode)
{
    DisplayStyle style = settings.displayStyle;
    bool tab = settings.tabularDigits;
    bool aligned = settings.alignColumns;
    bool insidePill = (style == STYLE_PILL_BADGES);

    COLORREF secColor = ComputeSecondaryColor(valueColor, darkMode, insidePill, settings.dimSecondaryInfo);
    COLORREF lblColor = labelColor;
    COLORREF threshColor = GetThresholdColor(e.percent, darkMode);

    std::wstring lbl = FormatLabelForStyle(e, style);
    std::wstring rst = FormatResetForStyle(e, style);
    int lw = lbl.empty() ? 0 : (aligned ? m.labelW : MeasureTextWidth(hdc, lbl, false, m.digitSlotW));
    int lblGap = (lw > 0) ? ((style == STYLE_CLASSIC) ? 2 : m.gapMed) : 0;
    int pw = aligned ? m.pctW : MeasureTextWidth(hdc, e.pctText, tab, m.digitSlotW);
    int rstw = aligned ? m.resetW : MeasureTextWidth(hdc, rst, tab, m.digitSlotW);

    // 1. GDI+ vector layer (pills, micro bars, clock icon)
    if (style == STYLE_PILL_BADGES || style == STYLE_PROGRESS_BAR)
    {
        EnsureGdiPlus();
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        if (style == STYLE_PILL_BADGES)
        {
            int padV = (rh >= 18) ? 1 : 0;
            Gdiplus::REAL px = (Gdiplus::REAL)rx;
            Gdiplus::REAL py = (Gdiplus::REAL)(ry + padV);
            Gdiplus::REAL pwRect = (Gdiplus::REAL)(rw - 1);
            Gdiplus::REAL phRect = (Gdiplus::REAL)(rh - padV * 2 - 1);
            Gdiplus::REAL radius = std::min(5.0f, phRect * 0.28f);

            // Translucent pill container adapting to text luminance so the taskbar wallpaper
            // shows through while keeping text crystal-clear.
            int valLum = (GetRValue(valueColor) * 299 + GetGValue(valueColor) * 587 + GetBValue(valueColor) * 114) / 1000;
            int lblLum = (GetRValue(lblColor) * 299 + GetGValue(lblColor) * 587 + GetBValue(lblColor) * 114) / 1000;
            bool textIsDark = (std::min(valLum, lblLum) < 128);

            Gdiplus::Color fillGp = textIsDark
                ? Gdiplus::Color(48, 255, 255, 255)
                : Gdiplus::Color(75, 20, 24, 32);
            Gdiplus::Color borderGp = textIsDark
                ? Gdiplus::Color(90, 45, 50, 62)
                : Gdiplus::Color(90, 255, 255, 255);

            Gdiplus::GraphicsPath path;
            AddRoundedRectPath(path, px, py, pwRect, phRect, radius);
            Gdiplus::SolidBrush fillBrush(fillGp);
            Gdiplus::Pen borderPen(borderGp, 1.0f);
            g.FillPath(&fillBrush, &path);
            g.DrawPath(&borderPen, &path);

            if (!rst.empty())
            {
                int clockStartX = rx + m.padX + lw + lblGap + pw + m.gapMed;
                Gdiplus::REAL r = m.clockW * 0.45f;
                Gdiplus::REAL cx = clockStartX + m.clockW * 0.5f;
                Gdiplus::REAL cy = ry + rh * 0.5f;
                DrawClockIcon(g, cx, cy, r, secColor);
            }
        }
        else if (style == STYLE_PROGRESS_BAR)
        {
            int barX = rx + lw + lblGap;
            int barH = std::max(3, std::min(5, rh / 4));
            Gdiplus::REAL bx = (Gdiplus::REAL)barX;
            Gdiplus::REAL by = (Gdiplus::REAL)(ry + (rh - barH) / 2.0f);
            Gdiplus::REAL bw = (Gdiplus::REAL)m.barW;
            Gdiplus::REAL bh = (Gdiplus::REAL)barH;
            Gdiplus::REAL r = bh * 0.5f;

            int valLum = (GetRValue(valueColor) * 299 + GetGValue(valueColor) * 587 + GetBValue(valueColor) * 114) / 1000;
            bool textIsDark = (valLum < 128);
            COLORREF trackClr = textIsDark ? RGB(202, 208, 218) : RGB(54, 60, 72);
            Gdiplus::GraphicsPath trackPath;
            AddRoundedRectPath(trackPath, bx, by, bw, bh, r);
            Gdiplus::SolidBrush trackBrush(ToGpColor(trackClr));
            g.FillPath(&trackBrush, &trackPath);

            if (e.percent > 0)
            {
                Gdiplus::REAL fillW = bw * (e.percent / 100.0f);
                if (fillW < bh) fillW = bh;
                if (fillW > bw) fillW = bw;
                Gdiplus::GraphicsPath fillPath;
                AddRoundedRectPath(fillPath, bx, by, fillW, bh, r);
                Gdiplus::SolidBrush fillBrush(ToGpColor(threshColor));
                g.FillPath(&fillBrush, &fillPath);
            }
        }
    }

    // 2. Crisp native GDI text layer
    int curX = rx + (insidePill ? m.padX : 0);
    int textY = ry;
    int textH = rh;

    if (!lbl.empty())
    {
        DrawStyledText(hdc, curX, textY, lw + 2, textH, lbl, lblColor, DT_LEFT, false, m.digitSlotW);
    }

    if (style == STYLE_CLASSIC)
    {
        curX += lw + lblGap;
        DrawStyledText(hdc, curX, textY, pw, textH, e.pctText, valueColor, aligned ? DT_RIGHT : DT_LEFT, tab, m.digitSlotW);
        if (!rst.empty())
        {
            curX += pw + m.gapSmall;
            DrawStyledText(hdc, curX, textY, rstw, textH, rst, secColor, aligned ? DT_RIGHT : DT_LEFT, tab, m.digitSlotW);
        }
    }
    else if (style == STYLE_TYPOGRAPHIC)
    {
        curX += lw + lblGap;
        DrawStyledText(hdc, curX, textY, pw, textH, e.pctText, valueColor, aligned ? DT_RIGHT : DT_LEFT, tab, m.digitSlotW);
        if (!rst.empty())
        {
            curX += pw + m.gapSmall;
            DrawStyledText(hdc, curX, textY, m.sepW + 2, textH, L"\x00B7", secColor, DT_CENTER, false, m.digitSlotW);
            curX += m.sepW + m.gapSmall;
            DrawStyledText(hdc, curX, textY, rstw, textH, rst, secColor, aligned ? DT_RIGHT : DT_LEFT, tab, m.digitSlotW);
        }
    }
    else if (style == STYLE_PROGRESS_BAR)
    {
        curX += lw + lblGap + m.barW + m.gapMed;
        DrawStyledText(hdc, curX, textY, pw, textH, e.pctText, valueColor, aligned ? DT_RIGHT : DT_LEFT, tab, m.digitSlotW);
        if (!rst.empty())
        {
            curX += pw + m.gapMed;
            DrawStyledText(hdc, curX, textY, rstw, textH, rst, secColor, aligned ? DT_RIGHT : DT_LEFT, tab, m.digitSlotW);
        }
    }
    else if (style == STYLE_MINIMAL_DOTS)
    {
        curX += lw + (lw > 0 ? m.gapSmall : 0);
        DrawStyledText(hdc, curX, textY, pw, textH, e.pctText, valueColor, DT_LEFT, tab, m.digitSlotW);
    }
    else if (style == STYLE_PILL_BADGES)
    {
        curX += lw + lblGap;
        DrawStyledText(hdc, curX, textY, pw, textH, e.pctText, valueColor, aligned ? DT_RIGHT : DT_LEFT, tab, m.digitSlotW);
        if (!rst.empty())
        {
            curX += pw + m.gapMed + m.clockW + m.clockGap;
            DrawStyledText(hdc, curX, textY, rstw, textH, rst, secColor, aligned ? DT_RIGHT : DT_LEFT, tab, m.digitSlotW);
        }
    }
}

static int ComputeMinimalDotsWidth(HDC hdc, const std::vector<QuotaDisplayEntry>& entries,
                                   const PluginSettings& settings, int& outDotSize, int& outDivW)
{
    SIZE szRef = { 0, 0 };
    GetTextExtentPoint32W(hdc, L"0", 1, &szRef);
    int fontH = (szRef.cy > 0) ? szRef.cy : 16;
    int digitSlotW = ComputeDigitSlotWidth(hdc);
    int gapSmall = std::max(3, digitSlotW / 2);
    int gapMed = std::max(5, (digitSlotW * 2) / 3);
    outDotSize = std::max(6, (fontH * 4) / 9);
    outDivW = 2;

    int totalW = 0;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (i > 0) totalW += gapMed + outDivW + gapMed;
        std::wstring lbl = FormatLabelForStyle(entries[i], STYLE_MINIMAL_DOTS);
        int lw = lbl.empty() ? 0 : MeasureTextWidth(hdc, lbl, false, digitSlotW);
        int pw = MeasureTextWidth(hdc, entries[i].pctText, settings.tabularDigits, digitSlotW);
        totalW += outDotSize + gapSmall + (lw > 0 ? lw + gapSmall : 0) + pw;
    }
    return totalW;
}

static int ComputeInterColGap(int digitSlotW, DisplayStyle style)
{
    if (style == STYLE_PILL_BADGES) return std::max(8, digitSlotW + 1);
    return std::max(14, digitSlotW * 2);
}

class CFontNormalizerGuard
{
public:
    explicit CFontNormalizerGuard(HDC hdc) : m_hdc(hdc), m_hOldFont(nullptr), m_hTempFont(nullptr)
    {
        if (!m_hdc) return;
        HFONT curFont = (HFONT)GetCurrentObject(m_hdc, OBJ_FONT);
        if (curFont)
        {
            LOGFONTW lf{};
            if (GetObjectW(curFont, sizeof(lf), &lf))
            {
                // Specifically for Segoe UI Semibold: when parent window passes lfWeight <= 400,
                // TrafficMonitor's DirectWrite renders Segoe UI at weight 400 while GDI renders 600.
                if (lf.lfWeight <= FW_NORMAL)
                {
                    std::wstring name(lf.lfFaceName);
                    std::wstring lower;
                    lower.reserve(name.size());
                    for (wchar_t c : name) lower.push_back(towlower(c));

                    if (lower.rfind(L"segoe ui ", 0) == 0)
                    {
                        const wchar_t* suffixes[] = { L" semibold", L" semi-bold", L" semi bold" };
                        for (const wchar_t* sfx : suffixes)
                        {
                            size_t pos = lower.rfind(sfx);
                            if (pos != std::wstring::npos && pos + wcslen(sfx) == lower.size())
                            {
                                lf.lfFaceName[pos] = L'\0';
                                m_hTempFont = CreateFontIndirectW(&lf);
                                if (m_hTempFont)
                                {
                                    m_hOldFont = (HFONT)SelectObject(m_hdc, m_hTempFont);
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    ~CFontNormalizerGuard()
    {
        if (m_hOldFont && m_hdc)
        {
            SelectObject(m_hdc, m_hOldFont);
        }
        if (m_hTempFont)
        {
            DeleteObject(m_hTempFont);
        }
    }

    CFontNormalizerGuard(const CFontNormalizerGuard&) = delete;
    CFontNormalizerGuard& operator=(const CFontNormalizerGuard&) = delete;

private:
    HDC m_hdc{nullptr};
    HFONT m_hOldFont{nullptr};
    HFONT m_hTempFont{nullptr};
};

static bool CanStackTwoRows(HDC hdc, int h)
{
    if (h <= 0) return true;
    SIZE szRef = { 0, 0 };
    GetTextExtentPoint32W(hdc, L"0", 1, &szRef);
    int fontH = (szRef.cy > 0) ? szRef.cy : 16;
    // Require >= 1.5x glyph height (min 24px) to stack two rows; single-row slots
    // (including 125%-150% DPI half-height cells and horizontal arrange) lay out side by side.
    int minDoubleRowH = std::max(24, (fontH * 3 + 1) / 2);
    return h >= minDoubleRowH;
}

static int ComputeSideBySideMetrics(HDC hdc, const std::vector<QuotaDisplayEntry>& entries,
                                    const PluginSettings& settings, int colGap,
                                    std::vector<ColumnLayoutMetrics>& outCols)
{
    outCols.clear();
    int totalW = 0;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        std::vector<const QuotaDisplayEntry*> singleRow = { &entries[i] };
        ColumnLayoutMetrics cm = ComputeColumnMetrics(hdc, singleRow, settings);
        if (i > 0) totalW += colGap;
        totalW += cm.totalW;
        outCols.push_back(cm);
    }
    return totalW;
}

int CPluginItem::IsDoubleLineExclusive() const
{
    if (!m_isCustomDraw) return 0;
    std::lock_guard<std::mutex> lock(m_renderMutex);
    if (m_renderSettings.displayStyle == STYLE_MINIMAL_DOTS) return 0;
    if (m_renderEntries.empty())
    {
        const wchar_t* l1 = m_labelBuffers[m_activeLabelBuf.load(std::memory_order_relaxed)];
        const wchar_t* l2 = m_buffers[m_activeBuf.load(std::memory_order_relaxed)];
        return (l1[0] == L'\0' && l2[0] == L'\0') ? 0 : 1;
    }
    return (m_renderEntries.size() >= 2) ? 1 : 0;
}

int CPluginItem::GetItemWidthEx(void* hDC) const
{
    if (!hDC) return 110;
    HDC hdc = static_cast<HDC>(hDC);
    CFontNormalizerGuard fontGuard(hdc);

    std::vector<QuotaDisplayEntry> entries;
    PluginSettings settings;
    {
        std::lock_guard<std::mutex> lock(m_renderMutex);
        entries = m_renderEntries;
        settings = m_renderSettings;
    }

    bool canStack = CanStackTwoRows(hdc, m_lastDrawH.load(std::memory_order_relaxed));

    if (entries.empty())
    {
        const wchar_t* l1 = m_labelBuffers[m_activeLabelBuf.load(std::memory_order_relaxed)];
        const wchar_t* l2 = m_buffers[m_activeBuf.load(std::memory_order_relaxed)];
        if (l1[0] == L'\0' && l2[0] == L'\0') return 0;
        if (!canStack)
        {
            std::wstring combined = std::wstring(l1) + L" " + std::wstring(l2);
            SIZE sz = { 0, 0 };
            GetTextExtentPoint32W(hdc, combined.c_str(), (int)combined.size(), &sz);
            return sz.cx + 4;
        }
        SIZE sz1 = { 0, 0 }, sz2 = { 0, 0 };
        GetTextExtentPoint32W(hdc, l1, (int)wcslen(l1), &sz1);
        GetTextExtentPoint32W(hdc, l2, (int)wcslen(l2), &sz2);
        return ((sz1.cx > sz2.cx) ? sz1.cx : sz2.cx) + 4;
    }

    if (settings.displayStyle == STYLE_MINIMAL_DOTS)
    {
        int dotSize = 0, divW = 0;
        return ComputeMinimalDotsWidth(hdc, entries, settings, dotSize, divW) + 4;
    }

    int digitSlotW = ComputeDigitSlotWidth(hdc);
    int colGap = ComputeInterColGap(digitSlotW, settings.displayStyle);

    if (entries.size() >= 2 && !canStack)
    {
        std::vector<ColumnLayoutMetrics> cols;
        return ComputeSideBySideMetrics(hdc, entries, settings, colGap, cols) + 4;
    }

    if (entries.size() == 1)
    {
        std::vector<const QuotaDisplayEntry*> colRows = { &entries[0] };
        ColumnLayoutMetrics col = ComputeColumnMetrics(hdc, colRows, settings);
        return col.totalW + 4;
    }

    // General 2-row column grid: col c holds entries[2*c] (top) and optional entries[2*c + 1] (bottom)
    int totalW = 0;
    size_t numCols = (entries.size() + 1) / 2;
    for (size_t c = 0; c < numCols; ++c)
    {
        std::vector<const QuotaDisplayEntry*> colRows = { &entries[c * 2] };
        if (c * 2 + 1 < entries.size()) colRows.push_back(&entries[c * 2 + 1]);
        ColumnLayoutMetrics col = ComputeColumnMetrics(hdc, colRows, settings);
        if (c > 0) totalW += colGap;
        totalW += col.totalW;
    }
    return totalW + 4;
}

void CPluginItem::DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode)
{
    if (!hDC) return;
    HDC hdc = static_cast<HDC>(hDC);
    CFontNormalizerGuard fontGuard(hdc);
    if (h > 0) m_lastDrawH.store(h, std::memory_order_relaxed);
    bool canStack = CanStackTwoRows(hdc, h);

    std::vector<QuotaDisplayEntry> entries;
    PluginSettings settings;
    {
        std::lock_guard<std::mutex> lock(m_renderMutex);
        entries = m_renderEntries;
        settings = m_renderSettings;
    }

    int prevBkMode = SetBkMode(hdc, TRANSPARENT);
    COLORREF prevColor = GetTextColor(hdc);

    // Host configured text colors from TrafficMonitor:
    COLORREF hostVal = m_hostValueColor.load(std::memory_order_relaxed);
    COLORREF hostLbl = m_hostLabelColor.load(std::memory_order_relaxed);

    COLORREF valColor = (hostVal != CLR_INVALID) ? hostVal : prevColor;
    COLORREF lblColor = (hostLbl != CLR_INVALID) ? hostLbl : valColor;
    SetTextColor(hdc, valColor);

    if (entries.empty())
    {
        const wchar_t* l1 = m_labelBuffers[m_activeLabelBuf.load(std::memory_order_relaxed)];
        const wchar_t* l2 = m_buffers[m_activeBuf.load(std::memory_order_relaxed)];
        if (l1[0] == L'\0' && l2[0] == L'\0')
        {
            SetTextColor(hdc, prevColor);
            SetBkMode(hdc, prevBkMode);
            return;
        }
        if (canStack)
        {
            int halfH = h / 2;
            RECT r1 = { x, y, x + w, y + halfH };
            RECT r2 = { x, y + halfH, x + w, y + h };
            DrawTextW(hdc, l1, -1, &r1, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            DrawTextW(hdc, l2, -1, &r2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        else
        {
            std::wstring combined = std::wstring(l1) + L" " + std::wstring(l2);
            RECT r = { x, y, x + w, y + h };
            DrawTextW(hdc, combined.c_str(), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        SetTextColor(hdc, prevColor);
        SetBkMode(hdc, prevBkMode);
        return;
    }

    // Minimal Status + Colored Dots (compact single line, centered vertically)
    if (settings.displayStyle == STYLE_MINIMAL_DOTS)
    {
        int digitSlotW = ComputeDigitSlotWidth(hdc);
        int gapSmall = std::max(3, digitSlotW / 2);
        int gapMed = std::max(5, (digitSlotW * 2) / 3);
        int dotSize = 0, divW = 0;
        ComputeMinimalDotsWidth(hdc, entries, settings, dotSize, divW);

        COLORREF secColor = ComputeSecondaryColor(valColor, dark_mode, false, settings.dimSecondaryInfo);

        EnsureGdiPlus();
        {
            Gdiplus::Graphics g(hdc);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            int curX = x;
            for (size_t i = 0; i < entries.size(); ++i)
            {
                if (i > 0)
                {
                    curX += gapMed;
                    Gdiplus::Pen divPen(ToGpColor(secColor), 1.0f);
                    Gdiplus::REAL dy1 = y + h * 0.25f;
                    Gdiplus::REAL dy2 = y + h * 0.75f;
                    g.DrawLine(&divPen, (Gdiplus::REAL)curX, dy1, (Gdiplus::REAL)curX, dy2);
                    curX += divW + gapMed;
                }
                COLORREF dotClr = GetThresholdColor(entries[i].percent, dark_mode);
                Gdiplus::SolidBrush dotBrush(ToGpColor(dotClr));
                Gdiplus::REAL dotY = y + (h - dotSize) * 0.5f;
                g.FillEllipse(&dotBrush, (Gdiplus::REAL)curX, dotY, (Gdiplus::REAL)dotSize, (Gdiplus::REAL)dotSize);

                std::wstring lbl = FormatLabelForStyle(entries[i], STYLE_MINIMAL_DOTS);
                int lw = lbl.empty() ? 0 : MeasureTextWidth(hdc, lbl, false, digitSlotW);
                int pw = MeasureTextWidth(hdc, entries[i].pctText, settings.tabularDigits, digitSlotW);
                curX += dotSize + gapSmall + (lw > 0 ? lw + gapSmall : 0) + pw;
            }
        }

        int curX = x;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            if (i > 0) curX += gapMed + divW + gapMed;
            curX += dotSize + gapSmall;
            std::wstring lbl = FormatLabelForStyle(entries[i], STYLE_MINIMAL_DOTS);
            int lw = lbl.empty() ? 0 : MeasureTextWidth(hdc, lbl, false, digitSlotW);
            int pw = MeasureTextWidth(hdc, entries[i].pctText, settings.tabularDigits, digitSlotW);
            if (!lbl.empty())
            {
                DrawStyledText(hdc, curX, y, lw + 2, h, lbl, lblColor, DT_LEFT, false, digitSlotW);
                curX += lw + gapSmall;
            }
            DrawStyledText(hdc, curX, y, pw + 2, h, entries[i].pctText, valColor, DT_LEFT, settings.tabularDigits, digitSlotW);
            curX += pw;
        }

        SetTextColor(hdc, prevColor);
        SetBkMode(hdc, prevBkMode);
        return;
    }

    int digitSlotW = ComputeDigitSlotWidth(hdc);
    int colGap = ComputeInterColGap(digitSlotW, settings.displayStyle);

    if (entries.size() == 1)
    {
        std::vector<const QuotaDisplayEntry*> colRows = { &entries[0] };
        ColumnLayoutMetrics m = ComputeColumnMetrics(hdc, colRows, settings);
        int rowH = (settings.displayStyle == STYLE_PILL_BADGES) ? std::min(h, std::max(18, m.fontH + 6)) : h;
        int rowY = y + (h - rowH) / 2;
        DrawSingleColumnEntry(hdc, x, rowY, m.totalW, rowH, entries[0], m, settings, lblColor, valColor, dark_mode);
    }
    else if (!canStack)
    {
        std::vector<ColumnLayoutMetrics> cols;
        ComputeSideBySideMetrics(hdc, entries, settings, colGap, cols);
        int curX = x;
        for (size_t i = 0; i < cols.size(); ++i)
        {
            int rowH = (settings.displayStyle == STYLE_PILL_BADGES) ? std::min(h, std::max(18, cols[i].fontH + 6)) : h;
            int rowY = y + (h - rowH) / 2;
            DrawSingleColumnEntry(hdc, curX, rowY, cols[i].totalW, rowH, entries[i], cols[i], settings, lblColor, valColor, dark_mode);
            curX += cols[i].totalW + colGap;
        }
    }
    else
    {
        // General 2-row column grid: col c holds entries[2*c] (top) and optional entries[2*c + 1] (bottom)
        int halfH = h / 2;
        int curX = x;
        size_t numCols = (entries.size() + 1) / 2;
        for (size_t c = 0; c < numCols; ++c)
        {
            std::vector<const QuotaDisplayEntry*> colRows = { &entries[c * 2] };
            if (c * 2 + 1 < entries.size()) colRows.push_back(&entries[c * 2 + 1]);
            ColumnLayoutMetrics col = ComputeColumnMetrics(hdc, colRows, settings);
            DrawSingleColumnEntry(hdc, curX, y, col.totalW, halfH, entries[c * 2], col, settings, lblColor, valColor, dark_mode);
            if (c * 2 + 1 < entries.size())
            {
                DrawSingleColumnEntry(hdc, curX, y + halfH, col.totalW, h - halfH, entries[c * 2 + 1], col, settings, lblColor, valColor, dark_mode);
            }
            curX += col.totalW + colGap;
        }
    }

    SetTextColor(hdc, prevColor);
    SetBkMode(hdc, prevBkMode);
}

CAntigravityPlugin::CAntigravityPlugin()
    : m_itemCombined(L"antigravity_quota", L"Antigravity Quota", L"G:100% A:100%", L"G:100% (4h59m) A:100% (6d23h)", true),
      m_itemGemini(L"antigravity_gemini", L"Antigravity Gemini", L"G:", L"G:100% (4h59m)", true),
      m_itemClaude(L"antigravity_claude", L"Antigravity Claude", L"C:", L"C:100% (4h59m)", true),
      m_itemAccount(L"antigravity_account", L"Antigravity Account", L"Acc:", L"AccountName", false),
      m_itemArgon(L"antigravity_argon", L"Antigravity Argon", L"A:", L"A:100% (6d23h)", true)
{
    wcscpy_s(m_tooltipBuffers[0], L"Antigravity Quota\r\nInitializing...");
    wcscpy_s(m_tooltipBuffers[1], L"Antigravity Quota\r\nInitializing...");

    LoadSettings();
    DataRequired();
}

CAntigravityPlugin::~CAntigravityPlugin()
{
    StopBackgroundWorker();
}

void CAntigravityPlugin::OnInitialize(ITrafficMonitor* pApp)
{
    m_pApp = pApp;
    LoadSettings();
    DataRequired();
    EnsureBackgroundWorkerStarted();
}

void CAntigravityPlugin::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    if (!data) return;

    auto applyPercentChange = [&]() {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        if (!m_lastJsonStr.empty())
        {
            ParseAndUpdate(m_lastJsonStr, false);
        }
        else
        {
            m_itemCombined.UpdateLabel(m_hidePercent ? L"G:100 A:100" : L"G:100% A:100%");
        }
    };

    if (index == EI_LABEL_TEXT_COLOR)
    {
        wchar_t* endPtr = nullptr;
        COLORREF clr = static_cast<COLORREF>(wcstoul(data, &endPtr, 10));
        m_itemCombined.SetHostLabelColor(clr);
        m_itemGemini.SetHostLabelColor(clr);
        m_itemClaude.SetHostLabelColor(clr);
        m_itemAccount.SetHostLabelColor(clr);
        m_itemArgon.SetHostLabelColor(clr);
    }
    else if (index == EI_VALUE_TEXT_COLOR)
    {
        wchar_t* endPtr = nullptr;
        COLORREF clr = static_cast<COLORREF>(wcstoul(data, &endPtr, 10));
        m_itemCombined.SetHostValueColor(clr);
        m_itemGemini.SetHostValueColor(clr);
        m_itemClaude.SetHostValueColor(clr);
        m_itemAccount.SetHostValueColor(clr);
        m_itemArgon.SetHostValueColor(clr);
    }
    else if (index == EI_TASKBAR_WND_NOT_SHOW_PERCENT)
    {
        bool hide = (*data == L'1' || _wcsicmp(data, L"true") == 0);
        m_taskbarHidePercent = hide;
        m_taskbarSettingReceived = true;
        if (m_hidePercent != hide)
        {
            m_hidePercent = hide;
            applyPercentChange();
        }
    }
    else if (index == EI_MAIN_WND_NOT_SHOW_PERCENT)
    {
        bool hide = (*data == L'1' || _wcsicmp(data, L"true") == 0);
        m_mainWndHidePercent = hide;
        if (!m_taskbarSettingReceived && m_hidePercent != hide)
        {
            m_hidePercent = hide;
            applyPercentChange();
        }
    }
    else if (index == EI_DRAW_TASKBAR_WND)
    {
        bool isTaskbar = (*data == L'1');
        bool targetHide = isTaskbar ? m_taskbarHidePercent : m_mainWndHidePercent;
        if (m_hidePercent != targetHide)
        {
            m_hidePercent = targetHide;
            applyPercentChange();
        }
    }
}

std::wstring CAntigravityPlugin::GetConfigPath() const
{
    if (m_pApp)
    {
        const wchar_t* pDir = m_pApp->GetPluginConfigDir();
        if (pDir && *pDir)
        {
            return std::wstring(pDir) + L"\\AntigravityPlugin.ini";
        }
    }

    wchar_t localAppData[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (len > 0 && len < MAX_PATH)
    {
        return std::wstring(localAppData) + L"\\AntigravityQuota\\trafficmonitor.ini";
    }
    return L"AntigravityPlugin.ini";
}

std::wstring CAntigravityPlugin::GetHostConfigPath() const
{
    if (!m_hostConfigPathOverride.empty()) return m_hostConfigPathOverride;
    if (m_pApp)
    {
        const wchar_t* pDir = m_pApp->GetPluginConfigDir();
        if (pDir && *pDir)
        {
            std::wstring dir(pDir);
            while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) dir.pop_back();
            size_t slash = dir.find_last_of(L"\\/");
            if (slash != std::wstring::npos)
            {
                return dir.substr(0, slash) + L"\\config.ini";
            }
        }
    }
    return L"";
}

std::wstring CAntigravityPlugin::ResolveHostItemLabel(const wchar_t* itemId, const wchar_t* defaultLabel, bool& outFromHost) const
{
    outFromHost = false;
    if (m_settings.hideLabels) return L"";
    std::wstring hostCfg = GetHostConfigPath();
    if (!hostCfg.empty() && GetFileAttributesW(hostCfg.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        wchar_t buf[64]{};
        GetPrivateProfileStringW(L"plugin_display_str_taskbar_window", itemId, L"__TM_UNSET__", buf, 64, hostCfg.c_str());
        if (wcscmp(buf, L"__TM_UNSET__") != 0)
        {
            outFromHost = true;
            return std::wstring(buf);
        }
    }
    return defaultLabel ? std::wstring(defaultLabel) : L"";
}

bool CAntigravityPlugin::HasHostConfigChanged()
{
    std::wstring hostCfg = GetHostConfigPath();
    if (hostCfg.empty()) return false;
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(hostCfg.c_str(), GetFileExInfoStandard, &fad)) return false;
    if (fad.ftLastWriteTime.dwLowDateTime != m_lastHostCfgWriteTime.dwLowDateTime ||
        fad.ftLastWriteTime.dwHighDateTime != m_lastHostCfgWriteTime.dwHighDateTime)
    {
        m_lastHostCfgWriteTime = fad.ftLastWriteTime;
        return true;
    }
    return false;
}

void CAntigravityPlugin::LoadSettings()
{
    std::wstring cfg = GetConfigPath();
    m_settings.shortResetTime = (GetPrivateProfileIntW(L"Display", L"ShortResetTime", 0, cfg.c_str()) != 0);
    m_settings.hideResetTime = (GetPrivateProfileIntW(L"Display", L"HideResetTime", 0, cfg.c_str()) != 0);
    m_settings.smartMode = (GetPrivateProfileIntW(L"Display", L"SmartMode", 0, cfg.c_str()) != 0);
    m_settings.hideWeeklyQuota = (GetPrivateProfileIntW(L"Display", L"HideWeeklyQuota", 0, cfg.c_str()) != 0);
    m_settings.hideLabels = (GetPrivateProfileIntW(L"Display", L"HideLabels", 0, cfg.c_str()) != 0);
    int styleVal = GetPrivateProfileIntW(L"Display", L"DisplayStyle", (int)STYLE_TYPOGRAPHIC, cfg.c_str());
    if (styleVal < STYLE_CLASSIC || styleVal > STYLE_PILL_BADGES) styleVal = STYLE_TYPOGRAPHIC;
    m_settings.displayStyle = static_cast<DisplayStyle>(styleVal);
    m_settings.alignColumns = (GetPrivateProfileIntW(L"Display", L"AlignColumns", 1, cfg.c_str()) != 0);
    m_settings.tabularDigits = (GetPrivateProfileIntW(L"Display", L"TabularDigits", 1, cfg.c_str()) != 0);
    m_settings.dimSecondaryInfo = (GetPrivateProfileIntW(L"Display", L"DimSecondaryInfo", 1, cfg.c_str()) != 0);
}

void CAntigravityPlugin::SaveSettings()
{
    std::wstring cfg = GetConfigPath();
    size_t lastSlash = cfg.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos)
    {
        std::wstring dir = cfg.substr(0, lastSlash);
        CreateDirectoryW(dir.c_str(), nullptr);
    }
    WritePrivateProfileStringW(L"Display", L"ShortResetTime", m_settings.shortResetTime ? L"1" : L"0", cfg.c_str());
    WritePrivateProfileStringW(L"Display", L"HideResetTime", m_settings.hideResetTime ? L"1" : L"0", cfg.c_str());
    WritePrivateProfileStringW(L"Display", L"SmartMode", m_settings.smartMode ? L"1" : L"0", cfg.c_str());
    WritePrivateProfileStringW(L"Display", L"HideWeeklyQuota", m_settings.hideWeeklyQuota ? L"1" : L"0", cfg.c_str());
    WritePrivateProfileStringW(L"Display", L"HideLabels", m_settings.hideLabels ? L"1" : L"0", cfg.c_str());
    WritePrivateProfileStringW(L"Display", L"DisplayStyle", std::to_wstring((int)m_settings.displayStyle).c_str(), cfg.c_str());
    WritePrivateProfileStringW(L"Display", L"AlignColumns", m_settings.alignColumns ? L"1" : L"0", cfg.c_str());
    WritePrivateProfileStringW(L"Display", L"TabularDigits", m_settings.tabularDigits ? L"1" : L"0", cfg.c_str());
    WritePrivateProfileStringW(L"Display", L"DimSecondaryInfo", m_settings.dimSecondaryInfo ? L"1" : L"0", cfg.c_str());
}

void CAntigravityPlugin::RefreshHostWindows()
{
    if (m_pApp && m_pApp->GetAPIVersion() >= 1)
    {
        HWND hTaskbar = static_cast<HWND>(m_pApp->GetTaskbarWindowHwnd());
        if (hTaskbar && IsWindow(hTaskbar))
        {
            InvalidateRect(hTaskbar, nullptr, FALSE);
        }
        HWND hMain = static_cast<HWND>(m_pApp->GetMainWindowHwnd());
        if (hMain && IsWindow(hMain))
        {
            InvalidateRect(hMain, nullptr, FALSE);
        }
    }
}

void CAntigravityPlugin::ApplySettings(const PluginSettings& settings)
{
    m_settings = settings;
    SaveSettings();
    {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        if (!m_lastJsonStr.empty())
        {
            ParseAndUpdate(m_lastJsonStr, false);
        }
        else
        {
            m_lastWriteTime.dwLowDateTime = 0;
            m_lastWriteTime.dwHighDateTime = 0;
            m_lastFileSize = 0;
        }
    }
    if (m_lastJsonStr.empty())
    {
        DataRequired();
    }

    RefreshHostWindows();
}

IPluginItem* CAntigravityPlugin::GetItem(int index)
{
    switch (index)
    {
    case 0: return &m_itemCombined;
    case 1: return &m_itemGemini;
    case 2: return &m_itemClaude;
    case 3: return &m_itemAccount;
    case 4: return &m_itemArgon;
    default: return nullptr;
    }
}

const wchar_t* CAntigravityPlugin::GetInfo(PluginInfoIndex index)
{
    static const wchar_t* s_name = L"Antigravity Quota";
    static const wchar_t* s_description = L"Displays Google Antigravity account quotas (Gemini, Claude, Argon).";
    static const wchar_t* s_author = L"Google DeepMind";
    static const wchar_t* s_copyright = L"Copyright (C) 2026";
    static const wchar_t* s_version = L"1.0.0";
    static const wchar_t* s_url = L"https://github.com/google/antigravity";

    switch (index)
    {
    case TMI_NAME: return s_name;
    case TMI_DESCRIPTION: return s_description;
    case TMI_AUTHOR: return s_author;
    case TMI_COPYRIGHT: return s_copyright;
    case TMI_VERSION: return s_version;
    case TMI_URL: return s_url;
    default: return L"";
    }
}

const wchar_t* CAntigravityPlugin::GetTooltipInfo()
{
    return m_tooltipBuffers[m_tooltipActiveBuf.load(std::memory_order_relaxed)];
}

void CAntigravityPlugin::UpdateTooltip(const wchar_t* newTooltip)
{
    int next = 1 - m_tooltipActiveBuf.load(std::memory_order_relaxed);
    wcscpy_s(m_tooltipBuffers[next], newTooltip);
    m_tooltipActiveBuf.store(next, std::memory_order_release);
}

const wchar_t* CAntigravityPlugin::GetCommandName(int command_index)
{
    switch (command_index)
    {
    case 0: return L"Refresh Quota Now";
    case 1: return L"Smart Mode (Active Quotas Only)";
    case 2: return L"Hide Weekly Quota (Wk)";
    case 3: return L"Hide Reset Time";
    case 4: return L"Short Reset Time (e.g. 5h)";
    case 5: return L"Sign In with Google...";
    default: return nullptr;
    }
}

int CAntigravityPlugin::IsCommandChecked(int command_index)
{
    switch (command_index)
    {
    case 1: return m_settings.smartMode ? 1 : 0;
    case 2: return m_settings.hideWeeklyQuota ? 1 : 0;
    case 3: return m_settings.hideResetTime ? 1 : 0;
    case 4: return (!m_settings.hideResetTime && m_settings.shortResetTime) ? 1 : 0;
    default: return 0;
    }
}

void CAntigravityPlugin::OnPluginCommand(int command_index, void* hWnd, void* para)
{
    if (command_index == 0)
    {
        {
            std::lock_guard<std::mutex> lock(m_dataMutex);
            m_lastWriteTime.dwLowDateTime = 0;
            m_lastWriteTime.dwHighDateTime = 0;
            m_lastFileSize = 0;
        }
        DataRequired();
        EnsureBackgroundWorkerStarted();
        RequestPollNow(false);
    }
    else if (command_index == 1)
    {
        PluginSettings s = m_settings;
        s.smartMode = !s.smartMode;
        ApplySettings(s);
    }
    else if (command_index == 2)
    {
        PluginSettings s = m_settings;
        s.hideWeeklyQuota = !s.hideWeeklyQuota;
        ApplySettings(s);
    }
    else if (command_index == 3)
    {
        PluginSettings s = m_settings;
        s.hideResetTime = !s.hideResetTime;
        ApplySettings(s);
    }
    else if (command_index == 4)
    {
        PluginSettings s = m_settings;
        if (s.hideResetTime)
        {
            s.hideResetTime = false;
            s.shortResetTime = true;
        }
        else
        {
            s.shortResetTime = !s.shortResetTime;
        }
        ApplySettings(s);
    }
    else if (command_index == 5)
    {
        EnsureBackgroundWorkerStarted();
        RequestPollNow(true);
    }
}

enum OptionsCtrlId
{
    IDC_CHK_SMART = 101,
    IDC_CHK_HIDE_WEEKLY = 102,

    IDC_GRP_STYLE = 199,
    IDC_STYLE_CLASSIC = 200,
    IDC_STYLE_TYPOGRAPHIC = 201,
    IDC_STYLE_PROGRESS = 202,
    IDC_STYLE_MINIMAL_DOTS = 203,
    IDC_STYLE_PILL_BADGES = 204,

    IDC_GRP_POLISH = 299,
    IDC_CHK_ALIGN_COLS = 301,
    IDC_CHK_TABULAR = 302,
    IDC_CHK_DIM_SEC = 303,
    IDC_CHK_HIDE_LABELS = 304,

    IDC_GRP_RESET = 399,
    IDC_CHK_HIDE_RESET = 401,
    IDC_RADIO_LONG = 402,
    IDC_RADIO_SHORT = 403,
    IDC_LBL_PERCENT_NOTE = 404,

    IDC_BTN_APPLY = 501
};

static bool SettingsEqual(const PluginSettings& a, const PluginSettings& b)
{
    return a.shortResetTime == b.shortResetTime &&
           a.hideResetTime == b.hideResetTime &&
           a.smartMode == b.smartMode &&
           a.hideWeeklyQuota == b.hideWeeklyQuota &&
           a.hideLabels == b.hideLabels &&
           a.displayStyle == b.displayStyle &&
           a.alignColumns == b.alignColumns &&
           a.tabularDigits == b.tabularDigits &&
           a.dimSecondaryInfo == b.dimSecondaryInfo;
}

struct OptionsDlgContext
{
    CAntigravityPlugin* pPlugin;
    PluginSettings initialSettings;
    bool everApplied;
    HFONT hFont;
};

static void ReadControlsToSettings(HWND hWnd, PluginSettings& out)
{
    out.smartMode = (IsDlgButtonChecked(hWnd, IDC_CHK_SMART) == BST_CHECKED);
    out.hideWeeklyQuota = (IsDlgButtonChecked(hWnd, IDC_CHK_HIDE_WEEKLY) == BST_CHECKED);

    if (IsDlgButtonChecked(hWnd, IDC_STYLE_CLASSIC) == BST_CHECKED) out.displayStyle = STYLE_CLASSIC;
    else if (IsDlgButtonChecked(hWnd, IDC_STYLE_PROGRESS) == BST_CHECKED) out.displayStyle = STYLE_PROGRESS_BAR;
    else if (IsDlgButtonChecked(hWnd, IDC_STYLE_MINIMAL_DOTS) == BST_CHECKED) out.displayStyle = STYLE_MINIMAL_DOTS;
    else if (IsDlgButtonChecked(hWnd, IDC_STYLE_PILL_BADGES) == BST_CHECKED) out.displayStyle = STYLE_PILL_BADGES;
    else out.displayStyle = STYLE_TYPOGRAPHIC;

    out.alignColumns = (IsDlgButtonChecked(hWnd, IDC_CHK_ALIGN_COLS) == BST_CHECKED);
    out.tabularDigits = (IsDlgButtonChecked(hWnd, IDC_CHK_TABULAR) == BST_CHECKED);
    out.dimSecondaryInfo = (IsDlgButtonChecked(hWnd, IDC_CHK_DIM_SEC) == BST_CHECKED);
    out.hideLabels = (IsDlgButtonChecked(hWnd, IDC_CHK_HIDE_LABELS) == BST_CHECKED);

    out.hideResetTime = (IsDlgButtonChecked(hWnd, IDC_CHK_HIDE_RESET) == BST_CHECKED);
    out.shortResetTime = (IsDlgButtonChecked(hWnd, IDC_RADIO_SHORT) == BST_CHECKED);
}

static void UpdateDependentControlStates(HWND hWnd)
{
    bool hideReset = (IsDlgButtonChecked(hWnd, IDC_CHK_HIDE_RESET) == BST_CHECKED);
    bool isMinimalDots = (IsDlgButtonChecked(hWnd, IDC_STYLE_MINIMAL_DOTS) == BST_CHECKED);
    BOOL enableTimerFormat = (!hideReset && !isMinimalDots) ? TRUE : FALSE;
    EnableWindow(GetDlgItem(hWnd, IDC_RADIO_LONG), enableTimerFormat);
    EnableWindow(GetDlgItem(hWnd, IDC_RADIO_SHORT), enableTimerFormat);
}

static HWND AddDlgButton(HWND hParent, HINSTANCE hInst, HFONT hFont, int id, const wchar_t* text,
                         DWORD style, int x, int y, int w, int h)
{
    HWND hCtrl = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | style,
                                 x, y, w, h, hParent, (HMENU)(INT_PTR)id, hInst, nullptr);
    SendMessageW(hCtrl, WM_SETFONT, (WPARAM)hFont, TRUE);
    return hCtrl;
}

static LRESULT CALLBACK OptionsDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    OptionsDlgContext* ctx = reinterpret_cast<OptionsDlgContext*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (uMsg)
    {
    case WM_CREATE:
    {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        ctx = reinterpret_cast<OptionsDlgContext*>(cs->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));

        ctx->hFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (!ctx->hFont) ctx->hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        const PluginSettings& s = ctx->initialSettings;
        HINSTANCE hi = cs->hInstance;

        // Top Checkboxes: Smart Mode & Hide Weekly Quota
        HWND hSmart = AddDlgButton(hWnd, hi, ctx->hFont, IDC_CHK_SMART,
                                   L"Smart Mode  (Only show actively decreasing quotas)",
                                   BS_AUTOCHECKBOX | WS_TABSTOP, 16, 12, 424, 22);
        SendMessageW(hSmart, BM_SETCHECK, s.smartMode ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND hHideWk = AddDlgButton(hWnd, hi, ctx->hFont, IDC_CHK_HIDE_WEEKLY,
                                    L"Hide Weekly Quota  (Omit 'Wk' weekly quotas across models)",
                                    BS_AUTOCHECKBOX | WS_TABSTOP, 16, 36, 424, 22);
        SendMessageW(hHideWk, BM_SETCHECK, s.hideWeeklyQuota ? BST_CHECKED : BST_UNCHECKED, 0);

        // Group 1: Visual Style
        AddDlgButton(hWnd, hi, ctx->hFont, IDC_GRP_STYLE, L"Visual Style", BS_GROUPBOX, 16, 66, 464, 146);

        AddDlgButton(hWnd, hi, ctx->hFont, IDC_STYLE_TYPOGRAPHIC,
                     L"Typographic Hierarchy  (G  73% \x00B7 4h \x2014 no colons/parens, dimmed timer)",
                     BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 30, 88, 436, 22);
        AddDlgButton(hWnd, hi, ctx->hFont, IDC_STYLE_PROGRESS,
                     L"Micro Progress Bars  (G  \x2588\x2588\x2588\x2591\x2591  73%  4h \x2014 green/yellow/red bar)",
                     BS_AUTORADIOBUTTON, 30, 112, 436, 22);
        AddDlgButton(hWnd, hi, ctx->hFont, IDC_STYLE_MINIMAL_DOTS,
                     L"Minimal Status Dots  (\x25CF G 73% | \x25CF A 84% \x2014 compact, timer in tooltip)",
                     BS_AUTORADIOBUTTON, 30, 136, 436, 22);
        AddDlgButton(hWnd, hi, ctx->hFont, IDC_STYLE_PILL_BADGES,
                     L"Stacked Pill Badges  (Adaptive rounded pills with aligned metric && clock icon)",
                     BS_AUTORADIOBUTTON, 30, 160, 436, 22);
        AddDlgButton(hWnd, hi, ctx->hFont, IDC_STYLE_CLASSIC,
                     L"Classic  (G:73% (4h) \x2014 original text formatting)",
                     BS_AUTORADIOBUTTON, 30, 184, 436, 22);

        int activeStyleId = IDC_STYLE_CLASSIC + (int)s.displayStyle;
        if (activeStyleId < IDC_STYLE_CLASSIC || activeStyleId > IDC_STYLE_PILL_BADGES)
            activeStyleId = IDC_STYLE_TYPOGRAPHIC;
        CheckRadioButton(hWnd, IDC_STYLE_CLASSIC, IDC_STYLE_PILL_BADGES, activeStyleId);

        // Group 2: Layout & Polish
        AddDlgButton(hWnd, hi, ctx->hFont, IDC_GRP_POLISH, L"Layout && Polish", BS_GROUPBOX | WS_GROUP, 16, 220, 464, 120);

        HWND hAlign = AddDlgButton(hWnd, hi, ctx->hFont, IDC_CHK_ALIGN_COLS,
                                   L"Align numbers and reset timers in columns",
                                   BS_AUTOCHECKBOX | WS_TABSTOP, 30, 240, 436, 22);
        SendMessageW(hAlign, BM_SETCHECK, s.alignColumns ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND hTab = AddDlgButton(hWnd, hi, ctx->hFont, IDC_CHK_TABULAR,
                                 L"Tabular / fixed-width digits (prevent taskbar jitter)",
                                 BS_AUTOCHECKBOX | WS_TABSTOP, 30, 264, 436, 22);
        SendMessageW(hTab, BM_SETCHECK, s.tabularDigits ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND hDim = AddDlgButton(hWnd, hi, ctx->hFont, IDC_CHK_DIM_SEC,
                                 L"Dim secondary info (~60% visual weight for labels && timers)",
                                 BS_AUTOCHECKBOX | WS_TABSTOP, 30, 288, 436, 22);
        SendMessageW(hDim, BM_SETCHECK, s.dimSecondaryInfo ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND hHideLabels = AddDlgButton(hWnd, hi, ctx->hFont, IDC_CHK_HIDE_LABELS,
                                        L"Hide model labels  (Omit G, C, A, Wk for a minimal look)",
                                        BS_AUTOCHECKBOX | WS_TABSTOP, 30, 312, 436, 22);
        SendMessageW(hHideLabels, BM_SETCHECK, s.hideLabels ? BST_CHECKED : BST_UNCHECKED, 0);

        // Group 3: Reset Time Display
        AddDlgButton(hWnd, hi, ctx->hFont, IDC_GRP_RESET, L"Reset Time Display", BS_GROUPBOX, 16, 348, 464, 96);

        HWND hHideReset = AddDlgButton(hWnd, hi, ctx->hFont, IDC_CHK_HIDE_RESET,
                                       L"Hide reset time on taskbar (always shown in tooltip)",
                                       BS_AUTOCHECKBOX | WS_TABSTOP, 30, 368, 436, 22);
        SendMessageW(hHideReset, BM_SETCHECK, s.hideResetTime ? BST_CHECKED : BST_UNCHECKED, 0);

        AddDlgButton(hWnd, hi, ctx->hFont, IDC_RADIO_LONG,
                     L"Long form (4h 50m, 6d 23h)",
                     BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 30, 394, 210, 22);
        AddDlgButton(hWnd, hi, ctx->hFont, IDC_RADIO_SHORT,
                     L"Short form (5h, 7d)",
                     BS_AUTORADIOBUTTON, 248, 394, 210, 22);
        CheckRadioButton(hWnd, IDC_RADIO_LONG, IDC_RADIO_SHORT, s.shortResetTime ? IDC_RADIO_SHORT : IDC_RADIO_LONG);

        HWND hPctNote = CreateWindowExW(0, L"STATIC",
                                        L"Note: '%' sign and labels also respect TrafficMonitor Display Text settings.",
                                        WS_CHILD | WS_VISIBLE | WS_GROUP, 30, 420, 440, 18,
                                        hWnd, (HMENU)IDC_LBL_PERCENT_NOTE, hi, nullptr);
        SendMessageW(hPctNote, WM_SETFONT, (WPARAM)ctx->hFont, TRUE);

        // Action Buttons: OK / Cancel / Apply
        AddDlgButton(hWnd, hi, ctx->hFont, IDOK, L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, 220, 458, 80, 28);
        AddDlgButton(hWnd, hi, ctx->hFont, IDCANCEL, L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 310, 458, 80, 28);
        HWND hApply = AddDlgButton(hWnd, hi, ctx->hFont, IDC_BTN_APPLY, L"Apply", BS_PUSHBUTTON | WS_TABSTOP, 400, 458, 80, 28);
        EnableWindow(hApply, FALSE);

        UpdateDependentControlStates(hWnd);
        return 0;
    }

    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);
        WORD notif = HIWORD(wParam);

        if (id == IDC_BTN_APPLY && notif == BN_CLICKED)
        {
            if (ctx && ctx->pPlugin)
            {
                PluginSettings newS = ctx->pPlugin->GetSettings();
                ReadControlsToSettings(hWnd, newS);
                ctx->pPlugin->ApplySettings(newS);
                ctx->everApplied = true;
                EnableWindow(GetDlgItem(hWnd, IDC_BTN_APPLY), FALSE);
            }
            return 0;
        }
        else if (id == IDOK && notif == BN_CLICKED)
        {
            if (ctx && ctx->pPlugin)
            {
                PluginSettings newS = ctx->pPlugin->GetSettings();
                ReadControlsToSettings(hWnd, newS);
                if (!SettingsEqual(newS, ctx->pPlugin->GetSettings()))
                {
                    ctx->pPlugin->ApplySettings(newS);
                    ctx->everApplied = true;
                }
            }
            DestroyWindow(hWnd);
            return 0;
        }
        else if (id == IDCANCEL && notif == BN_CLICKED)
        {
            DestroyWindow(hWnd);
            return 0;
        }
        else if (notif == BN_CLICKED &&
                 (id == IDC_CHK_SMART || id == IDC_CHK_HIDE_WEEKLY ||
                  (id >= IDC_STYLE_CLASSIC && id <= IDC_STYLE_PILL_BADGES) ||
                  (id >= IDC_CHK_ALIGN_COLS && id <= IDC_CHK_HIDE_LABELS) ||
                  (id >= IDC_CHK_HIDE_RESET && id <= IDC_RADIO_SHORT)))
        {
            UpdateDependentControlStates(hWnd);
            if (ctx && ctx->pPlugin)
            {
                PluginSettings currentUi = ctx->pPlugin->GetSettings();
                ReadControlsToSettings(hWnd, currentUi);
                EnableWindow(GetDlgItem(hWnd, IDC_BTN_APPLY),
                             !SettingsEqual(currentUi, ctx->pPlugin->GetSettings()) ? TRUE : FALSE);
            }
            return 0;
        }
        break;
    }

    case WM_CLOSE:
    {
        DestroyWindow(hWnd);
        return 0;
    }

    case WM_DESTROY:
    {
        if (ctx && ctx->hFont && ctx->hFont != (HFONT)GetStockObject(DEFAULT_GUI_FONT))
        {
            DeleteObject(ctx->hFont);
            ctx->hFont = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }

    default:
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }
    return 0;
}

ITMPlugin::OptionReturn CAntigravityPlugin::ShowOptionsDialog(void* hParent)
{
    HWND hWndParent = static_cast<HWND>(hParent);
    HINSTANCE hInstance = GetModuleHandleW(L"AntigravityPlugin.dll");
    if (!hInstance) hInstance = GetModuleHandleW(nullptr);

    static bool s_classRegistered = false;
    const wchar_t* className = L"AntigravityPluginOptionsWnd";
    if (!s_classRegistered)
    {
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = OptionsDlgProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = className;
        RegisterClassExW(&wc);
        s_classRegistered = true;
    }

    OptionsDlgContext ctx;
    ctx.pPlugin = this;
    ctx.initialSettings = m_settings;
    ctx.everApplied = false;
    ctx.hFont = nullptr;

    DWORD dwStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
    DWORD dwExStyle = WS_EX_DLGMODALFRAME | WS_EX_TOPMOST;
    RECT rc = { 0, 0, 496, 500 };
    AdjustWindowRectEx(&rc, dwStyle, FALSE, dwExStyle);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    if (hWndParent && IsWindow(hWndParent))
    {
        RECT prc;
        if (GetWindowRect(hWndParent, &prc))
        {
            x = prc.left + ((prc.right - prc.left) - width) / 2;
            y = prc.top + ((prc.bottom - prc.top) - height) / 2;
        }
    }

    HWND hWndDlg = CreateWindowExW(
        dwExStyle,
        className,
        L"Antigravity Quota Options",
        dwStyle,
        x, y, width, height,
        hWndParent,
        nullptr,
        hInstance,
        &ctx
    );

    if (!hWndDlg) return OR_OPTION_NOT_PROVIDED;

    if (hWndParent && IsWindow(hWndParent))
    {
        EnableWindow(hWndParent, FALSE);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        if (!IsDialogMessageW(hWndDlg, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (hWndParent && IsWindow(hWndParent))
    {
        EnableWindow(hWndParent, TRUE);
        SetForegroundWindow(hWndParent);
    }

    if (ctx.everApplied || !SettingsEqual(m_settings, ctx.initialSettings))
    {
        return OR_OPTION_CHANGED;
    }

    return OR_OPTION_UNCHANGED;
}

std::wstring CAntigravityPlugin::ResolveVaultPath() const
{
    return QuotaSync::ResolveVaultPath();
}

void CAntigravityPlugin::UpdateVaultFileCacheMeta(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA fileAttr;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fileAttr))
    {
        m_lastWriteTime = fileAttr.ftLastWriteTime;
        m_lastFileSize = fileAttr.nFileSizeLow;
    }
}

bool CAntigravityPlugin::ReadAndDecryptVault(std::string& outJsonStr)
{
    std::wstring path = ResolveVaultPath();

    WIN32_FILE_ATTRIBUTE_DATA fileAttr;
    if (path.empty() || !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fileAttr))
    {
        if (!m_hasLoadedData)
        {
            m_itemCombined.UpdateValue(L"N/A");
            m_itemGemini.UpdateValue(L"N/A");
            m_itemClaude.UpdateValue(L"N/A");
            m_itemAccount.UpdateValue(L"N/A");
            UpdateTooltip(L"Antigravity Quota\r\nNo account linked.\r\nRight-click -> Sign In with Google...");
        }
        return false;
    }

    // Check if file is unchanged
    if (m_hasLoadedData &&
        fileAttr.ftLastWriteTime.dwLowDateTime == m_lastWriteTime.dwLowDateTime &&
        fileAttr.ftLastWriteTime.dwHighDateTime == m_lastWriteTime.dwHighDateTime &&
        fileAttr.nFileSizeLow == m_lastFileSize)
    {
        return false; // No change needed
    }

    if (!QuotaSync::ReadVaultJson(outJsonStr))
    {
        return false;
    }

    // Update tracked file attributes upon successful read & decrypt
    m_lastWriteTime = fileAttr.ftLastWriteTime;
    m_lastFileSize = fileAttr.nFileSizeLow;

    return true;
}

void CAntigravityPlugin::ParseAndUpdate(const std::string& jsonStr, bool updateSmartState)
{
    if (&jsonStr != &m_lastJsonStr)
    {
        m_lastJsonStr = jsonStr;
    }
    try
    {
        json j = json::parse(jsonStr, nullptr, false);
        if (j.is_discarded()) return;

        const json* pArr = nullptr;
        if (j.is_array())
        {
            pArr = &j;
        }
        else if (j.is_object() && j.contains("value") && j["value"].is_array())
        {
            pArr = &j["value"];
        }
        else if (j.is_object() && j.contains("accounts") && j["accounts"].is_array())
        {
            pArr = &j["accounts"];
        }
        else
        {
            return;
        }

        if (pArr->empty()) return;

        const json* activeAccount = nullptr;
        for (const auto& acc : *pArr)
        {
            if (SafeGetBool(acc, "IsActive", false))
            {
                activeAccount = &acc;
                break;
            }
        }

        // Fallback to first account if none explicitly marked IsActive
        if (!activeAccount)
        {
            activeAccount = &(*pArr)[0];
        }

        std::string label = SafeGetString(*activeAccount, "Label", "");
        std::string email = SafeGetString(*activeAccount, "Email", "");
        double g5h = SafeGetDouble(*activeAccount, "GFiveH", 0.0);
        double gWeekly = SafeGetDouble(*activeAccount, "GWeekly", 0.0);
        std::string g5hReset = SafeGetString(*activeAccount, "GFiveHReset", "");
        std::string gWeeklyReset = SafeGetString(*activeAccount, "GWeeklyReset", "");
        double c5h = SafeGetDouble(*activeAccount, "CFiveH", 0.0);
        double cWeekly = SafeGetDouble(*activeAccount, "CWeekly", 0.0);
        std::string c5hReset = SafeGetString(*activeAccount, "CFiveHReset", "");
        std::string cWeeklyReset = SafeGetString(*activeAccount, "CWeeklyReset", "");
        bool hasArgon = SafeGetBool(*activeAccount, "HasArgon", false);
        double aWeekly = SafeGetDouble(*activeAccount, "AWeekly", 0.0);
        std::string aWeeklyReset = SafeGetString(*activeAccount, "AWeeklyReset", "");

        // Quota calculations
        // User requested:
        // Generally show 5h limit for Gemini and ChatGPT (Claude), and weekly limit for Argon
        int gPct = (int)std::round(g5h * 100.0);
        if (gPct < 0) gPct = 0; if (gPct > 100) gPct = 100;

        int cPct = (int)std::round(c5h * 100.0);
        if (cPct < 0) cPct = 0; if (cPct > 100) cPct = 100;

        int aPct = (int)std::round(aWeekly * 100.0);
        if (aPct < 0) aPct = 0; if (aPct > 100) aPct = 100;

        int gWeeklyPct = (int)std::round(gWeekly * 100.0);
        if (gWeeklyPct < 0) gWeeklyPct = 0; if (gWeeklyPct > 100) gWeeklyPct = 100;

        int cWeeklyPct = (int)std::round(cWeekly * 100.0);
        if (cWeeklyPct < 0) cWeeklyPct = 0; if (cWeeklyPct > 100) cWeeklyPct = 100;

        std::wstring gReset = m_settings.hideResetTime ? L"" : FormatResetTime(g5hReset, m_settings.shortResetTime);
        std::wstring cReset = m_settings.hideResetTime ? L"" : FormatResetTime(c5hReset, m_settings.shortResetTime);
        std::wstring aReset = m_settings.hideResetTime ? L"" : FormatResetTime(aWeeklyReset, m_settings.shortResetTime);

        std::wstring gWeeklyResetFmt = m_settings.hideResetTime ? L"" : FormatResetTime(gWeeklyReset, m_settings.shortResetTime);
        std::wstring cWeeklyResetFmt = m_settings.hideResetTime ? L"" : FormatResetTime(cWeeklyReset, m_settings.shortResetTime);

        std::wstring pctSuffix = m_hidePercent ? L"" : L"%";

        bool gFromHost = false;
        bool cFromHost = false;
        bool aFromHost = false;
        std::wstring lblG = ResolveHostItemLabel(L"antigravity_gemini", L"G", gFromHost);
        std::wstring lblC = ResolveHostItemLabel(L"antigravity_claude", L"C", cFromHost);
        std::wstring lblA = ResolveHostItemLabel(L"antigravity_argon", L"A", aFromHost);
        std::wstring lblWk = m_settings.hideLabels ? L"" : L"Wk";

        QuotaDisplayEntry eG{ lblG, gPct, std::to_wstring(gPct) + pctSuffix, gReset, false, gFromHost };
        QuotaDisplayEntry eC{ lblC, cPct, std::to_wstring(cPct) + pctSuffix, cReset, false, cFromHost };
        QuotaDisplayEntry eA{ lblA, aPct, std::to_wstring(aPct) + pctSuffix, aReset, false, aFromHost };
        QuotaDisplayEntry eGWk{ lblWk, gWeeklyPct, std::to_wstring(gWeeklyPct) + pctSuffix, gWeeklyResetFmt, true, false };
        QuotaDisplayEntry eCWk{ lblWk, cWeeklyPct, std::to_wstring(cWeeklyPct) + pctSuffix, cWeeklyResetFmt, true, false };

        std::vector<QuotaDisplayEntry> entries;
        bool showG = true;
        bool showC = true;
        bool showA = hasArgon;

        // Always update Smart Mode drop state on fresh snapshots so toggling Smart Mode mid-session uses accurate activity
        if (updateSmartState)
        {
            bool sShowG = false;
            bool sShowC = false;
            bool sShowA = false;
            bool gDrop = false;
            bool cDrop = false;
            bool aDrop = false;

            if (m_hasInitialSnapshot && email == m_prevAccountEmail)
            {
                if (m_prevQuota.g5h >= 0.0 && g5h < m_prevQuota.g5h - 0.0001) gDrop = true;
                if (m_prevQuota.c5h >= 0.0 && c5h < m_prevQuota.c5h - 0.0001) cDrop = true;
                if (hasArgon && m_prevQuota.aWeekly >= 0.0 && aWeekly < m_prevQuota.aWeekly - 0.0001) aDrop = true;
            }

            int dropCount = (gDrop ? 1 : 0) + (cDrop ? 1 : 0) + (aDrop ? 1 : 0);

            if (dropCount == 1)
            {
                if (gDrop) { sShowG = true; m_lastSingleDroppingModel = MODEL_GEMINI; }
                else if (cDrop) { sShowC = true; m_lastSingleDroppingModel = MODEL_CLAUDE; }
                else if (aDrop) { sShowA = true; m_lastSingleDroppingModel = MODEL_ARGON; }
                m_lastDroppingCount = 1;
            }
            else if (dropCount > 1)
            {
                sShowG = gDrop;
                sShowC = cDrop;
                sShowA = aDrop;
                m_lastDroppingCount = dropCount;
            }
            else // dropCount == 0
            {
                if (m_lastDroppingCount == 1)
                {
                    if (m_lastSingleDroppingModel == MODEL_GEMINI) sShowG = true;
                    else if (m_lastSingleDroppingModel == MODEL_CLAUDE) sShowC = true;
                    else if (m_lastSingleDroppingModel == MODEL_ARGON && hasArgon) sShowA = true;
                    else sShowG = true;
                }
                else
                {
                    sShowG = true;
                    m_lastDroppingCount = 0;
                }
            }

            m_lastShowG = sShowG;
            m_lastShowC = sShowC;
            m_lastShowA = sShowA;
        }

        if (!m_settings.smartMode)
        {
            // Standard Mode: show Gemini, Claude, and Argon (if active), paired with weekly quotas unless hidden
            entries.push_back(eG);
            if (!m_settings.hideWeeklyQuota) entries.push_back(eGWk);
            entries.push_back(eC);
            if (!m_settings.hideWeeklyQuota) entries.push_back(eCWk);
            if (hasArgon)
            {
                entries.push_back(eA);
            }
        }
        else
        {
            // Smart Mode (Option C): Dynamic Quota Tracking
            showG = m_lastShowG;
            showC = m_lastShowC;
            showA = m_lastShowA && hasArgon;
            if (!showG && !showC && !showA) showG = true;

            int activeCount = (showG ? 1 : 0) + (showC ? 1 : 0) + (showA ? 1 : 0);
            if (activeCount == 1)
            {
                if (showG)
                {
                    entries.push_back(eG);
                    if (!m_settings.hideWeeklyQuota) entries.push_back(eGWk);
                }
                else if (showC)
                {
                    entries.push_back(eC);
                    if (!m_settings.hideWeeklyQuota) entries.push_back(eCWk);
                }
                else // showA
                {
                    entries.push_back(eA);
                }
            }
            else
            {
                if (showG) entries.push_back(eG);
                if (showC) entries.push_back(eC);
                if (showA) entries.push_back(eA);
            }
        }

        if (updateSmartState)
        {
            m_prevQuota.g5h = g5h;
            m_prevQuota.c5h = c5h;
            m_prevQuota.aWeekly = hasArgon ? aWeekly : -1.0;
            m_prevQuota.valid = true;
            m_prevAccountEmail = email;
            m_hasInitialSnapshot = true;
        }

        auto fmtEntry = [&](const QuotaDisplayEntry& e) -> std::wstring {
            std::wstring lbl = FormatLabelForStyle(e, m_settings.displayStyle);
            std::wstring rst = FormatResetForStyle(e, m_settings.displayStyle);
            if (m_settings.displayStyle == STYLE_CLASSIC)
            {
                std::wstring classicPrefix = lbl;
                if (!classicPrefix.empty() && classicPrefix.back() != L':') classicPrefix += L" ";
                return classicPrefix + e.pctText + (rst.empty() ? L"" : L" " + rst);
            }
            if (m_settings.displayStyle == STYLE_MINIMAL_DOTS)
                return L"\x25CF " + (lbl.empty() ? L"" : lbl + L" ") + e.pctText;
            if (m_settings.displayStyle == STYLE_TYPOGRAPHIC)
                return (lbl.empty() ? L"" : lbl + L" ") + e.pctText + (rst.empty() ? L"" : L" \x00B7 " + rst);
            return (lbl.empty() ? L"" : lbl + L" ") + e.pctText + (rst.empty() ? L"" : L" " + rst);
        };

        auto updateCustomItem = [&](CPluginItem& item, const std::vector<QuotaDisplayEntry>& itemEntries, bool classicArgonNote) {
            std::wstring l1;
            std::wstring l2;
            if (m_settings.displayStyle == STYLE_MINIMAL_DOTS)
            {
                for (size_t i = 0; i < itemEntries.size(); ++i)
                {
                    if (i > 0) l1 += L" | ";
                    l1 += fmtEntry(itemEntries[i]);
                }
            }
            else if (itemEntries.size() == 1)
            {
                l1 = fmtEntry(itemEntries[0]);
                if (classicArgonNote && m_settings.displayStyle == STYLE_CLASSIC && hasArgon && m_settings.smartMode && m_lastShowA && !m_lastShowG && !m_lastShowC && !m_settings.hideWeeklyQuota)
                    l2 = L"Argon Weekly";
            }
            else if (!itemEntries.empty())
            {
                size_t numCols = (itemEntries.size() + 1) / 2;
                for (size_t c = 0; c < numCols; ++c)
                {
                    if (!l1.empty()) l1 += L"  ";
                    l1 += fmtEntry(itemEntries[c * 2]);
                    if (c * 2 + 1 < itemEntries.size())
                    {
                        if (!l2.empty()) l2 += L"  ";
                        l2 += fmtEntry(itemEntries[c * 2 + 1]);
                    }
                }
            }
            item.SetRenderState(itemEntries, m_settings);
            item.UpdateLabel(l1.c_str());
            item.UpdateValue(l2.c_str());
        };

        // Item 0: Combined Antigravity Quota
        updateCustomItem(m_itemCombined, entries, true);

        // Item 1: Split Gemini item (5h + optional Wk, hidden when inactive in Smart Mode)
        std::vector<QuotaDisplayEntry> geminiEntries;
        if (!m_settings.smartMode || showG)
        {
            geminiEntries.push_back(eG);
            if (!m_settings.hideWeeklyQuota) geminiEntries.push_back(eGWk);
        }
        updateCustomItem(m_itemGemini, geminiEntries, false);

        // Item 2: Split Claude item (5h + optional Wk, hidden when inactive in Smart Mode)
        std::vector<QuotaDisplayEntry> claudeEntries;
        if (!m_settings.smartMode || showC)
        {
            claudeEntries.push_back(eC);
            if (!m_settings.hideWeeklyQuota) claudeEntries.push_back(eCWk);
        }
        updateCustomItem(m_itemClaude, claudeEntries, false);

        // Item 4: Split Argon item (hidden when inactive in Smart Mode)
        std::vector<QuotaDisplayEntry> argonEntries;
        if (!m_settings.smartMode)
        {
            argonEntries.push_back(hasArgon ? eA : QuotaDisplayEntry{ lblA, 0, L"--", L"", false, aFromHost });
        }
        else if (showA && hasArgon)
        {
            argonEntries.push_back(eA);
        }
        updateCustomItem(m_itemArgon, argonEntries, false);

        // Item 3: Account
        std::wstring wLabel = Utf8ToWide(label);
        std::wstring wEmail = Utf8ToWide(email);
        if (wLabel.empty())
        {
            size_t atPos = wEmail.find(L'@');
            wLabel = (atPos != std::wstring::npos) ? wEmail.substr(0, atPos) : wEmail;
            if (wLabel.empty()) wLabel = L"Default";
        }
        m_itemAccount.UpdateValue(wLabel.c_str());

        // Tooltip summary formatting: ensure reset times are present for all tiers
        int c5hPct = (int)std::round(c5h * 100.0);
        if (c5hPct < 0) c5hPct = 0; if (c5hPct > 100) c5hPct = 100;

        std::wstring wG5hReset = Utf8ToWide(g5hReset);
        std::wstring wGWeeklyReset = Utf8ToWide(gWeeklyReset);
        std::wstring wC5hReset = Utf8ToWide(c5hReset);
        std::wstring wCWeeklyReset = Utf8ToWide(cWeeklyReset);

        std::wstring tooltip = L"Default Account: " + wLabel;
        if (!wEmail.empty())
        {
            tooltip += L" (" + wEmail + L")";
        }
        tooltip += L"\r\n";

        tooltip += L"Gemini 5h: " + std::to_wstring(gPct) + L"%";
        if (!wG5hReset.empty() && wG5hReset != L"--")
        {
            tooltip += L" (Reset: " + wG5hReset + L")";
        }
        tooltip += L" | Weekly: " + std::to_wstring(gWeeklyPct) + L"%";
        if (!wGWeeklyReset.empty() && wGWeeklyReset != L"--")
        {
            tooltip += L" (Reset: " + wGWeeklyReset + L")";
        }
        tooltip += L"\r\n";

        tooltip += L"Claude 5h: " + std::to_wstring(c5hPct) + L"%";
        if (!wC5hReset.empty() && wC5hReset != L"--")
        {
            tooltip += L" (Reset: " + wC5hReset + L")";
        }
        tooltip += L" | Weekly: " + std::to_wstring(cWeeklyPct) + L"%";
        if (!wCWeeklyReset.empty() && wCWeeklyReset != L"--")
        {
            tooltip += L" (Reset: " + wCWeeklyReset + L")";
        }

        if (hasArgon)
        {
            int aWeeklyPct = (int)std::round(aWeekly * 100.0);
            if (aWeeklyPct < 0) aWeeklyPct = 0; if (aWeeklyPct > 100) aWeeklyPct = 100;
            tooltip += L"\r\nArgon Weekly: " + std::to_wstring(aWeeklyPct) + L"%";
            std::wstring wAWeeklyReset = Utf8ToWide(aWeeklyReset);
            if (!wAWeeklyReset.empty() && wAWeeklyReset != L"--")
            {
                tooltip += L" (Reset: " + wAWeeklyReset + L")";
            }
        }

        if (SafeGetBool(*activeAccount, "AuthFailed", false))
        {
            tooltip += L"\r\n[!] Token expired - right-click -> Sign In with Google...";
        }

        UpdateTooltip(tooltip.c_str());
        m_hasLoadedData = true;
    }
    catch (...)
    {
        // Ignore parsing errors and keep previous data intact
    }
}

void CAntigravityPlugin::DataRequired()
{
    std::string jsonStr;
    bool updated = false;
    {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        bool cfgChanged = HasHostConfigChanged();
        if (ReadAndDecryptVault(jsonStr))
        {
            ParseAndUpdate(jsonStr);
            updated = true;
        }
        else if (cfgChanged && !m_lastJsonStr.empty())
        {
            ParseAndUpdate(m_lastJsonStr, false);
            updated = true;
        }
    }
    if (updated)
    {
        RefreshHostWindows();
    }
}

void CAntigravityPlugin::EnsureBackgroundWorkerStarted()
{
    if (m_workerThread != nullptr) return;
    m_stopWorker.store(false, std::memory_order_release);
    if (!m_wakeEvent)
    {
        m_wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }
    m_workerThread = CreateThread(nullptr, 0, WorkerThreadProc, this, 0, nullptr);
}

void CAntigravityPlugin::StopBackgroundWorker()
{
    m_stopWorker.store(true, std::memory_order_release);
    if (m_wakeEvent)
    {
        SetEvent(m_wakeEvent);
    }
    if (m_workerThread)
    {
        WaitForSingleObject(m_workerThread, 1500);
        CloseHandle(m_workerThread);
        m_workerThread = nullptr;
    }
    if (m_wakeEvent)
    {
        CloseHandle(m_wakeEvent);
        m_wakeEvent = nullptr;
    }
}

void CAntigravityPlugin::RequestPollNow(bool interactiveLogin)
{
    if (interactiveLogin)
    {
        m_loginRequested.store(true, std::memory_order_release);
    }
    else
    {
        m_pollNowRequested.store(true, std::memory_order_release);
    }
    if (m_wakeEvent)
    {
        SetEvent(m_wakeEvent);
    }
}

DWORD WINAPI CAntigravityPlugin::WorkerThreadProc(LPVOID lpParam)
{
    CAntigravityPlugin* self = static_cast<CAntigravityPlugin*>(lpParam);
    if (self)
    {
        self->BackgroundWorkerLoop();
    }
    return 0;
}

void CAntigravityPlugin::BackgroundWorkerLoop()
{
    const DWORD kPollIntervalMs = 5 * 60 * 1000; // 5 minutes

    // Initial poll right after initialization
    m_pollNowRequested.store(true, std::memory_order_release);

    while (!m_stopWorker.load(std::memory_order_acquire))
    {
        bool doLogin = m_loginRequested.exchange(false, std::memory_order_acq_rel);
        bool doPoll = m_pollNowRequested.exchange(false, std::memory_order_acq_rel);

        if (doLogin)
        {
            std::string updatedJson;
            std::string errMsg;
            if (QuotaSync::SignInWithBrowser(updatedJson, errMsg, &m_stopWorker))
            {
                std::lock_guard<std::mutex> lock(m_dataMutex);
                UpdateVaultFileCacheMeta(ResolveVaultPath());
                ParseAndUpdate(updatedJson, true);
                RefreshHostWindows();
            }
            else if (!m_stopWorker.load(std::memory_order_acquire) && !errMsg.empty())
            {
                std::wstring msg = L"Antigravity Quota\r\n" + Utf8ToWide(errMsg);
                UpdateTooltip(msg.c_str());
            }
        }
        else if (doPoll)
        {
            std::string updatedJson;
            if (QuotaSync::PollActiveAccountAndUpdateVault(updatedJson, &m_stopWorker))
            {
                std::lock_guard<std::mutex> lock(m_dataMutex);
                UpdateVaultFileCacheMeta(ResolveVaultPath());
                ParseAndUpdate(updatedJson, true);
                RefreshHostWindows();
            }
        }

        if (m_stopWorker.load(std::memory_order_acquire)) break;

        DWORD waitRes = WaitForSingleObject(m_wakeEvent, kPollIntervalMs);
        if (m_stopWorker.load(std::memory_order_acquire)) break;
        if (waitRes == WAIT_TIMEOUT)
        {
            m_pollNowRequested.store(true, std::memory_order_release);
        }
    }
}

// Global static instance and DLL exported function
static CAntigravityPlugin g_plugin;

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance()
{
    return &g_plugin;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
