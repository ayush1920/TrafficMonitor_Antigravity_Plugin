#include "AntigravityPlugin.h"
#include "json.hpp"
#include <fstream>

using json = nlohmann::json;

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

static std::wstring FormatCompactReset(const std::string& raw)
{
    if (raw.empty() || raw == "--") return L"";
    std::wstring result;
    for (char c : raw)
    {
        if (c != ' ')
        {
            result.push_back((wchar_t)c);
        }
    }
    return result;
}

int CPluginItem::GetItemWidthEx(void* hDC) const
{
    if (!hDC) return 110;
    HDC hdc = static_cast<HDC>(hDC);
    const wchar_t* l1 = m_labelBuffers[m_activeLabelBuf.load(std::memory_order_relaxed)];
    const wchar_t* l2 = m_buffers[m_activeBuf.load(std::memory_order_relaxed)];

    SIZE sz1 = { 0, 0 };
    SIZE sz2 = { 0, 0 };
    GetTextExtentPoint32W(hdc, l1, (int)wcslen(l1), &sz1);
    GetTextExtentPoint32W(hdc, l2, (int)wcslen(l2), &sz2);

    int maxW = (sz1.cx > sz2.cx) ? sz1.cx : sz2.cx;
    return maxW + 4;
}

void CPluginItem::DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode)
{
    HDC hdc = static_cast<HDC>(hDC);
    const wchar_t* l1 = m_labelBuffers[m_activeLabelBuf.load(std::memory_order_relaxed)];
    const wchar_t* l2 = m_buffers[m_activeBuf.load(std::memory_order_relaxed)];

    int prevBkMode = SetBkMode(hdc, TRANSPARENT);
    COLORREF prevColor = GetTextColor(hdc);

    COLORREF textColor = prevColor;
    if (dark_mode && textColor == RGB(0, 0, 0)) textColor = RGB(255, 255, 255);
    if (!dark_mode && textColor == RGB(255, 255, 255)) textColor = RGB(0, 0, 0);
    SetTextColor(hdc, textColor);

    if (h >= 24)
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
}

CAntigravityPlugin::CAntigravityPlugin()
    : m_itemCombined(L"antigravity_quota", L"Antigravity Quota", L"G:100% A:100%", L"G:100% (4h59m) A:100% (6d23h)", true),
      m_itemGemini(L"antigravity_gemini", L"Antigravity Gemini", L"G:", L"100% (4h59m)", false),
      m_itemClaude(L"antigravity_claude", L"Antigravity Claude", L"C:", L"100% (4h59m)", false),
      m_itemAccount(L"antigravity_account", L"Antigravity Account", L"Acc:", L"AccountName", false)
{
    wcscpy_s(m_tooltipBuffers[0], L"Antigravity Quota\r\nInitializing...");
    wcscpy_s(m_tooltipBuffers[1], L"Antigravity Quota\r\nInitializing...");

    // Initial read
    DataRequired();
}

void CAntigravityPlugin::OnInitialize(ITrafficMonitor* pApp)
{
    m_pApp = pApp;
    DataRequired();
}

IPluginItem* CAntigravityPlugin::GetItem(int index)
{
    switch (index)
    {
    case 0: return &m_itemCombined;
    case 1: return &m_itemGemini;
    case 2: return &m_itemClaude;
    case 3: return &m_itemAccount;
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
    if (command_index == 0) return L"Refresh Quota Vault";
    return nullptr;
}

void CAntigravityPlugin::OnPluginCommand(int command_index, void* hWnd, void* para)
{
    if (command_index == 0)
    {
        // Force reload by invalidating cached write time
        m_lastWriteTime.dwLowDateTime = 0;
        m_lastWriteTime.dwHighDateTime = 0;
        m_lastFileSize = 0;
        DataRequired();
    }
}

std::wstring CAntigravityPlugin::ResolveVaultPath() const
{
    // 1. Explicit primary path check
    const wchar_t* explicitPath = L"C:\\Users\\Ayush\\AppData\\Local\\AntigravityQuota\\vault.dat";
    DWORD attr = GetFileAttributesW(explicitPath);
    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        return explicitPath;
    }

    // 2. Dynamic fallback to %LOCALAPPDATA%
    wchar_t localAppData[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (len > 0 && len < MAX_PATH)
    {
        std::wstring fallbackPath = std::wstring(localAppData) + L"\\AntigravityQuota\\vault.dat";
        attr = GetFileAttributesW(fallbackPath.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
        {
            return fallbackPath;
        }
        return fallbackPath;
    }

    return explicitPath;
}

bool CAntigravityPlugin::ReadAndDecryptVault(std::string& outJsonStr)
{
    std::wstring path = ResolveVaultPath();

    WIN32_FILE_ATTRIBUTE_DATA fileAttr;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fileAttr))
    {
        if (!m_hasLoadedData)
        {
            m_itemCombined.UpdateValue(L"N/A");
            m_itemGemini.UpdateValue(L"N/A");
            m_itemClaude.UpdateValue(L"N/A");
            m_itemAccount.UpdateValue(L"N/A");
            UpdateTooltip(L"Antigravity Quota\r\nVault not found.");
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

    // Open file with full sharing permissions to avoid locking conflicts
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        // Could be momentarily locked by writer; keep existing data
        return false;
    }

    DWORD fileSize = GetFileSize(hFile, nullptr);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE)
    {
        CloseHandle(hFile);
        return false;
    }

    std::vector<BYTE> buffer(fileSize);
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, buffer.data(), fileSize, &bytesRead, nullptr) || bytesRead != fileSize)
    {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);

    // Decrypt DPAPI
    DATA_BLOB dataIn;
    dataIn.pbData = buffer.data();
    dataIn.cbData = fileSize;

    DATA_BLOB dataOut;
    ZeroMemory(&dataOut, sizeof(dataOut));

    if (!CryptUnprotectData(&dataIn, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &dataOut))
    {
        // Decrypt failed (temporary corruption or key issue); preserve existing data
        return false;
    }

    if (dataOut.pbData && dataOut.cbData > 0)
    {
        outJsonStr.assign(reinterpret_cast<const char*>(dataOut.pbData), dataOut.cbData);
    }
    LocalFree(dataOut.pbData);

    // Update tracked file attributes upon successful read & decrypt
    m_lastWriteTime = fileAttr.ftLastWriteTime;
    m_lastFileSize = fileAttr.nFileSizeLow;

    return true;
}

void CAntigravityPlugin::ParseAndUpdate(const std::string& jsonStr)
{
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

        std::wstring gReset = FormatCompactReset(g5hReset);
        std::wstring cReset = FormatCompactReset(c5hReset);

        std::wstring gStr = L"G:" + std::to_wstring(gPct) + L"%";
        if (!gReset.empty()) gStr += L" (" + gReset + L")";

        std::wstring cStr = L"C:" + std::to_wstring(cPct) + L"%";
        if (!cReset.empty()) cStr += L" (" + cReset + L")";

        std::wstring line1 = gStr;
        if (hasArgon)
        {
            int aPct = (int)std::round(aWeekly * 100.0);
            if (aPct < 0) aPct = 0; if (aPct > 100) aPct = 100;
            std::wstring aReset = FormatCompactReset(aWeeklyReset);
            std::wstring aStr = L"A:" + std::to_wstring(aPct) + L"%";
            if (!aReset.empty()) aStr += L" (" + aReset + L")";
            line1 += L"  " + aStr;
        }

        std::wstring line2 = cStr;

        // Item 0: Combined (Line 1 = Gemini + Argon, Line 2 = Claude)
        m_itemCombined.UpdateLabel(line1.c_str());
        m_itemCombined.UpdateValue(line2.c_str());

        // Item 1: Gemini
        m_itemGemini.UpdateValue(gReset.empty() ? (std::to_wstring(gPct) + L"%").c_str() : (std::to_wstring(gPct) + L"% (" + gReset + L")").c_str());

        // Item 2: Claude
        m_itemClaude.UpdateValue(cReset.empty() ? (std::to_wstring(cPct) + L"%").c_str() : (std::to_wstring(cPct) + L"% (" + cReset + L")").c_str());

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
        int gWeeklyPct = (int)std::round(gWeekly * 100.0);
        if (gWeeklyPct < 0) gWeeklyPct = 0; if (gWeeklyPct > 100) gWeeklyPct = 100;

        int c5hPct = (int)std::round(c5h * 100.0);
        if (c5hPct < 0) c5hPct = 0; if (c5hPct > 100) c5hPct = 100;

        int cWeeklyPct = (int)std::round(cWeekly * 100.0);
        if (cWeeklyPct < 0) cWeeklyPct = 0; if (cWeeklyPct > 100) cWeeklyPct = 100;

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
    if (ReadAndDecryptVault(jsonStr))
    {
        ParseAndUpdate(jsonStr);
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
