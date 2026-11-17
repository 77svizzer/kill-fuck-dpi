#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>
#include "windivert.h"

namespace kfdpi {

enum class IpVersion : uint8_t {
    Unknown,
    IPv4,
    IPv6
};

enum class TransportProtocol : uint8_t {
    Unknown,
    TCP,
    UDP
};

enum class L7Protocol : uint8_t {
    Unknown,
    TLS_ClientHello,
    HTTP_Request,
    QUIC_Initial,
    Discord_Voice_UDP
};

struct ParsedPacket {
    IpVersion ip_ver{IpVersion::Unknown};
    TransportProtocol transport{TransportProtocol::Unknown};
    L7Protocol l7{L7Protocol::Unknown};

    PWINDIVERT_IPHDR ip_hdr{nullptr};
    PWINDIVERT_IPV6HDR ip6_hdr{nullptr};
    PWINDIVERT_TCPHDR tcp_hdr{nullptr};
    PWINDIVERT_UDPHDR udp_hdr{nullptr};

    uint8_t* payload{nullptr};
    size_t payload_len{0};
    size_t header_len{0};

    uint16_t src_port{0};
    uint16_t dst_port{0};
    bool is_valid{false};

    // Точечная маршрутизация и L7-данные для обхода ТСПУ
    std::string hostname;        // Извлеченный SNI или HTTP Host (в нижнем регистре)
    size_t sni_offset{0};        // Смещение начала имени SNI от начала полезной нагрузки
    size_t sni_length{0};        // Длина имени хоста в байтах
    size_t midsld_offset{0};     // Смещение середины домена 2-го уровня (для split midsld)
    bool is_discord_voice{false};// Признак голосового UDP пакета Discord (порты 50000-65535)
};

class ProtocolParser {
public:
    static bool Parse(uint8_t* packet_buf, size_t packet_len, ParsedPacket& out);
    static bool IsTlsClientHello(const uint8_t* payload, size_t len);
    static bool IsGamingPort(uint16_t port);

private:
    static bool ParseTlsSni(const uint8_t* payload, size_t len, ParsedPacket& out);
    static bool ParseHttpHost(const uint8_t* payload, size_t len, ParsedPacket& out);
    static void CalculateMidSld(ParsedPacket& out);
};

} // namespace kfdpi
