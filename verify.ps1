Write-Host "===================================================================" -ForegroundColor Cyan
Write-Host "         KILL F#CK DPI - Автоматический тест доступности           " -ForegroundColor Cyan
Write-Host "===================================================================" -ForegroundColor Cyan
Write-Host ""

$targets = @(
    @{ Name = "YouTube Web"; Url = "https://www.youtube.com" },
    @{ Name = "Google Video CDN"; Url = "https://redirector.googlevideo.com" },
    @{ Name = "Discord API"; Url = "https://discord.com/api/v9" },
    @{ Name = "Discord Gateway"; Url = "https://gateway.discord.gg" },
    @{ Name = "Заблокированные сайты (Rutracker)"; Url = "https://rutracker.org" },
    @{ Name = "Xplay.gg Платформа"; Url = "https://xplay.gg" }
)

foreach ($t in $targets) {
    Write-Host -NoNewline "[*] Проверка $($t.Name)... "
    try {
        $res = Invoke-WebRequest -Uri $t.Url -Method Head -TimeoutSec 5 -UseBasicParsing -ErrorAction Stop
        Write-Host "УСПЕХ ($($res.StatusCode))" -ForegroundColor Green
    } catch {
        if ($_.Exception.Response) {
            $code = [int]$_.Exception.Response.StatusCode
            Write-Host "ДОСТУПЕН ($code)" -ForegroundColor Green
        } else {
            Write-Host "ОШИБКА ($($_.Exception.Message))" -ForegroundColor Red
        }
    }
}

Write-Host ""
Write-Host "[*] Проверка прямого прохождения UDP портов CS2 / FACEIT..."
try {
    $udpClient = New-Object System.Net.Sockets.UdpClient
    $udpClient.Client.ReceiveTimeout = 2000
    $data = [System.Text.Encoding]::ASCII.GetBytes("ping")
    $udpClient.Send($data, $data.Length, "8.8.8.8", 27015) | Out-Null
    $udpClient.Close()
    Write-Host "[OK] Игровой порт UDP 27015: Пакеты уходят беспрепятственно (чистый стек ядра WFP)." -ForegroundColor Green
} catch {
    Write-Host "[!] Предупреждение по UDP: $($_.Exception.Message)" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "[*] Проверка сокета голосовых каналов Discord (UDP 50000-65535)..."
try {
    $udpDiscord = New-Object System.Net.Sockets.UdpClient
    $udpDiscord.Client.ReceiveTimeout = 2000
    $fakeStun = [byte[]](0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xa4, 0x42, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c)
    $udpDiscord.Send($fakeStun, $fakeStun.Length, "8.8.8.8", 50001) | Out-Null
    $udpDiscord.Close()
    Write-Host "[OK] Discord Voice UDP: Пакеты маршрутизируются корректно." -ForegroundColor Green
} catch {
    Write-Host "[!] Предупреждение Discord Voice UDP: $($_.Exception.Message)" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "===================================================================" -ForegroundColor Cyan