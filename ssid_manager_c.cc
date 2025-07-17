#include "ssid_manager.h"
#include "ssid_manager_c.h"
#include <string>
#include <vector>
#include <sstream>
#include <cJSON.h>
#include <esp_log.h>

extern "C" {
const char* ssid_manager_get_scan_ssid_list_json() {
    static std::string bin_str;
    const auto& ssid_list = SsidManager::GetInstance().GetScanSsidList();
    bin_str.clear();
    
    for (const auto& ssid : ssid_list) {
        uint8_t len = static_cast<uint8_t>(ssid.size());
        bin_str.push_back(static_cast<char>(len));
        bin_str.append(ssid);
    }
    
    return bin_str.c_str();
}
} 