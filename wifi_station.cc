#include "wifi_station.h"
#include <cstring>
#include <cstdio>
#include <algorithm>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <nvs.h>
#include "nvs_flash.h"
#include <esp_netif.h>
#include <esp_system.h>
#include "ssid_manager.h"

#define TAG "wifi"
#define WIFI_EVENT_CONNECTED BIT0
#define MAX_RECONNECT_COUNT 5

// 静态变量定义
bool WifiStation::netif_initialized_ = false;

WifiStation& WifiStation::GetInstance() {
    static WifiStation instance;
    return instance;
}

WifiStation::WifiStation() {
    // Create the event group
    event_group_ = xEventGroupCreate();

    // 读取配置
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wifi", NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %d", err);
    }
    err = nvs_get_i8(nvs, "max_tx_power", &max_tx_power_);
    if (err != ESP_OK) {
        max_tx_power_ = 0;
    }
    err = nvs_get_u8(nvs, "remember_bssid", &remember_bssid_);
    if (err != ESP_OK) {
        remember_bssid_ = 0;
    }
    nvs_close(nvs);
}

WifiStation::~WifiStation() {
    vEventGroupDelete(event_group_);
}

void WifiStation::AddAuth(const std::string &&ssid, const std::string &&password) {
    auto& ssid_manager = SsidManager::GetInstance();
    ssid_manager.AddSsid(ssid, password);  // 使用默认的空 BSSID
}

void WifiStation::ClearAuth() {
    auto& ssid_manager = SsidManager::GetInstance();
    ssid_manager.Clear();
}

void WifiStation::Stop() {
    if (timer_handle_ != nullptr) {
        esp_timer_stop(timer_handle_);
        esp_timer_delete(timer_handle_);
        timer_handle_ = nullptr;
    }
    
    // 取消注册事件处理程序
    if (instance_any_id_ != nullptr) {
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id_));
        instance_any_id_ = nullptr;
    }
    if (instance_got_ip_ != nullptr) {
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip_));
        instance_got_ip_ = nullptr;
    }

    // 清除连接状态标志位
    xEventGroupClearBits(event_group_, WIFI_EVENT_CONNECTED);

    // 清理连接队列
    connect_queue_.clear();
    reconnect_count_ = 0;

    // 只停止 wifi，不销毁 netif
    ESP_ERROR_CHECK(esp_wifi_stop());
    // 注意：不调用 esp_wifi_deinit()，保持 wifi 栈初始化状态
    
    ESP_LOGI(TAG, "WifiStation stopped (wifi stack preserved)");
}

void WifiStation::OnScanBegin(std::function<void()> on_scan_begin) {
    on_scan_begin_ = on_scan_begin;
}

void WifiStation::OnConnect(std::function<void(const std::string& ssid)> on_connect) {
    on_connect_ = on_connect;
}

void WifiStation::OnConnected(std::function<void(const std::string& ssid)> on_connected) {
    on_connected_ = on_connected;
}


void WifiStation::OnScanResults(std::function<void(const std::vector<std::string>& ssids)> on_scan_results) {
    on_scan_results_ = on_scan_results;
}

void WifiStation::Start() {
    // 检查是否已经启动
    if (timer_handle_ != nullptr) {
        ESP_LOGW(TAG, "WifiStation already started");
        return;
    }

    // 清除连接状态
    xEventGroupClearBits(event_group_, WIFI_EVENT_CONNECTED);
    connect_queue_.clear();
    reconnect_count_ = 0;

    // Initialize the TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &WifiStation::WifiEventHandler,
                                                        this,
                                                        &instance_any_id_));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &WifiStation::IpEventHandler,
                                                        this,
                                                        &instance_got_ip_));

    // 只在第一次启动时创建默认的 wifi sta netif
    if (!netif_initialized_) {
        esp_netif_create_default_wifi_sta();
        netif_initialized_ = true;
        ESP_LOGI(TAG, "Created default wifi sta netif");
    }

    // 检查 wifi 栈是否已经初始化
    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err == ESP_ERR_WIFI_NOT_INIT) {
        // WiFi 栈未初始化，需要初始化
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        cfg.nvs_enable = false;
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_LOGI(TAG, "Initialized wifi stack");
    } else {
        // WiFi 栈已经初始化，只需要重新启动
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_LOGI(TAG, "Restarted existing wifi stack");
    }

    if (max_tx_power_ != 0) {
        ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(max_tx_power_));
    }

    // Setup the timer to scan WiFi
    esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            // 配置扫描参数，显示隐藏的 SSID
            wifi_scan_config_t scan_config = {
                .ssid = NULL,
                .bssid = NULL,
                .channel = 0,
                .show_hidden = true,  // 显示隐藏的 WiFi
            };
            esp_wifi_scan_start(&scan_config, false);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "WiFiScanTimer",
        .skip_unhandled_events = true
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer_handle_));
}

bool WifiStation::WaitForConnected(int timeout_ms) {
    auto bits = xEventGroupWaitBits(event_group_, WIFI_EVENT_CONNECTED, pdFALSE, pdFALSE, timeout_ms / portTICK_PERIOD_MS);
    return (bits & WIFI_EVENT_CONNECTED) != 0;
}

void WifiStation::HandleScanResult() {
    uint16_t ap_num = 0;
    esp_wifi_scan_get_ap_num(&ap_num);
    wifi_ap_record_t *ap_records = (wifi_ap_record_t *)malloc(ap_num * sizeof(wifi_ap_record_t));
    esp_wifi_scan_get_ap_records(&ap_num, ap_records);
    // sort by rssi descending
    std::sort(ap_records, ap_records + ap_num, [](const wifi_ap_record_t& a, const wifi_ap_record_t& b) {
        return a.rssi > b.rssi;
    });

    auto& ssid_manager = SsidManager::GetInstance();
    auto ssid_list = ssid_manager.GetSsidList();

    std::vector<std::string> all_ssids;

    for (int i = 0; i < ap_num; i++) {
        auto ap_record = ap_records[i];

        all_ssids.push_back((char *)ap_record.ssid);

        // 将 BSSID 转换为字符串格式以便比较
        char bssid_str[18];
        sprintf(bssid_str, "%02x:%02x:%02x:%02x:%02x:%02x",
            ap_record.bssid[0], ap_record.bssid[1], ap_record.bssid[2],
            ap_record.bssid[3], ap_record.bssid[4], ap_record.bssid[5]);
        
        // 查找匹配的 SSID 配置
        auto it = std::find_if(ssid_list.begin(), ssid_list.end(), [&ap_record, &bssid_str](const SsidItem& item) {
            // 1. 原有规则：SSID 匹配
            if (strlen((char *)ap_record.ssid) > 0 && strcmp((char *)ap_record.ssid, item.ssid.c_str()) == 0) {
                return true;
            }
            
            // 2. 新规则：对于隐藏 WiFi（SSID 为空），通过 BSSID 匹配
            if (strlen((char *)ap_record.ssid) == 0 && !item.bssid.empty() && 
                strcasecmp(bssid_str, item.bssid.c_str()) == 0) {
                ESP_LOGI(TAG, "Hidden WiFi matched by BSSID: %s", bssid_str);
                return true;
            }
            
            return false;
        });
        
        if (it != ssid_list.end()) {
            ESP_LOGI(TAG, "Found AP: %s, BSSID: %s, RSSI: %d, Channel: %d, Authmode: %d",
                strlen((char *)ap_record.ssid) > 0 ? (char *)ap_record.ssid : "[HIDDEN]", 
                bssid_str,
                ap_record.rssi, ap_record.primary, ap_record.authmode);
            WifiApRecord record = {
                .ssid = it->ssid,
                .password = it->password,
                .channel = ap_record.primary,
                .authmode = ap_record.authmode
            };
            memcpy(record.bssid, ap_record.bssid, 6);
            connect_queue_.push_back(record);
        }
    }


    // 扫描回调：返回 SSID 列表
    if (on_scan_results_ && !all_ssids.empty()) {
        
        on_scan_results_(all_ssids);
    }
    
    free(ap_records);

    if (connect_queue_.empty()) {
        ESP_LOGI(TAG, "Wait for next scan");
        esp_timer_start_once(timer_handle_, 10 * 1000 * 1000);  // 10 秒（单位是微秒）
        return;
    }

    StartConnect();
}

void WifiStation::StartConnect() {
    auto ap_record = connect_queue_.front();
    connect_queue_.erase(connect_queue_.begin());
    ssid_ = ap_record.ssid;
    password_ = ap_record.password;

    if (on_connect_) {
        on_connect_(ssid_);
    }

    wifi_config_t wifi_config;
    bzero(&wifi_config, sizeof(wifi_config));
    strcpy((char *)wifi_config.sta.ssid, ap_record.ssid.c_str());
    strcpy((char *)wifi_config.sta.password, ap_record.password.c_str());
    if (remember_bssid_) {
        wifi_config.sta.channel = ap_record.channel;
        memcpy(wifi_config.sta.bssid, ap_record.bssid, 6);
        wifi_config.sta.bssid_set = true;
    }
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    reconnect_count_ = 0;
    ESP_ERROR_CHECK(esp_wifi_connect());
}

int8_t WifiStation::GetRssi() {
    // Get station info
    wifi_ap_record_t ap_info;
    ESP_ERROR_CHECK(esp_wifi_sta_get_ap_info(&ap_info));
    return ap_info.rssi;
}

uint8_t WifiStation::GetChannel() {
    // Get station info
    wifi_ap_record_t ap_info;
    ESP_ERROR_CHECK(esp_wifi_sta_get_ap_info(&ap_info));
    return ap_info.primary;
}

bool WifiStation::IsConnected() {
    return xEventGroupGetBits(event_group_) & WIFI_EVENT_CONNECTED;
}

void WifiStation::SetPowerSaveMode(bool enabled) {
    ESP_ERROR_CHECK(esp_wifi_set_ps(enabled ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE));
}

bool WifiStation::ConnectToWifi(const std::string& ssid, const std::string& password) {
    ESP_LOGI(TAG, "Attempting temporary connection to WiFi: %s", ssid.c_str());
    
    // 检查是否已经启动
    if (timer_handle_ == nullptr) {
        ESP_LOGE(TAG, "WifiStation not started, call Start() first");
        return false;
    }
    
    // 清除连接状态
    xEventGroupClearBits(event_group_, WIFI_EVENT_CONNECTED);
    connect_queue_.clear();
    reconnect_count_ = 0;
    
    // 若正在扫描，先停止扫描，避免与连接流程冲突（STA is connecting, scan are not allowed）
    esp_err_t stop_scan_ret = esp_wifi_scan_stop();
    if (stop_scan_ret != ESP_OK && stop_scan_ret != ESP_ERR_WIFI_STATE) {
        // ESP_ERR_WIFI_STATE 表示当前未在扫描，属于可忽略状态
        ESP_LOGW(TAG, "Failed to stop wifi scan before connect: %s", esp_err_to_name(stop_scan_ret));
    }

    // 设置临时连接参数
    ssid_ = ssid;
    password_ = password;
    
    if (on_connect_) {
        on_connect_(ssid_);
    }
    
    // 配置 WiFi 连接参数
    wifi_config_t wifi_config;
    bzero(&wifi_config, sizeof(wifi_config));
    strcpy((char *)wifi_config.sta.ssid, ssid.c_str());
    strcpy((char *)wifi_config.sta.password, password.c_str());
    
    // 不设置 BSSID，让系统自动选择最佳信号
    wifi_config.sta.bssid_set = false;
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    // 开始连接
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi connection: %s", esp_err_to_name(ret));
        return false;
    }
    
    ESP_LOGI(TAG, "WiFi connection started for: %s", ssid.c_str());
    return true;
}

bool WifiStation::ConnectToWifiAndWait(const std::string& ssid, const std::string& password, int timeout_ms) {
    ESP_LOGI(TAG, "Attempting temporary connection to WiFi: %s (with timeout: %d ms)", ssid.c_str(), timeout_ms);
    
    // 先尝试连接
    if (!ConnectToWifi(ssid, password)) {
        return false;
    }
    
    // 等待连接结果
    bool connected = WaitForConnected(timeout_ms);
    if (connected) {
        ESP_LOGI(TAG, "Successfully connected to WiFi: %s", ssid.c_str());
    } else {
        ESP_LOGW(TAG, "Failed to connect to WiFi: %s (timeout: %d ms)", ssid.c_str(), timeout_ms);
    }
    
    return connected;
}

// Static event handler functions
void WifiStation::WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    auto* this_ = static_cast<WifiStation*>(arg);
    if (event_id == WIFI_EVENT_STA_START) {
        // 配置扫描参数，显示隐藏的 SSID
        wifi_scan_config_t scan_config = {
            .ssid = NULL,
            .bssid = NULL,
            .channel = 0,
            .show_hidden = true,  // 显示隐藏的 WiFi
        };
        esp_wifi_scan_start(&scan_config, false);
        if (this_->on_scan_begin_) {
            this_->on_scan_begin_();
        }
    } else if (event_id == WIFI_EVENT_SCAN_DONE) {
        this_->HandleScanResult();
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(this_->event_group_, WIFI_EVENT_CONNECTED);
        if (this_->reconnect_count_ < MAX_RECONNECT_COUNT) {
            esp_wifi_connect();
            this_->reconnect_count_++;
            ESP_LOGI(TAG, "Reconnecting %s (attempt %d / %d)", this_->ssid_.c_str(), this_->reconnect_count_, MAX_RECONNECT_COUNT);
            return;
        }

        if (!this_->connect_queue_.empty()) {
            this_->StartConnect();
            return;
        }
        
        ESP_LOGI(TAG, "No more AP to connect, wait for next scan");
        esp_timer_start_once(this_->timer_handle_, 10 * 1000 * 1000);  // 10 秒（单位是微秒）
    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
    }
}

void WifiStation::IpEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    auto* this_ = static_cast<WifiStation*>(arg);
    auto* event = static_cast<ip_event_got_ip_t*>(event_data);

    char ip_address[16];
    esp_ip4addr_ntoa(&event->ip_info.ip, ip_address, sizeof(ip_address));
    this_->ip_address_ = ip_address;
    ESP_LOGI(TAG, "Got IP: %s", this_->ip_address_.c_str());
    
    xEventGroupSetBits(this_->event_group_, WIFI_EVENT_CONNECTED);
    if (this_->on_connected_) {
        this_->on_connected_(this_->ssid_);
    }
    this_->connect_queue_.clear();
    this_->reconnect_count_ = 0;
}
