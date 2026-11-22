#include <iostream>
#include <cassert>
#include <string>
#include <windows.h>
#include "AntigravityPlugin.h"
#include "QuotaSync.h"

std::string MakeJson(double g5h, double c5h, double aWeekly, const std::string& email = "test@example.com")
{
    std::string s = "[{\"Label\":\"Test\",\"Email\":\"" + email + "\",\"IsActive\":true,"
                    "\"GFiveH\":" + std::to_string(g5h) + ",\"GWeekly\":1.0,\"GFiveHReset\":\"4h 25m\",\"GWeeklyReset\":\"6d 23h\","
                    "\"CFiveH\":" + std::to_string(c5h) + ",\"CWeekly\":1.0,\"CFiveHReset\":\"4h 59m\",\"CWeeklyReset\":\"6d 10h\","
                    "\"HasArgon\":true,\"AWeekly\":" + std::to_string(aWeekly) + ",\"AWeeklyReset\":\"6d 23h\"}]";
    return s;
}

int main()
{
    std::cout << "Running Smart Mode state transition tests..." << std::endl;

    CAntigravityPlugin plugin;
    PluginSettings originalSettings = plugin.GetSettings();
    PluginSettings s;
    s.smartMode = true;
    s.hideResetTime = true; // Use percentages only for clear string checks
    s.displayStyle = STYLE_CLASSIC;
    s.hideWeeklyQuota = false;
    plugin.ApplySettings(s);

    IPluginItem* item0 = plugin.GetItem(0);

    // 1. Initial startup / first snapshot (all at 100%)
    plugin.ParseAndUpdate(MakeJson(1.0, 1.0, 1.0));
    std::wstring l1 = item0->GetItemLableText();
    std::wstring l2 = item0->GetItemValueText();
    std::wcout << L"Startup: Line 1 = " << l1 << L", Line 2 = " << l2 << std::endl;
    assert(l1.find(L"G:100%") != std::wstring::npos);
    assert(l2.find(L"Wk:100%") != std::wstring::npos);

    // 2. Claude drops (1.0 -> 0.85)
    plugin.ParseAndUpdate(MakeJson(1.0, 0.85, 1.0));
    l1 = item0->GetItemLableText();
    l2 = item0->GetItemValueText();
    std::wcout << L"Claude drops: Line 1 = " << l1 << L", Line 2 = " << l2 << std::endl;
    assert(l1.find(L"C:85%") != std::wstring::npos);
    assert(l2.find(L"Wk:100%") != std::wstring::npos);

    // 3. Claude idle (remains at 0.85)
    plugin.ParseAndUpdate(MakeJson(1.0, 0.85, 1.0));
    l1 = item0->GetItemLableText();
    l2 = item0->GetItemValueText();
    std::wcout << L"Claude idle: Line 1 = " << l1 << L", Line 2 = " << l2 << std::endl;
    assert(l1.find(L"C:85%") != std::wstring::npos);
    assert(l2.find(L"Wk:100%") != std::wstring::npos);

    // 4. Gemini & Claude drop together (Gemini 1.0 -> 0.90, Claude 0.85 -> 0.80)
    plugin.ParseAndUpdate(MakeJson(0.90, 0.80, 1.0));
    l1 = item0->GetItemLableText();
    l2 = item0->GetItemValueText();
    std::wcout << L"Gemini & Claude drop: Line 1 = " << l1 << L", Line 2 = " << l2 << std::endl;
    assert(l1.find(L"G:90%") != std::wstring::npos);
    assert(l2.find(L"C:80%") != std::wstring::npos);

    // 5. Idle after multi-drop (both remain 0.90 and 0.80) -> must fallback to Gemini
    plugin.ParseAndUpdate(MakeJson(0.90, 0.80, 1.0));
    l1 = item0->GetItemLableText();
    l2 = item0->GetItemValueText();
    std::wcout << L"Idle after multi-drop: Line 1 = " << l1 << L", Line 2 = " << l2 << std::endl;
    assert(l1.find(L"G:90%") != std::wstring::npos);
    assert(l2.find(L"Wk:100%") != std::wstring::npos);

    // 6. Argon drops (1.0 -> 0.70)
    plugin.ParseAndUpdate(MakeJson(0.90, 0.80, 0.70));
    l1 = item0->GetItemLableText();
    l2 = item0->GetItemValueText();
    std::wcout << L"Argon drops: Line 1 = " << l1 << L", Line 2 = " << l2 << std::endl;
    assert(l1.find(L"A:70%") != std::wstring::npos);

    // 7. Argon idle (remains at 0.70)
    plugin.ParseAndUpdate(MakeJson(0.90, 0.80, 0.70));
    l1 = item0->GetItemLableText();
    l2 = item0->GetItemValueText();
    std::wcout << L"Argon idle: Line 1 = " << l1 << L", Line 2 = " << l2 << std::endl;
    assert(l1.find(L"A:70%") != std::wstring::npos);

    // 8. All three drop (G 0.90 -> 0.85, C 0.80 -> 0.75, A 0.70 -> 0.65)
    plugin.ParseAndUpdate(MakeJson(0.85, 0.75, 0.65));
    l1 = item0->GetItemLableText();
    l2 = item0->GetItemValueText();
    std::wcout << L"All three drop: Line 1 = " << l1 << L", Line 2 = " << l2 << std::endl;
    assert(l1.find(L"G:85%") != std::wstring::npos);
    assert(l1.find(L"A:65%") != std::wstring::npos);
    assert(l2.find(L"C:75%") != std::wstring::npos);

    // 9. All three idle (all unchanged) -> must fallback to Gemini
    plugin.ParseAndUpdate(MakeJson(0.85, 0.75, 0.65));
    l1 = item0->GetItemLableText();
    l2 = item0->GetItemValueText();
    std::wcout << L"Idle after 3-model drop: Line 1 = " << l1 << L", Line 2 = " << l2 << std::endl;
    assert(l1.find(L"G:85%") != std::wstring::npos);
    assert(l2.find(L"Wk:100%") != std::wstring::npos);

    // 10. Test Host TrafficMonitor controlling % factor via OnExtenedInfo
    plugin.OnExtenedInfo(ITMPlugin::EI_TASKBAR_WND_NOT_SHOW_PERCENT, L"1");
    l1 = item0->GetItemLableText();
    l2 = item0->GetItemValueText();
    std::wcout << L"Host hides %: Line 1 = " << l1 << L", Line 2 = " << l2 << std::endl;
    assert(l1.find(L"%") == std::wstring::npos);
    assert(l2.find(L"%") == std::wstring::npos);
    assert(l1.find(L"G:85") != std::wstring::npos);
    assert(l2.find(L"Wk:100") != std::wstring::npos);

    plugin.OnExtenedInfo(ITMPlugin::EI_TASKBAR_WND_NOT_SHOW_PERCENT, L"0");
    l1 = item0->GetItemLableText();
    l2 = item0->GetItemValueText();
    std::wcout << L"Host shows %: Line 1 = " << l1 << L", Line 2 = " << l2 << std::endl;
    assert(l1.find(L"%") != std::wstring::npos);
    assert(l2.find(L"%") != std::wstring::npos);
    assert(l1.find(L"G:85%") != std::wstring::npos);
    assert(l2.find(L"Wk:100%") != std::wstring::npos);

    // 11. Test Host toggling % during active multi-model drop does NOT collapse Smart Mode state
    plugin.ParseAndUpdate(MakeJson(0.80, 0.70, 0.65)); // G and C drop
    plugin.OnExtenedInfo(ITMPlugin::EI_TASKBAR_WND_NOT_SHOW_PERCENT, L"1");
    l1 = item0->GetItemLableText();
    l2 = item0->GetItemValueText();
    std::wcout << L"Multi-drop + host hides %: Line 1 = " << l1 << L", Line 2 = " << l2 << std::endl;
    assert(l1 == L"G:80");
    assert(l2 == L"C:70");

    // 12. Test Hide Weekly Quota ('Wk') checkbox in Smart Mode single-model view
    plugin.OnExtenedInfo(ITMPlugin::EI_TASKBAR_WND_NOT_SHOW_PERCENT, L"0");
    plugin.ParseAndUpdate(MakeJson(0.80, 0.70, 0.65)); // Idle after multi-drop -> Gemini single
    assert(std::wstring(item0->GetItemValueText()).find(L"Wk:100%") != std::wstring::npos);
    s.hideWeeklyQuota = true;
    plugin.ApplySettings(s);
    l1 = item0->GetItemLableText();
    l2 = item0->GetItemValueText();
    std::wcout << L"Hide Weekly Quota ON: Line 1 = " << l1 << L", Line 2 = '" << l2 << L"'" << std::endl;
    assert(l1 == L"G:80%");
    assert(l2.empty());

    // 13. Test Typographic Hierarchy style with reset time
    s.hideWeeklyQuota = false;
    s.hideResetTime = false;
    s.shortResetTime = true;
    s.displayStyle = STYLE_TYPOGRAPHIC;
    plugin.ApplySettings(s);
    l1 = item0->GetItemLableText();
    l2 = item0->GetItemValueText();
    std::wcout << L"Typographic Hierarchy: Line 1 = " << l1 << L", Line 2 = " << l2 << std::endl;
    assert(l1.find(L":") == std::wstring::npos);
    assert(l1.find(L"(") == std::wstring::npos);
    assert(l1.find(L"G 80%") != std::wstring::npos);
    assert(l1.find(L"\x00B7") != std::wstring::npos);

    // 14. Test Minimal Status Dots style during multi-model drop
    s.displayStyle = STYLE_MINIMAL_DOTS;
    plugin.ApplySettings(s);
    plugin.ParseAndUpdate(MakeJson(0.75, 0.65, 0.65)); // G and C drop
    l1 = item0->GetItemLableText();
    l2 = item0->GetItemValueText();
    std::wcout << L"Minimal Dots: Line 1 = " << l1 << L", Line 2 = '" << l2 << L"'" << std::endl;
    assert(l1.find(L"G 75%") != std::wstring::npos);
    assert(l1.find(L"|") != std::wstring::npos);
    assert(l1.find(L"C 65%") != std::wstring::npos);
    assert(l2.empty());

    // 15. Test GDI / GDI+ rendering across all 5 Visual Styles on offscreen DC (1, 2, 3 entries)
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, 400, 60);
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);
    HFONT hFont = CreateFontW(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT hOldFont = (HFONT)SelectObject(hdcMem, hFont);

    DisplayStyle styles[] = { STYLE_CLASSIC, STYLE_TYPOGRAPHIC, STYLE_PROGRESS_BAR, STYLE_MINIMAL_DOTS, STYLE_PILL_BADGES };
    for (DisplayStyle st : styles)
    {
        s.displayStyle = st;
        s.smartMode = false; // Tests 3-entry 2+1 layout
        plugin.ApplySettings(s);
        int w3 = item0->GetItemWidthEx(hdcMem);
        assert(w3 > 30);
        SetTextColor(hdcMem, RGB(255, 255, 255));
        item0->DrawItem(hdcMem, 0, 0, w3, 40, true);
        SetTextColor(hdcMem, RGB(0, 0, 0));
        item0->DrawItem(hdcMem, 0, 0, w3, 40, false);

        s.smartMode = true;
        s.hideWeeklyQuota = true; // Tests 1-entry centered layout
        plugin.ApplySettings(s);
        int w1 = item0->GetItemWidthEx(hdcMem);
        assert(w1 > 20);
        item0->DrawItem(hdcMem, 0, 0, w1, 40, true);

        s.hideLabels = true;
        plugin.ApplySettings(s);
        int wNoLbl = item0->GetItemWidthEx(hdcMem);
        assert(wNoLbl > 15 && wNoLbl < w1);
        item0->DrawItem(hdcMem, 0, 0, wNoLbl, 40, true);
        s.hideLabels = false;
    }

    // 15b. Test threshold color boundaries (<= 20% red, <= 60% yellow, > 60% green)
    {
        s.displayStyle = STYLE_MINIMAL_DOTS;
        s.smartMode = true;
        s.hideWeeklyQuota = true;
        plugin.ApplySettings(s);

        auto renderDotSample = [&](double frac, bool dark) -> COLORREF {
            plugin.ParseAndUpdate(MakeJson(frac, 1.0, 1.0));
            RECT clearRc = { 0, 0, 200, 40 };
            FillRect(hdcMem, &clearRc, (HBRUSH)GetStockObject(dark ? BLACK_BRUSH : WHITE_BRUSH));
            SetTextColor(hdcMem, dark ? RGB(255, 255, 255) : RGB(0, 0, 0));
            item0->DrawItem(hdcMem, 0, 0, 200, 40, dark);
            // Isolate the colored status dot by maximum chroma (maxRGB - minRGB) in leftmost column
            COLORREF best = 0;
            int bestChroma = -1;
            for (int px = 0; px < 12; ++px)
            {
                for (int py = 10; py < 30; ++py)
                {
                    COLORREF c = GetPixel(hdcMem, px, py);
                    int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
                    int hi = (r > g ? (r > b ? r : b) : (g > b ? g : b));
                    int lo = (r < g ? (r < b ? r : b) : (g < b ? g : b));
                    int chroma = hi - lo;
                    if (chroma > bestChroma) { bestChroma = chroma; best = c; }
                }
            }
            return best;
        };

        for (bool dark : { true, false })
        {
            COLORREF c61 = renderDotSample(0.61, dark); // Green (> 60%)
            COLORREF c60 = renderDotSample(0.60, dark); // Yellow (<= 60%)
            COLORREF c21 = renderDotSample(0.21, dark); // Yellow (> 20%)
            COLORREF c20 = renderDotSample(0.20, dark); // Red (<= 20%)
            COLORREF c00 = renderDotSample(0.00, dark); // Red (0%)

            assert(GetGValue(c61) > GetRValue(c61) + 50); // Green dominant
            assert(GetRValue(c60) > 180 && GetGValue(c60) > 100 && GetGValue(c60) < GetRValue(c60) && GetBValue(c60) < 80); // Yellow/Amber
            assert(GetRValue(c21) > 180 && GetGValue(c21) > 100 && GetGValue(c21) < GetRValue(c21) && GetBValue(c21) < 80); // Yellow/Amber
            assert(GetRValue(c20) > 180 && GetGValue(c20) < 130 && GetBValue(c20) < 130); // Red dominant
            assert(GetRValue(c00) > 180 && GetGValue(c00) < 130 && GetBValue(c00) < 130); // Red dominant
        }
        std::cout << "Threshold color boundary tests (<=20% red, <=60% yellow, >60% green) passed!" << std::endl;
    }

    // 15c. Test host Display Text Settings verbatim across all 5 Visual Styles
    {
        wchar_t tmpDir[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tmpDir);
        std::wstring mockCfg = std::wstring(tmpDir) + L"tm_host_labels_test.ini";
        WritePrivateProfileStringW(L"plugin_display_str_taskbar_window", L"antigravity_gemini", L"G:", mockCfg.c_str());
        WritePrivateProfileStringW(L"plugin_display_str_taskbar_window", L"antigravity_claude", L"Cl", mockCfg.c_str());

        plugin.SetHostConfigPathOverride(mockCfg);
        s.hideLabels = false;
        s.smartMode = false;
        s.hideResetTime = true;
        s.hideWeeklyQuota = false;

        DisplayStyle allStyles[] = {
            STYLE_CLASSIC, STYLE_TYPOGRAPHIC, STYLE_PROGRESS_BAR, STYLE_MINIMAL_DOTS, STYLE_PILL_BADGES
        };
        for (DisplayStyle st : allStyles)
        {
            s.displayStyle = st;
            plugin.ApplySettings(s);
            plugin.ParseAndUpdate(MakeJson(0.85, 0.75, 0.90), false);
            std::wstring hl1 = item0->GetItemLableText();
            std::wstring hl2 = item0->GetItemValueText();
            assert(hl1.find(L"G:") != std::wstring::npos);
            assert(hl1.find(L"Cl 75%") != std::wstring::npos);
            if (st != STYLE_MINIMAL_DOTS)
            {
                assert(hl2.find(L"100%") != std::wstring::npos);
            }
        }

        // Empty host label clears the label completely in all styles
        WritePrivateProfileStringW(L"plugin_display_str_taskbar_window", L"antigravity_gemini", L"", mockCfg.c_str());
        for (DisplayStyle st : allStyles)
        {
            s.displayStyle = st;
            plugin.ApplySettings(s);
            plugin.ParseAndUpdate(MakeJson(0.85, 0.75, 0.90), false);
            std::wstring hl1 = item0->GetItemLableText();
            assert(hl1.find(L"G:") == std::wstring::npos);
            assert(hl1.find(L"85%") != std::wstring::npos);
        }

        plugin.SetHostConfigPathOverride(L"");
        DeleteFileW(mockCfg.c_str());
        std::cout << "Host Display Text Settings verbatim label tests (all 5 Visual Styles) passed!" << std::endl;
    }

    // 15d. Single-row paired taskbar slot (h < minDoubleRowH) reports full side-by-side width when HideWeeklyQuota=false
    {
        s.displayStyle = STYLE_PROGRESS_BAR;
        s.smartMode = true;
        s.hideWeeklyQuota = false;
        s.hideResetTime = false;
        s.shortResetTime = true;
        s.hideLabels = false;
        plugin.ApplySettings(s);
        plugin.ParseAndUpdate(MakeJson(0.42, 1.0, 1.0), false);

        // Two-row stacked height (h = 40) -> compact 1-column width
        SetTextColor(hdcMem, RGB(255, 255, 255));
        item0->DrawItem(hdcMem, 0, 0, 200, 40, true);
        int stackedW = item0->GetItemWidthEx(hdcMem);

        // Single-row paired height (h = 20, e.g. paired beneath another item in TrafficMonitor) -> side-by-side width
        item0->DrawItem(hdcMem, 0, 0, 200, 20, true);
        int sideBySideW = item0->GetItemWidthEx(hdcMem);
        assert(sideBySideW > stackedW + 40);

        // Render side-by-side into allocated sideBySideW and verify 'Wk' column pixels are actually drawn past stackedW
        RECT clearRc = { 0, 0, 400, 30 };
        FillRect(hdcMem, &clearRc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        item0->DrawItem(hdcMem, 0, 0, sideBySideW, 20, true);
        bool drewRightHalf = false;
        for (int px = stackedW + 4; px < sideBySideW - 4 && !drewRightHalf; ++px)
        {
            for (int py = 0; py < 20; ++py)
            {
                if (GetPixel(hdcMem, px, py) != RGB(0, 0, 0)) { drewRightHalf = true; break; }
            }
        }
        assert(drewRightHalf);

        // Restore stacked height state
        item0->DrawItem(hdcMem, 0, 0, stackedW, 40, true);
        assert(item0->GetItemWidthEx(hdcMem) == stackedW);
        std::cout << "Single-line side-by-side weekly quota width tests passed (stacked="
                  << stackedW << "px, singleRow=" << sideBySideW << "px)!" << std::endl;
    }

    // 15e. Standard Mode (smartMode=false) respects hideWeeklyQuota=false by showing Wk for G & C
    {
        s.displayStyle = STYLE_CLASSIC;
        s.smartMode = false;
        s.hideWeeklyQuota = false;
        s.hideResetTime = true;
        s.hideLabels = false;
        plugin.ApplySettings(s);
        plugin.ParseAndUpdate(MakeJson(0.85, 0.75, 0.90), false);

        std::wstring sl1 = item0->GetItemLableText();
        std::wstring sl2 = item0->GetItemValueText();
        assert(sl1.find(L"G:85%") != std::wstring::npos);
        assert(sl1.find(L"C:75%") != std::wstring::npos);
        assert(sl1.find(L"A:90%") != std::wstring::npos);
        assert(sl2.find(L"Wk:100%") != std::wstring::npos);
        assert(sl2.find(L"Wk:100%", sl2.find(L"Wk:100%") + 1) != std::wstring::npos); // Both G Wk and C Wk

        item0->DrawItem(hdcMem, 0, 0, 400, 40, true);
        int wStdAll = item0->GetItemWidthEx(hdcMem);

        s.hideWeeklyQuota = true;
        plugin.ApplySettings(s);
        std::wstring sl1NoWk = item0->GetItemLableText();
        std::wstring sl2NoWk = item0->GetItemValueText();
        assert(sl2NoWk.find(L"Wk") == std::wstring::npos);
        assert(sl2NoWk.find(L"C:75%") != std::wstring::npos);
        int wStdNoWk = item0->GetItemWidthEx(hdcMem);
        assert(wStdAll > wStdNoWk);
        std::cout << "Standard Mode weekly quota visibility tests (withWk=" << wStdAll
                  << "px, noWk=" << wStdNoWk << "px) passed!" << std::endl;
    }

    SelectObject(hdcMem, hOldFont);
    DeleteObject(hFont);
    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

    plugin.ApplySettings(originalSettings);

    // 16. Test QuotaSync Cloud Code JSON parsing & reset timer formatting
    long long tNow = QuotaSync::ParseIsoUtcEpoch("2026-11-22T00:00:00Z");
    long long t5h = QuotaSync::ParseIsoUtcEpoch("2026-11-22T04:59:10Z");
    long long t7d = QuotaSync::ParseIsoUtcEpoch("2026-11-28T23:05:00Z");
    assert(tNow > 0 && t5h > tNow && t7d > t5h);
    assert(QuotaSync::FormatResetDuration(t5h, tNow) == "4h 59m");
    assert(QuotaSync::FormatResetDuration(t7d, tNow) == "6d 23h");
    assert(QuotaSync::FormatResetDuration(tNow - 10, tNow) == "Ready");

    std::string mockSummary =
        "{\"response\":{\"groups\":["
        "{\"displayName\":\"Gemini\",\"buckets\":["
        "{\"window\":\"5h\",\"remainingFraction\":0.76,\"resetTime\":\"2026-11-22T04:59:10Z\"},"
        "{\"window\":\"weekly\",\"remainingFraction\":0.93,\"resetTime\":\"2026-11-28T23:05:00Z\"}"
        "]},"
        "{\"displayName\":\"Claude & GPT\",\"buckets\":["
        "{\"window\":\"5h\",\"remainingFraction\":1.0,\"resetTime\":\"2026-11-22T04:59:10Z\"},"
        "{\"window\":\"7d\",\"remainingFraction\":0.50,\"resetTime\":\"2026-11-28T23:05:00Z\"}"
        "]},"
        "{\"displayName\":\"Gemini 4 Argon\",\"buckets\":["
        "{\"window\":\"weekly\",\"remainingFraction\":0.94,\"resetTime\":\"2026-11-28T23:05:00Z\"}"
        "]}]}}";

    double g5 = 0, gw = 0, c5 = 0, cw = 0, aw = 0;
    std::string g5r, gwr, c5r, cwr, awr;
    bool hasA = false;
    bool ok = QuotaSync::ApplyQuotaSummaryJson(mockSummary, g5, g5r, gw, gwr, c5, c5r, cw, cwr, hasA, aw, awr, tNow);
    assert(ok);
    assert(hasA);
    assert(g5 > 0.75 && g5 < 0.77 && g5r == "4h 59m");
    assert(gw > 0.92 && gw < 0.94 && gwr == "6d 23h");
    assert(c5 > 0.99 && cw > 0.49 && cw < 0.51);
    assert(aw > 0.93 && aw < 0.95 && awr == "6d 23h");
    std::cout << "QuotaSync parsing & formatting tests passed!" << std::endl;

    std::cout << "\nALL 16 STYLE, POLISH, TRANSITION, PERCENT AND QUOTASYNC TESTS PASSED PERFECTLY!" << std::endl;
    return 0;
}
