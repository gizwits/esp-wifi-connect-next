#include "wifi_configuration.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char* TAG = "WifiConfiguration";

// 定时器回调函数，用于延时启动蓝牙
static void ble_delayed_init_callback(void* arg) {
    std::string* product_key = static_cast<std::string*>(arg);
    ESP_LOGI(TAG, "Starting delayed BLE initialization...");
    
#if defined(CONFIG_BT_NIMBLE_ENABLED)
    auto& ble_config = WifiConfigurationBle::getInstance();
    ble_config.init(*product_key);
#endif
    
    // 清理内存
    delete product_key;
}

WifiConfiguration& WifiConfiguration::GetInstance() {
    static WifiConfiguration instance;
    return instance;
}

void WifiConfiguration::Initialize(const std::string& product_key, const std::string& ssid_prefix) {

    // Initialize WiFi connection manager
    WifiConnectionManager::GetInstance().InitializeWiFi();

#if defined(CONFIG_ESP_WIFI_SOFTAP_SUPPORT)
    // Initialize AP configuration
    auto& wifi_ap = WifiConfigurationAp::GetInstance();
    wifi_ap.SetSsidPrefix(ssid_prefix);
    wifi_ap.Start();
#endif

#if defined(CONFIG_BT_NIMBLE_ENABLED)
    // 创建定时器，延时3秒后启动蓝牙
    ESP_LOGI(TAG, "Scheduling BLE initialization after 3 seconds...");
    
    // 创建product_key的副本用于定时器回调
    std::string* product_key_copy = new std::string(product_key);
    
    // 创建一次性定时器，3秒后执行
    esp_timer_create_args_t timer_args = {};
    timer_args.callback = &ble_delayed_init_callback;
    timer_args.arg = product_key_copy;
    timer_args.name = "ble_delayed_init";
    
    esp_timer_handle_t timer_handle;
    esp_err_t ret = esp_timer_create(&timer_args, &timer_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timer: %s", esp_err_to_name(ret));
        delete product_key_copy;
    } else {
        ret = esp_timer_start_once(timer_handle, 3000000); // 3秒 = 3000000微秒
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start timer: %s", esp_err_to_name(ret));
            esp_timer_delete(timer_handle);
            delete product_key_copy;
        } else {
            ESP_LOGI(TAG, "BLE initialization timer started successfully");
        }
    }
#endif

}

void WifiConfiguration::SetLanguage(const std::string &language) {
    auto& wifi_ap = WifiConfigurationAp::GetInstance();
    wifi_ap.SetLanguage(language);
}

void WifiConfiguration::RegisterCallback(WifiConfigCallback callback) {
    if (callback) {
        callbacks_.push_back(callback);
    }
}

void WifiConfiguration::NotifyEvent(WifiConfigEvent event, const std::string& message) {
    for (const auto& callback : callbacks_) {
        if (callback) {
            callback(event, message);
        }
    }
}
// 