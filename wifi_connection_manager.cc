#include "wifi_connection_manager.h"
#include "ssid_manager.h"
#include <string.h>
#include <cstdio>  // Added for sprintf
#include <nvs_flash.h>
#include "wifi_manager_c.h"
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

esp_err_t WifiConnectionManager::Connect(const std::string& ssid, const std::string& password, char* bssid_out) {
    if (ssid.empty()) {
        ESP_LOGE(TAG, "SSID cannot be empty");
        return ESP_ERR_WIFI_SSID;
    }
    
    if (ssid.length() > 32) {
        ESP_LOGE(TAG, "SSID too long");
        return ESP_ERR_WIFI_SSID;
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
    
    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(ret));
        is_connecting_ = false;
        return ret;
    }
    
    int retry_count = 0;
    const int max_retries = 4;
    
    // 重置错误统计
    error_stats_count_ = 0;
    current_retry_count_ = 0;
    memset(error_stats_, 0, sizeof(error_stats_));
    
    while (retry_count < max_retries) {
        current_retry_count_ = retry_count;  // 更新当前重试次数
        ret = esp_wifi_connect();
        if (ret != ESP_OK) {
            // 详细处理esp_wifi_connect()的错误码
            const char* error_str = "Unknown error";
            switch (ret) {
                case ESP_ERR_WIFI_NOT_INIT:
                    error_str = "WiFi not initialized";
                    break;
                case ESP_ERR_WIFI_NOT_STARTED:
                    error_str = "WiFi not started";
                    break;
                case ESP_ERR_WIFI_CONN:
                    error_str = "WiFi connection failed";
                    break;
                case ESP_ERR_WIFI_SSID:
                    error_str = "Invalid SSID";
                    break;
                case ESP_ERR_WIFI_PASSWORD:
                    error_str = "Invalid password";
                    break;
                case ESP_ERR_WIFI_NVS:
                    error_str = "WiFi NVS error";
                    break;
                case ESP_ERR_WIFI_MODE:
                    error_str = "WiFi mode error";
                    break;
                case ESP_ERR_WIFI_STATE:
                    error_str = "WiFi state error";
                    break;
                default:
                    error_str = esp_err_to_name(ret);
                    break;
            }
            ESP_LOGE(TAG, "esp_wifi_connect() failed: %s (code: %d)", error_str, ret);
            
            // 统计错误出现次数
            bool found = false;
            for (int i = 0; i < error_stats_count_; i++) {
                if (error_stats_[i].error == ret && !error_stats_[i].is_disconnect_error) {
                    error_stats_[i].count++;
                    error_stats_[i].last_occurrence = retry_count;
                    found = true;
                    break;
                }
            }
            if (!found && error_stats_count_ < 10) {
                error_stats_[error_stats_count_].error = ret;
                error_stats_[error_stats_count_].disconnect_reason = WIFI_REASON_UNSPECIFIED;
                error_stats_[error_stats_count_].count = 1;
                error_stats_[error_stats_count_].last_occurrence = retry_count;
                error_stats_[error_stats_count_].is_disconnect_error = false;
                error_stats_count_++;
            }
            
            retry_count++;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        ESP_LOGI(TAG, "Connecting to WiFi %s (try %d/%d)", ssid.c_str(), retry_count + 1, max_retries);

        // 等待连接结果
        EventBits_t bits = xEventGroupWaitBits(event_group_, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "Connected to WiFi %s", ssid.c_str());
            
            // 获取连接后的 BSSID
            if (bssid_out != nullptr) {
                wifi_ap_record_t ap_info;
                if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                    sprintf(bssid_out, "%02x:%02x:%02x:%02x:%02x:%02x",
                        ap_info.bssid[0], ap_info.bssid[1], ap_info.bssid[2],
                        ap_info.bssid[3], ap_info.bssid[4], ap_info.bssid[5]);
                    ESP_LOGI(TAG, "Connected to BSSID: %s", bssid_out);
                } else {
                    ESP_LOGW(TAG, "Failed to get AP info for BSSID");
                    bssid_out[0] = '\0';  // 设置为空字符串
                }
            }
            
            is_connecting_ = false;
            return ESP_OK;
        } else if (bits & WIFI_FAIL_BIT) {
            ESP_LOGE(TAG, "Failed to connect to WiFi %s (try %d/%d)", ssid.c_str(), retry_count + 1, max_retries);
            // 统计连接失败错误
            esp_err_t conn_error = ESP_ERR_WIFI_CONN;
            bool found = false;
            for (int i = 0; i < error_stats_count_; i++) {
                if (error_stats_[i].error == conn_error) {
                    error_stats_[i].count++;
                    error_stats_[i].last_occurrence = retry_count;
                    found = true;
                    break;
                }
            }
            if (!found && error_stats_count_ < 10) {
                error_stats_[error_stats_count_].error = conn_error;
                error_stats_[error_stats_count_].count = 1;
                error_stats_[error_stats_count_].last_occurrence = retry_count;
                error_stats_count_++;
            }
        } else {
            ESP_LOGE(TAG, "Connection timeout for WiFi %s (try %d/%d)", ssid.c_str(), retry_count + 1, max_retries);
            // 统计超时错误
            esp_err_t timeout_error = ESP_ERR_TIMEOUT;
            bool found = false;
            for (int i = 0; i < error_stats_count_; i++) {
                if (error_stats_[i].error == timeout_error) {
                    error_stats_[i].count++;
                    error_stats_[i].last_occurrence = retry_count;
                    found = true;
                    break;
                }
            }
            if (!found && error_stats_count_ < 10) {
                error_stats_[error_stats_count_].error = timeout_error;
                error_stats_[error_stats_count_].count = 1;
                error_stats_[error_stats_count_].last_occurrence = retry_count;
                error_stats_count_++;
            }
        }
        retry_count++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    // 找出出现次数最多的错误，如果有重复则返回最后一次出现的
    esp_err_t most_frequent_error = ESP_OK;
    int max_count = 0;
    int last_occurrence = -1;
    
    // 首先检查是否存在4次握手超时错误
    bool has_handshake_timeout = false;
    int handshake_timeout_count = 0;
    int handshake_timeout_last_occurrence = -1;
    
    for (int i = 0; i < error_stats_count_; i++) {
        if (error_stats_[i].disconnect_reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT) {
            has_handshake_timeout = true;
            handshake_timeout_count = error_stats_[i].count;
            handshake_timeout_last_occurrence = error_stats_[i].last_occurrence;
            break;
        }
    }
    
    // 首先检查是否存在密码相关错误
    bool has_password_error = false;
    int password_error_count = 0;
    int password_error_last_occurrence = -1;
    
    for (int i = 0; i < error_stats_count_; i++) {
        if (error_stats_[i].is_disconnect_error) {
            // 检查是否为密码相关错误
            if (error_stats_[i].disconnect_reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                error_stats_[i].disconnect_reason == WIFI_REASON_AUTH_FAIL ||
                error_stats_[i].disconnect_reason == WIFI_REASON_MIC_FAILURE ||
                error_stats_[i].disconnect_reason == WIFI_REASON_CIPHER_SUITE_REJECTED) {
                has_password_error = true;
                password_error_count += error_stats_[i].count;
                if (error_stats_[i].last_occurrence > password_error_last_occurrence) {
                    password_error_last_occurrence = error_stats_[i].last_occurrence;
                }
            }
        }
    }
    
    // 如果存在密码相关错误，优先返回
    if (has_password_error) {
        most_frequent_error = ESP_ERR_WIFI_PASSWORD_INCORRECT;  // 返回密码错误码
        max_count = password_error_count;
        last_occurrence = password_error_last_occurrence;
        ESP_LOGI(TAG, "Found password-related errors, returning password error");
    } else {
        // 如果没有4次握手超时，则按原逻辑选择最频繁的错误
        for (int i = 0; i < error_stats_count_; i++) {
            if (error_stats_[i].count > max_count || 
                (error_stats_[i].count == max_count && error_stats_[i].last_occurrence > last_occurrence)) {
                max_count = error_stats_[i].count;
                last_occurrence = error_stats_[i].last_occurrence;
                most_frequent_error = error_stats_[i].error;
            }
        }
    }
    
    // 打印错误统计信息
    ESP_LOGI(TAG, "Error statistics after %d retries:", max_retries);
    for (int i = 0; i < error_stats_count_; i++) {
        if (error_stats_[i].is_disconnect_error) {
            ESP_LOGI(TAG, "  Disconnect error: 0x%x (reason: %d - %s), %d times, last at retry %d", 
                     error_stats_[i].error, error_stats_[i].disconnect_reason, 
                     GetDisconnectReasonString(error_stats_[i].disconnect_reason),
                     error_stats_[i].count, error_stats_[i].last_occurrence);
        } else {
            ESP_LOGI(TAG, "  Connect error: 0x%x, %d times, last at retry %d", 
                     error_stats_[i].error, error_stats_[i].count, error_stats_[i].last_occurrence);
        }
    }
    
    if (has_password_error) {
        ESP_LOGI(TAG, "Returning password error: 0x%x (password-related errors, occurred %d times)", 
                 most_frequent_error, max_count);
    } else {
        ESP_LOGI(TAG, "Returning most frequent error: 0x%x (occurred %d times)", 
                 most_frequent_error, max_count);
    }
    
    is_connecting_ = false;
    return most_frequent_error;
}

/*
 * 错误统计逻辑示例：
 * 
 * 假设5次重试中出现以下错误：
 * 重试1: ESP_ERR_WIFI_CONN (连接失败)
 * 重试2: ESP_ERR_WIFI_CONN (连接失败) 
 * 重试3: ESP_ERR_TIMEOUT (超时)
 * 重试4: ESP_ERR_WIFI_CONN (连接失败)
 * 重试5: ESP_ERR_TIMEOUT (超时)
 * 
 * 错误统计结果：
 * - ESP_ERR_WIFI_CONN: 3次 (最后出现在重试4)
 * - ESP_ERR_TIMEOUT: 2次 (最后出现在重试5)
 * 
 * 返回结果：ESP_ERR_WIFI_CONN (出现次数最多)
 * 
 * 如果有相同次数的错误：
 * 重试1: ESP_ERR_WIFI_CONN
 * 重试2: ESP_ERR_TIMEOUT
 * 重试3: ESP_ERR_WIFI_CONN
 * 重试4: ESP_ERR_TIMEOUT
 * 重试5: ESP_ERR_WIFI_CONN
 * 
 * 错误统计结果：
 * - ESP_ERR_WIFI_CONN: 3次 (最后出现在重试5)
 * - ESP_ERR_TIMEOUT: 2次 (最后出现在重试4)
 * 
 * 返回结果：ESP_ERR_WIFI_CONN (出现次数最多)
 * 
 * 如果出现次数相同：
 * 重试1: ESP_ERR_WIFI_CONN
 * 重试2: ESP_ERR_TIMEOUT
 * 重试3: ESP_ERR_WIFI_CONN
 * 重试4: ESP_ERR_TIMEOUT
 * 重试5: ESP_ERR_TIMEOUT
 * 
 * 错误统计结果：
 * - ESP_ERR_WIFI_CONN: 2次 (最后出现在重试3)
 * - ESP_ERR_TIMEOUT: 3次 (最后出现在重试5)
 * 
 * 返回结果：ESP_ERR_TIMEOUT (出现次数最多)
 */

void WifiConnectionManager::Disconnect() {
    esp_wifi_disconnect();
}

bool WifiConnectionManager::IsConnected() const {
    return (xEventGroupGetBits(event_group_) & WIFI_CONNECTED_BIT) != 0;
}

void WifiConnectionManager::SaveCredentials(const std::string& ssid, const std::string& password, const std::string& bssid) {
    ESP_LOGI(TAG, "Save SSID %s", ssid.c_str());
    
    if (!bssid.empty()) {
        ESP_LOGI(TAG, "Saving with BSSID: %s", bssid.c_str());
    } else {
        ESP_LOGI(TAG, "Saving without BSSID");
    }
    
    SsidManager::GetInstance().AddSsid(ssid, password, bssid);
}

void WifiConnectionManager::WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    WifiConnectionManager* self = static_cast<WifiConnectionManager*>(arg);
    if (event_id == WIFI_EVENT_STA_CONNECTED) {
        xEventGroupSetBits(self->event_group_, WIFI_CONNECTED_BIT);
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // 获取断开连接的具体原因
        wifi_event_sta_disconnected_t* disconnected_data = (wifi_event_sta_disconnected_t*)event_data;
        ESP_LOGE(TAG, "WiFi disconnected, reason: %d", disconnected_data->reason);
        
        // 记录断开原因到错误统计中
        bool found = false;
        for (int i = 0; i < self->error_stats_count_; i++) {
            if (self->error_stats_[i].disconnect_reason == static_cast<wifi_err_reason_t>(disconnected_data->reason) && 
                self->error_stats_[i].is_disconnect_error) {
                self->error_stats_[i].count++;
                self->error_stats_[i].last_occurrence = self->current_retry_count_;
                found = true;
                break;
            }
        }
        if (!found && self->error_stats_count_ < 10) {
            self->error_stats_[self->error_stats_count_].error = ESP_ERR_WIFI_CONN;
            self->error_stats_[self->error_stats_count_].disconnect_reason = static_cast<wifi_err_reason_t>(disconnected_data->reason);
            self->error_stats_[self->error_stats_count_].count = 1;
            self->error_stats_[self->error_stats_count_].last_occurrence = self->current_retry_count_;
            self->error_stats_[self->error_stats_count_].is_disconnect_error = true;
            self->error_stats_count_++;
        }
        
        // 根据错误码提供更详细的错误信息
        const char* reason_str = "Unknown";
        switch (disconnected_data->reason) {
            case WIFI_REASON_UNSPECIFIED:
                reason_str = "Unspecified reason";
                break;
            case WIFI_REASON_AUTH_EXPIRE:
                reason_str = "Authentication expired";
                break;
            case WIFI_REASON_AUTH_LEAVE:
                reason_str = "Authentication left";
                break;
            case WIFI_REASON_ASSOC_EXPIRE:
                reason_str = "Association expired";
                break;
            case WIFI_REASON_ASSOC_TOOMANY:
                reason_str = "Too many associations";
                break;
            case WIFI_REASON_NOT_AUTHED:
                reason_str = "Not authenticated";
                break;
            case WIFI_REASON_NOT_ASSOCED:
                reason_str = "Not associated";
                break;
            case WIFI_REASON_ASSOC_LEAVE:
                reason_str = "Association left";
                break;
            case WIFI_REASON_ASSOC_NOT_AUTHED:
                reason_str = "Associated but not authenticated";
                break;
            case WIFI_REASON_DISASSOC_PWRCAP_BAD:
                reason_str = "Power capability mismatch";
                break;
            case WIFI_REASON_DISASSOC_SUPCHAN_BAD:
                reason_str = "Supported channel mismatch";
                break;
            case WIFI_REASON_BSS_TRANSITION_DISASSOC:
                reason_str = "BSS transition disassociation";
                break;
            case WIFI_REASON_IE_INVALID:
                reason_str = "Invalid IE";
                break;
            case WIFI_REASON_MIC_FAILURE:
                reason_str = "MIC failure";
                break;
            case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
                reason_str = "4-way handshake timeout";
                break;
            case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
                reason_str = "Group key update timeout";
                break;
            case WIFI_REASON_IE_IN_4WAY_DIFFERS:
                reason_str = "4-way handshake IE differs";
                break;
            case WIFI_REASON_GROUP_CIPHER_INVALID:
                reason_str = "Group cipher invalid";
                break;
            case WIFI_REASON_PAIRWISE_CIPHER_INVALID:
                reason_str = "Pairwise cipher invalid";
                break;
            case WIFI_REASON_AKMP_INVALID:
                reason_str = "AKMP invalid";
                break;
            case WIFI_REASON_UNSUPP_RSN_IE_VERSION:
                reason_str = "Unsupported RSN IE version";
                break;
            case WIFI_REASON_INVALID_RSN_IE_CAP:
                reason_str = "Invalid RSN IE capability";
                break;
            case WIFI_REASON_802_1X_AUTH_FAILED:
                reason_str = "802.1x authentication failed";
                break;
            case WIFI_REASON_CIPHER_SUITE_REJECTED:
                reason_str = "Cipher suite rejected";
                break;
            case WIFI_REASON_TDLS_PEER_UNREACHABLE:
                reason_str = "TDLS peer unreachable";
                break;
            case WIFI_REASON_TDLS_UNSPECIFIED:
                reason_str = "TDLS unspecified";
                break;
            case WIFI_REASON_SSP_REQUESTED_DISASSOC:
                reason_str = "SSP requested disassociation";
                break;
            case WIFI_REASON_NO_SSP_ROAMING_AGREEMENT:
                reason_str = "No SSP roaming agreement";
                break;
            case WIFI_REASON_BAD_CIPHER_OR_AKM:
                reason_str = "Bad cipher or AKM";
                break;
            case WIFI_REASON_NOT_AUTHORIZED_THIS_LOCATION:
                reason_str = "Not authorized for this location";
                break;
            case WIFI_REASON_SERVICE_CHANGE_PERCLUDES_TS:
                reason_str = "Service change precludes TS";
                break;
            case WIFI_REASON_UNSPECIFIED_QOS:
                reason_str = "Unspecified QoS";
                break;
            case WIFI_REASON_NOT_ENOUGH_BANDWIDTH:
                reason_str = "Not enough bandwidth";
                break;
            case WIFI_REASON_MISSING_ACKS:
                reason_str = "Missing ACKs";
                break;
            case WIFI_REASON_EXCEEDED_TXOP:
                reason_str = "Exceeded TXOP";
                break;
            case WIFI_REASON_STA_LEAVING:
                reason_str = "Station leaving";
                break;
            case WIFI_REASON_END_BA:
                reason_str = "End BA";
                break;
            case WIFI_REASON_UNKNOWN_BA:
                reason_str = "Unknown BA";
                break;
            case WIFI_REASON_TIMEOUT:
                reason_str = "Timeout";
                break;
            case WIFI_REASON_PEER_INITIATED:
                reason_str = "Peer initiated";
                break;
            case WIFI_REASON_AP_INITIATED:
                reason_str = "AP initiated";
                break;
            case WIFI_REASON_INVALID_FT_ACTION_FRAME_COUNT:
                reason_str = "Invalid FT action frame count";
                break;
            case WIFI_REASON_INVALID_PMKID:
                reason_str = "Invalid PMKID";
                break;
            case WIFI_REASON_INVALID_MDE:
                reason_str = "Invalid MDE";
                break;
            case WIFI_REASON_INVALID_FTE:
                reason_str = "Invalid FTE";
                break;
            case WIFI_REASON_TRANSMISSION_LINK_ESTABLISH_FAILED:
                reason_str = "Transmission link establish failed";
                break;
            case WIFI_REASON_ALTERATIVE_CHANNEL_OCCUPIED:
                reason_str = "Alternative channel occupied";
                break;
            case WIFI_REASON_BEACON_TIMEOUT:
                reason_str = "Beacon timeout";
                break;
            case WIFI_REASON_NO_AP_FOUND:
                reason_str = "No AP found";
                break;
            case WIFI_REASON_AUTH_FAIL:
                reason_str = "Authentication failed";
                break;
            case WIFI_REASON_ASSOC_FAIL:
                reason_str = "Association failed";
                break;
            case WIFI_REASON_HANDSHAKE_TIMEOUT:
                reason_str = "Handshake timeout";
                break;
            case WIFI_REASON_CONNECTION_FAIL:
                reason_str = "Connection failed";
                break;
            case WIFI_REASON_AP_TSF_RESET:
                reason_str = "AP TSF reset";
                break;
            case WIFI_REASON_ROAMING:
                reason_str = "Roaming";
                break;
            case WIFI_REASON_ASSOC_COMEBACK_TIME_TOO_LONG:
                reason_str = "Association comeback time too long";
                break;
            case WIFI_REASON_SA_QUERY_TIMEOUT:
                reason_str = "SA query timeout";
                break;
            case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
                reason_str = "No AP found with compatible security";
                break;
            case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
                reason_str = "No AP found in authmode threshold";
                break;
            case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
                reason_str = "No AP found in RSSI threshold";
                break;
            default:
                reason_str = "Unknown reason";
                break;
        }
        
        ESP_LOGE(TAG, "WiFi disconnect reason: %s (code: %d)", reason_str, disconnected_data->reason);
        xEventGroupSetBits(self->event_group_, WIFI_FAIL_BIT);
    } else if (event_id == WIFI_EVENT_SCAN_DONE) {
        // 新增：保存扫描到的所有 SSID，按 rssi 降序排序，并回调上层
        uint16_t ap_num = 0;
        std::vector<SsidRssiItem> scan_ssid_rssi_list;
        std::vector<std::string> ssid_list;
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
                    scan_ssid_rssi_list.emplace_back(ssid, ap_records[i].rssi);
                    ssid_list.emplace_back(ssid);
                }
            }
        }
        // 保存带RSSI的扫描结果
        SsidManager::GetInstance().ScanSsidRssiList(scan_ssid_rssi_list);
        // 回调仅包含 SSID 列表，供上层快速判断
        if (self->on_scan_results_) {
            self->on_scan_results_(ssid_list);
        }
        
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

// 静态函数：将WiFi断开原因码转换为可读的错误信息
const char* WifiConnectionManager::GetDisconnectReasonString(wifi_err_reason_t reason) {
    switch (reason) {
        case WIFI_REASON_UNSPECIFIED:
            return "Unspecified reason";
        case WIFI_REASON_AUTH_EXPIRE:
            return "Authentication expired";
        case WIFI_REASON_AUTH_LEAVE:
            return "Authentication left";
        case WIFI_REASON_ASSOC_EXPIRE:
            return "Association expired";
        case WIFI_REASON_ASSOC_TOOMANY:
            return "Too many associations";
        case WIFI_REASON_NOT_AUTHED:
            return "Not authenticated";
        case WIFI_REASON_NOT_ASSOCED:
            return "Not associated";
        case WIFI_REASON_ASSOC_LEAVE:
            return "Association left";
        case WIFI_REASON_ASSOC_NOT_AUTHED:
            return "Associated but not authenticated";
        case WIFI_REASON_DISASSOC_PWRCAP_BAD:
            return "Power capability mismatch";
        case WIFI_REASON_DISASSOC_SUPCHAN_BAD:
            return "Supported channel mismatch";
        case WIFI_REASON_BSS_TRANSITION_DISASSOC:
            return "BSS transition disassociation";
        case WIFI_REASON_IE_INVALID:
            return "Invalid IE";
        case WIFI_REASON_MIC_FAILURE:
            return "MIC failure";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
            return "4-way handshake timeout";
        case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
            return "Group key update timeout";
        case WIFI_REASON_IE_IN_4WAY_DIFFERS:
            return "4-way handshake IE differs";
        case WIFI_REASON_GROUP_CIPHER_INVALID:
            return "Group cipher invalid";
        case WIFI_REASON_PAIRWISE_CIPHER_INVALID:
            return "Pairwise cipher invalid";
        case WIFI_REASON_AKMP_INVALID:
            return "AKMP invalid";
        case WIFI_REASON_UNSUPP_RSN_IE_VERSION:
            return "Unsupported RSN IE version";
        case WIFI_REASON_INVALID_RSN_IE_CAP:
            return "Invalid RSN IE capability";
        case WIFI_REASON_802_1X_AUTH_FAILED:
            return "802.1x authentication failed";
        case WIFI_REASON_CIPHER_SUITE_REJECTED:
            return "Cipher suite rejected";
        case WIFI_REASON_TDLS_PEER_UNREACHABLE:
            return "TDLS peer unreachable";
        case WIFI_REASON_TDLS_UNSPECIFIED:
            return "TDLS unspecified";
        case WIFI_REASON_SSP_REQUESTED_DISASSOC:
            return "SSP requested disassociation";
        case WIFI_REASON_NO_SSP_ROAMING_AGREEMENT:
            return "No SSP roaming agreement";
        case WIFI_REASON_BAD_CIPHER_OR_AKM:
            return "Bad cipher or AKM";
        case WIFI_REASON_NOT_AUTHORIZED_THIS_LOCATION:
            return "Not authorized for this location";
        case WIFI_REASON_SERVICE_CHANGE_PERCLUDES_TS:
            return "Service change precludes TS";
        case WIFI_REASON_UNSPECIFIED_QOS:
            return "Unspecified QoS";
        case WIFI_REASON_NOT_ENOUGH_BANDWIDTH:
            return "Not enough bandwidth";
        case WIFI_REASON_MISSING_ACKS:
            return "Missing ACKs";
        case WIFI_REASON_EXCEEDED_TXOP:
            return "Exceeded TXOP";
        case WIFI_REASON_STA_LEAVING:
            return "Station leaving";
        case WIFI_REASON_END_BA:
            return "End BA";
        case WIFI_REASON_UNKNOWN_BA:
            return "Unknown BA";
        case WIFI_REASON_TIMEOUT:
            return "Timeout";
        case WIFI_REASON_PEER_INITIATED:
            return "Peer initiated";
        case WIFI_REASON_AP_INITIATED:
            return "AP initiated";
        case WIFI_REASON_INVALID_FT_ACTION_FRAME_COUNT:
            return "Invalid FT action frame count";
        case WIFI_REASON_INVALID_PMKID:
            return "Invalid PMKID";
        case WIFI_REASON_INVALID_MDE:
            return "Invalid MDE";
        case WIFI_REASON_INVALID_FTE:
            return "Invalid FTE";
        case WIFI_REASON_TRANSMISSION_LINK_ESTABLISH_FAILED:
            return "Transmission link establish failed";
        case WIFI_REASON_ALTERATIVE_CHANNEL_OCCUPIED:
            return "Alternative channel occupied";
        case WIFI_REASON_BEACON_TIMEOUT:
            return "Beacon timeout";
        case WIFI_REASON_NO_AP_FOUND:
            return "No AP found";
        case WIFI_REASON_AUTH_FAIL:
            return "Authentication failed";
        case WIFI_REASON_ASSOC_FAIL:
            return "Association failed";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return "Handshake timeout";
        case WIFI_REASON_CONNECTION_FAIL:
            return "Connection failed";
        case WIFI_REASON_AP_TSF_RESET:
            return "AP TSF reset";
        case WIFI_REASON_ROAMING:
            return "Roaming";
        case WIFI_REASON_ASSOC_COMEBACK_TIME_TOO_LONG:
            return "Association comeback time too long";
        case WIFI_REASON_SA_QUERY_TIMEOUT:
            return "SA query timeout";
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
            return "No AP found with compatible security";
        case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
            return "No AP found in authmode threshold";
        case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
            return "No AP found in RSSI threshold";
        default:
            return "Unknown reason";
    }
} 