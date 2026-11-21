#pragma once

#include <windows.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <atomic>
#include <cmath>
#include <algorithm>
#include "PluginInterface.h"

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

class CPluginItem : public IPluginItem
{
public:
    CPluginItem(const wchar_t* id, const wchar_t* name, const wchar_t* label, const wchar_t* sample, bool isCustomDraw = false)
        : m_id(id), m_name(name), m_label(label), m_sample(sample), m_isCustomDraw(isCustomDraw),
          m_activeBuf(0), m_activeLabelBuf(0)
    {
        wcscpy_s(m_buffers[0], L"N/A");
        wcscpy_s(m_buffers[1], L"N/A");
        wcscpy_s(m_labelBuffers[0], label);
        wcscpy_s(m_labelBuffers[1], label);
    }

    virtual const wchar_t* GetItemName() const override { return m_name.c_str(); }
    virtual const wchar_t* GetItemId() const override { return m_id.c_str(); }
    virtual const wchar_t* GetItemLableText() const override
    {
        return m_labelBuffers[m_activeLabelBuf.load(std::memory_order_relaxed)];
    }
    virtual const wchar_t* GetItemValueText() const override
    {
        return m_buffers[m_activeBuf.load(std::memory_order_relaxed)];
    }
    virtual const wchar_t* GetItemValueSampleText() const override { return m_sample.c_str(); }

    virtual bool IsCustomDraw() const override { return m_isCustomDraw; }
    virtual int GetItemWidth() const override { return 110; }
    virtual int GetItemWidthEx(void* hDC) const override;
    virtual void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override;

    void UpdateValue(const wchar_t* newValue)
    {
        int next = 1 - m_activeBuf.load(std::memory_order_relaxed);
        wcscpy_s(m_buffers[next], newValue);
        m_activeBuf.store(next, std::memory_order_release);
    }

    void UpdateLabel(const wchar_t* newLabel)
    {
        int next = 1 - m_activeLabelBuf.load(std::memory_order_relaxed);
        wcscpy_s(m_labelBuffers[next], newLabel);
        m_activeLabelBuf.store(next, std::memory_order_release);
    }

private:
    std::wstring m_id;
    std::wstring m_name;
    std::wstring m_label;
    std::wstring m_sample;
    bool m_isCustomDraw;
    wchar_t m_buffers[2][256];
    wchar_t m_labelBuffers[2][256];
    std::atomic<int> m_activeBuf;
    std::atomic<int> m_activeLabelBuf;
};

struct PluginSettings
{
    bool shortResetTime = false; // false = Long form (4h59m), true = Short form (5h)
    bool hideResetTime = false;  // true = Hide reset time completely from taskbar interface
};

class CAntigravityPlugin : public ITMPlugin
{
public:
    CAntigravityPlugin();
    virtual ~CAntigravityPlugin() = default;

    // ITMPlugin interfaces
    virtual int GetAPIVersion() const override { return 8; }
    virtual IPluginItem* GetItem(int index) override;
    virtual void DataRequired() override;
    virtual OptionReturn ShowOptionsDialog(void* hParent) override;
    virtual const wchar_t* GetInfo(PluginInfoIndex index) override;
    virtual const wchar_t* GetTooltipInfo() override;
    virtual int GetCommandCount() override { return 3; }
    virtual const wchar_t* GetCommandName(int command_index) override;
    virtual int IsCommandChecked(int command_index) override;
    virtual void OnPluginCommand(int command_index, void* hWnd, void* para) override;
    virtual void OnInitialize(ITrafficMonitor* pApp) override;

    const PluginSettings& GetSettings() const { return m_settings; }
    void ApplySettings(const PluginSettings& settings);

private:
    std::wstring GetConfigPath() const;
    void LoadSettings();
    void SaveSettings();
    std::wstring ResolveVaultPath() const;
    bool ReadAndDecryptVault(std::string& outJsonStr);
    void ParseAndUpdate(const std::string& jsonStr);
    void UpdateTooltip(const wchar_t* newTooltip);

    PluginSettings m_settings;

    CPluginItem m_itemCombined; // Item 0: antigravity_quota
    CPluginItem m_itemGemini;   // Item 1: antigravity_gemini
    CPluginItem m_itemClaude;   // Item 2: antigravity_claude
    CPluginItem m_itemAccount;  // Item 3: antigravity_account

    wchar_t m_tooltipBuffers[2][2048];
    std::atomic<int> m_tooltipActiveBuf{0};

    FILETIME m_lastWriteTime{0, 0};
    DWORD m_lastFileSize{0};
    bool m_hasLoadedData{false};

    ITrafficMonitor* m_pApp{nullptr};
};
