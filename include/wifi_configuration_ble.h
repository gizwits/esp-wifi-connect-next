#ifndef wifi_configuration_ble_H
#define wifi_configuration_ble_H

#include <string>
#include <esp_log.h>

// 声明外部变量和函数
extern "C" {
    extern bool is_init;
    void ble_init(const char *product_key);
    void ble_stop(void);
    bool ble_send_notify(const uint8_t *data, size_t len);
    void process_wifi_config(const char* ssid, const char* password, const char* uid);
}

class WifiConfigurationBle {
public:
    /**
     * @brief 获取单例实例
     * @return WifiConfigurationBle& 单例引用
     */
    static WifiConfigurationBle& getInstance();

    /**
     * @brief 初始化BLE
     * @param product_key 产品密钥
     * @return true 初始化成功
     * @return false 初始化失败
     */
    bool init(const std::string& product_key);

    /**
     * @brief 销毁BLE
     * @return true 销毁成功
     * @return false 销毁失败
     */
    bool deinit();

    /**
     * @brief 发送通知数据
     * @param data 数据指针
     * @param len 数据长度
     * @return true 发送成功
     * @return false 发送失败
     */
    bool sendNotify(const uint8_t* data, size_t len);

    // 添加友元函数声明
    friend void process_wifi_config(const char* ssid, const char* password, const char* uid);

private:
    WifiConfigurationBle() = default;
    ~WifiConfigurationBle() = default;
    WifiConfigurationBle(const WifiConfigurationBle&) = delete;
    WifiConfigurationBle& operator=(const WifiConfigurationBle&) = delete;

    bool isInitialized_ = false;
};

#endif // wifi_configuration_ble_H 