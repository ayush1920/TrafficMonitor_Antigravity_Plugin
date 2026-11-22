#pragma once

#include <windows.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <cmath>
#include <algorithm>
#include "PluginInterface.h"

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

enum DisplayStyle
{
    STYLE_CLASSIC = 0,
    STYLE_TYPOGRAPHIC = 1,
    STYLE_PROGRESS_BAR = 2,
    STYLE_MINIMAL_DOTS = 3,
    STYLE_PILL_BADGES = 4
};

struct QuotaDisplayEntry
{
    std::wstring label;
    int percent{0};
    std::wstring pctText;
    std::wstring resetText;
    bool isWeekly{false};
    bool labelFromHost{false};
};

struct PluginSettings
{
    bool shortResetTime = false;                 // false = Long form (4h59m), true = Short form (5h)
    bool hideResetTime = false;                  // true = Hide reset time from taskbar interface
    bool smartMode = false;                      // true = Option C: Only display models whose quota is decreasing
    bool hideWeeklyQuota = false;                // true = Hide 'Wk' row when showing a single model in Smart Mode
    bool hideLabels = false;                     // true = Omit model labels (G, C, A, Wk) for a cleaner look
    DisplayStyle displayStyle = STYLE_TYPOGRAPHIC; // 0=Classic, 1=Typographic, 2=ProgressBar, 3=MinimalDots, 4=PillBadges
    bool alignColumns = true;                    // true = Right-align numbers & reset times into columns
    bool tabularDigits = true;                   // true = Monospace/fixed-width digit cells to prevent jitter
    bool dimSecondaryInfo = true;                // true = Dim secondary info (timers/labels) for visual hierarchy
};

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
    virtual int IsDoubleLineExclusive() const override;
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

    void SetRenderState(const std::vector<QuotaDisplayEntry>& entries, const PluginSettings& settings)
    {
        std::lock_guard<std::mutex> lock(m_renderMutex);
        m_renderEntries = entries;
        m_renderSettings = settings;
    }

    void SetHostLabelColor(COLORREF clr) { m_hostLabelColor.store(clr, std::memory_order_relaxed); }
    void SetHostValueColor(COLORREF clr) { m_hostValueColor.store(clr, std::memory_order_relaxed); }
    COLORREF GetHostLabelColor() const { return m_hostLabelColor.load(std::memory_order_relaxed); }
    COLORREF GetHostValueColor() const { return m_hostValueColor.load(std::memory_order_relaxed); }

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
    std::atomic<COLORREF> m_hostLabelColor{ CLR_INVALID };
    std::atomic<COLORREF> m_hostValueColor{ CLR_INVALID };

    mutable std::mutex m_renderMutex;
    std::vector<QuotaDisplayEntry> m_renderEntries;
    PluginSettings m_renderSettings;
    mutable std::atomic<int> m_lastDrawH{ 0 };
};

enum ActiveModelType
{
    MODEL_GEMINI = 0,
    MODEL_CLAUDE = 1,
    MODEL_ARGON = 2
};

struct QuotaSnapshot
{
    double g5h{-1.0};
    double c5h{-1.0};
    double aWeekly{-1.0};
    bool valid{false};
};

class CAntigravityPlugin : public ITMPlugin
{
public:
    CAntigravityPlugin();
    virtual ~CAntigravityPlugin();

    // ITMPlugin interfaces
    virtual int GetAPIVersion() const override { return 8; }
    virtual IPluginItem* GetItem(int index) override;
    virtual void DataRequired() override;
    virtual OptionReturn ShowOptionsDialog(void* hParent) override;
    virtual const wchar_t* GetInfo(PluginInfoIndex index) override;
    virtual const wchar_t* GetTooltipInfo() override;
    virtual int GetCommandCount() override { return 6; }
    virtual const wchar_t* GetCommandName(int command_index) override;
    virtual int IsCommandChecked(int command_index) override;
    virtual void OnPluginCommand(int command_index, void* hWnd, void* para) override;
    virtual void OnInitialize(ITrafficMonitor* pApp) override;
    virtual void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;

    const PluginSettings& GetSettings() const { return m_settings; }
    void ApplySettings(const PluginSettings& settings);
    void ParseAndUpdate(const std::string& jsonStr, bool updateSmartState = true);
    bool GetHidePercent() const { return m_hidePercent; }
    void SetHostConfigPathOverride(const std::wstring& path) { m_hostConfigPathOverride = path; }

private:
    std::wstring GetConfigPath() const;
    std::wstring GetHostConfigPath() const;
    std::wstring ResolveHostItemLabel(const wchar_t* itemId, const wchar_t* defaultLabel, bool& outFromHost) const;
    bool HasHostConfigChanged();
    void LoadSettings();
    void SaveSettings();
    std::wstring ResolveVaultPath() const;
    bool ReadAndDecryptVault(std::string& outJsonStr);
    void UpdateVaultFileCacheMeta(const std::wstring& path);
    void UpdateTooltip(const wchar_t* newTooltip);
    void RefreshHostWindows();
    void EnsureBackgroundWorkerStarted();
    void StopBackgroundWorker();
    void RequestPollNow(bool interactiveLogin = false);
    static DWORD WINAPI WorkerThreadProc(LPVOID lpParam);
    void BackgroundWorkerLoop();

    HANDLE m_workerThread{nullptr};
    HANDLE m_wakeEvent{nullptr};
    std::atomic<bool> m_stopWorker{false};
    std::atomic<bool> m_pollNowRequested{false};
    std::atomic<bool> m_loginRequested{false};
    std::mutex m_dataMutex;

    PluginSettings m_settings;

    // Smart Mode State
    QuotaSnapshot m_prevQuota;
    ActiveModelType m_lastSingleDroppingModel{MODEL_GEMINI};
    int m_lastDroppingCount{0};
    bool m_hasInitialSnapshot{false};
    std::string m_prevAccountEmail;
    bool m_lastShowG{true};
    bool m_lastShowC{false};
    bool m_lastShowA{false};

    CPluginItem m_itemCombined; // Item 0: antigravity_quota
    CPluginItem m_itemGemini;   // Item 1: antigravity_gemini
    CPluginItem m_itemClaude;   // Item 2: antigravity_claude
    CPluginItem m_itemAccount;  // Item 3: antigravity_account
    CPluginItem m_itemArgon;    // Item 4: antigravity_argon

    wchar_t m_tooltipBuffers[2][2048];
    std::atomic<int> m_tooltipActiveBuf{0};

    FILETIME m_lastWriteTime{0, 0};
    DWORD m_lastFileSize{0};
    FILETIME m_lastHostCfgWriteTime{0, 0};
    bool m_hasLoadedData{false};

    // Percentage display flag controlled by TrafficMonitor main window / taskbar settings
    bool m_hidePercent{false};
    bool m_taskbarHidePercent{false};
    bool m_mainWndHidePercent{false};
    bool m_taskbarSettingReceived{false};
    std::string m_lastJsonStr;
    std::wstring m_hostConfigPathOverride;

    ITrafficMonitor* m_pApp{nullptr};
};
