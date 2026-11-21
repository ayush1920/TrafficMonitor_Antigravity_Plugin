#include <iostream>
#include <cassert>
#include <string>
#include <windows.h>
#include "AntigravityPlugin.h"

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
    }

    SelectObject(hdcMem, hOldFont);
    DeleteObject(hFont);
    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

    plugin.ApplySettings(originalSettings);

    std::cout << "\nALL 15 STYLE, POLISH, TRANSITION AND PERCENT TESTS PASSED PERFECTLY!" << std::endl;
    return 0;
}
