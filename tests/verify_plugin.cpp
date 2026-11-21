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

    // Toggle Command 1 (Smart Mode)
    std::cout << "\nToggling Command 1 (Smart Mode)..." << std::endl;
    plugin->OnPluginCommand(1, nullptr, nullptr);
    std::wcout << L"After toggle Smart Mode - Command 1 Checked: " << plugin->IsCommandChecked(1) << std::endl;
    std::wcout << L"After toggle Smart Mode - Line 1: " << item0->GetItemLableText() << std::endl;
    std::wcout << L"After toggle Smart Mode - Line 2: " << item0->GetItemValueText() << std::endl;

    // Toggle Command 3 (Short Reset Time)
    std::cout << "\nToggling Command 3 (Short Reset Time)..." << std::endl;
    plugin->OnPluginCommand(3, nullptr, nullptr);
    std::wcout << L"After toggle Short - Command 3 Checked: " << plugin->IsCommandChecked(3) << std::endl;
    std::wcout << L"After toggle Short - Line 1: " << item0->GetItemLableText() << std::endl;
    std::wcout << L"After toggle Short - Line 2: " << item0->GetItemValueText() << std::endl;

    // Toggle Command 2 (Hide Reset Time)
    std::cout << "\nToggling Command 2 (Hide Reset Time)..." << std::endl;
    plugin->OnPluginCommand(2, nullptr, nullptr);
    std::wcout << L"After toggle Hide - Command 2 Checked: " << plugin->IsCommandChecked(2) << std::endl;
    std::wcout << L"After toggle Hide - Line 1: " << item0->GetItemLableText() << std::endl;
    std::wcout << L"After toggle Hide - Line 2: " << item0->GetItemValueText() << std::endl;

    // Restore to defaults (Smart Mode: OFF, Hide: OFF, Short: OFF)
    std::cout << "\nRestoring default settings..." << std::endl;
    if (plugin->IsCommandChecked(1)) plugin->OnPluginCommand(1, nullptr, nullptr);
    if (plugin->IsCommandChecked(2)) plugin->OnPluginCommand(2, nullptr, nullptr);
    if (plugin->IsCommandChecked(3)) plugin->OnPluginCommand(3, nullptr, nullptr);
    std::wcout << L"Defaults restored - Line 1: " << item0->GetItemLableText() << std::endl;
    std::wcout << L"Defaults restored - Line 2: " << item0->GetItemValueText() << std::endl;

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
