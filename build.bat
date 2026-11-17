@echo off
setlocal
cd /d "%~dp0"

echo ===================================================================
echo             Build KILL F#CK DPI (MSVC x64)
echo ===================================================================

set "VCVARS="
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    goto FOUND_VCVARS
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    goto FOUND_VCVARS
)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    goto FOUND_VCVARS
)

where cl >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] MSVC compiler cl.exe not found.
    echo Please run this from Visual Studio Native Tools Command Prompt.
    exit /b 1
)
goto DO_BUILD

:FOUND_VCVARS
echo [+] Setting up MSVC environment...
call "%VCVARS%" >nul 2>&1

:DO_BUILD
echo [*] Compiling modular C++17 engine (GUI + Service + Core) -> dpi_bypass.exe...
cl.exe /nologo /EHsc /O2 /utf-8 /DUNICODE /D_UNICODE /std:c++17 /Iinclude src\main.cpp src\ProtocolParser.cpp src\DesyncEngine.cpp src\ConfigManager.cpp src\HostlistManager.cpp src\ServiceController.cpp src\GuiManager.cpp /link /LIBPATH:lib WinDivert.lib ws2_32.lib advapi32.lib shell32.lib comctl32.lib user32.lib gdi32.lib dwmapi.lib /OUT:dpi_bypass.exe

if %errorlevel% equ 0 (
    echo.
    echo ===================================================================
    echo [OK] Build successful: dpi_bypass.exe created!
    echo Run start.bat to launch and configure.
    echo ===================================================================
    del /q *.obj >nul 2>&1
) else (
    echo.
    echo [ERROR] Compilation failed.
)
