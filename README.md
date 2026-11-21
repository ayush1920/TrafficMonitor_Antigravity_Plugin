# TrafficMonitor Antigravity Plugin

An ultra-lightweight, native C++ (x86 & x64) plugin for **TrafficMonitor** on Windows that monitors Google Antigravity quotas directly in your Windows taskbar.

---

## Key Features

- **Standalone & Shared Quota Polling**: Works completely independently (no Rainmeter or AgVerse installation required) with built-in background polling every 5 minutes, automatic OAuth token refresh, IDE credential discovery (`~/.gemini/jetski-standalone-oauth-token` / Windows Credential Manager), and a **Sign In with Google...** context menu command. When AgVerse or Rainmeter is also installed, it shares `%LOCALAPPDATA%\AntigravityQuota\vault.dat` seamlessly to avoid duplicate logins.
- **Multi-Model Support**: Tracks 5-hour and weekly quotas for:
  - **Gemini** (5h & Weekly limits)
  - **Claude / Opus** (5h & Weekly limits)
  - **Argon** (Weekly limit)
- **Zero Configuration**: Automatically discovers active credentials, tokens, and quota tiers.
- **Compact Taskbar Presentation**: Custom double-line display optimized for Windows taskbar real estate.
- **Rich Hover Tooltip**: Displays comprehensive breakdown of active accounts, 5-hour & weekly percentages, and exact countdown timers to reset.

---

## Display Modes & Visualization Settings

Right-click the plugin on the taskbar to access quick toggles or open the **Options Dialog** (with live **Apply** preview):

### 1. Visual Style Presets
Choose between five crisp native GDI / GDI+ visual layouts:
- **Typographic Hierarchy** (Default): Drops noisy colons and parentheses and dims secondary timers for clean instant focus (`G  73% · 4h` / `A  84% · 7d`).
- **Micro Progress Bars**: Adds a 4px capsule progress bar color-coded by threshold (green `> 50%`, amber `20–50%`, red `< 20%`).
- **Minimal Status Dots**: Compact horizontal indicator strip with anti-aliased threshold dots (`● G 73% | ● A 84%`), keeping reset timers in the hover tooltip.
- **Stacked Pill Badges**: Dark rounded pill containers with aligned label, metric, and vector clock timer columns.
- **Classic**: Traditional colon & parentheses formatting (`G:73% (4h)`).

### 2. Layout & Polish Options
- **Align Columns**: Right-aligns percentages and reset timers cleanly beneath one another.
- **Tabular Fixed-Width Digits**: Draws digits in fixed monospace advance cells so taskbar width never jitters when numbers change.
- **Dim Secondary Info**: Mutes labels and reset timers visually so primary quota percentages pop first.

### 3. Smart Mode & Quota Visibility
- **Smart Mode (Dynamic Quota Tracking)** *(Default: Disabled)*:
  - **Single Active Model**: Displays only the dropping model (5-hour limit on row 1, optional weekly quota `Wk` on row 2).
  - **Multiple Active Models**: Displays all models dropping concurrently.
  - **Idle / Zero Drop**: Retains the previous single active model or cleanly falls back to Gemini after multi-model usage or cold startup.
- **Hide Weekly Quota (`Wk`)**: Omit the `Wk` row when displaying a single model (weekly tiers remain available in the hover tooltip).

### 4. Reset Time Display Options
- **Hide Reset Time**: Checkbox to hide reset timers on the taskbar while keeping full countdowns in the hover tooltip.
- **Long Form** (Default): Detailed countdown format (`4h 50m`, `6d 23h`).
- **Short Form**: Rounded single-unit countdown (`5h`, `7d`).

### 5. Native Percent (%) Factor Control
Following the standard convention established in the [TrafficMonitor Copilot Plugin](https://github.com/ayush1920/TrafficMonitor_Copilot_Plugin), `%` symbol visibility is controlled directly by TrafficMonitor's host settings (`Don't show '%'` / `不显示百分号` under Main Window or Taskbar Window settings via `OnExtenedInfo`).

---

## Installation

1. Download the latest release from the [Releases](https://github.com/ayush1920/TrafficMonitor_Antigravity_Plugin/releases) page.
2. Select the architecture matching your TrafficMonitor installation (`x64` or `x86`).
3. Copy `AntigravityPlugin.dll` to the `plugins/` directory inside your TrafficMonitor folder:
   ```text
   TrafficMonitor/
   ├── plugins/
   │   └── AntigravityPlugin.dll
   └── TrafficMonitor.exe
   ```
4. Restart TrafficMonitor.
5. In TrafficMonitor settings, enable the **Antigravity Quota** plugin item in the Taskbar display options.

---

## Building from Source

### Prerequisites
- Visual Studio 2022 (with MSVC C++17 build tools)
- CMake 3.21 or higher

### Build Steps
Run the automated build script:
```cmd
build.bat
```
Or build using CMake:
```cmd
cmake -B build/x64 -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build/x64 --config Release
ctest --test-dir build/x64 -C Release --output-on-failure
```
