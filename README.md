# TrafficMonitor Antigravity Plugin

An ultra-lightweight, native C++ (x86 & x64) plugin for **TrafficMonitor** on Windows that monitors Google Antigravity quotas directly in your Windows taskbar.

---

## Key Features

- **Live Quota Monitoring**: Automatically decrypts and reads quota information from the secure local DPAPI vault managed by Gemini Profile / AGVerse.
- **Multi-Model Support**: Tracks 5-hour and weekly quotas for:
  - **Gemini** (5h & Weekly limits)
  - **Claude / Opus** (5h & Weekly limits)
  - **Argon** (Weekly limit)
- **Zero Configuration**: Automatically discovers active credentials, tokens, and quota tiers.
- **Compact Taskbar Presentation**: Custom double-line display optimized for Windows taskbar real estate.
- **Rich Hover Tooltip**: Displays comprehensive breakdown of active accounts, 5-hour & weekly percentages, and exact countdown timers to reset.

---

## Display Modes & Visualization Settings

Right-click the plugin on the taskbar to access options or open the **Options Dialog**:

### 1. Smart Mode (Dynamic Quota Tracking)
> **Default: Disabled (Toggle via right-click menu or Options dialog)**

Smart Mode intelligently adapts the taskbar display based on real-time quota consumption:
- **Single Active Model**: If only one model is being used (e.g., Claude drops from 80% to 75%), the taskbar displays only that model (Line 1: 5-hour limit, Line 2: Weekly limit).
- **Multiple Active Models**: If multiple models drop simultaneously (e.g., concurrent Gemini and Claude queries), both models are displayed.
- **Idle / Zero Drop**: When quotas remain unchanged between intervals:
  - If a single model was being actively used prior to idling, that model continues to be displayed.
  - If multiple models were used or on clean system launch, the display cleanly falls back to Gemini.

### 2. Reset Time Display Options
- **Long Form** (Default): Detailed countdown format (e.g., `4h 50m`, `6d 23h`).
- **Short Form**: Minimalist countdown representation (e.g., `5h`, `7d`).
- **Hide Reset Time**: Completely hides reset times, showing clean percentage indicators only (e.g., `G:100% C:100%`).

### 3. Native Percent (%) Factor Control
Following the standard convention established in the [TrafficMonitor Copilot Plugin](https://github.com/ayush1920/TrafficMonitor_Copilot_Plugin), the `%` symbol display is controlled directly by TrafficMonitor's host application settings:
- In TrafficMonitor, go to **Options** (`选项设置`) $\to$ **Taskbar Window Settings** or **Main Window Settings**.
- Check or uncheck **Don't show '%'** (`不显示百分号`).
- The plugin immediately reacts via `OnExtenedInfo` (`EI_TASKBAR_WND_NOT_SHOW_PERCENT` / `EI_MAIN_WND_NOT_SHOW_PERCENT`) to show or hide `%` (e.g., `G:100 (4h59m)` vs `G:100% (4h59m)`), ensuring consistent formatting across all TrafficMonitor widgets.

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
