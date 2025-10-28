#ifndef WIFI_CONFIGURATION_H
#define WIFI_CONFIGURATION_H

#include <string>
#include <functional>
#include <vector>
#include "wifi_connection_manager.h"
#include "wifi_configuration_ap.h"
#ifdef CONFIG_BT_NIMBLE_ENABLED
#include "wifi_configuration_ble.h"
#endif

// Event types for WiFi configuration
enum class WifiConfigEvent {
    CONFIG_PACKET_RECEIVED,    // When configuration packet is received
    CONFIG_FAILED,             // When configuration fails
};

// Callback function type for WiFi configuration events
using WifiConfigCallback = std::function<void(WifiConfigEvent, const std::string&)>;

class WifiConfiguration {
public:
    static WifiConfiguration& GetInstance();

    void Initialize(const std::string& product_key, const std::string& ssid_prefix = "XPG-GAgent");
    void SetLanguage(const std::string& language);
    
    // Register callback for WiFi configuration events
    void RegisterCallback(WifiConfigCallback callback);
    
    // Notify all registered callbacks about an event
    void NotifyEvent(WifiConfigEvent event, const std::string& message = "");

private:
    WifiConfiguration() = default;
    ~WifiConfiguration() = default;
    WifiConfiguration(const WifiConfiguration&) = delete;
    WifiConfiguration& operator=(const WifiConfiguration&) = delete;

    std::vector<WifiConfigCallback> callbacks_;
};

#endif // WIFI_CONFIGURATION_H
