#ifndef SSID_MANAGER_C_H
#define SSID_MANAGER_C_H

#ifdef __cplusplus
extern "C" {
#endif


// 新增：获取带RSSI的扫描结果（二进制格式）
// 数据格式：[SSID长度][SSID内容][RSSI值][SSID长度][SSID内容][RSSI值]...
// RSSI值说明：原始RSSI范围-100~0，编码为0~100的正值
// 客户端解析：实际RSSI = 编码值 - 100
const char* ssid_manager_get_scan_ssid_rssi_list_json();

#ifdef __cplusplus
}
#endif

#endif // SSID_MANAGER_C_H 