#pragma once

#include <windows.h>
#include <string>
#include <functional>

namespace kfdpi {

class ServiceController {
public:
    static bool IsAdmin();
    static bool Elevate(int argc, char* argv[]);

    static bool Install(const std::string& name, const std::string& display_name, const std::string& exe_path);
    static bool Uninstall(const std::string& name);
    static bool Start(const std::string& name);
    static bool Stop(const std::string& name);

    static int RunAsService(const std::string& name, std::function<void()> worker_func, std::function<void()> stop_func);
};

} // namespace kfdpi
