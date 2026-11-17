#include "HostlistManager.hpp"
#include <fstream>
#include <algorithm>
#include <iostream>

namespace kfdpi {

static inline std::string trim(const std::string& s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, (last - first + 1));
}

static inline std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

void HostlistManager::LoadFile(const std::string& filepath, std::vector<std::string>& target) {
    std::ifstream file(filepath);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        // Удаляем завершающие точки, если они есть
        while (!line.empty() && line.back() == '.') {
            line.pop_back();
        }

        if (!line.empty()) {
            target.push_back(to_lower(line));
        }
    }
}

bool HostlistManager::LoadLists(const std::string& lists_dir) {
    list_google_.clear();
    list_general_.clear();
    list_gaming_.clear();
    list_exclude_.clear();

    std::string dir = lists_dir;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') {
        dir += "/";
    }

    // 1. Google / YouTube
    LoadFile(dir + "list-google.txt", list_google_);
    LoadFile(dir + "list-google-user.txt", list_google_);

    // 2. Общие заблокированные сайты и сервисы
    LoadFile(dir + "list-general.txt", list_general_);
    LoadFile(dir + "list-general-user.txt", list_general_);

    // 3. Игровые домены (Steam, Counter-Strike, FACEIT)
    LoadFile(dir + "list-gaming.txt", list_gaming_);
    LoadFile(dir + "list-gaming-user.txt", list_gaming_);

    // 4. Исключения (госуслуги, банки, российские порталы)
    LoadFile(dir + "list-exclude.txt", list_exclude_);
    LoadFile(dir + "list-exclude-user.txt", list_exclude_);

    loaded_ = true;
    return true;
}

bool HostlistManager::MatchDomain(const std::string& host, const std::string& rule) {
    if (rule.empty() || host.empty()) return false;

    // Поддержка префиксных правил zapret вида ^domain.com
    if (rule[0] == '^') {
        std::string clean = rule.substr(1);
        if (host == clean) return true;
        if (host.length() > clean.length()) {
            if (host.compare(0, clean.length(), clean) == 0) return true;
        }
        return false;
    }

    // Точное совпадение
    if (host == rule) return true;

    // Совпадение по суффиксу (например, *.googlevideo.com с правилом googlevideo.com)
    if (host.length() > rule.length()) {
        size_t offset = host.length() - rule.length();
        if (host.compare(offset, rule.length(), rule) == 0) {
            // Предыдущий символ должен быть точкой
            if (host[offset - 1] == '.') {
                return true;
            }
        }
    }

    return false;
}

bool HostlistManager::IsHostInList(const std::string& host, const std::vector<std::string>& list) const {
    if (host.empty() || list.empty()) return false;
    for (const auto& rule : list) {
        if (MatchDomain(host, rule)) {
            return true;
        }
    }
    return false;
}

ProfileType HostlistManager::MatchProfile(
    const std::string& hostname,
    uint16_t dst_port,
    TransportProtocol transport,
    bool is_discord_voice
) const {
    // 1. Голосовой трафик Discord UDP 50000-65535
    if (is_discord_voice || (transport == TransportProtocol::UDP && dst_port >= 50000 && dst_port <= 65535)) {
        return ProfileType::Discord;
    }

    // 2. Игровые порты (CS2, FACEIT, Xplay: 27000-27100) — всегда прямой пропуск
    if (ProtocolParser::IsGamingPort(dst_port)) {
        return ProfileType::DirectBypass;
    }

    // 3. Анализ имени хоста (SNI / HTTP Host)
    if (!hostname.empty()) {
        // Проверка белого списка исключений (банки, Госуслуги, VK, Яндекс)
        if (IsHostInList(hostname, list_exclude_)) {
            return ProfileType::DirectBypass;
        }

        // Проверка игровых сервисов (Steam, Counter-Strike, FACEIT)
        if (IsHostInList(hostname, list_gaming_)) {
            return ProfileType::DirectBypass;
        }

        // YouTube и сервисы Google
        if (IsHostInList(hostname, list_google_)) {
            return ProfileType::YouTube;
        }

        // Общий список заблокированных сайтов
        if (IsHostInList(hostname, list_general_)) {
            if (hostname.find("discord") != std::string::npos) {
                return ProfileType::Discord;
            }
            return ProfileType::General;
        }

        // Прямая проверка доменов Discord
        if (hostname == "discord.com" || hostname == "discord.gg" || hostname == "gateway.discord.gg" ||
            MatchDomain(hostname, "discordapp.com") || MatchDomain(hostname, "discordapp.net") ||
            MatchDomain(hostname, "discord.media") || MatchDomain(hostname, "discordcdn.com")) {
            return ProfileType::Discord;
        }

        // Прямая проверка доменов YouTube
        if (hostname == "youtube.com" || hostname == "youtu.be" || MatchDomain(hostname, "googlevideo.com") ||
            MatchDomain(hostname, "youtube.com") || MatchDomain(hostname, "ytimg.com")) {
            return ProfileType::YouTube;
        }
    }

    // 4. Трафик без определенного хоста или не найденный в списках
    if (transport == TransportProtocol::UDP && dst_port == 443) {
        // Сброс QUIC для YouTube
        return ProfileType::YouTube;
    }

    if (transport == TransportProtocol::TCP && (dst_port == 443 || dst_port == 80)) {
        // Если хост не в списках исключений, применяем профиль General
        return ProfileType::General;
    }

    return ProfileType::DirectBypass;
}

} // namespace kfdpi
