#include "ConfigManager.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace kfdpi {

static inline std::string trim_str(const std::string& s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, (last - first + 1));
}

static inline std::string to_lower_str(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool ConfigManager::Load(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.config_file = path;
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        line = trim_str(line);
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = to_lower_str(trim_str(line.substr(0, eq)));
        std::string val = trim_str(line.substr(eq + 1));
        std::string val_lower = to_lower_str(val);

        if (key == "strategy") {
            config_.strategy = val_lower;
        } else if (key == "split_position") {
            config_.split_position = val_lower;
        } else if (key == "fooling") {
            config_.fooling = val_lower;
        } else if (key == "fake_ttl") {
            try { config_.fake_ttl = std::stoi(val); } catch (...) {}
        } else if (key == "drop_quic") {
            config_.drop_quic = (val_lower == "true" || val_lower == "1" || val_lower == "yes");
        } else if (key == "discord_voice_fake") {
            config_.discord_voice_fake = (val_lower == "true" || val_lower == "1" || val_lower == "yes");
        } else if (key == "use_hostlists") {
            config_.use_hostlists = (val_lower == "true" || val_lower == "1" || val_lower == "yes");
        } else if (key == "lists_dir") {
            config_.lists_dir = val;
        } else if (key == "target_ports") {
            std::stringstream ss(val);
            std::string port_str;
            std::vector<uint16_t> ports;
            while (std::getline(ss, port_str, ',')) {
                try {
                    int p = std::stoi(trim_str(port_str));
                    if (p > 0 && p <= 65535) ports.push_back(static_cast<uint16_t>(p));
                } catch (...) {}
            }
            if (!ports.empty()) config_.target_ports = ports;
        }
    }
    return true;
}

RuntimeConfig ConfigManager::Get() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void ConfigManager::SetPreset(const std::string& strategy, const std::string& fooling, bool persist_to_disk) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.strategy = strategy;
    config_.fooling = fooling;

    if (persist_to_disk && !config_.config_file.empty()) {
        std::ifstream in(config_.config_file);
        if (in.is_open()) {
            std::vector<std::string> lines;
            std::string line;
            while (std::getline(in, line)) {
                std::string trimmed = trim_str(line);
                if (!trimmed.empty() && trimmed[0] != '#' && trimmed[0] != ';' && trimmed[0] != '[') {
                    size_t eq = trimmed.find('=');
                    if (eq != std::string::npos) {
                        std::string key = to_lower_str(trim_str(trimmed.substr(0, eq)));
                        if (key == "strategy") {
                            lines.push_back("strategy = " + strategy);
                            continue;
                        } else if (key == "fooling") {
                            lines.push_back("fooling = " + fooling);
                            continue;
                        }
                    }
                }
                lines.push_back(line);
            }
            in.close();

            std::ofstream out(config_.config_file, std::ios::trunc);
            if (out.is_open()) {
                for (const auto& l : lines) {
                    out << l << "\n";
                }
            }
        }
    }
}

std::string ConfigManager::BuildFilter() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream ss;
    ss << "outbound and !loopback and (";

    // 1. TCP целевые порты (80, 443) с полезной нагрузкой
    ss << "(tcp and (";
    for (size_t i = 0; i < config_.target_ports.size(); ++i) {
        if (i > 0) ss << " or ";
        ss << "tcp.DstPort == " << config_.target_ports[i];
    }
    ss << ") and tcp.PayloadLength > 0)";

    // 2. UDP 443 (QUIC drop)
    if (config_.drop_quic) {
        ss << " or (udp and udp.DstPort == 443)";
    }

    // 3. UDP 50000-65535 (Discord Voice)
    if (config_.discord_voice_fake) {
        ss << " or (udp and udp.DstPort >= 50000 and udp.DstPort <= 65535 and udp.PayloadLength > 0)";
    }

    // 4. Закрываем общее условие фильтрации.
    // Игровой трафик (CS2, FACEIT, Xplay: UDP 27000-27100) аппаратно изолирован в ядре (не попадает в порты выше).
    ss << ")";

    return ss.str();
}

} // namespace kfdpi
