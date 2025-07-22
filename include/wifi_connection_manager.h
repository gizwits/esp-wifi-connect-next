#pragma once

#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_timer.h>

// 定义事件位
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t WifiConnectionManager_Connect(const char* ssid, const char* password);
void WifiConnectionManager_SaveCredentials(const char* ssid, const char* password);

#ifdef __cplusplus
}
#endif

class WifiConnectionManager {
public:
    static WifiConnectionManager& GetInstance();

    static esp_err_t InitializeWiFi();

    esp_err_t Connect(const std::string& ssid, const std::string& password);
    void Disconnect();
    void SaveUid(const std::string& uid);
    bool IsConnected() const;
    void SaveCredentials(const std::string& ssid, const std::string& password);

private:
    WifiConnectionManager();
    ~WifiConnectionManager();

    static void WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void IpEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    void StartScanTimer();
    void StopScanTimer();
    static void ScanTimerCallback(void* arg);
    static const char* GetDisconnectReasonString(wifi_err_reason_t reason);

    EventGroupHandle_t event_group_;
    bool is_connecting_;
    esp_event_handler_instance_t instance_any_id_;
    esp_event_handler_instance_t instance_got_ip_;
    esp_timer_handle_t scan_timer_ = nullptr;
    
    // 错误统计相关
    struct {
        esp_err_t error;
        wifi_err_reason_t disconnect_reason;
        int count;
        int last_occurrence;
        bool is_disconnect_error;
    } error_stats_[10];
    int error_stats_count_;
    int current_retry_count_;
    
    static const char* TAG;
    
}; 