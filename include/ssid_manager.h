#ifndef SSID_MANAGER_H
#define SSID_MANAGER_H

#include <string>
#include <vector>

struct SsidItem {
    std::string ssid;
    std::string password;
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

    // 新增：保存扫描到的 SSID 列表（不保存密码）
    void ScanSsidList(const std::vector<std::string>& ssid_list);

    // 获取扫描到的 SSID 列表
    const std::vector<std::string>& GetScanSsidList() const { return scan_ssid_list_; }

    // 获取扫描到的 SSID 列表（标准 getter 命名）
    const std::vector<std::string>& get_scan_ssid_list() const { return scan_ssid_list_; }

private:
    SsidManager();
    ~SsidManager();

    void LoadFromNvs();
    void SaveToNvs();

    std::vector<SsidItem> ssid_list_;
    // 新增：保存扫描到的 SSID 列表（不保存密码）
    std::vector<std::string> scan_ssid_list_;
};

#endif // SSID_MANAGER_H
