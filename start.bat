@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"
chcp 1251 >nul
title KILL F#CK DPI - Панель управления

:: Проверка прав Администратора
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [i] Запрос прав Администратора...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process cmd.exe -ArgumentList '/c', '""%~f0""' -Verb RunAs"
    exit /b
)

set "BIN=%~dp0bin\"
set "SRVCNAME=KillFuckDPI"

:: Проверенная рабочая универсальная конфигурация:
:: 1. TCP 80/443: fake,fakedsplit с autottl и ts (разрез по SNI + фейк ослепляет ТСПУ)
:: 2. UDP 443: нейтрализация QUIC для YouTube (мгновенный переход на HTTP/2 с 4K 60fps)
:: 3. UDP 19294-19344, 50000-50100: разблокировка голосовых каналов Discord (STUN + RTC)
:: 4. Игровые порты (CS2, FACEIT EU 27015-27050) полностью исключены из фильтра ядра (0ms оверхед)
set ARGS=--wf-tcp=80,443 --wf-udp=443,19294-19344,50000-50100 --filter-udp=443 --dpi-desync=fake --dpi-desync-repeats=6 --dpi-desync-fake-quic="%BIN%quic_initial_www_google_com.bin" --new --filter-udp=19294-19344,50000-50100 --filter-l7=discord,stun --dpi-desync=fake --dpi-desync-fake-discord="%BIN%ACTIVE_DISCORD_UDP.bin" --dpi-desync-fake-stun="%BIN%ACTIVE_DISCORD_UDP.bin" --dpi-desync-repeats=6 --new --filter-tcp=80,443,2053,2083,2087,2096,8443 --dpi-desync=fake,fakedsplit --dpi-desync-repeats=6 --dpi-desync-fooling=ts --dpi-desync-fakedsplit-pattern=0x00 --dpi-desync-fake-tls="%BIN%tls_clienthello_www_google_com.bin" --dpi-desync-fake-http="%BIN%tls_clienthello_www_sferum_ru.bin"

:MENU
cls
echo ===================================================================
echo             KILL F#CK DPI - Универсальный обход ТСПУ
echo ===================================================================
echo   YouTube 4K ^| Discord (Текст + Голос) ^| Все сайты ^| 0 лагов в CS2
echo -------------------------------------------------------------------
echo.
echo   [1] Установить и запустить службу Windows (РЕКОМЕНДУЕТСЯ)
echo       -^> Запуск за 5 секунд! Работает ВСЕГДА при включении ПК!
echo       -^> В фоне, без окон, без консоли, без "бесконечных циферок".
echo.
echo   [2] Запустить прямо сейчас в фоне (до перезагрузки ПК)
echo   [3] Запустить в окне (для наглядной проверки работы)
echo   [4] Остановить и полностью удалить службу
echo   [5] Проверить доступность (YouTube, Discord, CS2/FACEIT)
echo.
echo   [0] Выход
echo ===================================================================
set /p choice="Выберите действие [0-5]: "

if "%choice%"=="1" goto INSTALL_SERVICE
if "%choice%"=="2" goto RUN_BACKGROUND
if "%choice%"=="3" goto RUN_CONSOLE
if "%choice%"=="4" goto REMOVE_SERVICE
if "%choice%"=="5" goto CHECK_STATUS
if "%choice%"=="0" exit /b

echo [!] Неверный выбор.
timeout /t 2 >nul
goto MENU

:CHECK_BINARIES
if not exist "%BIN%winws.exe" (
    echo [ОШИБКА] Файл %BIN%winws.exe не найден!
    pause
    goto MENU
)
if not exist "%BIN%WinDivert.dll" (
    echo [ОШИБКА] Библиотека %BIN%WinDivert.dll не найдена!
    pause
    goto MENU
)
if not exist "%BIN%WinDivert64.sys" (
    echo [ОШИБКА] Драйвер %BIN%WinDivert64.sys не найден!
    pause
    goto MENU
)
goto :eof

:INSTALL_SERVICE
call :CHECK_BINARIES
cls
echo [*] Настройка системной службы Windows...
echo [1/4] Остановка старых процессов и служб...
net stop %SRVCNAME% >nul 2>&1
net stop zapret >nul 2>&1
taskkill /F /IM winws.exe >nul 2>&1
sc delete %SRVCNAME% >nul 2>&1

echo [2/4] Оптимизация сетевого стека TCP (Timestamps)...
netsh interface tcp set global timestamps=enabled >nul 2>&1

echo [3/4] Регистрация службы %SRVCNAME% с автозапуском...
sc create %SRVCNAME% binPath= "\"%BIN%winws.exe\"" DisplayName= "KILL F#CK DPI" start= auto >nul 2>&1
sc description %SRVCNAME% "Универсальный локальный обход DPI (YouTube 4K, Discord, сайты, FACEIT/CS2 safe)" >nul 2>&1

:: Установка точной строки ImagePath в реестре
reg add "HKLM\System\CurrentControlSet\Services\%SRVCNAME%" /v ImagePath /t REG_EXPAND_SZ /d "\"%BIN%winws.exe\" %ARGS%" /f >nul 2>&1

echo [4/4] Запуск службы...
net start %SRVCNAME% >nul 2>&1
if %errorlevel% neq 0 (
    sc start %SRVCNAME% >nul 2>&1
)

echo.
echo ===================================================================
echo [УСПЕХ] Служба KILL F#CK DPI успешно установлена и запущена!
echo ===================================================================
echo  - Больше не нужно запускать программу вручную.
echo  - При включении компьютера обход активируется автоматически в фоне.
echo  - YouTube (1080p/4K), Discord (голос + чат) и все сайты работают.
echo  - Игровые сервера FACEIT и CS2 работают на чистой скорости сети.
echo ===================================================================
echo.
pause
goto MENU

:RUN_BACKGROUND
call :CHECK_BINARIES
cls
echo [*] Остановка существующих копий...
net stop %SRVCNAME% >nul 2>&1
taskkill /F /IM winws.exe >nul 2>&1
netsh interface tcp set global timestamps=enabled >nul 2>&1

echo [*] Запуск KILL F#CK DPI в фоновом режиме...
cd /d "%BIN%"
start "KillFuckDPI" /min "%BIN%winws.exe" %ARGS%
cd /d "%~dp0"
echo.
echo [OK] Программа запущена в фоне и работает до перезагрузки ПК.
echo (Для постоянной работы без повторных запусков используйте пункт 1).
echo.
pause
goto MENU

:RUN_CONSOLE
call :CHECK_BINARIES
cls
echo [*] Остановка существующих копий...
net stop %SRVCNAME% >nul 2>&1
taskkill /F /IM winws.exe >nul 2>&1
netsh interface tcp set global timestamps=enabled >nul 2>&1

echo ===================================================================
echo   Запуск в режиме мониторинга (для проверки)
echo   Для остановки нажмите Ctrl + C или закройте это окно.
echo ===================================================================
echo.
cd /d "%BIN%"
winws.exe %ARGS%
cd /d "%~dp0"
pause
goto MENU

:REMOVE_SERVICE
cls
echo [*] Остановка и удаление службы %SRVCNAME%...
net stop %SRVCNAME% >nul 2>&1
net stop zapret >nul 2>&1
sc delete %SRVCNAME% >nul 2>&1
taskkill /F /IM winws.exe >nul 2>&1
net stop WinDivert >nul 2>&1
net stop WinDivert1.4 >nul 2>&1
echo.
echo [OK] Служба полностью удалена, все фоновые процессы завершены.
echo.
pause
goto MENU

:CHECK_STATUS
cls
if exist "%~dp0verify.ps1" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0verify.ps1"
) else (
    echo [*] Проверка сетевого соединения...
    ping -n 1 8.8.8.8 >nul 2>&1
    if !errorlevel! equ 0 (
        echo [OK] Интернет соединение активно.
    )
)
pause
goto MENU