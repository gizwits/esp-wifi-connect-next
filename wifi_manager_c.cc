#include "wifi_manager_c.h"
#include "wifi_connection_manager.h"
#include "ssid_manager.h"
#include <string>

extern "C" {

bool WifiConnectionManager_Connect(const char* ssid, const char* password) {
    return WifiConnectionManager::GetInstance().Connect(
        std::string(ssid), 
        std::string(password)
    );
}

void WifiConnectionManager_SaveCredentials(const char* ssid, const char* password) {
    WifiConnectionManager::GetInstance().SaveCredentials(
        std::string(ssid), 
        std::string(password)
    );
}

void WifiConnectionManager_SaveUid(const char* uid) {
    WifiConnectionManager::GetInstance().SaveUid(std::string(uid));
}

} // extern "C" 