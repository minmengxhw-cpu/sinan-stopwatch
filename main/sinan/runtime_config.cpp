#include "runtime_config.h"

#include <nvs.h>
#include <nvs_flash.h>

namespace sinan::runtime_config {
namespace {

constexpr char kNamespace[] = "sinan_bridge";

std::string read_string(nvs_handle_t handle, const char* key)
{
    size_t size = 0;
    if (nvs_get_str(handle, key, nullptr, &size) != ESP_OK || size < 2) return {};
    std::string value(size, '\0');
    if (nvs_get_str(handle, key, value.data(), &size) != ESP_OK) return {};
    if (!value.empty() && value.back() == '\0') value.pop_back();
    return value;
}

bool valid_uri(const std::string& uri)
{
    return uri.rfind("ws://", 0) == 0 || uri.rfind("wss://", 0) == 0;
}

}  // namespace

bool BridgeConfig::configured() const
{
    return !ssid.empty() && valid_uri(uri) && token.size() >= 32;
}

BridgeConfig load()
{
    BridgeConfig config;
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return config;
    config.ssid     = read_string(handle, "ssid");
    config.password = read_string(handle, "password");
    config.uri      = read_string(handle, "uri");
    config.token    = read_string(handle, "token");
    nvs_close(handle);
    return config;
}

bool save(const BridgeConfig& config, std::string& message)
{
    if (config.ssid.empty() || config.ssid.size() > 32) {
        message = "Wi-Fi name must be 1-32 characters";
        return false;
    }
    if (config.password.size() > 63) {
        message = "Wi-Fi password is too long";
        return false;
    }
    if (!valid_uri(config.uri) || config.uri.size() > 160) {
        message = "Mac address must start with ws:// or wss://";
        return false;
    }
    if (config.token.size() < 32 || config.token.size() > 128) {
        message = "Pairing token must be 32-128 characters";
        return false;
    }

    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        message = "Could not open device settings";
        return false;
    }
    esp_err_t result = nvs_set_str(handle, "ssid", config.ssid.c_str());
    if (result == ESP_OK) result = nvs_set_str(handle, "password", config.password.c_str());
    if (result == ESP_OK) result = nvs_set_str(handle, "uri", config.uri.c_str());
    if (result == ESP_OK) result = nvs_set_str(handle, "token", config.token.c_str());
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    if (result != ESP_OK) {
        message = "Could not save device settings";
        return false;
    }
    message = "Saved. Close this page and restart the watch.";
    return true;
}

}  // namespace sinan::runtime_config
