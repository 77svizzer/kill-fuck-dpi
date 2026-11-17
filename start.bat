@echo off
setlocal EnableExtensions
cd /d "%~dp0"
chcp 65001 >nul
title KILL F#CK DPI - Панель управления

:: Проверка прав Администратора
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo.
    echo ===================================================================
    echo   [!] Требуются права Администратора для управления WinDivert.
    echo   [*] Запрос подтверждения прав [UAC]...
    echo ===================================================================
    powershell -NoProfile -ExecutionPolicy Bypass -Command "try { Start-Process -FilePath '%~f0' -WorkingDirectory '%~dp0' -Verb RunAs } catch { exit 1 }"
    if %errorlevel% neq 0 (
        echo.
        echo [ОШИБКА] Права Администратора не были предоставлены.
        echo Пожалуйста, запустите start.bat правой кнопкой мыши:
        echo   "Запуск от имени администратора"
        echo.
        pause
    )
    exit /b
)

:: Предварительная автоочистка от зависших служб
call :CLEAN_SERVICES_SILENT

:MENU
cls
echo ===================================================================
echo             KILL F#CK DPI - Автономный C++ обход ТСПУ
echo ===================================================================
echo   YouTube 4K  *  Discord  *  CS2 / FACEIT / Xplay  *  Без winws и zapret
echo -------------------------------------------------------------------
echo   [!] Автозапуск при старте ПК полностью отключен.
echo   [!] При закрытии драйвер WinDivert мгновенно выгружается из ядра.
echo ===================================================================
echo.
echo   [1] Запустить обход DPI (Удобный GUI и значок в трее)
echo   [2] Запустить в консольном окне (для наглядной проверки пакетов)
echo   [3] Полная очистка и восстановление сети (Сброс WinDivert)
echo   [4] Открыть файл настроек (config.ini)
echo   [5] Проверить доступность (YouTube, Discord, CS2/FACEIT/Xplay)
echo.
echo   [0] Выход
echo ===================================================================
set "choice="
set /p choice="Выберите действие [0-5]: "

if not defined choice goto MENU
if "%choice%"=="1" goto RUN_GUI
if "%choice%"=="2" goto RUN_CONSOLE
if "%choice%"=="3" goto FORCE_CLEAN
if "%choice%"=="4" goto EDIT_CONFIG
if "%choice%"=="5" goto CHECK_STATUS
if "%choice%"=="0" goto EXIT_APP

echo [!] Неверный выбор. Пожалуйста, введите цифру от 0 до 5.
timeout /t 2 >nul
goto MENU

:CHECK_BINARIES
if not exist "dpi_bypass.exe" (
    echo [ОШИБКА] Файл dpi_bypass.exe не найден!
    echo Выполните сборку через build.bat.
    pause
    goto MENU
)
if not exist "WinDivert.dll" (
    echo [ОШИБКА] Библиотека WinDivert.dll не найдена!
    pause
    goto MENU
)
if not exist "WinDivert64.sys" (
    echo [ОШИБКА] Драйвер WinDivert64.sys не найден!
    pause
    goto MENU
)
goto :eof

:CLEAN_SERVICES_SILENT
sc.exe stop KillFuckDPI >nul 2>&1
sc.exe delete KillFuckDPI >nul 2>&1
sc.exe stop WinDivert >nul 2>&1
sc.exe delete WinDivert >nul 2>&1
sc.exe stop WinDivert14 >nul 2>&1
sc.exe delete WinDivert14 >nul 2>&1
goto :eof

:RUN_GUI
call :CHECK_BINARIES
cls
echo [*] Запуск KILL F#CK DPI в графическом режиме...
start "" "%~dp0dpi_bypass.exe" --gui
echo [OK] Приложение запущено. Значок доступен в системном трее Windows.
timeout /t 2 >nul
exit /b

:RUN_CONSOLE
call :CHECK_BINARIES
cls
echo ===================================================================
echo   Запуск в консольном окне. Для остановки нажмите Ctrl + C.
echo ===================================================================
echo.
"%~dp0dpi_bypass.exe" --console
echo.
echo [*] Выгрузка WinDivert из ядра Windows...
call :CLEAN_SERVICES_SILENT
pause
goto MENU

:FORCE_CLEAN
cls
echo [*] Принудительная остановка и удаление сетевых хуков WinDivert...
call :CLEAN_SERVICES_SILENT
echo.
echo ===================================================================
echo [УСПЕХ] Драйвер WinDivert полностью выгружен из ядра Windows!
echo Сетевой стек работает в чистом штатном режиме.
echo Никакие фоновые службы при перезагрузке ПК стартовать не будут.
echo ===================================================================
echo.
pause
goto MENU

:EDIT_CONFIG
if exist "config.ini" (
    start notepad.exe "config.ini"
) else (
    echo [ОШИБКА] Файл config.ini не найден!
    pause
)
goto MENU

:CHECK_STATUS
cls
if exist "verify.ps1" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "verify.ps1"
) else (
    echo [ОШИБКА] Файл verify.ps1 не найден!
)
pause
goto MENU

:EXIT_APP
call :CLEAN_SERVICES_SILENT
exit /b
