#include "ProtocolParser.hpp"
#include <cstring>
#include <algorithm>

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
            ParseTlsSni(out.payload, out.payload_len, out);
        } else if (out.payload_len >= 4 && (
                   memcmp(out.payload, "GET ", 4) == 0 ||
                   memcmp(out.payload, "POST", 4) == 0 ||
                   memcmp(out.payload, "HEAD", 4) == 0 ||
                   memcmp(out.payload, "PUT ", 4) == 0 ||
                   memcmp(out.payload, "CONN", 4) == 0)) {
            out.l7 = L7Protocol::HTTP_Request;
            ParseHttpHost(out.payload, out.payload_len, out);
        }
    } else if (out.udp_hdr != nullptr) {
        out.transport = TransportProtocol::UDP;
        out.src_port = ntohs(out.udp_hdr->SrcPort);
        out.dst_port = ntohs(out.udp_hdr->DstPort);

        if (out.dst_port == 443) {
            out.l7 = L7Protocol::QUIC_Initial;
        } else if (out.dst_port >= 50000 && out.dst_port <= 65535 && out.payload_len > 0) {
            out.l7 = L7Protocol::Discord_Voice_UDP;
            out.is_discord_voice = true;
        }
    }

    out.is_valid = true;
    return true;
}

bool ProtocolParser::IsTlsClientHello(const uint8_t* payload, size_t len) {
    if (!payload || len < 6) return false;
    // Record Layer: 0x16 (Handshake), Version: 0x03, 0x01..0x03
    if (payload[0] != 0x16 || payload[1] != 0x03) return false;
    // Handshake Layer: Type 0x01 (ClientHello)
    return (payload[5] == 0x01);
}

bool ProtocolParser::IsGamingPort(uint16_t port) {
    // CS2, FACEIT, Steam Matchmaking & Xplay ports: 27000-27100
    return (port >= 27000 && port <= 27100);
}

bool ProtocolParser::ParseTlsSni(const uint8_t* payload, size_t len, ParsedPacket& out) {
    // TLS Record header: 5 bytes
    // Handshake header: 4 bytes
    // Client Version: 2 bytes
    // Random: 32 bytes
    // Total min header before Session ID = 5 + 4 + 2 + 32 = 43 bytes
    if (!payload || len < 44) return false;

    if (payload[0] != 0x16 || payload[1] != 0x03) return false;
    if (payload[5] != 0x01) return false;

    size_t pos = 43; // Смещение длины Session ID
    if (pos >= len) return false;
    uint8_t session_id_len = payload[pos];
    pos += 1 + session_id_len;

    // Длина Cipher Suites (uint16_t)
    if (pos + 2 > len) return false;
    uint16_t cipher_suites_len = (static_cast<uint16_t>(payload[pos]) << 8) | payload[pos + 1];
    pos += 2 + cipher_suites_len;

    // Длина Compression Methods (uint8_t)
    if (pos + 1 > len) return false;
    uint8_t compression_len = payload[pos];
    pos += 1 + compression_len;

    // Общая длина расширений Extensions (uint16_t)
    if (pos + 2 > len) return false;
    uint16_t extensions_len = (static_cast<uint16_t>(payload[pos]) << 8) | payload[pos + 1];
    pos += 2;

    size_t extensions_end = pos + extensions_len;
    if (extensions_end > len) {
        extensions_end = len;
    }

    // Итерация по расширениям TLS (RFC 6066)
    while (pos + 4 <= extensions_end) {
        uint16_t ext_type = (static_cast<uint16_t>(payload[pos]) << 8) | payload[pos + 1];
        uint16_t ext_len = (static_cast<uint16_t>(payload[pos + 2]) << 8) | payload[pos + 3];
        pos += 4;

        if (pos + ext_len > extensions_end) break;

        if (ext_type == 0x0000) { // server_name (SNI)
            if (ext_len >= 5) {
                // Server Name List Length (uint16_t)
                uint16_t sn_list_len = (static_cast<uint16_t>(payload[pos]) << 8) | payload[pos + 1];
                (void)sn_list_len;
                size_t sn_pos = pos + 2;
                if (sn_pos < pos + ext_len) {
                    uint8_t name_type = payload[sn_pos];
                    if (name_type == 0x00 && sn_pos + 3 <= pos + ext_len) { // host_name
                        uint16_t name_len = (static_cast<uint16_t>(payload[sn_pos + 1]) << 8) | payload[sn_pos + 2];
                        sn_pos += 3;
                        if (sn_pos + name_len <= pos + ext_len && name_len > 0) {
                            out.sni_offset = sn_pos;
                            out.sni_length = name_len;
                            out.hostname.assign(reinterpret_cast<const char*>(payload + sn_pos), name_len);
                            // Приводим домен к нижнему регистру для регистронезависимого сопоставления
                            for (char& c : out.hostname) {
                                if (c >= 'A' && c <= 'Z') c = c + ('a' - 'A');
                            }
                            CalculateMidSld(out);
                            return true;
                        }
                    }
                }
            }
        }
        pos += ext_len;
    }

    return false;
}

bool ProtocolParser::ParseHttpHost(const uint8_t* payload, size_t len, ParsedPacket& out) {
    if (!payload || len < 10) return false;
    std::string_view data(reinterpret_cast<const char*>(payload), len);

    size_t host_hdr_pos = data.find("\nHost:");
    size_t prefix_len = 6;
    if (host_hdr_pos == std::string_view::npos) {
        host_hdr_pos = data.find("\nhost:");
    }
    if (host_hdr_pos == std::string_view::npos) {
        if (data.rfind("Host:", 0) == 0) {
            host_hdr_pos = 0;
            prefix_len = 5;
        } else if (data.rfind("host:", 0) == 0) {
            host_hdr_pos = 0;
            prefix_len = 5;
        } else {
            return false;
        }
    }

    size_t val_start = host_hdr_pos + prefix_len;
    while (val_start < len && (data[val_start] == ' ' || data[val_start] == '\t')) {
        val_start++;
    }

    size_t val_end = val_start;
    while (val_end < len && data[val_end] != '\r' && data[val_end] != '\n' && data[val_end] != ':') {
        val_end++;
    }

    if (val_end > val_start) {
        out.sni_offset = val_start;
        out.sni_length = val_end - val_start;
        out.hostname = std::string(data.substr(val_start, out.sni_length));
        for (char& c : out.hostname) {
            if (c >= 'A' && c <= 'Z') c = c + ('a' - 'A');
        }
        CalculateMidSld(out);
        return true;
    }

    return false;
}

void ProtocolParser::CalculateMidSld(ParsedPacket& out) {
    if (out.hostname.empty() || out.sni_offset == 0) {
        out.midsld_offset = 2;
        return;
    }

    const std::string& h = out.hostname;
    std::vector<size_t> dots;
    for (size_t i = 0; i < h.size(); ++i) {
        if (h[i] == '.') dots.push_back(i);
    }

    if (dots.empty()) {
        out.midsld_offset = out.sni_offset + (out.sni_length / 2);
        return;
    }

    size_t sld_start = 0;
    size_t sld_end = dots.back();

    if (dots.size() == 1) {
        // Например: "youtube.com" -> SLD "youtube"
        sld_start = 0;
        sld_end = dots[0];
    } else {
        // Проверка составных доменов верхнего уровня (например, .com.ru, .co.uk)
        bool compound_tld = false;
        if (dots.size() >= 2) {
            std::string tld_part = h.substr(dots[dots.size() - 2]);
            if (tld_part == ".com.ru" || tld_part == ".net.ru" || tld_part == ".org.ru" ||
                tld_part == ".co.uk" || tld_part == ".co.jp" || tld_part == ".com.ua") {
                compound_tld = true;
            }
        }

        if (compound_tld && dots.size() >= 3) {
            sld_start = dots[dots.size() - 3] + 1;
            sld_end = dots[dots.size() - 2];
        } else {
            sld_start = dots[dots.size() - 2] + 1;
            sld_end = dots.back();
        }
    }

    if (sld_end > sld_start) {
        size_t sld_len = sld_end - sld_start;
        size_t sld_mid = sld_start + (sld_len / 2);
        out.midsld_offset = out.sni_offset + sld_mid;
    } else {
        out.midsld_offset = out.sni_offset + (out.sni_length / 2);
    }

    // Защита от выхода за границы пакета
    if (out.midsld_offset == 0 || out.midsld_offset >= out.payload_len) {
        out.midsld_offset = out.sni_offset > 0 ? out.sni_offset : 2;
    }
}

} // namespace kfdpi
