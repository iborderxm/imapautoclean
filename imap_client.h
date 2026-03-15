// imap_client.h - IMAP协议客户端头文件
// IMAP批量清理工具
// 提供IMAP协议的核心功能接口

#ifndef IMAP_CLIENT_H
#define IMAP_CLIENT_H

#include "config.h"
#include "tls_mbedtls.h"
#include "utils.h"

// ==================== 配置数据结构 ====================
typedef struct {
    char server[MAX_SERVER_LEN];      // IMAP服务器地址
    int port;                          // IMAP服务器端口
    char username[MAX_USER_LEN];      // 邮箱账号
    char password[MAX_PASS_LEN];      // 密码/授权码
    char folder[MAX_FOLDER_LEN];      // 目标文件夹
    int keep_days;                     // 保留天数
    char keyword[MAX_KEYWORD_LEN];    // 标题关键词（可选）
    bool has_keyword;                  // 是否有关键词
} config_t;

// ==================== 搜索UID回调函数类型 ====================
// 参数:
//   uid - 邮件UID
//   ctx - 用户上下文指针
// 返回:
//   继续搜索返回0，停止搜索返回非0
typedef int (*uid_callback_t)(uint32_t uid, void *ctx);

// ==================== IMAP客户端函数 ====================

// 建立IMAP连接并认证
// 参数:
//   tls_ctx - TLS上下文指针
//   cfg - 配置信息指针
// 返回:
//   成功返回0，失败返回错误码
int imap_connect_and_auth(tls_context_t *tls_ctx, const config_t *cfg);

// 选择IMAP文件夹
// 参数:
//   tls_ctx - TLS上下文指针
//   folder - 文件夹名称
// 返回:
//   成功返回0，失败返回错误码
int imap_select_folder(tls_context_t *tls_ctx, const char *folder);

// 搜索符合条件的邮件UID
// 参数:
//   tls_ctx - TLS上下文指针
//   before_date - 日期（格式: "01-Jan-2024"）
//   keyword - 标题关键词（NULL表示无关键词）
//   callback - UID回调函数
//   callback_ctx - 回调函数上下文
// 返回:
//   成功返回找到的UID数量，失败返回-1
int imap_search_uids(tls_context_t *tls_ctx, const char *before_date, 
                     const char *keyword, uid_callback_t callback, void *callback_ctx);

// 批量标记邮件为删除
// 参数:
//   tls_ctx - TLS上下文指针
//   uids - UID数组
//   count - UID数量
// 返回:
//   成功返回0，失败返回错误码
int imap_mark_deleted(tls_context_t *tls_ctx, const uint32_t *uids, int count);

// 永久删除已标记的邮件
// 参数:
//   tls_ctx - TLS上下文指针
// 返回:
//   成功返回0，失败返回错误码
int imap_expunge(tls_context_t *tls_ctx);

// 登出IMAP服务器
// 参数:
//   tls_ctx - TLS上下文指针
// 返回:
//   成功返回0，失败返回错误码
int imap_logout(tls_context_t *tls_ctx);

#endif // IMAP_CLIENT_H
