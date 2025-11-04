#ifndef _PACK_PROTOCOL_H_
#define _PACK_PROTOCOL_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// 消息ID定义
#define MSG_ID_DATA_POINT    0x04

// 命令定义
#define CMD_WIFI_CONFIG_RESP      0x41
#define CMD_NOTI_WIFI_CONFIG_STATE      0x42
#define CMD_WIFI_LIST_RESP        0x46

// 响应状态码
#define RESP_STATUS_OK       0x00
#define RESP_STATUS_ERROR    0x80

// 配网状态定义
#define EVENT_INVALID_ONBOARDING_PKG    0x01    // 解析配置包失败
#define EVENT_CONNECTING_ROUTER         0x02    // 准备发起连接Wi-Fi推送
#define EVENT_CONNECT_ROUTER_FAILED     0x03    // 连接Wi-Fi失败
#define EVENT_REGISTERING               0x04    // 正在进行设备注册
#define EVENT_REGISTER_FAILED           0x05    // 设备注册失败
#define EVENT_PROVISIONING              0x06    // 正在进行provision
#define EVENT_PROVISION_FAILED          0x07    // provision失败
#define EVENT_CONNECTING_M2M            0x08    // 正在连接m2m
#define EVENT_CONNECT_M2M_FAILED        0x09    // 连接m2m失败
#define EVENT_CLOUD_CONNECTED           0x0A    // 连接m2m成功

/**
 * @brief 构建WiFi配置响应包
 * @param frame_seq 原请求的帧序号
 * @param status 响应状态 (0=成功, 0x80=失败)
 * @param out_buf 输出缓冲区
 * @param buf_size 缓冲区大小
 * @return 响应包长度，失败返回0
 */
size_t pack_wifi_config_response(uint8_t frame_seq, uint8_t msg_id,uint8_t status, 
                               uint8_t *out_buf, size_t buf_size);

/**
 * @brief 构建配网状态推送包
 * @param frame_seq 帧序号
 * @param msg_id 消息ID
 * @param status 配网状态
 * @param log_content 日志内容
 * @param log_len 日志长度
 * @param out_buf 输出缓冲区
 * @param buf_size 缓冲区大小
 * @return 响应包长度，失败返回0
 */
size_t pack_wifi_config_state_notification(uint8_t frame_seq, uint8_t msg_id, uint8_t status,
                                          const char* log_content, uint8_t log_len,
                                          uint8_t *out_buf, size_t buf_size);

#define BLE_FRAME_MAX_PAYLOAD 251
#define BLE_HEADER_LEN 4

typedef void (*ble_frame_send_cb)(const uint8_t* frame, size_t frame_len, void* user_data);

void pack_and_send_wifi_list_response(
    uint8_t msg_id,
    uint8_t cmd,
    const uint8_t* payload, size_t payload_len,
    ble_frame_send_cb cb, void* user_data
);

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
                                        ble_frame_send_cb cb, void* user_data);
#endif /* _PACK_PROTOCOL_H_ */

