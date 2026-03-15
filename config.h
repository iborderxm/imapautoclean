// config.h - 编译时配置常量和错误码定义
// IMAP批量清理工具
// 用于MIPSel架构资源受限设备

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

// ==================== 批次处理配置 ====================
#define BATCH_SIZE          100         // 每批次UID数量，避免内存占用过大
#define CMD_BUF_SIZE        2048        // IMAP命令缓冲区大小
#define RESP_BUF_SIZE       8192        // IMAP响应缓冲区大小（8KB足够解析单行响应）
#define LINE_BUF_SIZE       1024        // 行读取缓冲区大小

// ==================== 字符串长度限制 ====================
#define MAX_SERVER_LEN      256         // IMAP服务器地址最大长度
#define MAX_USER_LEN        128         // 用户名最大长度
#define MAX_PASS_LEN        128         // 密码/授权码最大长度
#define MAX_FOLDER_LEN      256         // 文件夹名称最大长度
#define MAX_KEYWORD_LEN     128         // 搜索关键词最大长度
#define TEMP_PATH_LEN       64          // 临时文件路径最大长度
#define MAX_BATCH_FILES     100         // 最多支持的批次文件数量

// ==================== 临时文件配置 ====================
#define TEMP_DIR            "/tmp"      // 临时文件目录
#define TEMP_PREFIX         "imap_batch_" // 临时文件前缀

// ==================== 网络与间隔配置 ====================
#define BATCH_INTERVAL      30          // 批次间隔秒数，避免触发服务器限流
#define NETWORK_TIMEOUT     30000       // 网络超时毫秒数

// ==================== 错误码定义 ====================
enum {
    ERR_OK = 0,              // 操作成功
    ERR_PARAM = -1,          // 参数错误
    ERR_CONNECT = -2,        // 连接失败
    ERR_TLS = -3,            // TLS错误
    ERR_AUTH = -4,           // 认证失败
    ERR_SELECT = -5,         // 选择文件夹失败
    ERR_SEARCH = -6,         // 搜索失败
    ERR_DELETE = -7,         // 删除失败
    ERR_FILE = -8,           // 文件操作错误
    ERR_MEMORY = -9,         // 内存不足（预留）
    ERR_TIMEOUT = -10,       // 超时错误
    ERR_NETWORK = -11,       // 网络错误
};

#endif // CONFIG_H
