#include "wifi.hpp"

#if defined(__linux__)
#include <filesystem>
#include <fstream>
#endif

bool isWifiConnected()
{
#if defined(__linux__)
    namespace fs = std::filesystem;
    std::error_code directoryError;
    for (fs::directory_iterator it("/sys/class/net", directoryError), end;
         !directoryError && it != end; it.increment(directoryError)) {
        const auto& path = it->path();
        std::error_code interfaceError;
        if (!fs::exists(path / "phy80211", interfaceError) && !fs::is_directory(path / "wireless", interfaceError))
            continue;
        int carrier = 0;
        std::ifstream link(path / "carrier");
        if (link >> carrier && carrier == 1)
            return true;
    }
#endif
    return false;
}
