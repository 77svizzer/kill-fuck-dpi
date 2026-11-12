#include "ProtocolParser.hpp"

namespace kfdpi {

bool ProtocolParser::Parse(uint8_t* packet_buf, size_t packet_len, ParsedPacket& out) {
    if (!packet_buf || packet_len < sizeof(WINDIVERT_IPHDR)) {
        return false;
    }

    out = ParsedPacket{};
    UINT payload_len = 0;
    PVOID payload_ptr = nullptr;

    BOOL ok = WinDivertHelperParsePacket(
        packet_buf, static_cast<UINT>(packet_len),
        &out.ip_hdr, &out.ip6_hdr, nullptr,
        nullptr, nullptr,
        &out.tcp_hdr, &out.udp_hdr,
        &payload_ptr, &payload_len,
        nullptr, nullptr
    );

    if (!ok) {
        return false;
    }

    if (out.ip_hdr != nullptr) {
        out.ip_ver = IpVersion::IPv4;
    } else if (out.ip6_hdr != nullptr) {
        out.ip_ver = IpVersion::IPv6;
    } else {
        return false;
    }

    out.payload = static_cast<uint8_t*>(payload_ptr);
    out.payload_len = static_cast<size_t>(payload_len);
    if (out.payload != nullptr) {
        out.header_len = out.payload - packet_buf;
    } else {
        out.header_len = packet_len;
    }

    if (out.tcp_hdr != nullptr) {
        out.transport = TransportProtocol::TCP;
        out.src_port = ntohs(out.tcp_hdr->SrcPort);
        out.dst_port = ntohs(out.tcp_hdr->DstPort);

        if (out.payload_len > 0 && IsTlsClientHello(out.payload, out.payload_len)) {
            out.l7 = L7Protocol::TLS_ClientHello;
        } else if (out.payload_len >= 4 && (
                   memcmp(out.payload, "GET ", 4) == 0 ||
                   memcmp(out.payload, "POST", 4) == 0 ||
                   memcmp(out.payload, "HEAD", 4) == 0)) {
            out.l7 = L7Protocol::HTTP_Request;
        }
    } else if (out.udp_hdr != nullptr) {
        out.transport = TransportProtocol::UDP;
        out.src_port = ntohs(out.udp_hdr->SrcPort);
        out.dst_port = ntohs(out.udp_hdr->DstPort);

        if (out.dst_port == 443) {
            out.l7 = L7Protocol::QUIC_Initial;
        }
    }

    out.is_valid = true;
    return true;
}

bool ProtocolParser::IsTlsClientHello(const uint8_t* payload, size_t len) {
    if (!payload || len < 6) return false;
    // Record Layer: 0x16 (Handshake), Version: 0x03, 0x01/02/03
    if (payload[0] != 0x16 || payload[1] != 0x03) return false;
    // Handshake Layer: Type 0x01 (ClientHello)
    return (payload[5] == 0x01);
}

bool ProtocolParser::IsGamingPort(uint16_t port) {
    // CS2, FACEIT, Steam Matchmaking & Xplay ports: 27000-27100
    return (port >= 27000 && port <= 27100);
}

} // namespace kfdpi
