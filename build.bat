@echo off
setlocal enabledelayedexpansion

echo ======================================================================
echo Building TrafficMonitor Antigravity Plugin (x64)
echo ======================================================================

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if %ERRORLEVEL% neq 0 (
    echo Error: Failed to initialize vcvars64 environment!
    exit /b 1
)

cd /d "%~dp0"

echo.
echo Compiling AntigravityPlugin.dll...
cl /nologo /LD /EHsc /O2 /std:c++17 /I include /I src src\AntigravityPlugin.cpp /DEF:src\AntigravityPlugin.def /Fe:AntigravityPlugin.dll /link /MACHINE:X64
if %ERRORLEVEL% neq 0 (
    echo Error: Failed to build AntigravityPlugin.dll!
    exit /b 1
)

echo.
echo Compiling verify_plugin.exe...
cl /nologo /EHsc /std:c++17 /I include tests\verify_plugin.cpp /Fe:verify_plugin.exe /link /MACHINE:X64
if %ERRORLEVEL% neq 0 (
    echo Error: Failed to build verify_plugin.exe!
    exit /b 1
)

echo.
echo Verifying local AntigravityPlugin.dll...
verify_plugin.exe AntigravityPlugin.dll
if %ERRORLEVEL% neq 0 (
    echo Error: Plugin verification failed!
    exit /b 1
)

echo.
echo Compiling and running test_smart_transitions.exe...
cl /nologo /EHsc /std:c++17 /I include /I src tests\test_smart_transitions.cpp src\AntigravityPlugin.cpp /Fe:test_smart_transitions.exe /link /MACHINE:X64
if %ERRORLEVEL% neq 0 (
    echo Error: Failed to build test_smart_transitions.exe!
    exit /b 1
)
test_smart_transitions.exe
if %ERRORLEVEL% neq 0 (
    echo Error: Smart mode transition tests failed!
    exit /b 1
)

echo.
echo ======================================================================
echo Build and verification completed successfully!
echo ======================================================================
exit /b 0
