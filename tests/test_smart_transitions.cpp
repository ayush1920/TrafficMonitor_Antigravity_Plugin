#include <iostream>
#include <cassert>
#include <string>
#include <windows.h>
#include "AntigravityPlugin.h"

std::string MakeJson(double g5h, double c5h, double aWeekly, const std::string& email = "test@example.com")
{
    std::string s = "[{\"Label\":\"Test\",\"Email\":\"" + email + "\",\"IsActive\":true,"
                    "\"GFiveH\":" + std::to_string(g5h) + ",\"GWeekly\":1.0,\"GFiveHReset\":\"2026-11-22T05:00:00Z\",\"GWeeklyReset\":\"2026-11-28T05:00:00Z\","
                    "\"CFiveH\":" + std::to_string(c5h) + ",\"CWeekly\":1.0,\"CFiveHReset\":\"2026-11-22T05:00:00Z\",\"CWeeklyReset\":\"2026-11-28T05:00:00Z\","
                    "\"HasArgon\":true,\"AWeekly\":" + std::to_string(aWeekly) + ",\"AWeeklyReset\":\"2026-11-28T05:00:00Z\"}]";
    return s;
}

int main()
{
    std::cout << "Running Smart Mode state transition tests..." << std::endl;

    CAntigravityPlugin plugin;
    PluginSettings s;
    s.smartMode = true;
    s.hideResetTime = true; // Use percentages only for clear string checks
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

    std::cout << "\nALL 9 TRANSITION TESTS PASSED PERFECTLY!" << std::endl;
    return 0;
}
