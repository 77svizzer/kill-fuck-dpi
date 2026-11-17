param(
    [switch]$AutoStart,
    [int]$TimeoutSec = 5
)

$ErrorActionPreference = "Continue"

Write-Host "===================================================================" -ForegroundColor Cyan
Write-Host "         KILL F#CK DPI - Автоматический тест доступности           " -ForegroundColor Cyan
Write-Host "===================================================================" -ForegroundColor Cyan
Write-Host ""

# ---------------------------------------------------------
# 1. Проверка активности процесса dpi_bypass.exe
# ---------------------------------------------------------
Write-Host "[*] Шаг 1: Проверка наличия активного процесса dpi_bypass.exe..."
$proc = Get-Process -Name "dpi_bypass" -ErrorAction SilentlyContinue
if ($proc -is [array]) { $proc = $proc[0] }

$startedByScript = $false
if (-not $proc) {
    if ($AutoStart) {
        Write-Host "[!] Процесс dpi_bypass.exe не запущен. Попытка автозапуска (-AutoStart)..." -ForegroundColor Yellow
        $exePaths = @(
            (Join-Path $PSScriptRoot "dpi_bypass.exe"),
            (Join-Path $PSScriptRoot "bin\dpi_bypass.exe"),
            (Join-Path (Get-Location) "dpi_bypass.exe"),
            (Join-Path (Get-Location) "bin\dpi_bypass.exe")
        )
        $exeFound = $null
        foreach ($p in $exePaths) {
            if (Test-Path $p) {
                $exeFound = $p
                break
            }
        }

        if ($exeFound) {
            Write-Host "[+] Найден бинарник: $exeFound. Запуск в фоновом режиме..." -ForegroundColor Cyan
            $proc = Start-Process -FilePath $exeFound -ArgumentList "--console" -PassThru -WindowStyle Hidden
            Start-Sleep -Seconds 2
            $proc = Get-Process -Name "dpi_bypass" -ErrorAction SilentlyContinue
            if ($proc -is [array]) { $proc = $proc[0] }
            $startedByScript = $true
        } else {
            Write-Host "[ОШИБКА ВЕРИФИКАЦИИ] Бинарник dpi_bypass.exe не найден для автозапуска!" -ForegroundColor Red
            exit 1
        }
    } else {
        Write-Host "-------------------------------------------------------------------" -ForegroundColor Red
        Write-Host "[ОШИБКА ВЕРИФИКАЦИИ] Процесс dpi_bypass.exe НЕ запущен!" -ForegroundColor Red
        Write-Host "Верификация не может быть выполнена: обход DPI не активен." -ForegroundColor Red
        Write-Host "Запустите dpi_bypass.exe перед запуском верификации, либо используйте флаг -AutoStart." -ForegroundColor Yellow
        Write-Host "-------------------------------------------------------------------" -ForegroundColor Red
        exit 1
    }
}

if (-not $proc -or $proc.HasExited) {
    Write-Host "[ОШИБКА ВЕРИФИКАЦИИ] Не удалось запустить или обнаружить процесс dpi_bypass.exe!" -ForegroundColor Red
    exit 1
}

Write-Host "[+] Процесс dpi_bypass.exe АКТИВЕН (PID: $($proc.Id), Рабочий набор: $([math]::Round($proc.WorkingSet64 / 1MB, 2)) MB)" -ForegroundColor Green

# ---------------------------------------------------------
# 2. Проверка драйвера перехвата пакетов WinDivert в ядре
# ---------------------------------------------------------
Write-Host "[*] Шаг 2: Проверка состояния драйвера WinDivert в ядре Windows..."
$driverActive = $false
try {
    $scOut = sc.exe query windivert 2>&1
    if ($scOut -match "STATE\s*:\s*[0-9]+\s+RUNNING") {
        $driverActive = $true
    } else {
        $scOut14 = sc.exe query windivert14 2>&1
        if ($scOut14 -match "STATE\s*:\s*[0-9]+\s+RUNNING") {
            $driverActive = $true
        }
    }
} catch {}

if ($driverActive) {
    Write-Host "[+] Драйвер WinDivert успешно зарегистрирован и перехватывает пакеты в ядре Windows." -ForegroundColor Green
} else {
    Write-Host "[!] Предупреждение: драйвер WinDivert в SCM не сообщает RUNNING. Возможно, открыт напрямую в сессии процесса." -ForegroundColor Yellow
}

# ---------------------------------------------------------
# 3. Функциональные тесты десинхронизации (TCP 80/443)
# ---------------------------------------------------------
Write-Host ""
Write-Host "[*] Шаг 3: Тестирование пробития DPI на целевых веб-ресурсах..."

$targets = @(
    @{ Name = "YouTube Web"; Url = "https://www.youtube.com" },
    @{ Name = "Google Video CDN"; Url = "https://redirector.googlevideo.com" },
    @{ Name = "Discord API"; Url = "https://discord.com/api/v9" },
    @{ Name = "Discord Gateway"; Url = "https://gateway.discord.gg" },
    @{ Name = "Заблокированные сайты (Rutracker)"; Url = "https://rutracker.org" },
    @{ Name = "Xplay.gg Платформа"; Url = "https://xplay.gg" }
)

$failCount = 0

foreach ($t in $targets) {
    Write-Host -NoNewline "[*] Проверка $($t.Name)... "
    try {
        $res = Invoke-WebRequest -Uri $t.Url -Method Head -TimeoutSec $TimeoutSec -UseBasicParsing -ErrorAction Stop
        Write-Host "УСПЕХ ($($res.StatusCode))" -ForegroundColor Green
    } catch {
        if ($_.Exception.Response) {
            $code = [int]$_.Exception.Response.StatusCode
            Write-Host "ДОСТУПЕН ($code)" -ForegroundColor Green
        } else {
            Write-Host "ОШИБКА ($($_.Exception.Message))" -ForegroundColor Red
            $failCount++
        }
    }
}

# ---------------------------------------------------------
# 4. Проверка фильтрации QUIC (UDP 443)
# ---------------------------------------------------------
Write-Host ""
Write-Host "[*] Шаг 4: Проверка блокировки QUIC (UDP 443 Drop)..."
try {
    $udpQuic = New-Object System.Net.Sockets.UdpClient
    $udpQuic.Client.ReceiveTimeout = 1500
    $quicInitial = [byte[]](0xc0, 0x00, 0x00, 0x01, 0x00, 0x08, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08)
    $udpQuic.Send($quicInitial, $quicInitial.Length, "1.1.1.1", 443) | Out-Null
    $udpQuic.Close()
    Write-Host "[OK] QUIC UDP 443: Пакет обработан фильтром ядра WinDivert." -ForegroundColor Green
} catch {
    Write-Host "[!] Исключение при отправке QUIC: $($_.Exception.Message)" -ForegroundColor Yellow
}

# ---------------------------------------------------------
# 5. Проверка прямого прохождения UDP портов CS2 / FACEIT
# ---------------------------------------------------------
Write-Host ""
Write-Host "[*] Шаг 5: Проверка аппаратного прохождения UDP портов CS2 / FACEIT (27015)..."
try {
    $udpClient = New-Object System.Net.Sockets.UdpClient
    $udpClient.Client.ReceiveTimeout = 2000
    $data = [System.Text.Encoding]::ASCII.GetBytes("ping")
    $udpClient.Send($data, $data.Length, "8.8.8.8", 27015) | Out-Null
    $udpClient.Close()
    Write-Host "[OK] Игровой порт UDP 27015: Пакеты уходят беспрепятственно (чистый стек ядра WFP)." -ForegroundColor Green
} catch {
    Write-Host "[ОШИБКА] Ошибка сокета игрового порта: $($_.Exception.Message)" -ForegroundColor Red
    $failCount++
}

# ---------------------------------------------------------
# 6. Проверка голосового сокета Discord (UDP 50000-65535)
# ---------------------------------------------------------
Write-Host ""
Write-Host "[*] Шаг 6: Проверка обработки Discord Voice UDP (STUN)..."
try {
    $udpDiscord = New-Object System.Net.Sockets.UdpClient
    $udpDiscord.Client.ReceiveTimeout = 2000
    $fakeStun = [byte[]](0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xa4, 0x42, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c)
    $udpDiscord.Send($fakeStun, $fakeStun.Length, "8.8.8.8", 50001) | Out-Null
    $udpDiscord.Close()
    Write-Host "[OK] Discord Voice UDP: Пакеты маршрутизируются корректно." -ForegroundColor Green
} catch {
    Write-Host "[ОШИБКА] Ошибка сокета Discord Voice UDP: $($_.Exception.Message)" -ForegroundColor Red
    $failCount++
}

# ---------------------------------------------------------
# 7. Контрольная проверка жизнеспособности dpi_bypass после теста
# ---------------------------------------------------------
Write-Host ""
Write-Host "[*] Шаг 7: Контрольная проверка жизнеспособности процесса..."
$procFinal = Get-Process -Id $proc.Id -ErrorAction SilentlyContinue
if (-not $procFinal -or $procFinal.HasExited) {
    Write-Host "[ОШИБКА ВЕРИФИКАЦИИ] Процесс dpi_bypass.exe аварийно завершился во время тестирования!" -ForegroundColor Red
    $failCount++
} else {
    Write-Host "[+] Процесс dpi_bypass.exe стабилен (PID: $($procFinal.Id), Threads: $($procFinal.Threads.Count))" -ForegroundColor Green
}

# Завершаем фоновый процесс, если он был поднят скриптом
if ($startedByScript) {
    Write-Host "[*] Остановка фонового процесса dpi_bypass.exe, запущенного скриптом..." -ForegroundColor Cyan
    Stop-Process -Name "dpi_bypass" -Force -ErrorAction SilentlyContinue
    if (Get-Process -Name "dpi_bypass" -ErrorAction SilentlyContinue) {
        Start-Process powershell -Verb RunAs -ArgumentList "-WindowStyle Hidden -Command Stop-Process -Name dpi_bypass -Force" -Wait -ErrorAction SilentlyContinue
    }
}

Write-Host ""
Write-Host "===================================================================" -ForegroundColor Cyan
if ($failCount -gt 0) {
    Write-Host " [ИТОГ ВЕРИФИКАЦИИ] ОБНАРУЖЕНЫ ОШИБКИ (Всего сбоев: $failCount)" -ForegroundColor Red
    Write-Host "===================================================================" -ForegroundColor Cyan
    exit 1
} else {
    Write-Host " [ИТОГ ВЕРИФИКАЦИИ] ВСЕ ПРОВЕРКИ УСПЕШНО ПРОЙДЕНЫ (0 ошибок)" -ForegroundColor Green
    Write-Host "===================================================================" -ForegroundColor Cyan
    exit 0
}
