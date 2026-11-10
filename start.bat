@echo off
setlocal EnableDelayedExpansion
title KILL F#CK DPI - Панель управления

:: Установка кодировки UTF-8 для корректного отображения русского языка
chcp 65001 >nul

:: Проверка прав Администратора
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [i] Запрос прав Администратора...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

:: Переход в директорию скрипта
cd /d "%~dp0"

:MENU
cls
echo ===================================================================
echo             KILL F#CK DPI - Локальный обход блокировок             
echo ===================================================================
echo  [1] Запустить стандартный обход (разрез 2 байта, FACEIT / CS2 safe)
echo  [2] Запустить агрессивный режим (разрез 1 байт, усиленный ТСПУ обход)
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

echo.
echo [!] Неверный выбор. Пожалуйста, введите число от 1 до 5.
timeout /t 2 >nul
goto MENU

:CHECK_FILES
set MISSING=0
if not exist "dpi_bypass.exe" (
    echo [КРИТИЧЕСКАЯ ОШИБКА] Не найден исполняемый файл dpi_bypass.exe!
    echo Выполните сборку проекта через build.bat или CMake перед запуском.
    set MISSING=1
)
if not exist "WinDivert.dll" (
    echo [КРИТИЧЕСКАЯ ОШИБКА] Не найдена библиотека WinDivert.dll в папке программы!
    set MISSING=1
)
if not exist "WinDivert64.sys" (
    echo [КРИТИЧЕСКАЯ ОШИБКА] Не найден драйвер ядра WinDivert64.sys в папке программы!
    set MISSING=1
)
if %MISSING% neq 0 (
    echo.
    echo Нажмите любую клавишу для возврата в меню...
    pause >nul
    goto MENU
)
goto :eof

:START_STANDARD
call :CHECK_FILES
cls
echo [+] Запуск стандартного режима обхода DPI (split-pos = 2 байта)...
echo [i] UDP трафик (FACEIT Anti-Cheat, CS2, Discord) аппаратно изолирован.
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
echo [+] Запуск агрессивного режима обхода DPI (split-pos = 1 байт)...
echo [i] Эффективен против глубоких эвристик ТСПУ и замедлений.
echo [i] Для остановки нажмите Ctrl + C в окне утилиты.
echo.
dpi_bypass.exe --split-pos 1 --config config.ini
echo.
echo Служба завершила работу.
pause
goto MENU

:EDIT_CONFIG
if not exist "config.ini" (
    echo [!] Файл config.ini не найден. Создание шаблона...
    (
        echo [General]
        echo split_position = 2
        echo target_ports = 80, 443
        echo isolate_game_udp = true
    ) > config.ini
)
start notepad.exe "config.ini"
goto MENU

:STOP_ALL
cls
echo [*] Завершение активных процессов dpi_bypass.exe...
taskkill /F /IM dpi_bypass.exe >nul 2>&1
net stop "WinDivert" >nul 2>&1
net stop "WinDivert1.4" >nul 2>&1
echo [OK] Все процессы и драйверы WinDivert успешно остановлены.
timeout /t 2 >nul
goto MENU

:EXIT_APP
exit /b
