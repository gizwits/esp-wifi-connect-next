#include "ssid_manager.h"
#include "ssid_manager_c.h"
#include <string>
#include <vector>
#include <sstream>
#include <cJSON.h>
#include <esp_log.h>

extern "C" {
// 新增：获取带RSSI的扫描结果（二进制格式）
const char* ssid_manager_get_scan_ssid_rssi_list_json() {
    static std::string bin_str;
    const auto& ssid_rssi_list = SsidManager::GetInstance().GetScanSsidRssiList();
    bin_str.clear();
    
    for (const auto& item : ssid_rssi_list) {
        uint8_t len = static_cast<uint8_t>(item.ssid.size());
        bin_str.push_back(static_cast<char>(len));
        bin_str.append(item.ssid);
        // 将RSSI从-100~0转换为0~100的正值范围
        // RSSI通常范围是-100到0，我们将其映射到0-100
        uint8_t rssi_normalized = static_cast<uint8_t>(100 + item.rssi);
        bin_str.push_back(static_cast<char>(rssi_normalized));
    }
    
    return bin_str.c_str();
}
} 