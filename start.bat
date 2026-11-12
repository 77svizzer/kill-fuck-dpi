@echo off
setlocal
cd /d "%~dp0"
chcp 65001 >nul
title KILL F#CK DPI - Панель управления

:: Проверка прав Администратора
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [i] Запрос прав Администратора...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process cmd.exe -ArgumentList '/c', '""%~f0""' -Verb RunAs"
    exit /b
)

:MENU
cls
echo ===================================================================
echo             KILL F#CK DPI - Автономный C++ обход ТСПУ
echo ===================================================================
echo   YouTube 4K | Discord | CS2 / FACEIT / Xplay | Без winws и zapret
echo -------------------------------------------------------------------
echo.
echo   [1] Установить и запустить службу Windows (РЕКОМЕНДУЕТСЯ)
echo       -^> Автозапуск в тихом фоне при включении ПК.
echo       -^> 0ms задержки для игр, без консольных окон.
echo.
echo   [2] Запустить в консольном окне (для наглядной проверки)
echo   [3] Остановить и удалить службу
echo   [4] Открыть файл настроек (config.ini)
echo   [5] Проверить доступность (YouTube, Discord, CS2/FACEIT/Xplay)
echo.
echo   [0] Выход
echo ===================================================================
set /p choice="Выберите действие [0-5]: "

if "%choice%"=="1" goto INSTALL_SERVICE
if "%choice%"=="2" goto RUN_CONSOLE
if "%choice%"=="3" goto REMOVE_SERVICE
if "%choice%"=="4" goto EDIT_CONFIG
if "%choice%"=="5" goto CHECK_STATUS
if "%choice%"=="0" exit /b

echo [!] Неверный выбор.
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

:INSTALL_SERVICE
call :CHECK_BINARIES
cls
echo [*] Установка автономной службы Windows...
dpi_bypass.exe --uninstall >nul 2>&1
dpi_bypass.exe --install
echo.
echo ===================================================================
echo [УСПЕХ] Служба KILL F#CK DPI успешно установлена и запущена!
echo ===================================================================
echo  - YouTube (1080p/4K), Discord, заблокированные сайты работают.
echo  - Сервера Xplay, FACEIT и CS2 работают на чистой скорости ядра.
echo ===================================================================
echo.
pause
goto MENU

:RUN_CONSOLE
call :CHECK_BINARIES
cls
echo [*] Остановка фоновой службы перед запуском в консоли...
dpi_bypass.exe --stop >nul 2>&1
echo ===================================================================
echo   Запуск в консольном окне. Для остановки нажмите Ctrl + C.
echo ===================================================================
echo.
dpi_bypass.exe
pause
goto MENU

:REMOVE_SERVICE
cls
echo [*] Остановка и удаление службы KILL F#CK DPI...
dpi_bypass.exe --uninstall
net stop WinDivert >nul 2>&1
echo.
echo [OK] Служба остановлена и удалена из системы.
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
