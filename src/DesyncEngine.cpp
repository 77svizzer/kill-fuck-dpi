#include "DesyncEngine.hpp"
#include <cstring>
#include <iostream>

namespace kfdpi {

// Валидный минимальный TLS 1.3 ClientHello с SNI "www.google.com" (RFC 8446 / RFC 6066)
static const uint8_t FAKE_TLS_CLIENT_HELLO[] = {
    0x16, 0x03, 0x01, 0x00, 0xba, // Record Header: Handshake, TLS 1.0 (compat), length 186
    0x01, 0x00, 0x00, 0xb6,       // Handshake Header: ClientHello, length 182
    0x03, 0x03,                   // Client Version: TLS 1.2 (0x0303)
    // Random (32 bytes):
    0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x00,                         // Session ID length: 0
    0x00, 0x04,                   // Cipher Suites length: 4
    0x13, 0x01,                   // TLS_AES_128_GCM_SHA256
    0x13, 0x02,                   // TLS_AES_256_GCM_SHA384
    0x01, 0x00,                   // Compression Methods length: 1, 0x00 (null)
    0x00, 0x89,                   // Extensions length: 137
    // Extension: server_name (0x0000)
    0x00, 0x00, 0x00, 0x13,       // Type 0, length 19
    0x00, 0x11,                   // Server Name list length: 17
    0x00,                         // Name type: host_name (0)
    0x00, 0x0e,                   // Name length: 14 ("www.google.com")
    'w','w','w','.','g','o','o','g','l','e','.','c','o','m',
    // Extension: supported_versions (0x002b)
    0x00, 0x2b, 0x00, 0x03, 0x02, 0x03, 0x04,
    // Extension: signature_algorithms (0x000d)
    0x00, 0x0d, 0x00, 0x08, 0x00, 0x06, 0x04, 0x03, 0x05, 0x03, 0x06, 0x03,
    // Extension: supported_groups (0x000a)
    0x00, 0x0a, 0x00, 0x04, 0x00, 0x02, 0x00, 0x1d, // X25519
    // Extension: key_share (0x0033)
    0x00, 0x33, 0x00, 0x26, 0x00, 0x24, 0x00, 0x1d, 0x00, 0x20, // X25519 (32 bytes key)
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
};

// Валидный заголовок STUN Binding Request для Discord Voice UDP (RFC 5389)
static const uint8_t FAKE_STUN_REQUEST[] = {
    0x00, 0x01,             // Message Type: Binding Request (0x0001)
    0x00, 0x00,             // Message Length: 0
    0x21, 0x12, 0xa4, 0x42, // Magic Cookie: 0x2112A442
    // Transaction ID (12 bytes):
    0x70, 0x6b, 0x74, 0x66, 0x61, 0x6b, 0x65, 0x73, 0x74, 0x75, 0x6e, 0x01
};

void DesyncEngine::FixChecksums(uint8_t* packet_data, size_t len, WINDIVERT_ADDRESS& addr) {
    if (!packet_data || len < sizeof(WINDIVERT_IPHDR)) {
        return;
    }

    // Защитная синхронизация L3: гарантируем совпадение длины заголовка с размером буфера
    uint8_t version = (packet_data[0] >> 4) & 0x0F;
    if (version == 4) {
        auto* ip = reinterpret_cast<PWINDIVERT_IPHDR>(packet_data);
        ip->Length = htons(static_cast<uint16_t>(len));
    } else if (version == 6 && len >= sizeof(WINDIVERT_IPV6HDR)) {
        auto* ip6 = reinterpret_cast<PWINDIVERT_IPV6HDR>(packet_data);
        ip6->Length = htons(static_cast<uint16_t>(len - sizeof(WINDIVERT_IPV6HDR)));
    }

    PWINDIVERT_IPHDR ip_hdr = nullptr;
    PWINDIVERT_IPV6HDR ip6_hdr = nullptr;
    PWINDIVERT_TCPHDR tcp_hdr = nullptr;
    PWINDIVERT_UDPHDR udp_hdr = nullptr;

    BOOL parse_ok = WinDivertHelperParsePacket(
        packet_data, static_cast<UINT>(len),
        &ip_hdr, &ip6_hdr, nullptr,
        nullptr, nullptr,
        &tcp_hdr, &udp_hdr,
        nullptr, nullptr,
        nullptr, nullptr
    );

    // Двухуровневый fallback: прямое извлечение указателей при сбое парсера
    if (!parse_ok || (ip_hdr == nullptr && ip6_hdr == nullptr)) {
        if (version == 4) {
            ip_hdr = reinterpret_cast<PWINDIVERT_IPHDR>(packet_data);
        } else if (version == 6 && len >= sizeof(WINDIVERT_IPV6HDR)) {
            ip6_hdr = reinterpret_cast<PWINDIVERT_IPV6HDR>(packet_data);
        }
    }
    if (!parse_ok || (tcp_hdr == nullptr && udp_hdr == nullptr)) {
        size_t ip_len = (version == 4 && ip_hdr) ? (ip_hdr->HdrLength * 4) : sizeof(WINDIVERT_IPV6HDR);
        uint8_t proto = (version == 4 && ip_hdr) ? ip_hdr->Protocol : (ip6_hdr ? ip6_hdr->NextHdr : 0);
        if (proto == IPPROTO_TCP && len >= ip_len + sizeof(WINDIVERT_TCPHDR)) {
            tcp_hdr = reinterpret_cast<PWINDIVERT_TCPHDR>(packet_data + ip_len);
        } else if (proto == IPPROTO_UDP && len >= ip_len + sizeof(WINDIVERT_UDPHDR)) {
            udp_hdr = reinterpret_cast<PWINDIVERT_UDPHDR>(packet_data + ip_len);
        }
    }

    if (ip_hdr != nullptr) {
        ip_hdr->Checksum = 0;
    }
    if (tcp_hdr != nullptr) {
        tcp_hdr->Checksum = 0;
    }
    if (udp_hdr != nullptr) {
        udp_hdr->Checksum = 0;
    }

    // Вычисляем 100% реальные контрольные суммы прямо в буфер пакета (nullptr вместо &addr)
    WinDivertHelperCalcChecksums(packet_data, static_cast<UINT>(len), nullptr, 0);

    // Сбрасываем флаги NDIS оффлоада, чтобы сетевая карта отправляла готовые суммы без сбоев
    addr.IPChecksum = 0;
    addr.TCPChecksum = 0;
    addr.UDPChecksum = 0;
}

void DesyncEngine::ApplyBadChecksum(uint8_t* packet_data, size_t len, WINDIVERT_ADDRESS& addr) {
    if (!packet_data || len < sizeof(WINDIVERT_IPHDR)) {
        return;
    }

    uint8_t version = (packet_data[0] >> 4) & 0x0F;
    if (version == 4) {
        auto* ip = reinterpret_cast<PWINDIVERT_IPHDR>(packet_data);
        ip->Length = htons(static_cast<uint16_t>(len));
    } else if (version == 6 && len >= sizeof(WINDIVERT_IPV6HDR)) {
        auto* ip6 = reinterpret_cast<PWINDIVERT_IPV6HDR>(packet_data);
        ip6->Length = htons(static_cast<uint16_t>(len - sizeof(WINDIVERT_IPV6HDR)));
    }

    PWINDIVERT_IPHDR ip_hdr = nullptr;
    PWINDIVERT_IPV6HDR ip6_hdr = nullptr;
    PWINDIVERT_TCPHDR tcp_hdr = nullptr;

    BOOL parse_ok = WinDivertHelperParsePacket(
        packet_data, static_cast<UINT>(len),
        &ip_hdr, &ip6_hdr, nullptr, nullptr, nullptr,
        &tcp_hdr, nullptr, nullptr, nullptr, nullptr, nullptr
    );

    if (!parse_ok || (ip_hdr == nullptr && ip6_hdr == nullptr)) {
        if (version == 4) {
            ip_hdr = reinterpret_cast<PWINDIVERT_IPHDR>(packet_data);
        } else if (version == 6 && len >= sizeof(WINDIVERT_IPV6HDR)) {
            ip6_hdr = reinterpret_cast<PWINDIVERT_IPV6HDR>(packet_data);
        }
    }
    if (!parse_ok || tcp_hdr == nullptr) {
        size_t ip_len = (version == 4 && ip_hdr) ? (ip_hdr->HdrLength * 4) : sizeof(WINDIVERT_IPV6HDR);
        if (len >= ip_len + sizeof(WINDIVERT_TCPHDR)) {
            tcp_hdr = reinterpret_cast<PWINDIVERT_TCPHDR>(packet_data + ip_len);
        }
    }

    // 1. Рассчитываем валидную сумму для IP прямо в буфер
    if (ip_hdr != nullptr) {
        ip_hdr->Checksum = 0;
    }
    WinDivertHelperCalcChecksums(packet_data, static_cast<UINT>(len), nullptr, WINDIVERT_HELPER_NO_TCP_CHECKSUM);

    // 2. Искажаем TCP сумму
    if (tcp_hdr != nullptr) {
        tcp_hdr->Checksum = htons(0xDEAD);
    }

    addr.IPChecksum = 0;
    addr.TCPChecksum = 0;
}

PacketFragment DesyncEngine::CreateFakeTcpPacket(
    uint8_t* raw_packet,
    const ParsedPacket& parsed,
    const WINDIVERT_ADDRESS& addr
) {
    PacketFragment fake;
    size_t header_len = parsed.header_len;
    size_t fake_payload_len = sizeof(FAKE_TLS_CLIENT_HELLO);
    size_t total_fake_len = header_len + fake_payload_len;

    fake.buffer.resize(total_fake_len);
    // Клонируем заголовки оригинального соединения (IP + TCP)
    memcpy(fake.buffer.data(), raw_packet, header_len);
    // Подставляем фейковый TLS ClientHello
    memcpy(fake.buffer.data() + header_len, FAKE_TLS_CLIENT_HELLO, fake_payload_len);
    fake.address = addr;
    fake.is_fake = true;

    // ОБЯЗАТЕЛЬНО: Предварительно обновляем длину IP/IPv6 в заголовке нового буфера ДО вызова парсера,
    // иначе WinDivertHelperParsePacket бракует пакет из-за длины оригинального пакета (e.g. 517 > 186)
    if (parsed.ip_ver == IpVersion::IPv4 || parsed.ip_hdr != nullptr) {
        auto* ip_pre = reinterpret_cast<PWINDIVERT_IPHDR>(fake.buffer.data());
        ip_pre->Length = htons(static_cast<uint16_t>(total_fake_len));
    } else if (parsed.ip_ver == IpVersion::IPv6 || parsed.ip6_hdr != nullptr) {
        auto* ip6_pre = reinterpret_cast<PWINDIVERT_IPV6HDR>(fake.buffer.data());
        ip6_pre->Length = htons(static_cast<uint16_t>(total_fake_len - sizeof(WINDIVERT_IPV6HDR)));
    }

    PWINDIVERT_IPHDR ip = nullptr;
    PWINDIVERT_IPV6HDR ip6 = nullptr;
    PWINDIVERT_TCPHDR tcp = nullptr;
    BOOL parse_ok = WinDivertHelperParsePacket(
        fake.buffer.data(), static_cast<UINT>(total_fake_len),
        &ip, &ip6, nullptr, nullptr, nullptr,
        &tcp, nullptr, nullptr, nullptr, nullptr, nullptr
    );

    // Прямой fallback по смещениям на случай нестандартных пакетов
    if (!parse_ok || (ip == nullptr && ip6 == nullptr) || tcp == nullptr) {
        if (parsed.ip_ver == IpVersion::IPv4 || parsed.ip_hdr != nullptr) {
            ip = reinterpret_cast<PWINDIVERT_IPHDR>(fake.buffer.data());
        } else if (parsed.ip_ver == IpVersion::IPv6 || parsed.ip6_hdr != nullptr) {
            ip6 = reinterpret_cast<PWINDIVERT_IPV6HDR>(fake.buffer.data());
        }
        if (parsed.tcp_hdr != nullptr) {
            size_t tcp_offset = reinterpret_cast<const uint8_t*>(parsed.tcp_hdr) - raw_packet;
            if (tcp_offset + sizeof(WINDIVERT_TCPHDR) <= total_fake_len) {
                tcp = reinterpret_cast<PWINDIVERT_TCPHDR>(fake.buffer.data() + tcp_offset);
            }
        }
    }

    if (ip != nullptr) {
        ip->Length = htons(static_cast<uint16_t>(total_fake_len));
        if (config_.fooling & FoolingMethod::AutoTTL) {
            ip->TTL = config_.fake_ttl;
        }
    } else if (ip6 != nullptr) {
        ip6->Length = htons(static_cast<uint16_t>(total_fake_len - sizeof(WINDIVERT_IPV6HDR)));
        if (config_.fooling & FoolingMethod::AutoTTL) {
            ip6->HopLimit = config_.fake_ttl;
        }
    }

    if (tcp != nullptr && parsed.tcp_hdr != nullptr) {
        if (config_.fooling & FoolingMethod::BadSeq) {
            // BadSeq: смещаем номер последовательности на 10000 байт назад
            // Целевой сервер (Google/Discord) отбрасывает фейк как out-of-window дубликат, а ТСПУ принимает
            uint32_t original_seq = ntohl(parsed.tcp_hdr->SeqNum);
            tcp->SeqNum = htonl(original_seq - 10000);
        } else {
            tcp->SeqNum = parsed.tcp_hdr->SeqNum;
        }
        tcp->Psh = 0;
    }

    if (config_.fooling & FoolingMethod::BadSum) {
        ApplyBadChecksum(fake.buffer.data(), total_fake_len, fake.address);
    } else {
        FixChecksums(fake.buffer.data(), total_fake_len, fake.address);
    }

    return fake;
}

bool DesyncEngine::Process(
    uint8_t* raw_packet,
    size_t packet_len,
    const WINDIVERT_ADDRESS& addr,
    const ParsedPacket& parsed,
    std::vector<PacketFragment>& out_fragments
) {
    out_fragments.clear();

    // 1. Игровой трафик (CS2, FACEIT, Xplay: 27000-27100) — всегда прямой пропуск 1-в-1 с 0ms задержкой
    if (ProtocolParser::IsGamingPort(parsed.dst_port) || ProtocolParser::IsGamingPort(parsed.src_port)) {
        PacketFragment frag;
        frag.buffer.assign(raw_packet, raw_packet + packet_len);
        frag.address = addr;
        out_fragments.push_back(std::move(frag));
        return true;
    }

    // 2. Определение профиля ресурса через HostlistManager
    ProfileType profile = ProfileType::General;
    if (config_.use_hostlists) {
        profile = HostlistManager::Instance().MatchProfile(
            parsed.hostname,
            parsed.dst_port,
            parsed.transport,
            parsed.is_discord_voice
        );
    }

    // Если ресурс в белом списке исключений или играх — пропускаем без модификаций
    if (profile == ProfileType::DirectBypass) {
        PacketFragment frag;
        frag.buffer.assign(raw_packet, raw_packet + packet_len);
        frag.address = addr;
        out_fragments.push_back(std::move(frag));
        return true;
    }

    // 3. Обработка UDP трафика
    if (parsed.transport == TransportProtocol::UDP) {
        // Сброс QUIC на порту 443 для перехода браузера на HTTP/2
        if (parsed.dst_port == 443 && config_.drop_quic) {
            return false;
        }

        // Голосовой трафик Discord UDP 50000-65535
        if (parsed.is_discord_voice || profile == ProfileType::Discord) {
            return ProcessDiscordVoice(raw_packet, packet_len, addr, parsed, out_fragments);
        }

        // Любой другой UDP отправляется без изменений
        PacketFragment frag;
        frag.buffer.assign(raw_packet, raw_packet + packet_len);
        frag.address = addr;
        out_fragments.push_back(std::move(frag));
        return true;
    }

    // 4. Обработка TCP трафика
    if (parsed.transport == TransportProtocol::TCP) {
        return ProcessTcp(raw_packet, packet_len, addr, parsed, profile, out_fragments);
    }

    // Прочие протоколы
    PacketFragment frag;
    frag.buffer.assign(raw_packet, raw_packet + packet_len);
    frag.address = addr;
    out_fragments.push_back(std::move(frag));
    return true;
}

bool DesyncEngine::ProcessDiscordVoice(
    uint8_t* raw_packet,
    size_t packet_len,
    const WINDIVERT_ADDRESS& addr,
    const ParsedPacket& parsed,
    std::vector<PacketFragment>& out_fragments
) {
    if (!config_.discord_voice_fake) {
        PacketFragment frag;
        frag.buffer.assign(raw_packet, raw_packet + packet_len);
        frag.address = addr;
        out_fragments.push_back(std::move(frag));
        return true;
    }

    // Идентификатор голосовой сессии (комбинация портов)
    uint64_t session_id = (static_cast<uint64_t>(parsed.dst_port) << 32) | parsed.src_port;
    auto now = std::chrono::steady_clock::now();
    bool is_new_session = true;

    auto it = active_voice_sessions_.find(session_id);
    if (it != active_voice_sessions_.end()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
        if (elapsed < 15) {
            is_new_session = false;
        }
    }
    active_voice_sessions_[session_id] = now;

    // Очистка старых сессий каждые 100 записей
    if (active_voice_sessions_.size() > 100) {
        for (auto iter = active_voice_sessions_.begin(); iter != active_voice_sessions_.end();) {
            if (std::chrono::duration_cast<std::chrono::seconds>(now - iter->second).count() > 60) {
                iter = active_voice_sessions_.erase(iter);
            } else {
                ++iter;
            }
        }
    }

    // Для новой сессии генерируем фейковый STUN-пакет с малым TTL для пробития ТСПУ
    if (is_new_session) {
        PacketFragment fake_stun;
        size_t header_len = parsed.header_len;
        size_t stun_payload_len = sizeof(FAKE_STUN_REQUEST);
        size_t total_fake_len = header_len + stun_payload_len;

        fake_stun.buffer.resize(total_fake_len);
        memcpy(fake_stun.buffer.data(), raw_packet, header_len);
        memcpy(fake_stun.buffer.data() + header_len, FAKE_STUN_REQUEST, stun_payload_len);
        fake_stun.address = addr;
        fake_stun.is_fake = true;

        // Предварительное обновление длины IP/IPv6 и UDP ДО вызова WinDivertHelperParsePacket
        if (parsed.ip_ver == IpVersion::IPv4 || parsed.ip_hdr != nullptr) {
            auto* ip_pre = reinterpret_cast<PWINDIVERT_IPHDR>(fake_stun.buffer.data());
            ip_pre->Length = htons(static_cast<uint16_t>(total_fake_len));
        } else if (parsed.ip_ver == IpVersion::IPv6 || parsed.ip6_hdr != nullptr) {
            auto* ip6_pre = reinterpret_cast<PWINDIVERT_IPV6HDR>(fake_stun.buffer.data());
            ip6_pre->Length = htons(static_cast<uint16_t>(total_fake_len - sizeof(WINDIVERT_IPV6HDR)));
        }

        if (parsed.udp_hdr != nullptr) {
            size_t udp_offset = reinterpret_cast<const uint8_t*>(parsed.udp_hdr) - raw_packet;
            if (udp_offset + sizeof(WINDIVERT_UDPHDR) <= total_fake_len) {
                auto* udp_pre = reinterpret_cast<PWINDIVERT_UDPHDR>(fake_stun.buffer.data() + udp_offset);
                udp_pre->Length = htons(static_cast<uint16_t>(sizeof(WINDIVERT_UDPHDR) + stun_payload_len));
            }
        }

        PWINDIVERT_IPHDR ip = nullptr;
        PWINDIVERT_IPV6HDR ip6 = nullptr;
        PWINDIVERT_UDPHDR udp = nullptr;
        BOOL parse_ok = WinDivertHelperParsePacket(
            fake_stun.buffer.data(), static_cast<UINT>(total_fake_len),
            &ip, &ip6, nullptr, nullptr, nullptr,
            nullptr, &udp, nullptr, nullptr, nullptr, nullptr
        );

        // Fallback по смещениям
        if (!parse_ok || (ip == nullptr && ip6 == nullptr) || udp == nullptr) {
            if (parsed.ip_ver == IpVersion::IPv4 || parsed.ip_hdr != nullptr) {
                ip = reinterpret_cast<PWINDIVERT_IPHDR>(fake_stun.buffer.data());
            } else if (parsed.ip_ver == IpVersion::IPv6 || parsed.ip6_hdr != nullptr) {
                ip6 = reinterpret_cast<PWINDIVERT_IPV6HDR>(fake_stun.buffer.data());
            }
            if (parsed.udp_hdr != nullptr) {
                size_t udp_offset = reinterpret_cast<const uint8_t*>(parsed.udp_hdr) - raw_packet;
                if (udp_offset + sizeof(WINDIVERT_UDPHDR) <= total_fake_len) {
                    udp = reinterpret_cast<PWINDIVERT_UDPHDR>(fake_stun.buffer.data() + udp_offset);
                }
            }
        }

        if (ip != nullptr) {
            ip->Length = htons(static_cast<uint16_t>(total_fake_len));
            ip->TTL = config_.fake_ttl;
        } else if (ip6 != nullptr) {
            ip6->Length = htons(static_cast<uint16_t>(total_fake_len - sizeof(WINDIVERT_IPV6HDR)));
            ip6->HopLimit = config_.fake_ttl;
        }

        if (udp != nullptr) {
            udp->Length = htons(static_cast<uint16_t>(sizeof(WINDIVERT_UDPHDR) + stun_payload_len));
        }

        FixChecksums(fake_stun.buffer.data(), total_fake_len, fake_stun.address);
        out_fragments.push_back(std::move(fake_stun));
    }

    // Реальный голосовой пакет
    PacketFragment real_frag;
    real_frag.buffer.assign(raw_packet, raw_packet + packet_len);
    real_frag.address = addr;
    out_fragments.push_back(std::move(real_frag));

    return true;
}

bool DesyncEngine::ProcessTcp(
    uint8_t* raw_packet,
    size_t packet_len,
    const WINDIVERT_ADDRESS& addr,
    const ParsedPacket& parsed,
    ProfileType profile,
    std::vector<PacketFragment>& out_fragments
) {
    (void)profile;

    // Десинхронизируем TLS ClientHello и HTTP Request
    bool is_tls = (parsed.l7 == L7Protocol::TLS_ClientHello);
    bool is_http = (parsed.l7 == L7Protocol::HTTP_Request);

    if ((!is_tls && !is_http) || parsed.payload == nullptr || parsed.payload_len <= 2) {
        PacketFragment frag;
        frag.buffer.assign(raw_packet, raw_packet + packet_len);
        frag.address = addr;
        out_fragments.push_back(std::move(frag));
        return true;
    }

    // Вычисляем оптимальную позицию разделения
    size_t split_pos = config_.split_pos;
    if (config_.split_mode == SplitPositionMode::Midsld && parsed.midsld_offset > 0 && parsed.midsld_offset < parsed.payload_len) {
        split_pos = parsed.midsld_offset;
    } else if (config_.split_mode == SplitPositionMode::Sni && parsed.sni_offset > 0 && parsed.sni_offset < parsed.payload_len) {
        split_pos = parsed.sni_offset;
    } else if (split_pos == 0 || split_pos >= parsed.payload_len) {
        split_pos = (parsed.payload_len > 2) ? 2 : 1;
    }

    // Шаг 1: Формирование фейкового пакета (если стратегия fake_*)
    bool use_fake = (config_.strategy == DesyncStrategy::Fake_Split2 || config_.strategy == DesyncStrategy::Fake_Disorder);
    if (use_fake && is_tls) {
        PacketFragment fake = CreateFakeTcpPacket(raw_packet, parsed, addr);
        out_fragments.push_back(std::move(fake));
    }

    // Шаг 2: Разделение реального пакета на Seg1 и Seg2
    size_t header_len = parsed.header_len;
    size_t seg1_payload_len = split_pos;
    size_t seg2_payload_len = parsed.payload_len - split_pos;

    size_t seg1_total_len = header_len + seg1_payload_len;
    size_t seg2_total_len = header_len + seg2_payload_len;

    // --- СЕГМЕНТ 1 ---
    PacketFragment frag1;
    frag1.buffer.resize(seg1_total_len);
    memcpy(frag1.buffer.data(), raw_packet, header_len);
    memcpy(frag1.buffer.data() + header_len, parsed.payload, seg1_payload_len);
    frag1.address = addr;

    // Предварительное обновление длины IP/IPv6 ДО вызова WinDivertHelperParsePacket
    if (parsed.ip_ver == IpVersion::IPv4 || parsed.ip_hdr != nullptr) {
        auto* ip_pre = reinterpret_cast<PWINDIVERT_IPHDR>(frag1.buffer.data());
        ip_pre->Length = htons(static_cast<uint16_t>(seg1_total_len));
    } else if (parsed.ip_ver == IpVersion::IPv6 || parsed.ip6_hdr != nullptr) {
        auto* ip6_pre = reinterpret_cast<PWINDIVERT_IPV6HDR>(frag1.buffer.data());
        ip6_pre->Length = htons(static_cast<uint16_t>(seg1_total_len - sizeof(WINDIVERT_IPV6HDR)));
    }

    PWINDIVERT_IPHDR ip1 = nullptr;
    PWINDIVERT_IPV6HDR ip6_1 = nullptr;
    PWINDIVERT_TCPHDR tcp1 = nullptr;
    BOOL parse1_ok = WinDivertHelperParsePacket(
        frag1.buffer.data(), static_cast<UINT>(seg1_total_len),
        &ip1, &ip6_1, nullptr, nullptr, nullptr,
        &tcp1, nullptr, nullptr, nullptr, nullptr, nullptr
    );

    // Fallback по смещениям
    if (!parse1_ok || (ip1 == nullptr && ip6_1 == nullptr) || tcp1 == nullptr) {
        if (parsed.ip_ver == IpVersion::IPv4 || parsed.ip_hdr != nullptr) {
            ip1 = reinterpret_cast<PWINDIVERT_IPHDR>(frag1.buffer.data());
        } else if (parsed.ip_ver == IpVersion::IPv6 || parsed.ip6_hdr != nullptr) {
            ip6_1 = reinterpret_cast<PWINDIVERT_IPV6HDR>(frag1.buffer.data());
        }
        if (parsed.tcp_hdr != nullptr) {
            size_t tcp_offset = reinterpret_cast<const uint8_t*>(parsed.tcp_hdr) - raw_packet;
            if (tcp_offset + sizeof(WINDIVERT_TCPHDR) <= seg1_total_len) {
                tcp1 = reinterpret_cast<PWINDIVERT_TCPHDR>(frag1.buffer.data() + tcp_offset);
            }
        }
    }

    if (ip1 != nullptr) {
        ip1->Length = htons(static_cast<uint16_t>(seg1_total_len));
    } else if (ip6_1 != nullptr) {
        ip6_1->Length = htons(static_cast<uint16_t>(seg1_total_len - sizeof(WINDIVERT_IPV6HDR)));
    }
    if (tcp1 != nullptr) {
        tcp1->Psh = 0; // Снимаем флаг PSH с первого сегмента
    }
    FixChecksums(frag1.buffer.data(), seg1_total_len, frag1.address);

    // --- СЕГМЕНТ 2 ---
    PacketFragment frag2;
    frag2.buffer.resize(seg2_total_len);
    memcpy(frag2.buffer.data(), raw_packet, header_len);
    memcpy(frag2.buffer.data() + header_len, parsed.payload + seg1_payload_len, seg2_payload_len);
    frag2.address = addr;

    // Предварительное обновление длины IP/IPv6 ДО вызова WinDivertHelperParsePacket
    if (parsed.ip_ver == IpVersion::IPv4 || parsed.ip_hdr != nullptr) {
        auto* ip_pre = reinterpret_cast<PWINDIVERT_IPHDR>(frag2.buffer.data());
        ip_pre->Length = htons(static_cast<uint16_t>(seg2_total_len));
    } else if (parsed.ip_ver == IpVersion::IPv6 || parsed.ip6_hdr != nullptr) {
        auto* ip6_pre = reinterpret_cast<PWINDIVERT_IPV6HDR>(frag2.buffer.data());
        ip6_pre->Length = htons(static_cast<uint16_t>(seg2_total_len - sizeof(WINDIVERT_IPV6HDR)));
    }

    PWINDIVERT_IPHDR ip2 = nullptr;
    PWINDIVERT_IPV6HDR ip6_2 = nullptr;
    PWINDIVERT_TCPHDR tcp2 = nullptr;
    BOOL parse2_ok = WinDivertHelperParsePacket(
        frag2.buffer.data(), static_cast<UINT>(seg2_total_len),
        &ip2, &ip6_2, nullptr, nullptr, nullptr,
        &tcp2, nullptr, nullptr, nullptr, nullptr, nullptr
    );

    // Fallback по смещениям
    if (!parse2_ok || (ip2 == nullptr && ip6_2 == nullptr) || tcp2 == nullptr) {
        if (parsed.ip_ver == IpVersion::IPv4 || parsed.ip_hdr != nullptr) {
            ip2 = reinterpret_cast<PWINDIVERT_IPHDR>(frag2.buffer.data());
        } else if (parsed.ip_ver == IpVersion::IPv6 || parsed.ip6_hdr != nullptr) {
            ip6_2 = reinterpret_cast<PWINDIVERT_IPV6HDR>(frag2.buffer.data());
        }
        if (parsed.tcp_hdr != nullptr) {
            size_t tcp_offset = reinterpret_cast<const uint8_t*>(parsed.tcp_hdr) - raw_packet;
            if (tcp_offset + sizeof(WINDIVERT_TCPHDR) <= seg2_total_len) {
                tcp2 = reinterpret_cast<PWINDIVERT_TCPHDR>(frag2.buffer.data() + tcp_offset);
            }
        }
    }

    if (ip2 != nullptr) {
        ip2->Length = htons(static_cast<uint16_t>(seg2_total_len));
        if (parsed.ip_hdr != nullptr) {
            ip2->Id = htons(ntohs(parsed.ip_hdr->Id) + 1);
        }
    } else if (ip6_2 != nullptr) {
        ip6_2->Length = htons(static_cast<uint16_t>(seg2_total_len - sizeof(WINDIVERT_IPV6HDR)));
    }

    if (tcp2 != nullptr && parsed.tcp_hdr != nullptr) {
        uint32_t original_seq = ntohl(parsed.tcp_hdr->SeqNum);
        tcp2->SeqNum = htonl(original_seq + static_cast<uint32_t>(seg1_payload_len));
        tcp2->Psh = parsed.tcp_hdr->Psh;
    }
    FixChecksums(frag2.buffer.data(), seg2_total_len, frag2.address);

    // Размещение сегментов согласно стратегии
    if (config_.strategy == DesyncStrategy::Disorder || config_.strategy == DesyncStrategy::Fake_Disorder) {
        out_fragments.push_back(std::move(frag2));
        out_fragments.push_back(std::move(frag1));
    } else {
        out_fragments.push_back(std::move(frag1));
        out_fragments.push_back(std::move(frag2));
    }

    return true;
}

} // namespace kfdpi
