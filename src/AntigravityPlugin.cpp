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

static std::wstring FormatResetTime(const std::string& raw, bool shortForm)
{
    if (raw.empty() || raw == "--") return L"";

    if (!shortForm)
    {
        // Long form: strip spaces (e.g. "4h 50m" -> "4h50m")
        std::wstring result;
        for (char c : raw)
        {
            if (c != ' ') result.push_back((wchar_t)c);
        }
        return result;
    }

    // Short form: smart rounding to single unit (e.g. 4h 50m -> 5h, 6d 23h -> 7d)
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
        if (hours >= 12) days += 1;
        return std::to_wstring(days) + L"d";
    }
    if (days == 0 && hours >= 0)
    {
        if (mins >= 30) hours += 1;
        return std::to_wstring(hours) + L"h";
    }
    if (hours > 0)
    {
        if (mins >= 30) hours += 1;
        return std::to_wstring(hours) + L"h";
    }
    if (hours == 0 && mins >= 0)
    {
        return std::to_wstring(mins) + L"m";
    }
    if (mins >= 0)
    {
        return std::to_wstring(mins) + L"m";
    }

    std::wstring fallback;
    for (char c : raw) if (c != ' ') fallback.push_back((wchar_t)c);
    return fallback;
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

    LoadSettings();
    DataRequired();
}

void CAntigravityPlugin::OnInitialize(ITrafficMonitor* pApp)
{
    m_pApp = pApp;
    LoadSettings();
    DataRequired();
}

void CAntigravityPlugin::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    if (!data) return;

    if (index == EI_TASKBAR_WND_NOT_SHOW_PERCENT)
    {
        bool hide = (*data == L'1' || _wcsicmp(data, L"true") == 0);
        m_taskbarHidePercent = hide;
        m_taskbarSettingReceived = true;
        if (m_hidePercent != hide)
        {
            m_hidePercent = hide;
            if (!m_lastJsonStr.empty())
            {
                ParseAndUpdate(m_lastJsonStr, false);
            }
        }
    }
    else if (index == EI_MAIN_WND_NOT_SHOW_PERCENT)
    {
        bool hide = (*data == L'1' || _wcsicmp(data, L"true") == 0);
        m_mainWndHidePercent = hide;
        if (!m_taskbarSettingReceived && m_hidePercent != hide)
        {
            m_hidePercent = hide;
            if (!m_lastJsonStr.empty())
            {
                ParseAndUpdate(m_lastJsonStr, false);
            }
        }
    }
    else if (index == EI_DRAW_TASKBAR_WND)
    {
        bool isTaskbar = (*data == L'1');
        bool targetHide = isTaskbar ? m_taskbarHidePercent : m_mainWndHidePercent;
        if (m_hidePercent != targetHide)
        {
            m_hidePercent = targetHide;
            if (!m_lastJsonStr.empty())
            {
                ParseAndUpdate(m_lastJsonStr, false);
            }
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

void CAntigravityPlugin::LoadSettings()
{
    std::wstring cfg = GetConfigPath();
    m_settings.shortResetTime = (GetPrivateProfileIntW(L"Display", L"ShortResetTime", 0, cfg.c_str()) != 0);
    m_settings.hideResetTime = (GetPrivateProfileIntW(L"Display", L"HideResetTime", 0, cfg.c_str()) != 0);
    m_settings.smartMode = (GetPrivateProfileIntW(L"Display", L"SmartMode", 0, cfg.c_str()) != 0);
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
}

void CAntigravityPlugin::ApplySettings(const PluginSettings& settings)
{
    m_settings = settings;
    SaveSettings();
    if (!m_lastJsonStr.empty())
    {
        ParseAndUpdate(m_lastJsonStr, false);
    }
    else
    {
        m_lastWriteTime.dwLowDateTime = 0;
        m_lastWriteTime.dwHighDateTime = 0;
        m_lastFileSize = 0;
        DataRequired();
    }
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
    switch (command_index)
    {
    case 0: return L"Refresh Quota Vault";
    case 1: return L"Smart Mode (Active Quotas Only)";
    case 2: return L"Hide Reset Time";
    case 3: return L"Short Reset Time (e.g. 5h)";
    default: return nullptr;
    }
}

int CAntigravityPlugin::IsCommandChecked(int command_index)
{
    switch (command_index)
    {
    case 1: return m_settings.smartMode ? 1 : 0;
    case 2: return m_settings.hideResetTime ? 1 : 0;
    case 3: return (!m_settings.hideResetTime && m_settings.shortResetTime) ? 1 : 0;
    default: return 0;
    }
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
    else if (command_index == 1)
    {
        PluginSettings s = m_settings;
        s.smartMode = !s.smartMode;
        ApplySettings(s);
    }
    else if (command_index == 2)
    {
        PluginSettings s = m_settings;
        s.hideResetTime = !s.hideResetTime;
        ApplySettings(s);
    }
    else if (command_index == 3)
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
}

struct OptionsDlgContext
{
    CAntigravityPlugin* pPlugin;
    bool shortResetTime;
    bool hideResetTime;
    bool smartMode;
    bool confirmed;
    HFONT hFont;
};

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

        // Smart Mode Checkbox
        HWND hChkSmart = CreateWindowExW(0, L"BUTTON", L"Smart Mode  (Only show actively decreasing quotas)",
                                         WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
                                         16, 12, 310, 24, hWnd, (HMENU)104, cs->hInstance, nullptr);
        SendMessageW(hChkSmart, WM_SETFONT, (WPARAM)ctx->hFont, TRUE);
        SendMessageW(hChkSmart, BM_SETCHECK, ctx->smartMode ? BST_CHECKED : BST_UNCHECKED, 0);

        // GroupBox: Reset Time Display
        HWND hGroup = CreateWindowExW(0, L"BUTTON", L"Reset Time Display",
                                      WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                      16, 42, 312, 125, hWnd, (HMENU)100, cs->hInstance, nullptr);
        SendMessageW(hGroup, WM_SETFONT, (WPARAM)ctx->hFont, TRUE);

        HWND hRadio1 = CreateWindowExW(0, L"BUTTON", L"Long form  (e.g. 4h 50m, 6d 23h)",
                                       WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP,
                                       32, 66, 280, 24, hWnd, (HMENU)101, cs->hInstance, nullptr);
        SendMessageW(hRadio1, WM_SETFONT, (WPARAM)ctx->hFont, TRUE);

        HWND hRadio2 = CreateWindowExW(0, L"BUTTON", L"Short form  (e.g. 5h, 7d)",
                                       WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                       32, 96, 280, 24, hWnd, (HMENU)102, cs->hInstance, nullptr);
        SendMessageW(hRadio2, WM_SETFONT, (WPARAM)ctx->hFont, TRUE);

        HWND hRadio3 = CreateWindowExW(0, L"BUTTON", L"Hide reset time  (Percentages only)",
                                       WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                       32, 126, 280, 24, hWnd, (HMENU)103, cs->hInstance, nullptr);
        SendMessageW(hRadio3, WM_SETFONT, (WPARAM)ctx->hFont, TRUE);

        int activeRadio = 101;
        if (ctx->hideResetTime) activeRadio = 103;
        else if (ctx->shortResetTime) activeRadio = 102;
        CheckRadioButton(hWnd, 101, 103, activeRadio);

        HWND hBtnOk = CreateWindowExW(0, L"BUTTON", L"OK",
                                      WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                                      135, 180, 85, 28, hWnd, (HMENU)IDOK, cs->hInstance, nullptr);
        SendMessageW(hBtnOk, WM_SETFONT, (WPARAM)ctx->hFont, TRUE);

        HWND hBtnCancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
                                          WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                                          235, 180, 85, 28, hWnd, (HMENU)IDCANCEL, cs->hInstance, nullptr);
        SendMessageW(hBtnCancel, WM_SETFONT, (WPARAM)ctx->hFont, TRUE);

        return 0;
    }

    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);
        if (id == IDOK)
        {
            if (ctx)
            {
                ctx->smartMode = (IsDlgButtonChecked(hWnd, 104) == BST_CHECKED);
                if (IsDlgButtonChecked(hWnd, 103) == BST_CHECKED)
                {
                    ctx->hideResetTime = true;
                }
                else if (IsDlgButtonChecked(hWnd, 102) == BST_CHECKED)
                {
                    ctx->hideResetTime = false;
                    ctx->shortResetTime = true;
                }
                else
                {
                    ctx->hideResetTime = false;
                    ctx->shortResetTime = false;
                }
                ctx->confirmed = true;
            }
            DestroyWindow(hWnd);
            return 0;
        }
        else if (id == IDCANCEL)
        {
            if (ctx)
            {
                ctx->confirmed = false;
            }
            DestroyWindow(hWnd);
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
    ctx.shortResetTime = m_settings.shortResetTime;
    ctx.hideResetTime = m_settings.hideResetTime;
    ctx.smartMode = m_settings.smartMode;
    ctx.confirmed = false;
    ctx.hFont = nullptr;

    int width = 355;
    int height = 260;
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
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        className,
        L"Antigravity Quota Options",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
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

    if (ctx.confirmed)
    {
        if (ctx.shortResetTime != m_settings.shortResetTime ||
            ctx.hideResetTime != m_settings.hideResetTime ||
            ctx.smartMode != m_settings.smartMode)
        {
            PluginSettings newSettings = m_settings;
            newSettings.shortResetTime = ctx.shortResetTime;
            newSettings.hideResetTime = ctx.hideResetTime;
            newSettings.smartMode = ctx.smartMode;
            ApplySettings(newSettings);
            return OR_OPTION_CHANGED;
        }
    }

    return OR_OPTION_UNCHANGED;
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

        std::wstring gStr = L"G:" + std::to_wstring(gPct) + pctSuffix;
        if (!gReset.empty()) gStr += L" (" + gReset + L")";

        std::wstring cStr = L"C:" + std::to_wstring(cPct) + pctSuffix;
        if (!cReset.empty()) cStr += L" (" + cReset + L")";

        std::wstring aStr = L"A:" + std::to_wstring(aPct) + pctSuffix;
        if (!aReset.empty()) aStr += L" (" + aReset + L")";

        std::wstring gWeeklyStr = L"Wk:" + std::to_wstring(gWeeklyPct) + pctSuffix;
        if (!gWeeklyResetFmt.empty()) gWeeklyStr += L" (" + gWeeklyResetFmt + L")";

        std::wstring cWeeklyStr = L"Wk:" + std::to_wstring(cWeeklyPct) + pctSuffix;
        if (!cWeeklyResetFmt.empty()) cWeeklyStr += L" (" + cWeeklyResetFmt + L")";

        std::wstring line1;
        std::wstring line2;

        if (!m_settings.smartMode)
        {
            // Standard / Non-smart Mode: Line 1 = Gemini (+ Argon), Line 2 = Claude
            line1 = gStr;
            if (hasArgon)
            {
                line1 += L"  " + aStr;
            }
            line2 = cStr;
        }
        else
        {
            // Smart Mode (Option C): Dynamic Quota Tracking
            bool showG = false;
            bool showC = false;
            bool showA = false;

            if (updateSmartState)
            {
                // Detect decreasing quota between intervals.
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
                    if (gDrop)
                    {
                        showG = true;
                        m_lastSingleDroppingModel = MODEL_GEMINI;
                    }
                    else if (cDrop)
                    {
                        showC = true;
                        m_lastSingleDroppingModel = MODEL_CLAUDE;
                    }
                    else if (aDrop)
                    {
                        showA = true;
                        m_lastSingleDroppingModel = MODEL_ARGON;
                    }
                    m_lastDroppingCount = 1;
                }
                else if (dropCount > 1)
                {
                    showG = gDrop;
                    showC = cDrop;
                    showA = aDrop;
                    m_lastDroppingCount = dropCount;
                }
                else // dropCount == 0
                {
                    if (m_lastDroppingCount == 1)
                    {
                        // Retain the single model that was dropping previously
                        if (m_lastSingleDroppingModel == MODEL_GEMINI) showG = true;
                        else if (m_lastSingleDroppingModel == MODEL_CLAUDE) showC = true;
                        else if (m_lastSingleDroppingModel == MODEL_ARGON && hasArgon) showA = true;
                        else showG = true;
                    }
                    else
                    {
                        // Multiple dropped previously or startup/idle -> default to Gemini
                        showG = true;
                        m_lastDroppingCount = 0;
                    }
                }

                m_lastShowG = showG;
                m_lastShowC = showC;
                m_lastShowA = showA;
            }
            else
            {
                showG = m_lastShowG;
                showC = m_lastShowC;
                showA = m_lastShowA && hasArgon;
                if (!showG && !showC && !showA)
                {
                    showG = true;
                }
            }

            int activeCount = (showG ? 1 : 0) + (showC ? 1 : 0) + (showA ? 1 : 0);
            if (activeCount == 1)
            {
                if (showG)
                {
                    line1 = gStr;
                    line2 = gWeeklyStr;
                }
                else if (showC)
                {
                    line1 = cStr;
                    line2 = cWeeklyStr;
                }
                else // showA
                {
                    line1 = aStr;
                    line2 = L"Argon Weekly";
                }
            }
            else if (activeCount == 2)
            {
                if (showG && showC)
                {
                    line1 = gStr;
                    line2 = cStr;
                }
                else if (showG && showA)
                {
                    line1 = gStr;
                    line2 = aStr;
                }
                else // showC && showA
                {
                    line1 = cStr;
                    line2 = aStr;
                }
            }
            else // activeCount == 3 (all three models dropping)
            {
                line1 = gStr + L"  " + aStr;
                line2 = cStr;
            }
        }

        if (updateSmartState)
        {
            // Update snapshot for subsequent drop detection
            m_prevQuota.g5h = g5h;
            m_prevQuota.c5h = c5h;
            m_prevQuota.aWeekly = hasArgon ? aWeekly : -1.0;
            m_prevQuota.valid = true;
            m_prevAccountEmail = email;
            m_hasInitialSnapshot = true;
        }

        // Item 0: Combined
        m_itemCombined.UpdateLabel(line1.c_str());
        m_itemCombined.UpdateValue(line2.c_str());

        // Item 1: Gemini
        m_itemGemini.UpdateValue(gReset.empty() ? (std::to_wstring(gPct) + pctSuffix).c_str() : (std::to_wstring(gPct) + pctSuffix + L" (" + gReset + L")").c_str());

        // Item 2: Claude
        m_itemClaude.UpdateValue(cReset.empty() ? (std::to_wstring(cPct) + pctSuffix).c_str() : (std::to_wstring(cPct) + pctSuffix + L" (" + cReset + L")").c_str());

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
