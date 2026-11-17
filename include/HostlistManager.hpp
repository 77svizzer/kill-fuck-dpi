#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <cstdint>
#include "ProtocolParser.hpp"

namespace kfdpi {

enum class ProfileType {
    DirectBypass,   // Пропуск 1-в-1 без изменений (Игры, исключения, российские сервисы)
    YouTube,        // Профиль YouTube 4K: Fake TLS + badsum/autottl + midsld split + QUIC drop
    Discord,        // Профиль Discord: Fake STUN для Voice UDP 50000-65535, Fake TLS для Gateway
    General         // Общий профиль для заблокированных сайтов: Fake TLS + badsum + midsld/sni split
};

class HostlistManager {
public:
    static HostlistManager& Instance() {
        static HostlistManager inst;
        return inst;
    }

    // Загрузка списков из директории (по умолчанию "lists")
    bool LoadLists(const std::string& lists_dir = "lists");

    // Определение профиля обработки для пакета
    ProfileType MatchProfile(
        const std::string& hostname,
        uint16_t dst_port,
        TransportProtocol transport,
        bool is_discord_voice
    ) const;

    bool IsHostInList(const std::string& host, const std::vector<std::string>& list) const;

    size_t GetGoogleCount() const { return list_google_.size(); }
    size_t GetGeneralCount() const { return list_general_.size(); }
    size_t GetGamingCount() const { return list_gaming_.size(); }
    size_t GetExcludeCount() const { return list_exclude_.size(); }
    bool IsLoaded() const { return loaded_; }

private:
    HostlistManager() = default;

    void LoadFile(const std::string& filepath, std::vector<std::string>& target);
    static bool MatchDomain(const std::string& host, const std::string& rule);

    std::vector<std::string> list_google_;
    std::vector<std::string> list_general_;
    std::vector<std::string> list_gaming_;
    std::vector<std::string> list_exclude_;
    bool loaded_{false};
};

} // namespace kfdpi
