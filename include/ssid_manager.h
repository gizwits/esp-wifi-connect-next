#ifndef SSID_MANAGER_H
#define SSID_MANAGER_H

#include <string>
#include <vector>

struct SsidItem {
    std::string ssid;
    std::string password;
};

// 新增：包含RSSI信息的SSID结构体
struct SsidRssiItem {
    std::string ssid;
    int8_t rssi;
    
    SsidRssiItem(const std::string& s, int8_t r) : ssid(s), rssi(r) {}
};

class SsidManager {
public:
    static SsidManager& GetInstance() {
        static SsidManager instance;
        return instance;
    }

    void AddSsid(const std::string& ssid, const std::string& password);
    void RemoveSsid(int index);
    void SetDefaultSsid(int index);
    void Clear();
    const std::vector<SsidItem>& GetSsidList() const { return ssid_list_; }

    // 新增：保存带RSSI的扫描结果
    void ScanSsidRssiList(const std::vector<SsidRssiItem>& ssid_rssi_list);

    // 新增：获取带RSSI的扫描结果
    const std::vector<SsidRssiItem>& GetScanSsidRssiList() const { return scan_ssid_rssi_list_; }

private:
    SsidManager();
    ~SsidManager();

    void LoadFromNvs();
    void SaveToNvs();

    std::vector<SsidItem> ssid_list_;
    // 新增：保存带RSSI的扫描结果
    std::vector<SsidRssiItem> scan_ssid_rssi_list_;
};

#endif // SSID_MANAGER_H
