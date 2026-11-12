#pragma once

#include <vector>
#include <cstdint>
#include "ProtocolParser.hpp"

namespace kfdpi {

struct PacketFragment {
    std::vector<uint8_t> buffer;
    WINDIVERT_ADDRESS address{};
};

enum class DesyncStrategy {
    Split,
    Disorder
};

struct DesyncConfig {
    DesyncStrategy strategy{DesyncStrategy::Split};
    size_t split_pos{2};
    bool drop_quic{true};
};

class DesyncEngine {
public:
    explicit DesyncEngine(const DesyncConfig& cfg = {}) : config_(cfg) {}

    void SetConfig(const DesyncConfig& cfg) { config_ = cfg; }
    const DesyncConfig& GetConfig() const { return config_; }

    // Обработка пакета.
    // Если возвращает false: пакет нужно отбросить (например, QUIC).
    // Если true: out_fragments содержит 1 пакет (без изменений) или 2 пакета (десинхронизированные).
    bool Process(
        uint8_t* raw_packet,
        size_t packet_len,
        const WINDIVERT_ADDRESS& addr,
        const ParsedPacket& parsed,
        std::vector<PacketFragment>& out_fragments
    );

    static void FixChecksums(uint8_t* packet_data, size_t len, WINDIVERT_ADDRESS& addr);

private:
    bool ProcessTcp(
        uint8_t* raw_packet,
        size_t packet_len,
        const WINDIVERT_ADDRESS& addr,
        const ParsedPacket& parsed,
        std::vector<PacketFragment>& out_fragments
    );

    DesyncConfig config_;
};

} // namespace kfdpi
