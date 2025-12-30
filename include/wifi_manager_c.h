#ifndef WIFI_MANAGER_C_H
#define WIFI_MANAGER_C_H

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_ERR_WIFI_PASSWORD_INCORRECT 0x3008

#include "esp_err.h"

/**
 * @brief 连接WiFi
 * @param ssid WiFi名称
 * @param password WiFi密码
 * @return ESP_OK 连接成功，其他值表示失败
 */
esp_err_t WifiConnectionManager_Connect(const char* ssid, const char* password);

/**
 * @brief 连接WiFi并返回BSSID
 * @param ssid WiFi名称
 * @param password WiFi密码
 * @param bssid_out 输出BSSID字符串（格式：xx:xx:xx:xx:xx:xx），缓冲区至少18字节
 * @return ESP_OK 连接成功，其他值表示失败
 */
esp_err_t WifiConnectionManager_ConnectWithBssid(const char* ssid, const char* password, char* bssid_out);

/**
 * @brief 保存WiFi凭证
 * @param ssid WiFi名称
 * @param password WiFi密码
 */
void WifiConnectionManager_SaveCredentials(const char* ssid, const char* password);

/**
 * @brief 保存WiFi凭证（包含BSSID）
 * @param ssid WiFi名称
 * @param password WiFi密码
 * @param bssid BSSID字符串（格式：xx:xx:xx:xx:xx:xx）
 */
void WifiConnectionManager_SaveCredentialsWithBssid(const char* ssid, const char* password, const char* bssid);

/**
 * @brief 保存用户ID
 * @param uid 用户ID
 */
void WifiConnectionManager_SaveUid(const char* uid);

/**
 * @brief 保存服务器URL
 * @param server_url 服务器URL（可以是完整域名、IP:端口等格式）
 */
void WifiConnectionManager_SaveServerUrl(const char* server_url);

#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_C_H 