#pragma once

#include <vector>
#include <cstdint>
#include <unordered_map>
#include <chrono>
#include "ProtocolParser.hpp"
#include "HostlistManager.hpp"

namespace kfdpi {

struct PacketFragment {
    std::vector<uint8_t> buffer;
    WINDIVERT_ADDRESS address{};
    bool is_fake{false};
};

enum class DesyncStrategy : uint8_t {
    Split,
    Disorder,
    Fake_Split2,
    Fake_Disorder
};

enum class SplitPositionMode : uint8_t {
    Fixed,
    Sni,
    Midsld
};

enum class FoolingMethod : uint8_t {
    None = 0,
    BadSum = 1 << 0,
    AutoTTL = 1 << 1,
    BadSum_AutoTTL = (1 << 0) | (1 << 1)
};

inline FoolingMethod operator|(FoolingMethod a, FoolingMethod b) {
    return static_cast<FoolingMethod>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline bool operator&(FoolingMethod a, FoolingMethod b) {
    return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
}

struct DesyncConfig {
    DesyncStrategy strategy{DesyncStrategy::Fake_Split2};
    SplitPositionMode split_mode{SplitPositionMode::Midsld};
    size_t split_pos{2};
    FoolingMethod fooling{FoolingMethod::BadSum_AutoTTL};
    uint8_t fake_ttl{6};
    bool drop_quic{true};
    bool discord_voice_fake{true};
    bool use_hostlists{true};
};

class DesyncEngine {
public:
    explicit DesyncEngine(const DesyncConfig& cfg = {}) : config_(cfg) {}

    void SetConfig(const DesyncConfig& cfg) { config_ = cfg; }
    const DesyncConfig& GetConfig() const { return config_; }

    // Обработка пакета.
    // Если возвращает false: пакет нужно отбросить (например, QUIC).
    // Если true: out_fragments содержит пакеты для отправки.
    bool Process(
        uint8_t* raw_packet,
        size_t packet_len,
        const WINDIVERT_ADDRESS& addr,
        const ParsedPacket& parsed,
        std::vector<PacketFragment>& out_fragments
    );

    static void FixChecksums(uint8_t* packet_data, size_t len, WINDIVERT_ADDRESS& addr);
    static void ApplyBadChecksum(uint8_t* packet_data, size_t len, WINDIVERT_ADDRESS& addr);

private:
    bool ProcessTcp(
        uint8_t* raw_packet,
        size_t packet_len,
        const WINDIVERT_ADDRESS& addr,
        const ParsedPacket& parsed,
        ProfileType profile,
        std::vector<PacketFragment>& out_fragments
    );

    bool ProcessDiscordVoice(
        uint8_t* raw_packet,
        size_t packet_len,
        const WINDIVERT_ADDRESS& addr,
        const ParsedPacket& parsed,
        std::vector<PacketFragment>& out_fragments
    );

    PacketFragment CreateFakeTcpPacket(
        uint8_t* raw_packet,
        const ParsedPacket& parsed,
        const WINDIVERT_ADDRESS& addr
    );

    DesyncConfig config_;
    // Кэш активных сессий Discord Voice для отправки фейкового STUN только при старте
    std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> active_voice_sessions_;
};

} // namespace kfdpi
