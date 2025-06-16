#include "wifi_configuration_ble.h"
#include "wifi_configuration.h"
#include "ble.h"
#include "gatt_svr.h"
#include "esp_log.h"
#include "wifi_connection_manager.h"
#include "ssid_manager.h"

static const char* TAG = "WifiConfigurationBle";

WifiConfigurationBle& WifiConfigurationBle::getInstance() {
    static WifiConfigurationBle instance;
    return instance;
}

bool WifiConfigurationBle::init(const std::string& product_key) {
    if (isInitialized_) {
        ESP_LOGW(TAG, "BLE already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing BLE...");
    
    // 初始化BLE
    ble_init(product_key.c_str());
    
    // 检查初始化状态
    if (!is_init) {
        ESP_LOGE(TAG, "BLE initialization failed");
        return false;
    }
    
    
    isInitialized_ = true;
    ESP_LOGI(TAG, "BLE initialized successfully");
    return true;
}

bool WifiConfigurationBle::deinit() {
    if (!isInitialized_) {
        ESP_LOGW(TAG, "BLE not initialized");
        return true;
    }

    ESP_LOGI(TAG, "Deinitializing BLE...");
    
    // 停止BLE
    ble_stop();
    
    isInitialized_ = false;
    ESP_LOGI(TAG, "BLE deinitialized successfully");
    return true;
}

bool WifiConfigurationBle::sendNotify(const uint8_t* data, size_t len) {
    if (!isInitialized_) {
        ESP_LOGE(TAG, "BLE not initialized");
        return false;
    }

    if (!data || len == 0) {
        ESP_LOGE(TAG, "Invalid notify data");
        return false;
    }
    
    return ble_send_notify(data, len);
}
