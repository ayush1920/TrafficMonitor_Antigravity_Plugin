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

    // Toggle Short Reset Time command (index 1)
    std::cout << "\nToggling Command 1 (Short Reset Time)..." << std::endl;
    plugin->OnPluginCommand(1, nullptr, nullptr);
    std::wcout << L"After toggle - Command 1 Checked: " << plugin->IsCommandChecked(1) << std::endl;
    std::wcout << L"After toggle - Line 1: " << item0->GetItemLableText() << std::endl;
    std::wcout << L"After toggle - Line 2: " << item0->GetItemValueText() << std::endl;

    // Toggle back to original
    std::cout << "\nToggling back..." << std::endl;
    plugin->OnPluginCommand(1, nullptr, nullptr);
    std::wcout << L"Original restored - Line 1: " << item0->GetItemLableText() << std::endl;
    std::wcout << L"Original restored - Line 2: " << item0->GetItemValueText() << std::endl;

    std::wcout << L"\nTooltip:\n" << plugin->GetTooltipInfo() << std::endl;

    FreeLibrary(hMod);
    std::cout << "Verification SUCCESS!" << std::endl;
    return 0;
}
