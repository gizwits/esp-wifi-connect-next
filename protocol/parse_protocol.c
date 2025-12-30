#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "parse_protocol.h"

// 协议格式定义
typedef struct {
    struct {
        uint8_t msg_id: 5;    // Bit 0-4: Msg ID (1-31)
        uint8_t ver: 2;       // Bit 5-6: Payload格式版本
        uint8_t reserved: 1;  // Bit 7: 保留位
    } byte0;
    uint8_t cmd;             // 指令值
    struct {
        uint8_t seq: 4;      // Bit 0-3: 帧序号 (0-15)
        uint8_t frames: 4;   // Bit 4-7: 总拆包帧数 (实际帧数=值+1)
    } byte2;
    uint8_t frame_len;       // 帧数据长度
} protocol_header_t;

// 解析协议头
static void parse_protocol_header(const uint8_t *data, protocol_header_t *header) {
    // 解析第一个字节
    header->byte0.msg_id = data[0] & 0x1F;        // 取低5位
    header->byte0.ver = (data[0] >> 5) & 0x03;    // 取第5-6位
    header->byte0.reserved = (data[0] >> 7) & 0x01;// 取第7位

    // 解析第二个字节 (CMD)
    header->cmd = data[1];

    // 解析第三个字节
    header->byte2.seq = data[2] & 0x0F;           // 取低4位
    header->byte2.frames = (data[2] >> 4) & 0x0F; // 取高4位

    // 解析第四个字节 (帧长度)
    header->frame_len = data[3];
}

// 打印协议头信息
static void print_protocol_header(const protocol_header_t *header) {
    printf("Protocol Header:\n");
    printf("  Msg ID: %d\n", header->byte0.msg_id);
    printf("  Version: %d\n", header->byte0.ver);
    printf("  Reserved: %d\n", header->byte0.reserved);
    printf("  CMD: 0x%02X\n", header->cmd);
    printf("  Sequence: %d\n", header->byte2.seq);
    printf("  Total Frames: %d\n", header->byte2.frames + 1);
    printf("  Frame Length: %d\n", header->frame_len);
}

// 通用的字段解析函数
static bool parse_field(const uint8_t *data, size_t data_len, size_t *offset, 
                       uint8_t *out_len, void *out_data, size_t max_len, 
                       const char *field_name) {
    // 检查长度字段
    if (*offset + 1 > data_len) {
        printf("Error: Not enough data for %s length. Required: %zu, Available: %zu\n", 
               field_name, 1, data_len - *offset);
        return false;
    }
    
    // 获取字段长度
    *out_len = data[(*offset)++];
    printf("%s Length: %d\n", field_name, *out_len);
    
    // 检查字段数据
    if (*out_len > 0) {
        if (*offset + *out_len > data_len || *out_len > max_len) {
            printf("Error: %s length exceeds limits. Length: %d, Available: %zu, Max: %zu\n", 
                   field_name, *out_len, data_len - *offset, max_len);
            return false;
        }
        memcpy(out_data, &data[*offset], *out_len);
        if (max_len > *out_len) {  // 如果是字符串，添加结束符
            ((uint8_t*)out_data)[*out_len] = '\0';
        }
        *offset += *out_len;
        
        // 打印解析结果
        if (field_name[0] != '_') {  // 如果字段名不以_开头就打印
            printf("Parsed %s: %s\n", field_name, (char*)out_data);
        }
    }
    
    return true;
}

// 检测协议格式：旧格式（有UID）还是新格式（有append data）
// 旧格式：password 字段只包含密码，后面是 uidlen + uid
// 新格式：password 字段包含 password + appendString（appendString 以 0x00 0x1B 开头）
// 注意：appendString 是在 password 字段内部的，不是在 password 字段之后
static bool detect_protocol_format_in_password(const uint8_t *password_data, size_t password_len) {
    // 检查 password 字段内部是否包含 0x00 0x1B（appendString 的起始标记）
    if (password_len < 2) {
        return true;  // 数据不足，默认按旧格式处理
    }
    
    // 在 password 字段中查找 0x00 0x1B
    for (size_t i = 0; i < password_len - 1; i++) {
        if (password_data[i] == 0x00 && password_data[i + 1] == 0x1B) {
            return false;  // 找到 appendString，是新格式
        }
    }
    
    // 没有找到 appendString，是旧格式
    return true;  // 旧格式
}

bool parse_wifi_config(const uint8_t *data, size_t len, wifi_config_t *wifi_config) {
    size_t offset = 4;  // 跳过协议头
    bool is_old_format = false;

    // 1. 解析NTP时间戳
    if (offset + 4 > len) {
        printf("Error: Not enough data for NTP timestamp\n");
        return false;
    }
    wifi_config->ntp = (data[offset] << 24) | (data[offset+1] << 16) | 
                      (data[offset+2] << 8) | data[offset+3];
    offset += 4;
    printf("Parsed NTP: %lu\n", wifi_config->ntp);

    // 2. 解析SSID
    if (!parse_field(data, len, &offset, &wifi_config->ssid_len, 
                    wifi_config->ssid, sizeof(wifi_config->ssid) - 1, "SSID")) {
        return false;
    }

    // 3. 解析BSSID
    if (!parse_field(data, len, &offset, &wifi_config->bssid_len,
                    wifi_config->bssid, sizeof(wifi_config->bssid) - 1, "BSSID")) {
        return false;
    }

    // 4. 解析密码
    if (!parse_field(data, len, &offset, &wifi_config->password_len,
                    wifi_config->password, sizeof(wifi_config->password) - 1, "Password")) {
        return false;
    }

    // 5. 检测协议格式：在 password 字段内部查找 0x00 0x1B
    // 新格式的 appendString 在 password 字段内部，以 0x00 0x1B 开头
    is_old_format = detect_protocol_format_in_password(
        (const uint8_t *)wifi_config->password, 
        wifi_config->password_len
    );
    
    if (is_old_format) {
        // 旧格式：解析 UID
        printf("Detected old protocol format (with UID)\n");
        if (!parse_field(data, len, &offset, &wifi_config->uid_len,
                        wifi_config->uid, sizeof(wifi_config->uid) - 1, "UID")) {
            return false;
        }
    } else {
        // 新格式：处理 append data
        printf("Detected new protocol format (with append data)\n");
        wifi_config->uid_len = 0;
        wifi_config->uid[0] = '\0';
        
        // 新格式的协议结构：
        // 1. password 字段 = password + appendString（一起编码）
        // 2. appendBytes（在 password 字段之后，如果 needBind=true，包含 "\x1B" + userId）
        //
        // appendString 格式："\u0000\u001b$domain\u001B$h$m\u001B$timeZoneCode"
        // appendBytes 格式（如果 needBind）："\u001B" + userId.toByteArray()
        
        // 从 password 字段中提取实际的密码（去除 appendString）
        // appendString 以 0x00 0x1B 开头
        // appendString 格式："\u0000\u001b$domain\u001B$h$m\u001B$timeZoneCode"
        uint8_t *password_bytes = (uint8_t *)wifi_config->password;
        size_t original_password_len = wifi_config->password_len;  // 保存原始长度
        
        // 初始化新协议字段
        wifi_config->domain_len = 0;
        wifi_config->domain[0] = '\0';
        wifi_config->timezone_h = '\0';
        wifi_config->timezone_m = '\0';
        wifi_config->timezone_code_len = 0;
        wifi_config->timezone_code[0] = '\0';
        
        // 查找 appendString 的起始位置（0x00 0x1B）
        // 注意：使用字节比较，因为 password 可能包含二进制数据
        size_t append_string_start = 0;
        for (size_t i = 0; i < original_password_len - 1; i++) {
            if (password_bytes[i] == 0x00 && password_bytes[i + 1] == 0x1B) {
                // 找到 appendString 的起始位置
                append_string_start = i + 2;  // 跳过 0x00 0x1B
                // 截断 password（在实际密码末尾添加结束符）
                password_bytes[i] = '\0';
                wifi_config->password_len = i;
                printf("Extracted actual password (removed appendString), new length: %d\n", 
                       wifi_config->password_len);
                break;
            }
        }
        
        // 解析 appendString 中的字段
        // 格式：domain + 0x1B + h + m + 0x1B + timeZoneCode
        if (append_string_start > 0 && append_string_start < original_password_len) {
            size_t pos = append_string_start;
            
            // 1. 解析 domain（直到遇到 0x1B）
            size_t domain_start = pos;
            while (pos < original_password_len && password_bytes[pos] != 0x1B) {
                pos++;
            }
            if (pos > domain_start && pos < original_password_len) {
                size_t domain_len = pos - domain_start;
                if (domain_len < sizeof(wifi_config->domain)) {
                    memcpy(wifi_config->domain, &password_bytes[domain_start], domain_len);
                    wifi_config->domain[domain_len] = '\0';
                    wifi_config->domain_len = domain_len;
                    printf("Parsed domain: %s (length: %d)\n", wifi_config->domain, wifi_config->domain_len);
                }
                pos++;  // 跳过 0x1B
            }
            
            // 2. 解析 h 和 m（各一个字符）
            if (pos + 2 <= original_password_len) {
                wifi_config->timezone_h = password_bytes[pos];
                wifi_config->timezone_m = password_bytes[pos + 1];
                pos += 2;
                printf("Parsed timezone: h=%c, m=%c\n", wifi_config->timezone_h, wifi_config->timezone_m);
                
                // 3. 跳过下一个 0x1B（如果存在）
                if (pos < original_password_len && password_bytes[pos] == 0x1B) {
                    pos++;
                }
                
                // 4. 解析 timeZoneCode（剩余部分）
                if (pos < original_password_len) {
                    size_t timezone_code_start = pos;
                    size_t timezone_code_len = original_password_len - pos;
                    if (timezone_code_len < sizeof(wifi_config->timezone_code)) {
                        memcpy(wifi_config->timezone_code, &password_bytes[timezone_code_start], timezone_code_len);
                        wifi_config->timezone_code[timezone_code_len] = '\0';
                        wifi_config->timezone_code_len = timezone_code_len;
                        printf("Parsed timezone_code: %s (length: %d)\n", 
                               wifi_config->timezone_code, wifi_config->timezone_code_len);
                    }
                }
            }
        }
        
        // 处理 appendBytes（在 password 字段之后）
        // appendBytes 可能包含 "\x1B" + userId（如果 needBind=true）
        if (offset < len) {
            size_t append_bytes_len = len - offset;
            printf("Append bytes length: %zu\n", append_bytes_len);
            
            // 查找 "\x1B" 后的 userId
            if (append_bytes_len > 0 && data[offset] == 0x1B && append_bytes_len > 1) {
                // 提取 userId（从 0x1B 之后开始）
                size_t uid_data_len = append_bytes_len - 1;  // 减去 0x1B
                if (uid_data_len > 0 && uid_data_len < sizeof(wifi_config->uid)) {
                    memcpy(wifi_config->uid, &data[offset + 1], uid_data_len);
                    wifi_config->uid[uid_data_len] = '\0';
                    wifi_config->uid_len = uid_data_len;
                    printf("Extracted UID from appendBytes: %s (length: %d)\n", 
                           wifi_config->uid, wifi_config->uid_len);
                } else {
                    printf("UID data length out of range: %zu\n", uid_data_len);
                }
            } else if (append_bytes_len > 0) {
                printf("Append bytes present but no UID (first byte: 0x%02X)\n", data[offset]);
            }
        }
    }

    // 打印最终配置摘要
    printf("\nWiFi Configuration Summary:\n");
    printf("  Protocol Format: %s\n", is_old_format ? "Old (with UID field)" : "New (with append data)");
    printf("  NTP: %lu\n", wifi_config->ntp);
    printf("  SSID (%d): %s\n", wifi_config->ssid_len, wifi_config->ssid);
    if (wifi_config->bssid_len > 0) {
        printf("  BSSID (%d): ", wifi_config->bssid_len);
        for (int i = 0; i < wifi_config->bssid_len; i++) {
            printf("%02X", (uint8_t)wifi_config->bssid[i]);
            if (i < wifi_config->bssid_len - 1) printf(":");
        }
        printf("\n");
    }
    printf("  Password (%d): %s\n", wifi_config->password_len, wifi_config->password);
    if (wifi_config->uid_len > 0) {
        printf("  UID (%d): %s\n", wifi_config->uid_len, wifi_config->uid);
    } else {
        printf("  UID: (empty)\n");
    }
    // 新协议格式的附加字段
    if (!is_old_format) {
        if (wifi_config->domain_len > 0) {
            printf("  Domain (%d): %s\n", wifi_config->domain_len, wifi_config->domain);
        }
        if (wifi_config->timezone_h != '\0') {
            printf("  Timezone: h=%c, m=%c\n", wifi_config->timezone_h, wifi_config->timezone_m);
        }
        if (wifi_config->timezone_code_len > 0) {
            printf("  Timezone Code (%d): %s\n", wifi_config->timezone_code_len, wifi_config->timezone_code);
        }
    }

    return true;
}

// 解析协议数据
protocol_data_t protocol_parse_data(const uint8_t *data, size_t len) {
    protocol_data_t result = {0};
    result.success = false;

    if (len < 4) {  // 确保至少有协议头的长度
        printf("Invalid protocol data length\n");
        return result;
    }

    // 解析协议头
    protocol_header_t header;
    parse_protocol_header(data, &header);
    
    // 打印协议头信息
    print_protocol_header(&header);

    // 设置命令类型和消息ID
    result.cmd = header.cmd;
    result.msg_id = header.byte0.msg_id;
    
    // 只解析协议头，不解析具体数据
    // 具体数据的解析应该在 switch 的各个 case 中进行
    result.success = true;

    return result;
}
