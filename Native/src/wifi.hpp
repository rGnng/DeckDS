#pragma once

#include <optional>
#include <string>
#include <vector>

struct WifiNetwork {
    std::string id, name;
    enum class Security { Open, Personal, Sae, Other } security = Security::Other;
    int strength = 0;
    bool connected = false;
    std::string ssidBytes{};
};

struct WifiResult {
    std::vector<WifiNetwork> networks{};
    std::string message{};
    bool error = false;
};

struct WifiConnection {
    std::string name, id;
    std::optional<int> strength;
};

bool isWifiConnected();
bool wifiSetupSupported();
std::optional<WifiConnection> activeWifi();
WifiResult scanWifi();
WifiResult connectWifi(const WifiNetwork& network, const std::string& password);
