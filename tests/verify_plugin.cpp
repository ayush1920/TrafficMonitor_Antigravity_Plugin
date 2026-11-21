#include <windows.h>
#include <iostream>
#include "PluginInterface.h"

int main(int argc, char* argv[])
{
    const char* dllPath = (argc > 1) ? argv[1] : "AntigravityPlugin.dll";
    std::cout << "Loading DLL: " << dllPath << "..." << std::endl;

    HMODULE hMod = LoadLibraryA(dllPath);
    if (!hMod)
    {
        std::cerr << "Failed to load DLL! Error: " << GetLastError() << std::endl;
        return 1;
    }

    typedef ITMPlugin* (*PFN_TMPluginGetInstance)();
    PFN_TMPluginGetInstance pfnGetInstance = (PFN_TMPluginGetInstance)GetProcAddress(hMod, "TMPluginGetInstance");
    if (!pfnGetInstance)
    {
        std::cerr << "Failed to find TMPluginGetInstance export!" << std::endl;
        FreeLibrary(hMod);
        return 2;
    }

    ITMPlugin* plugin = pfnGetInstance();
    if (!plugin)
    {
        std::cerr << "TMPluginGetInstance returned nullptr!" << std::endl;
        FreeLibrary(hMod);
        return 3;
    }

    std::cout << "Plugin Name: " << plugin->GetInfo(ITMPlugin::TMI_NAME) << std::endl;
    std::cout << "API Version: " << plugin->GetAPIVersion() << std::endl;

    plugin->DataRequired();

    IPluginItem* item0 = plugin->GetItem(0);
    if (!item0)
    {
        std::cerr << "Item 0 is null!" << std::endl;
        FreeLibrary(hMod);
        return 4;
    }

    std::wcout << L"Item 0 ID: " << item0->GetItemId() << std::endl;
    std::wcout << L"Item 0 IsCustomDraw: " << (item0->IsCustomDraw() ? L"true" : L"false") << std::endl;
    std::wcout << L"Item 0 Label (Line 1): " << item0->GetItemLableText() << std::endl;
    std::wcout << L"Item 0 Value (Line 2): " << item0->GetItemValueText() << std::endl;

    std::cout << "\nTesting Commands & Options..." << std::endl;
    std::cout << "Command Count: " << plugin->GetCommandCount() << std::endl;
    for (int i = 0; i < plugin->GetCommandCount(); ++i)
    {
        std::wcout << L"  Command " << i << L": " << plugin->GetCommandName(i)
                   << L" [Checked: " << plugin->IsCommandChecked(i) << L"]" << std::endl;
    }

    bool origSmart = (plugin->IsCommandChecked(1) != 0);
    bool origHideWk = (plugin->IsCommandChecked(2) != 0);
    bool origHideRst = (plugin->IsCommandChecked(3) != 0);
    bool origShortRst = (plugin->IsCommandChecked(4) != 0);

    // Toggle Command 1 (Smart Mode)
    std::cout << "\nToggling Command 1 (Smart Mode)..." << std::endl;
    plugin->OnPluginCommand(1, nullptr, nullptr);
    std::wcout << L"After toggle Smart Mode - Command 1 Checked: " << plugin->IsCommandChecked(1) << std::endl;
    std::wcout << L"After toggle Smart Mode - Line 1: " << item0->GetItemLableText() << std::endl;
    std::wcout << L"After toggle Smart Mode - Line 2: " << item0->GetItemValueText() << std::endl;

    // Toggle Command 2 (Hide Weekly Quota)
    std::cout << "\nToggling Command 2 (Hide Weekly Quota)..." << std::endl;
    plugin->OnPluginCommand(2, nullptr, nullptr);
    std::wcout << L"After toggle Hide Weekly - Command 2 Checked: " << plugin->IsCommandChecked(2) << std::endl;
    std::wcout << L"After toggle Hide Weekly - Line 1: " << item0->GetItemLableText() << std::endl;
    std::wcout << L"After toggle Hide Weekly - Line 2: " << item0->GetItemValueText() << std::endl;

    // Toggle Command 4 (Short Reset Time)
    std::cout << "\nToggling Command 4 (Short Reset Time)..." << std::endl;
    plugin->OnPluginCommand(4, nullptr, nullptr);
    std::wcout << L"After toggle Short - Command 4 Checked: " << plugin->IsCommandChecked(4) << std::endl;
    std::wcout << L"After toggle Short - Line 1: " << item0->GetItemLableText() << std::endl;
    std::wcout << L"After toggle Short - Line 2: " << item0->GetItemValueText() << std::endl;

    // Toggle Command 3 (Hide Reset Time)
    std::cout << "\nToggling Command 3 (Hide Reset Time)..." << std::endl;
    plugin->OnPluginCommand(3, nullptr, nullptr);
    std::wcout << L"After toggle Hide - Command 3 Checked: " << plugin->IsCommandChecked(3) << std::endl;
    std::wcout << L"After toggle Hide - Line 1: " << item0->GetItemLableText() << std::endl;
    std::wcout << L"After toggle Hide - Line 2: " << item0->GetItemValueText() << std::endl;

    // Restore initial settings
    std::cout << "\nRestoring initial settings..." << std::endl;
    if ((plugin->IsCommandChecked(1) != 0) != origSmart) plugin->OnPluginCommand(1, nullptr, nullptr);
    if ((plugin->IsCommandChecked(2) != 0) != origHideWk) plugin->OnPluginCommand(2, nullptr, nullptr);
    if ((plugin->IsCommandChecked(3) != 0) != origHideRst) plugin->OnPluginCommand(3, nullptr, nullptr);
    if ((plugin->IsCommandChecked(4) != 0) != origShortRst) plugin->OnPluginCommand(4, nullptr, nullptr);
    std::wcout << L"Restored - Line 1: " << item0->GetItemLableText() << std::endl;
    std::wcout << L"Restored - Line 2: " << item0->GetItemValueText() << std::endl;

    // Test CustomDraw rendering onto offscreen memory DC
    std::cout << "\nTesting GDI / GDI+ CustomDraw rendering..." << std::endl;
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, 320, 48);
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);
    HFONT hFont = CreateFontW(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT hOldFont = (HFONT)SelectObject(hdcMem, hFont);
    int itemW = item0->GetItemWidthEx(hdcMem);
    std::cout << "Computed GetItemWidthEx: " << itemW << " px" << std::endl;
    if (itemW <= 20)
    {
        std::cerr << "Error: Invalid width returned from GetItemWidthEx!" << std::endl;
        SelectObject(hdcMem, hOldFont);
        DeleteObject(hFont);
        SelectObject(hdcMem, hOldBmp);
        DeleteObject(hBmp);
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        FreeLibrary(hMod);
        return 7;
    }
    SetTextColor(hdcMem, RGB(255, 255, 255));
    item0->DrawItem(hdcMem, 0, 0, itemW, 40, true);
    SetTextColor(hdcMem, RGB(0, 0, 0));
    item0->DrawItem(hdcMem, 0, 0, itemW, 40, false);
    SelectObject(hdcMem, hOldFont);
    DeleteObject(hFont);
    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

    // Test OnExtenedInfo - EI_TASKBAR_WND_NOT_SHOW_PERCENT (Controlled by host TrafficMonitor)
    std::cout << "\nTesting OnExtenedInfo (EI_TASKBAR_WND_NOT_SHOW_PERCENT)..." << std::endl;
    plugin->OnExtenedInfo(ITMPlugin::EI_TASKBAR_WND_NOT_SHOW_PERCENT, L"1");
    std::wcout << L"After host hides percent - Line 1: " << item0->GetItemLableText() << std::endl;
    std::wcout << L"After host hides percent - Line 2: " << item0->GetItemValueText() << std::endl;
    if (std::wstring(item0->GetItemLableText()).find(L"%") != std::wstring::npos)
    {
        std::cerr << "Error: '%' should be hidden when host sets EI_TASKBAR_WND_NOT_SHOW_PERCENT to 1!" << std::endl;
        FreeLibrary(hMod);
        return 5;
    }

    plugin->OnExtenedInfo(ITMPlugin::EI_TASKBAR_WND_NOT_SHOW_PERCENT, L"0");
    std::wcout << L"After host shows percent - Line 1: " << item0->GetItemLableText() << std::endl;
    std::wcout << L"After host shows percent - Line 2: " << item0->GetItemValueText() << std::endl;
    if (std::wstring(item0->GetItemLableText()).find(L"%") == std::wstring::npos)
    {
        std::cerr << "Error: '%' should be visible when host sets EI_TASKBAR_WND_NOT_SHOW_PERCENT to 0!" << std::endl;
        FreeLibrary(hMod);
        return 6;
    }

    std::wcout << L"\nTooltip (retains full details):\n" << plugin->GetTooltipInfo() << std::endl;

    FreeLibrary(hMod);
    std::cout << "Verification SUCCESS!" << std::endl;
    return 0;
}
