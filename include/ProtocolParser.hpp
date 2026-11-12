#pragma once

#include <cstdint>
#include <string_view>
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
    QUIC_Initial
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
};

class ProtocolParser {
public:
    static bool Parse(uint8_t* packet_buf, size_t packet_len, ParsedPacket& out);
    static bool IsTlsClientHello(const uint8_t* payload, size_t len);
    static bool IsGamingPort(uint16_t port);
};

} // namespace kfdpi
