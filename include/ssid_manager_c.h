#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// 返回 JSON 字符串，内部静态存储，调用者无需释放
const char* ssid_manager_get_scan_ssid_list_json();

#ifdef __cplusplus
}
#endif 