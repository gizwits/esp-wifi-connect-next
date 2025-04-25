#ifndef WIFI_MANAGER_C_H
#define WIFI_MANAGER_C_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 连接WiFi
 * @param ssid WiFi名称
 * @param password WiFi密码
 * @return true 连接成功，false 连接失败
 */
bool WifiConnectionManager_Connect(const char* ssid, const char* password);

/**
 * @brief 保存WiFi凭证
 * @param ssid WiFi名称
 * @param password WiFi密码
 */
void WifiConnectionManager_SaveCredentials(const char* ssid, const char* password);

/**
 * @brief 保存用户ID
 * @param uid 用户ID
 */
void WifiConnectionManager_SaveUid(const char* uid);

#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_C_H 