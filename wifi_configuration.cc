#include "wifi_configuration.h"

WifiConfiguration& WifiConfiguration::GetInstance() {
    static WifiConfiguration instance;
    return instance;
}

void WifiConfiguration::Initialize(const std::string& product_key, const std::string& ssid_prefix) {
    // Initialize WiFi connection manager
    WifiConnectionManager::GetInstance().InitializeWiFi();

    // Initialize AP configuration
    auto& wifi_ap = WifiConfigurationAp::GetInstance();
    wifi_ap.SetSsidPrefix(ssid_prefix);
    wifi_ap.Start();

#if defined(CONFIG_BT_NIMBLE_ENABLED)
    // Initialize BLE configuration only if NimBLE is enabled
    auto& ble_config = WifiConfigurationBle::getInstance();
    ble_config.init(product_key);
#endif
}

void WifiConfiguration::SetLanguage(const std::string &language) {
    auto& wifi_ap = WifiConfigurationAp::GetInstance();
    wifi_ap.SetLanguage(language);
}
