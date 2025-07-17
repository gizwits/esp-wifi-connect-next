#include "wifi_connection_manager.h"
#include "ssid_manager.h"
#include <string.h>
#include <nvs_flash.h>
#include <algorithm> // Added for std::sort
#define NVS_NAMESPACE "wifi"
#define MAX_WIFI_SCAN_SSID_COUNT 30

const char* WifiConnectionManager::TAG = "WifiConnectionManager";

// C++ 类实现
WifiConnectionManager& WifiConnectionManager::GetInstance() {
    static WifiConnectionManager instance;
    return instance;
}

esp_err_t WifiConnectionManager::InitializeWiFi() {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        esp_wifi_deinit();
        return ret;
    }
    return esp_wifi_start();
}

WifiConnectionManager::WifiConnectionManager() 
    : event_group_(xEventGroupCreate())
    , is_connecting_(false)
    , scan_timer_(nullptr) {
    
    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                      ESP_EVENT_ANY_ID,
                                                      &WifiConnectionManager::WifiEventHandler,
                                                      this,
                                                      &instance_any_id_));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                      IP_EVENT_STA_GOT_IP,
                                                      &WifiConnectionManager::IpEventHandler,
                                                      this,
                                                      &instance_got_ip_));
    StartScanTimer();
}

WifiConnectionManager::~WifiConnectionManager() {
    StopScanTimer();
    if (event_group_) {
        vEventGroupDelete(event_group_);
    }
    if (instance_any_id_) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id_);
    }
    if (instance_got_ip_) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip_);
    }
    // Stop and deinit WiFi
    esp_wifi_stop();
    esp_wifi_deinit();
}

void WifiConnectionManager::StartScanTimer() {
    if (scan_timer_) return;
    esp_timer_create_args_t timer_args = {
        .callback = &WifiConnectionManager::ScanTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "wifi_scan_timer",
        .skip_unhandled_events = true
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &scan_timer_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(scan_timer_, 5 * 1000000)); // 5秒周期

    esp_wifi_scan_start(nullptr, false);
}

void WifiConnectionManager::StopScanTimer() {
    if (scan_timer_) {
        esp_timer_stop(scan_timer_);
        esp_timer_delete(scan_timer_);
        scan_timer_ = nullptr;
    }
}

void WifiConnectionManager::ScanTimerCallback(void* arg) {
    auto* self = static_cast<WifiConnectionManager*>(arg);
    if (!self->is_connecting_) {
        esp_wifi_scan_start(nullptr, false);
    }
}

bool WifiConnectionManager::Connect(const std::string& ssid, const std::string& password) {
    if (ssid.empty()) {
        ESP_LOGE(TAG, "SSID cannot be empty");
        return false;
    }
    
    if (ssid.length() > 32) {
        ESP_LOGE(TAG, "SSID too long");
        return false;
    }
    
    is_connecting_ = true;
    xEventGroupClearBits(event_group_, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));
    bzero(&wifi_config, sizeof(wifi_config));
    strcpy((char *)wifi_config.sta.ssid, ssid.c_str());
    strcpy((char *)wifi_config.sta.password, password.c_str());
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.failure_retry_cnt = 1;
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    int retry_count = 0;
    const int max_retries = 5;
    bool connected = false;
    while (retry_count < max_retries && !connected) {
        esp_err_t ret = esp_wifi_connect();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_connect() failed: %d", ret);
            retry_count++;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        ESP_LOGI(TAG, "Connecting to WiFi %s (try %d/%d)", ssid.c_str(), retry_count + 1, max_retries);

        // 等待连接结果
        EventBits_t bits = xEventGroupWaitBits(event_group_, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "Connected to WiFi %s", ssid.c_str());
            connected = true;
            break;
        } else if (bits & WIFI_FAIL_BIT) {
            ESP_LOGE(TAG, "Failed to connect to WiFi %s (try %d/%d)", ssid.c_str(), retry_count + 1, max_retries);
        } else {
            ESP_LOGE(TAG, "Connection timeout for WiFi %s (try %d/%d)", ssid.c_str(), retry_count + 1, max_retries);
        }
        retry_count++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    is_connecting_ = false;
    return connected;
}

void WifiConnectionManager::Disconnect() {
    esp_wifi_disconnect();
}

bool WifiConnectionManager::IsConnected() const {
    return (xEventGroupGetBits(event_group_) & WIFI_CONNECTED_BIT) != 0;
}

void WifiConnectionManager::SaveCredentials(const std::string& ssid, const std::string& password) {
    ESP_LOGI(TAG, "Save SSID %s", ssid.c_str());
    SsidManager::GetInstance().AddSsid(ssid, password);
}

void WifiConnectionManager::WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    WifiConnectionManager* self = static_cast<WifiConnectionManager*>(arg);
    if (event_id == WIFI_EVENT_STA_CONNECTED) {
        xEventGroupSetBits(self->event_group_, WIFI_CONNECTED_BIT);
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(self->event_group_, WIFI_FAIL_BIT);
    } else if (event_id == WIFI_EVENT_SCAN_DONE) {
        // 新增：保存扫描到的所有 SSID，按 rssi 降序排序
        uint16_t ap_num = 0;
        std::vector<std::string> scan_ssid_list;
        if (esp_wifi_scan_get_ap_num(&ap_num) == ESP_OK && ap_num > 0) {
            std::vector<wifi_ap_record_t> ap_records(ap_num);
            if (esp_wifi_scan_get_ap_records(&ap_num, ap_records.data()) == ESP_OK) {
                // 按 rssi 降序排序
                std::sort(ap_records.begin(), ap_records.end(), [](const wifi_ap_record_t& a, const wifi_ap_record_t& b) {
                    return a.rssi > b.rssi;
                });
                // 只保留前 MAX_WIFI_SCAN_SSID_COUNT 个
                int count = std::min<int>(ap_num, MAX_WIFI_SCAN_SSID_COUNT);
                for (int i = 0; i < count; ++i) {
                    std::string ssid(reinterpret_cast<char*>(ap_records[i].ssid));
                    scan_ssid_list.push_back(ssid);
                    // ESP_LOGI(TAG, "Scanned SSID: %s RSSI: %d", ssid.c_str(), ap_records[i].rssi);
                }
            }
        }
        SsidManager::GetInstance().ScanSsidList(scan_ssid_list);
    }
}


void WifiConnectionManager::SaveUid(const std::string& uid) {
    // 如果 uid 有效，保存到 NVS 并设置 need_activation flag
    if (!uid.empty()) {
        nvs_handle_t nvs_handle;
        ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle));
        
        // 保存 uid
        ESP_ERROR_CHECK(nvs_set_str(nvs_handle, "uid", uid.c_str()));
        
        // 设置 need_activation flag
        int32_t need_activation = 1;
        ESP_ERROR_CHECK(nvs_set_i32(nvs_handle, "need_activation", need_activation));
        
        ESP_ERROR_CHECK(nvs_commit(nvs_handle));
        nvs_close(nvs_handle);
        
        ESP_LOGI(TAG, "Saved uid: %s and set need_activation flag", uid.c_str());
    }
}

void WifiConnectionManager::IpEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    WifiConnectionManager* self = static_cast<WifiConnectionManager*>(arg);
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(self->event_group_, WIFI_CONNECTED_BIT);
    }
} 