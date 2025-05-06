#ifndef WIFI_CONFIGURATION_H
#define WIFI_CONFIGURATION_H

#include <string>
#include "wifi_connection_manager.h"
#include "wifi_configuration_ap.h"
#ifdef CONFIG_BT_NIMBLE_ENABLED
#include "wifi_configuration_ble.h"
#endif

class WifiConfiguration {
public:
    static WifiConfiguration& GetInstance();

    void Initialize(const std::string& product_key, const std::string& ssid_prefix = "XPG-GAgent");
    void SetLanguage(const std::string& language);

private:
    WifiConfiguration() = default;
    ~WifiConfiguration() = default;
    WifiConfiguration(const WifiConfiguration&) = delete;
    WifiConfiguration& operator=(const WifiConfiguration&) = delete;
};

#endif // WIFI_CONFIGURATION_H
