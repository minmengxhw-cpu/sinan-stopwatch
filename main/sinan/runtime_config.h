#pragma once

#include <string>

namespace sinan::runtime_config {

struct BridgeConfig {
    std::string ssid;
    std::string password;
    std::string uri;
    std::string token;

    bool configured() const;
};

BridgeConfig load();
bool save(const BridgeConfig& config, std::string& message);

}  // namespace sinan::runtime_config
