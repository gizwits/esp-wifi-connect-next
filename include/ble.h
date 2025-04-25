#ifndef BLE_H
#define BLE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 声明外部变量
extern bool is_init;

// BLE 初始化
void ble_init(const char *product_key);

// BLE 停止
void ble_stop(void);

/**
 * @brief 设置广播包中的设备名称和自定义数据
 * 
 * @param device_name 设备名称
 * @param pk 4字节的 PK 短编码
 * @param mac 6字节的 MAC 地址
 * @return esp_err_t ESP_OK: 成功, 其他: 失败
 */
esp_err_t ble_set_adv_data(const char *device_name, uint32_t pk, const uint8_t *mac);

void ble_set_notify_handle(uint16_t handle);
void ble_set_conn_handle(uint16_t handle);

/**
 * @brief 设置设备的配网状态
 * 
 * @param configured true: 已配网, false: 未配网
 */
void ble_set_network_status(bool configured);

/**
 * @brief 通过BLE发送通知数据
 * @param data 要发送的数据
 * @param len 数据长度
 * @return true成功，false失败
 */
bool ble_send_notify(const uint8_t *data, size_t len);

/**
 * @brief 处理WiFi配置的回调函数
 * @param ssid WiFi名称
 * @param password WiFi密码
 * @param uid 用户ID
 */
void process_wifi_config(const char* ssid, const char* password, const char* uid);

#ifdef __cplusplus
}
#endif

#endif // BLE_H
