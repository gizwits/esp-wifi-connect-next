#include "wifi_manager_c.h"
#include "wifi_connection_manager.h"
#include "ssid_manager.h"
#include "wifi_configuration.h"
#include <string>

extern "C" {

esp_err_t WifiConnectionManager_Connect(const char* ssid, const char* password) {
    // Notify that WiFi connection is being attempted
    WifiConfiguration::GetInstance().NotifyEvent(WifiConfigEvent::CONFIG_PACKET_RECEIVED, 
        "Attempting to connect to WiFi: " + std::string(ssid));
    
    return WifiConnectionManager::GetInstance().Connect(
        std::string(ssid), 
        std::string(password),
        nullptr  // 不需要返回 BSSID
    );
}

esp_err_t WifiConnectionManager_ConnectWithBssid(const char* ssid, const char* password, char* bssid_out) {
    // Notify that WiFi connection is being attempted
    WifiConfiguration::GetInstance().NotifyEvent(WifiConfigEvent::CONFIG_PACKET_RECEIVED, 
        "Attempting to connect to WiFi: " + std::string(ssid));
    
    return WifiConnectionManager::GetInstance().Connect(
        std::string(ssid), 
        std::string(password),
        bssid_out
    );
}

void WifiConnectionManager_SaveCredentials(const char* ssid, const char* password) {
    WifiConnectionManager::GetInstance().SaveCredentials(
        std::string(ssid), 
        std::string(password)
    );  // 使用默认空 BSSID
}

void WifiConnectionManager_SaveCredentialsWithBssid(const char* ssid, const char* password, const char* bssid) {
    WifiConnectionManager::GetInstance().SaveCredentials(
        std::string(ssid), 
        std::string(password),
        std::string(bssid ? bssid : "")
    );
}

void WifiConnectionManager_SaveUid(const char* uid) {
    WifiConnectionManager::GetInstance().SaveUid(std::string(uid));
}

} // extern "C" 