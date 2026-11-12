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

bool ConfigManager::Load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        line = trim_str(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim_str(line.substr(0, eq));
        std::string val = trim_str(line.substr(eq + 1));

        if (key == "split_position") {
            try { config_.split_position = std::stoi(val); } catch (...) {}
        } else if (key == "disorder") {
            std::string lower = val;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            config_.disorder = (lower == "true" || lower == "1" || lower == "yes");
        } else if (key == "drop_quic") {
            std::string lower = val;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            config_.drop_quic = (lower == "true" || lower == "1" || lower == "yes");
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

std::string ConfigManager::BuildFilter() const {
    std::ostringstream ss;
    ss << "outbound and !loopback and (";

    // 1. TCP веб-порты (80, 443) с полезной нагрузкой
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

    // 3. Строгая аппаратная изоляция игровых портов (CS2, FACEIT, Xplay: UDP 27000-27100)
    ss << ") and !(udp.DstPort >= 27000 and udp.DstPort <= 27100) and !(udp.SrcPort >= 27000 and udp.SrcPort <= 27100)";

    return ss.str();
}

} // namespace kfdpi
