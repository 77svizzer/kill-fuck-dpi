#include "DesyncEngine.hpp"
#include <cstring>

namespace kfdpi {

void DesyncEngine::FixChecksums(uint8_t* packet_data, size_t len, WINDIVERT_ADDRESS& addr) {
    PWINDIVERT_IPHDR ip_hdr = nullptr;
    PWINDIVERT_IPV6HDR ip6_hdr = nullptr;
    PWINDIVERT_TCPHDR tcp_hdr = nullptr;
    PWINDIVERT_UDPHDR udp_hdr = nullptr;

    WinDivertHelperParsePacket(
        packet_data, static_cast<UINT>(len),
        &ip_hdr, &ip6_hdr, nullptr,
        nullptr, nullptr,
        &tcp_hdr, &udp_hdr,
        nullptr, nullptr,
        nullptr, nullptr
    );

    if (ip_hdr != nullptr) {
        ip_hdr->Checksum = 0;
    }
    if (tcp_hdr != nullptr) {
        tcp_hdr->Checksum = 0;
    }
    if (udp_hdr != nullptr) {
        udp_hdr->Checksum = 0;
    }

    addr.IPChecksum = 0;
    addr.TCPChecksum = 0;
    addr.UDPChecksum = 0;

    // Передаем NULL в качестве pAddr, чтобы WinDivert вычислил реальные софтверные чексуммы
    WinDivertHelperCalcChecksums(packet_data, static_cast<UINT>(len), NULL, 0);
}

bool DesyncEngine::Process(
    uint8_t* raw_packet,
    size_t packet_len,
    const WINDIVERT_ADDRESS& addr,
    const ParsedPacket& parsed,
    std::vector<PacketFragment>& out_fragments
) {
    out_fragments.clear();

    // 1. Игровой трафик никогда не должен сюда попадать, но если попал — пропускаем 1-в-1
    if (ProtocolParser::IsGamingPort(parsed.dst_port) || ProtocolParser::IsGamingPort(parsed.src_port)) {
        PacketFragment frag;
        frag.buffer.assign(raw_packet, raw_packet + packet_len);
        frag.address = addr;
        out_fragments.push_back(std::move(frag));
        return true;
    }

    // 2. Обработка UDP 443 (QUIC)
    if (parsed.transport == TransportProtocol::UDP) {
        if (parsed.dst_port == 443 && config_.drop_quic) {
            // Дроп QUIC: возвращаем false, пакет не отправляется
            return false;
        }
        // Любой другой UDP отправляется без изменений
        PacketFragment frag;
        frag.buffer.assign(raw_packet, raw_packet + packet_len);
        frag.address = addr;
        out_fragments.push_back(std::move(frag));
        return true;
    }

    // 3. Обработка TCP
    if (parsed.transport == TransportProtocol::TCP) {
        return ProcessTcp(raw_packet, packet_len, addr, parsed, out_fragments);
    }

    // Прочие протоколы
    PacketFragment frag;
    frag.buffer.assign(raw_packet, raw_packet + packet_len);
    frag.address = addr;
    out_fragments.push_back(std::move(frag));
    return true;
}

bool DesyncEngine::ProcessTcp(
    uint8_t* raw_packet,
    size_t packet_len,
    const WINDIVERT_ADDRESS& addr,
    const ParsedPacket& parsed,
    std::vector<PacketFragment>& out_fragments
) {
    // Десинхронизируем ТОЛЬКО TLS ClientHello
    if (parsed.l7 != L7Protocol::TLS_ClientHello || parsed.payload == nullptr || parsed.payload_len <= config_.split_pos) {
        PacketFragment frag;
        frag.buffer.assign(raw_packet, raw_packet + packet_len);
        frag.address = addr;
        out_fragments.push_back(std::move(frag));
        return true;
    }

    size_t split_pos = config_.split_pos;
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

    PWINDIVERT_IPHDR ip1 = nullptr;
    PWINDIVERT_IPV6HDR ip6_1 = nullptr;
    PWINDIVERT_TCPHDR tcp1 = nullptr;
    WinDivertHelperParsePacket(
        frag1.buffer.data(), static_cast<UINT>(seg1_total_len),
        &ip1, &ip6_1, nullptr, nullptr, nullptr,
        &tcp1, nullptr, nullptr, nullptr, nullptr, nullptr
    );

    if (ip1 != nullptr) {
        ip1->Length = htons(static_cast<uint16_t>(seg1_total_len));
    } else if (ip6_1 != nullptr) {
        ip6_1->Length = htons(static_cast<uint16_t>(seg1_total_len - sizeof(WINDIVERT_IPV6HDR)));
    }
    if (tcp1 != nullptr) {
        tcp1->Psh = 0; // Первый сегмент без флага PSH
    }
    FixChecksums(frag1.buffer.data(), seg1_total_len, frag1.address);

    // --- СЕГМЕНТ 2 (содержит SNI) ---
    PacketFragment frag2;
    frag2.buffer.resize(seg2_total_len);
    memcpy(frag2.buffer.data(), raw_packet, header_len);
    memcpy(frag2.buffer.data() + header_len, parsed.payload + seg1_payload_len, seg2_payload_len);
    frag2.address = addr;

    PWINDIVERT_IPHDR ip2 = nullptr;
    PWINDIVERT_IPV6HDR ip6_2 = nullptr;
    PWINDIVERT_TCPHDR tcp2 = nullptr;
    WinDivertHelperParsePacket(
        frag2.buffer.data(), static_cast<UINT>(seg2_total_len),
        &ip2, &ip6_2, nullptr, nullptr, nullptr,
        &tcp2, nullptr, nullptr, nullptr, nullptr, nullptr
    );

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

    // Добавляем в выходной вектор согласно стратегии
    if (config_.strategy == DesyncStrategy::Disorder) {
        out_fragments.push_back(std::move(frag2));
        out_fragments.push_back(std::move(frag1));
    } else {
        out_fragments.push_back(std::move(frag1));
        out_fragments.push_back(std::move(frag2));
    }

    return true;
}

} // namespace kfdpi
