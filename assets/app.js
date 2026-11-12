// KILL F#CK DPI - Interactive Interface Engine & Motion Layer
document.addEventListener('DOMContentLoaded', () => {
  // Activate motion layer
  document.documentElement.classList.add('js');

  // Intersection Observer for scroll reveal animations
  const revealElements = document.querySelectorAll('[data-reveal]');
  if ('IntersectionObserver' in window) {
    const observer = new IntersectionObserver((entries) => {
      entries.forEach(entry => {
        if (entry.isIntersecting) {
          entry.target.classList.add('is-in');
          observer.unobserve(entry.target);
        }
      });
    }, {
      rootMargin: '0px 0px -40px 0px',
      threshold: 0.1
    });

    revealElements.forEach(el => observer.observe(el));
  } else {
    revealElements.forEach(el => el.classList.add('is-in'));
  }

  // Fragment 1: Packet Simulator
  const tabs = document.querySelectorAll('.tab-btn');
  const codeDisplay = document.getElementById('packet-code');
  const metricPing = document.getElementById('metric-ping');
  const metricLoss = document.getElementById('metric-loss');
  const metricAction = document.getElementById('metric-action');

  const modes = {
    youtube: {
      ping: '14 ms',
      loss: '0%',
      action: 'TLS Split + QUIC Drop',
      code: `[OUTBOUND TCP:443] TLS 1.3 ClientHello (512 bytes)
 -> Десинхронизация: Split at offset 2 + Disorder
 -> Fragment 1: TCP Seq 1..2 (2 bytes)
 -> Fragment 2: TCP Seq 3..512 (510 bytes, SNI: googlevideo.com)
 -> Статус инъекции: Пакеты доставлены с перерасчетом RFC 793 Checksum
[OUTBOUND UDP:443] QUIC Packet Detected
 -> Принудительный сброс: Браузер переведен на HTTP/2 (4K 60fps доступен)`
    },
    cs2: {
      ping: '0.00 ms',
      loss: '0%',
      action: 'NDIS Pass-Through',
      code: `[OUTBOUND UDP:27015] Valve Source 2 SNP Protocol
 -> Проверка BPF фильтра: Исключение портов 27000-27100
 -> Аппаратный обход: Драйвер WinDivert пропускает пакет без перехвата
 -> Задержка ядра: 0 мс (чистый стек Windows Filtering Platform)
 -> Античит / Replay Window: 100% целостность пакетов без модификаций`
    },
    discord: {
      ping: '22 ms',
      loss: '0%',
      action: 'Gateway Desync',
      code: `[OUTBOUND TCP:443] Discord Gateway Handshake
 -> Десинхронизация TLS рукопожатия: Обход ТСПУ блокировки
 -> Голосовые порты UDP: Прямая маршрутизация WFP
 -> Статус: Подключение к голосовым каналам и тексту стабильно`
    },
    blocked: {
      ping: '28 ms',
      loss: '0%',
      action: 'Disorder Bypass',
      code: `[OUTBOUND TCP:443] Rutracker.org / Заблокированные ресурсы
 -> TLS ClientHello сегментирован и отправлен в обратном порядке
 -> ТСПУ DPI пропускает сессию без блокирующего TCP RST
 -> Ответ сервера: HTTP/2 200 OK получен напрямую`
    }
  };

  if (tabs.length && codeDisplay) {
    tabs.forEach(btn => {
      btn.addEventListener('click', () => {
        tabs.forEach(t => t.classList.remove('active'));
        btn.classList.add('active');

        const target = btn.dataset.target;
        const data = modes[target];
        if (data) {
          codeDisplay.textContent = data.code;
          metricPing.textContent = data.ping;
          metricLoss.textContent = data.loss;
          metricAction.textContent = data.action;
        }
      });
    });
  }

  // Fragment 2: GUI Mock Toggle Switch
  const guiToggleBtn = document.getElementById('gui-mock-toggle');
  const guiStatusBanner = document.getElementById('gui-mock-status');
  if (guiToggleBtn && guiStatusBanner) {
    let isActive = true;
    guiToggleBtn.addEventListener('click', () => {
      isActive = !isActive;
      if (isActive) {
        guiStatusBanner.textContent = 'Статус: Обход АКТИВЕН (YouTube 4K, Discord, Сайты)';
        guiToggleBtn.textContent = 'Остановить обход DPI';
      } else {
        guiStatusBanner.textContent = 'Статус: Обход отключен (чистый стек Windows)';
        guiToggleBtn.textContent = 'Включить обход DPI';
      }
    });
  }
});
