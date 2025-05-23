#pragma once

#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>

// 定义事件位
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#ifdef __cplusplus
extern "C" {
#endif

bool WifiConnectionManager_Connect(const char* ssid, const char* password);
void WifiConnectionManager_SaveCredentials(const char* ssid, const char* password);

#ifdef __cplusplus
}
#endif

class WifiConnectionManager {
public:
    static WifiConnectionManager& GetInstance();

    static esp_err_t InitializeWiFi();

    bool Connect(const std::string& ssid, const std::string& password);
    void Disconnect();
    void SaveUid(const std::string& uid);
    bool IsConnected() const;
    void SaveCredentials(const std::string& ssid, const std::string& password);

private:
    WifiConnectionManager();
    ~WifiConnectionManager();

    static void WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void IpEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

    EventGroupHandle_t event_group_;
    bool is_connecting_;
    esp_event_handler_instance_t instance_any_id_;
    esp_event_handler_instance_t instance_got_ip_;
    static const char* TAG;
}; 