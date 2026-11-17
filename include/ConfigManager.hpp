#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <mutex>

namespace kfdpi {

struct RuntimeConfig {
    std::string strategy{"disorder"};      // disorder (рекомендуется для ТСПУ РФ), fake_split2, fake_disorder, split2
    std::string split_position{"2"};       // 2 (отсекает 0x16 0x03 от тела SNI), midsld, sni
    std::string fooling{"badseq,autottl"}; // badseq, autottl, badseq,autottl, badsum, none
    int fake_ttl{4};                       // TTL для фейковых пакетов (3-5 хопов)
    bool drop_quic{true};                  // Сброс исходящего QUIC на UDP 443
    bool discord_voice_fake{true};         // Обход голосовых каналов Discord UDP 50000-65535
    bool use_hostlists{true};              // Использование списков из папки lists/
    std::string lists_dir{"lists"};        // Путь к папке списков
    std::vector<uint16_t> target_ports{80, 443};
    std::string config_file{"config.ini"};
};

class ConfigManager {
public:
    static ConfigManager& Instance() {
        static ConfigManager inst;
        return inst;
    }

    bool Load(const std::string& path);
    RuntimeConfig Get() const;
    void SetPreset(const std::string& strategy, const std::string& fooling, bool persist_to_disk = true);
    std::string BuildFilter() const;

private:
    ConfigManager() = default;
    mutable std::mutex mutex_;
    RuntimeConfig config_;
};

} // namespace kfdpi
