#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace kfdpi {

struct RuntimeConfig {
    int split_position{2};
    bool disorder{false};
    bool drop_quic{true};
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
    const RuntimeConfig& Get() const { return config_; }
    std::string BuildFilter() const;

private:
    ConfigManager() = default;
    RuntimeConfig config_;
};

} // namespace kfdpi
