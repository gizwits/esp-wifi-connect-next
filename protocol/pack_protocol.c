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