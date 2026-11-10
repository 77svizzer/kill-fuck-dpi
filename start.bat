@echo off
setlocal
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

:MENU
cls
echo ===================================================================
echo             KILL F#CK DPI - Локальный обход блокировок             
echo ===================================================================
echo  [1] Запустить стандартный обход (разрез 2 байта, безопасен для CS2)
echo  [2] Запустить агрессивный режим (разрез 1 байт, усиленный обход)
echo  [3] Открыть конфигурацию config.ini в Блокноте
echo  [4] Остановить все процессы обхода и очистить службу
echo  [5] Выход
echo ===================================================================
set /p choice="Выберите действие [1-5]: "

if "%choice%"=="1" goto START_STANDARD
if "%choice%"=="2" goto START_AGGRESSIVE
if "%choice%"=="3" goto EDIT_CONFIG
if "%choice%"=="4" goto STOP_ALL
if "%choice%"=="5" goto EXIT_APP

echo [!] Неверный выбор. Введите число от 1 до 5.
timeout /t 2 >nul
goto MENU

:CHECK_FILES
if not exist "dpi_bypass.exe" (
    echo [ОШИБКА] Не найден файл dpi_bypass.exe!
    pause
    goto MENU
)
if not exist "WinDivert.dll" (
    echo [ОШИБКА] Не найден файл WinDivert.dll!
    pause
    goto MENU
)
if not exist "WinDivert64.sys" (
    echo [ОШИБКА] Не найден файл WinDivert64.sys!
    pause
    goto MENU
)
goto :eof

:START_STANDARD
call :CHECK_FILES
cls
echo [+] Запуск стандартного режима обхода (split-pos = 2 байта)...
echo [i] UDP трафик (CS2, FACEIT, Discord) аппаратно изолирован.
echo [i] Для остановки нажмите Ctrl + C в окне утилиты.
echo.
dpi_bypass.exe --split-pos 2 --config config.ini
echo.
echo Служба завершила работу.
pause
goto MENU

:START_AGGRESSIVE
call :CHECK_FILES
cls
echo [+] Запуск агрессивного режима (split-pos = 1 байт)...
echo [i] Эффективен против жестких блокировок ТСПУ.
echo [i] Для остановки нажмите Ctrl + C в окне утилиты.
echo.
dpi_bypass.exe --split-pos 1 --config config.ini
echo.
echo Служба завершила работу.
pause
goto MENU

:EDIT_CONFIG
if not exist "config.ini" (
    echo [General] > config.ini
    echo split_position = 2 >> config.ini
    echo target_ports = 80, 443 >> config.ini
    echo isolate_game_udp = true >> config.ini
)
start notepad.exe "config.ini"
goto MENU

:STOP_ALL
cls
echo [*] Завершение процессов dpi_bypass.exe...
taskkill /F /IM dpi_bypass.exe >nul 2>&1
net stop "WinDivert" >nul 2>&1
net stop "WinDivert1.4" >nul 2>&1
echo [OK] Все процессы и драйверы WinDivert успешно остановлены.
timeout /t 2 >nul
goto MENU

:EXIT_APP
exit /b