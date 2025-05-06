#ifndef _WIFI_CONFIGURATION_AP_H_
#define _WIFI_CONFIGURATION_AP_H_

#include <string>
#include <vector>
#include <mutex>

#include <esp_http_server.h>
#include <esp_event.h>
#include <esp_timer.h>
#include <esp_netif.h>
#include <esp_wifi_types_generic.h>

#include "dns_server.h"


struct WifiConfigData {
    std::string ssid;
    std::string password;
    std::string uid;
    uint8_t flag;
    uint16_t cmd;
};

class WifiConfigurationAp {
public:
    static WifiConfigurationAp& GetInstance();
    void SetSsidPrefix(const std::string &&ssid_prefix);
    void SetLanguage(const std::string &&language);
    void Start();
    void Stop();
    void StartSmartConfig();

    std::string GetSsid();
    std::string GetWebServerUrl();

    // Delete copy constructor and assignment operator
    WifiConfigurationAp(const WifiConfigurationAp&) = delete;
    WifiConfigurationAp& operator=(const WifiConfigurationAp&) = delete;
    void SetShouldRedirect(bool should_redirect) { should_redirect_ = should_redirect; }
    bool GetShouldRedirect() const { return should_redirect_; } 

private:
    void StartUdpServer();
    void UdpServerTask(void* arg);
    static void UdpServerTaskWrapper(void* arg);
    int tcp_server_socket_ = -1;
    TaskHandle_t tcp_server_task_ = nullptr; 
    // Private constructor
    WifiConfigurationAp();
    bool ParseWifiConfig(const uint8_t* data, size_t len, WifiConfigData& config);
    ~WifiConfigurationAp();
    bool should_redirect_ = false;  // Default to true for backward compatibility
    std::mutex mutex_;
    DnsServer dns_server_;
    httpd_handle_t server_ = NULL;
    EventGroupHandle_t event_group_;
    std::string ssid_prefix_;
    std::string language_;
    esp_event_handler_instance_t instance_any_id_;
    esp_event_handler_instance_t instance_got_ip_;
    esp_timer_handle_t scan_timer_ = nullptr;
    bool is_connecting_ = false;
    esp_netif_t* ap_netif_ = nullptr;
    std::vector<wifi_ap_record_t> ap_records_;

    // 高级配置项
    std::string ota_url_;
    int8_t max_tx_power_;
    bool remember_bssid_;

    void StartAccessPoint();
    void StartWebServer();
    bool ConnectToWifi(const std::string &ssid, const std::string &password);
    void Save(const std::string &ssid, const std::string &password);

    // Event handlers
    static void WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void IpEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void SmartConfigEventHandler(void* arg, esp_event_base_t event_base, 
                                      int32_t event_id, void* event_data);
    esp_event_handler_instance_t sc_event_instance_ = nullptr;
};

#endif // _WIFI_CONFIGURATION_AP_H_
