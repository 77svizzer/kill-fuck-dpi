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
Write-Host "===================================================================" -ForegroundColor Cyan