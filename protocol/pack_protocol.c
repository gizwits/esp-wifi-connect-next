#include <string.h>
#include "pack_protocol.h"
// #include "config.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static const char *TAG = "PACK_PROTO";

// WiFi配置响应包结构
typedef struct {
    uint8_t msg_id;         // 消息ID和版本 (0x04)
    uint8_t cmd;            // 命令字 (0x41)
    uint8_t frame_seq;      // 帧序号
    uint8_t frame_len;      // 帧长度
    uint8_t status;         // 状态
    char hw_ver[8];         // 硬件版本
    char sw_ver[8];         // 软件版本
} __attribute__((packed)) wifi_config_response_t;

// 配网状态推送包结构
typedef struct {
    uint8_t msg_id;         // 消息ID和版本 (0x04)
    uint8_t cmd;            // 命令字 (0x42)
    uint8_t frame_seq;      // 帧序号
    uint8_t frame_len;      // 帧长度
    uint8_t status;         // 配网状态
    uint8_t log_len;        // 日志长度
    char log_content[];     // 日志内容 (变长)
} __attribute__((packed)) wifi_config_state_notification_t;

size_t pack_wifi_config_response(uint8_t frame_seq,uint8_t msg_id, uint8_t status,
                               uint8_t *out_buf, size_t buf_size)
{
    if (!out_buf || buf_size < sizeof(wifi_config_response_t)) {
        ESP_LOGE(TAG, "Invalid buffer or size");
        return 0;
    }

    wifi_config_response_t *resp = (wifi_config_response_t *)out_buf;
    
    // 填充响应包
    resp->msg_id = msg_id;
    resp->cmd = CMD_WIFI_CONFIG_RESP;
    resp->frame_seq = frame_seq;
    resp->frame_len = sizeof(wifi_config_response_t) - 4;  // 减去前4个字节
    resp->status = status;
    
    // 复制版本信息
    // strncpy(resp->hw_ver, sdk_get_hardware_version(), sizeof(resp->hw_ver));
    // strncpy(resp->sw_ver, sdk_get_software_version(), sizeof(resp->sw_ver));

    ESP_LOGD(TAG, "Built WiFi config response: status=0x%02x, seq=%d", 
             status, frame_seq);

    return sizeof(wifi_config_response_t);
}

size_t pack_wifi_config_state_notification(uint8_t frame_seq, uint8_t msg_id, uint8_t status,
                                          const char* log_content, uint8_t log_len,
                                          uint8_t *out_buf, size_t buf_size)
{
    // 计算总长度：固定头部 + 日志内容
    size_t total_len = sizeof(wifi_config_state_notification_t) + log_len;
    
    if (!out_buf || buf_size < total_len) {
        ESP_LOGE(TAG, "Invalid buffer or size: buf_size=%d, required=%d", buf_size, total_len);
        return 0;
    }

    wifi_config_state_notification_t *notif = (wifi_config_state_notification_t *)out_buf;
    
    // 填充推送包
    notif->msg_id = msg_id;
    notif->cmd = CMD_NOTI_WIFI_CONFIG_STATE;
    notif->frame_seq = frame_seq;
    notif->frame_len = total_len - 4;  // 减去前4个字节
    notif->status = status;
    notif->log_len = log_len;
    
    // 复制日志内容
    if (log_content && log_len > 0) {
        memcpy(notif->log_content, log_content, log_len);
    }

    // 获取状态描述
    const char* status_desc = "Unknown";
    switch (status) {
        case EVENT_INVALID_ONBOARDING_PKG:
            status_desc = "Invalid onboarding package";
            break;
        case EVENT_CONNECTING_ROUTER:
            status_desc = "Connecting to router";
            break;
        case EVENT_CONNECT_ROUTER_FAILED:
            status_desc = "Connect router failed";
            break;
        case EVENT_REGISTERING:
            status_desc = "Registering device";
            break;
        case EVENT_REGISTER_FAILED:
            status_desc = "Register failed";
            break;
        case EVENT_PROVISIONING:
            status_desc = "Provisioning";
            break;
        case EVENT_PROVISION_FAILED:
            status_desc = "Provision failed";
            break;
        case EVENT_CONNECTING_M2M:
            status_desc = "Connecting to M2M";
            break;
        case EVENT_CONNECT_M2M_FAILED:
            status_desc = "Connect M2M failed";
            break;
        case EVENT_CLOUD_CONNECTED:
            status_desc = "Cloud connected";
            break;
    }

    ESP_LOGD(TAG, "Built WiFi config state notification: status=0x%02x (%s), seq=%d, log_len=%d", 
             status, status_desc, frame_seq, log_len);

    return total_len;
}

void pack_and_send_wifi_list_response(
    uint8_t msg_id,
    uint8_t cmd,
    const uint8_t* payload, size_t payload_len,
    ble_frame_send_cb cb, void* user_data
) {
    if (!payload && payload_len > 0) return;
    uint8_t ver = 0b00; // 机智云数据点协议
    uint8_t reserved = 0;
    uint8_t total_frames = (payload_len + BLE_FRAME_MAX_PAYLOAD - 1) / BLE_FRAME_MAX_PAYLOAD;
    if (total_frames == 0) total_frames = 1;
    ESP_LOGI(TAG, "total_frames: %d, payload_len: %d", total_frames, payload_len);
    for (uint8_t seq = 0; seq < total_frames; ++seq) {
        size_t offset = seq * BLE_FRAME_MAX_PAYLOAD;
        size_t this_len = (payload_len - offset > BLE_FRAME_MAX_PAYLOAD) ? BLE_FRAME_MAX_PAYLOAD : (payload_len - offset);

        uint8_t frame[BLE_HEADER_LEN + BLE_FRAME_MAX_PAYLOAD];
        // Header
        frame[0] = ((msg_id & 0x1F) | ((ver & 0x03) << 5) | ((reserved & 0x01) << 7));
        frame[1] = cmd;
        frame[2] = ((seq & 0x0F) | (((total_frames - 1) & 0x0F) << 4));
        frame[3] = this_len;
        // Payload
        if (this_len > 0) {
            memcpy(&frame[BLE_HEADER_LEN], payload + offset, this_len);
        }
        cb(frame, BLE_HEADER_LEN + this_len, user_data);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/**
 * @brief 发送配网状态通知
 * @param frame_seq 帧序号
 * @param status 配网状态
 * @param log_content 日志内容
 * @param cb 发送回调函数
 * @param user_data 用户数据
 */
void send_wifi_config_state_notification(uint8_t frame_seq, uint8_t status,
                                        const char* log_content,
                                        ble_frame_send_cb cb, void* user_data)
{
    if (!cb) {
        ESP_LOGE(TAG, "Send callback is NULL");
        return;
    }

    // 计算日志长度
    uint8_t log_len = 0;
    if (log_content) {
        log_len = strlen(log_content);
        // 限制日志长度，避免超出BLE帧大小
        if (log_len > BLE_FRAME_MAX_PAYLOAD - 10) {
            log_len = BLE_FRAME_MAX_PAYLOAD - 10;
        }
    }

    // 分配缓冲区
    uint8_t buffer[BLE_FRAME_MAX_PAYLOAD];
    size_t pack_len = pack_wifi_config_state_notification(
        frame_seq, MSG_ID_DATA_POINT, status, log_content, log_len, buffer, sizeof(buffer)
    );

    if (pack_len > 0) {
        // 发送状态通知
        cb(buffer, pack_len, user_data);
        ESP_LOGI(TAG, "Sent WiFi config state notification: status=0x%02x, len=%d", status, pack_len);
    } else {
        ESP_LOGE(TAG, "Failed to pack WiFi config state notification");
    }
}