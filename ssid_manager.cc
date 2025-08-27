#include "ssid_manager.h"

#include <algorithm>
#include <esp_log.h>
#include <nvs_flash.h>

#define TAG "SsidManager"
#define NVS_NAMESPACE "wifi"
#define MAX_WIFI_SSID_COUNT 10

SsidManager::SsidManager() {
    LoadFromNvs();
}

SsidManager::~SsidManager() {
}

void SsidManager::Clear() {
    ssid_list_.clear();
    SaveToNvs();
}

void SsidManager::LoadFromNvs() {
    ssid_list_.clear();

    // Load ssid and password from NVS from namespace "wifi"
    // ssid, ssid1, ssid2, ... ssid9
    // password, password1, password2, ... password9
    nvs_handle_t nvs_handle;
    auto ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        // The namespace doesn't exist, just return
        ESP_LOGW(TAG, "NVS namespace %s doesn't exist", NVS_NAMESPACE);
        return;
    }
    for (int i = 0; i < MAX_WIFI_SSID_COUNT; i++) {
        std::string ssid_key = "ssid";
        if (i > 0) {
            ssid_key += std::to_string(i);
        }
        std::string password_key = "password";
        if (i > 0) {
            password_key += std::to_string(i);
        }
        std::string bssid_key = "bssid";
        if (i > 0) {
            bssid_key += std::to_string(i);
        }
        
        char ssid[33];
        char password[65];
        char bssid[18];  // "xx:xx:xx:xx:xx:xx" + '\0'
        
        size_t length = sizeof(ssid);
        if (nvs_get_str(nvs_handle, ssid_key.c_str(), ssid, &length) != ESP_OK) {
            continue;
        }
        length = sizeof(password);
        if (nvs_get_str(nvs_handle, password_key.c_str(), password, &length) != ESP_OK) {
            continue;
        }
        
        // BSSID 是可选的，如果不存在则使用空字符串（兼容旧数据）
        length = sizeof(bssid);
        if (nvs_get_str(nvs_handle, bssid_key.c_str(), bssid, &length) != ESP_OK) {
            bssid[0] = '\0';  // 旧数据没有 BSSID，设置为空字符串
        }
        
        ssid_list_.push_back({ssid, password, bssid});
    }
    nvs_close(nvs_handle);
}

void SsidManager::SaveToNvs() {
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle));
    for (int i = 0; i < MAX_WIFI_SSID_COUNT; i++) {
        std::string ssid_key = "ssid";
        if (i > 0) {
            ssid_key += std::to_string(i);
        }
        std::string password_key = "password";
        if (i > 0) {
            password_key += std::to_string(i);
        }
        std::string bssid_key = "bssid";
        if (i > 0) {
            bssid_key += std::to_string(i);
        }
        
        if (i < ssid_list_.size()) {
            nvs_set_str(nvs_handle, ssid_key.c_str(), ssid_list_[i].ssid.c_str());
            nvs_set_str(nvs_handle, password_key.c_str(), ssid_list_[i].password.c_str());
            // 只有在 BSSID 非空时才保存，保持向后兼容
            if (!ssid_list_[i].bssid.empty()) {
                nvs_set_str(nvs_handle, bssid_key.c_str(), ssid_list_[i].bssid.c_str());
            } else {
                // 如果 BSSID 为空，删除可能存在的旧 BSSID 键
                nvs_erase_key(nvs_handle, bssid_key.c_str());
            }
        } else {
            nvs_erase_key(nvs_handle, ssid_key.c_str());
            nvs_erase_key(nvs_handle, password_key.c_str());
            nvs_erase_key(nvs_handle, bssid_key.c_str());
        }
    }
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
}

void SsidManager::AddSsid(const std::string& ssid, const std::string& password, const std::string& bssid) {
    for (auto& item : ssid_list_) {
        ESP_LOGI(TAG, "compare [%s:%d] [%s:%d]", item.ssid.c_str(), item.ssid.size(), ssid.c_str(), ssid.size());
        if (item.ssid == ssid) {
            ESP_LOGW(TAG, "SSID %s already exists, overwrite it", ssid.c_str());
            item.password = password;
            // 更新 BSSID（如果提供了新的 BSSID）
            if (!bssid.empty()) {
                item.bssid = bssid;
                ESP_LOGI(TAG, "Updated BSSID: %s", bssid.c_str());
            }
            SaveToNvs();
            return;
        }
    }

    if (ssid_list_.size() >= MAX_WIFI_SSID_COUNT) {
        ESP_LOGW(TAG, "SSID list is full, pop one");
        ssid_list_.pop_back();
    }
    // Add the new ssid to the front of the list
    ssid_list_.insert(ssid_list_.begin(), {ssid, password, bssid});
    if (!bssid.empty()) {
        ESP_LOGI(TAG, "Added new SSID %s with BSSID: %s", ssid.c_str(), bssid.c_str());
    } else {
        ESP_LOGI(TAG, "Added new SSID %s without BSSID", ssid.c_str());
    }
    SaveToNvs();
}

void SsidManager::RemoveSsid(int index) {
    if (index < 0 || index >= ssid_list_.size()) {
        ESP_LOGW(TAG, "Invalid index %d", index);
        return;
    }
    ssid_list_.erase(ssid_list_.begin() + index);
    SaveToNvs();
}

void SsidManager::SetDefaultSsid(int index) {
    if (index < 0 || index >= ssid_list_.size()) {
        ESP_LOGW(TAG, "Invalid index %d", index);
        return;
    }
    // Move the ssid at index to the front of the list
    auto item = ssid_list_[index];  // 这里自动拷贝整个结构，包括 bssid
    ssid_list_.erase(ssid_list_.begin() + index);
    ssid_list_.insert(ssid_list_.begin(), item);
    SaveToNvs();
}

// 新增：保存带RSSI的扫描结果
void SsidManager::ScanSsidRssiList(const std::vector<SsidRssiItem>& ssid_rssi_list) {
    scan_ssid_rssi_list_ = ssid_rssi_list;
    ESP_LOGI(TAG, "ScanSsidRssiList updated, count: %d", (int)scan_ssid_rssi_list_.size());
}
