// imap_client.c - IMAP协议客户端实现
// IMAP批量清理工具
// 实现IMAP协议的核心功能：连接、认证、搜索、删除等

#include "imap_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ==================== 全局静态缓冲区 ====================
// 使用静态缓冲区避免动态内存分配
static char g_cmd_buf[CMD_BUF_SIZE];
static char g_line_buf[LINE_BUF_SIZE];
static int g_tag_seq = 0;  // IMAP命令标签序列号

// ==================== 内部辅助函数声明 ====================

// 生成下一个IMAP命令标签
static void next_tag(char *tag, size_t tag_len);

// 发送IMAP命令
static int send_command(tls_context_t *tls_ctx, const char *cmd);

// 读取一行响应
static int read_line(tls_context_t *tls_ctx, char *buf, size_t buf_len);

// 检查响应是否成功（以OK结尾）
static int check_response(tls_context_t *tls_ctx, const char *tag);

// 构建UID STORE命令
static int build_uid_store_cmd(char *buf, size_t buf_size, 
                               const uint32_t *uids, int count);

// ==================== 内部辅助函数实现 ====================

// 生成下一个IMAP命令标签（格式: A0001, A0002, ...）
static void next_tag(char *tag, size_t tag_len) {
    g_tag_seq++;
    snprintf(tag, tag_len, "A%04d", g_tag_seq);
}

// 发送IMAP命令
static int send_command(tls_context_t *tls_ctx, const char *cmd) {
    size_t len = strlen(cmd);
    int ret = tls_send(tls_ctx, (const unsigned char *)cmd, len);
    if (ret < 0) {
        return ERR_NETWORK;
    }
    return ERR_OK;
}

// 读取一行响应
static int read_line(tls_context_t *tls_ctx, char *buf, size_t buf_len) {
    size_t pos = 0;
    int ret;
    
    while (pos < buf_len - 1) {
        ret = tls_recv(tls_ctx, (unsigned char *)&buf[pos], 1);
        if (ret <= 0) {
            return ret;  // 错误或连接关闭
        }
        
        if (buf[pos] == '\n') {
            // 移除末尾的\r（如果存在）
            if (pos > 0 && buf[pos - 1] == '\r') {
                pos--;
            }
            buf[pos] = '\0';
            return 1;  // 成功读取一行
        }
        
        pos++;
    }
    
    // 缓冲区已满
    buf[buf_len - 1] = '\0';
    return 1;
}

// 检查响应是否成功（读取到指定标签的OK响应）
static int check_response(tls_context_t *tls_ctx, const char *tag) {
    size_t tag_len = strlen(tag);
    
    while (1) {
        int ret = read_line(tls_ctx, g_line_buf, sizeof(g_line_buf));
        if (ret <= 0) {
            return ERR_NETWORK;
        }
        
        // 10086 日志：服务器响应
        printf("[10086] IMAP: 服务器响应: %s\n", g_line_buf);
        
        // 检查是否是标签响应行
        if (strncmp(g_line_buf, tag, tag_len) == 0 && g_line_buf[tag_len] == ' ') {
            // 检查是否是OK响应
            if (strstr(g_line_buf, "OK") != NULL) {
                return ERR_OK;
            } else {
                // NO或BAD响应
                return ERR_SELECT;
            }
        }
    }
}

// 构建UID STORE命令
static int build_uid_store_cmd(char *buf, size_t buf_size, 
                               const uint32_t *uids, int count) {
    int pos = 0;
    int n;
    
    // 添加命令前缀
    n = snprintf(buf + pos, buf_size - pos, "UID STORE ");
    if (n < 0 || (size_t)n >= buf_size - pos) return -1;
    pos += n;
    
    // 添加UID列表，用逗号分隔
    for (int i = 0; i < count; i++) {
        if (i > 0) {
            n = snprintf(buf + pos, buf_size - pos, ",");
            if (n < 0 || (size_t)n >= buf_size - pos) return -1;
            pos += n;
        }
        n = snprintf(buf + pos, buf_size - pos, "%u", uids[i]);
        if (n < 0 || (size_t)n >= buf_size - pos) return -1;
        pos += n;
    }
    
    // 添加删除标记
    n = snprintf(buf + pos, buf_size - pos, " +FLAGS (\\Deleted)");
    if (n < 0 || (size_t)n >= buf_size - pos) return -1;
    pos += n;
    
    return pos;
}

// ==================== 公共IMAP函数实现 ====================

// 建立IMAP连接并认证
int imap_connect_and_auth(tls_context_t *tls_ctx, const config_t *cfg) {
    int ret;
    char tag[16];
    
    // ==================== 1. 初始化TLS并连接 ====================
    tls_init(tls_ctx);
    ret = tls_connect(tls_ctx, cfg->server, cfg->port, NETWORK_TIMEOUT);
    if (ret != ERR_OK) {
        return ret;
    }
    
    // 读取服务器banner
    ret = read_line(tls_ctx, g_line_buf, sizeof(g_line_buf));
    if (ret <= 0) {
        tls_close(tls_ctx);
        return ERR_NETWORK;
    }
    
    // ==================== 2. 发送CAPABILITY命令 ====================
    next_tag(tag, sizeof(tag));
    snprintf(g_cmd_buf, sizeof(g_cmd_buf), "%s CAPABILITY\r\n", tag);
    ret = send_command(tls_ctx, g_cmd_buf);
    if (ret != ERR_OK) {
        tls_close(tls_ctx);
        return ret;
    }
    ret = check_response(tls_ctx, tag);
    if (ret != ERR_OK) {
        tls_close(tls_ctx);
        return ret;
    }
    
    // ==================== 3. LOGIN认证 ====================
    next_tag(tag, sizeof(tag));
    snprintf(g_cmd_buf, sizeof(g_cmd_buf), "%s LOGIN \"%s\" \"%s\"\r\n", 
             tag, cfg->username, cfg->password);
    ret = send_command(tls_ctx, g_cmd_buf);
    if (ret != ERR_OK) {
        tls_close(tls_ctx);
        return ret;
    }
    ret = check_response(tls_ctx, tag);
    if (ret != ERR_OK) {
        tls_close(tls_ctx);
        return ERR_AUTH;
    }
    
    return ERR_OK;
}

// 选择IMAP文件夹
int imap_select_folder(tls_context_t *tls_ctx, const char *folder) {
    int ret;
    char tag[16];
    char encoded_folder[MAX_FOLDER_LEN * 2]; // 为编码后的数据预留足够空间
    
    // 10086 日志：开始选择文件夹
    printf("[10086] IMAP: 开始选择文件夹，原始名称: %s\n", folder);
    
    // 将文件夹名称转换为IMAP UTF-7格式
    if (utf8_to_imap_utf7(folder, encoded_folder, sizeof(encoded_folder)) != 0) {
        // 10086 日志：编码失败
        fprintf(stderr, "[10086] IMAP: 文件夹名称编码失败\n");
        return ERR_PARAM;
    }
    
    // 10086 日志：编码后的文件夹名称
    printf("[10086] IMAP: 编码后的文件夹名称: %s\n", encoded_folder);
    
    next_tag(tag, sizeof(tag));
    snprintf(g_cmd_buf, sizeof(g_cmd_buf), "%s SELECT \"%s\"\r\n", tag, encoded_folder);
    
    // 10086 日志：发送SELECT命令
    printf("[10086] IMAP: 发送命令: %s", g_cmd_buf);
    
    ret = send_command(tls_ctx, g_cmd_buf);
    if (ret != ERR_OK) {
        // 10086 日志：发送命令失败
        fprintf(stderr, "[10086] IMAP: 发送命令失败，错误码: %d\n", ret);
        return ret;
    }
    
    ret = check_response(tls_ctx, tag);
    if (ret != ERR_OK) {
        // 10086 日志：响应失败
        fprintf(stderr, "[10086] IMAP: 服务器响应失败，错误码: %d\n", ret);
        return ERR_SELECT;
    }
    
    // 10086 日志：选择文件夹成功
    printf("[10086] IMAP: 选择文件夹成功\n");
    
    return ERR_OK;
}

// 搜索符合条件的邮件UID
int imap_search_uids(tls_context_t *tls_ctx, const char *before_date, 
                     const char *keyword, uid_callback_t callback, void *callback_ctx) {
    int ret;
    char tag[16];
    int uid_count = 0;
    size_t tag_len;
    
    // ==================== 1. 构建SEARCH命令 ====================
    next_tag(tag, sizeof(tag));
    if (keyword && keyword[0] != '\0') {
        // 有关键词：搜索日期前且标题包含关键词
        snprintf(g_cmd_buf, sizeof(g_cmd_buf), 
                 "%s UID SEARCH BEFORE \"%s\" SUBJECT \"%s\"\r\n", 
                 tag, before_date, keyword);
    } else {
        // 无关键词：仅搜索日期前
        snprintf(g_cmd_buf, sizeof(g_cmd_buf), 
                 "%s UID SEARCH BEFORE \"%s\"\r\n", 
                 tag, before_date);
    }
    
    // ==================== 2. 发送命令 ====================
    ret = send_command(tls_ctx, g_cmd_buf);
    if (ret != ERR_OK) {
        return -1;
    }
    
    // ==================== 3. 解析响应 ====================
    tag_len = strlen(tag);
    
    while (1) {
        ret = read_line(tls_ctx, g_line_buf, sizeof(g_line_buf));
        if (ret <= 0) {
            return -1;
        }
        
        // 检查是否是SEARCH响应行（格式: * SEARCH uid1 uid2 ...）
        if (strncmp(g_line_buf, "* SEARCH", 8) == 0) {
            char *p = g_line_buf + 9;
            while (*p) {
                // 跳过空格
                while (*p && isspace((unsigned char)*p)) p++;
                if (!*p) break;
                
                // 解析UID
                uint32_t uid = 0;
                while (*p && isdigit((unsigned char)*p)) {
                    uid = uid * 10 + (*p - '0');
                    p++;
                }
                
                // 调用回调函数
                if (callback) {
                    if (callback(uid, callback_ctx) != 0) {
                        // 回调要求停止搜索
                        goto cleanup;
                    }
                }
                uid_count++;
            }
        }
        
        // 检查是否是标签响应行
        if (strncmp(g_line_buf, tag, tag_len) == 0 && g_line_buf[tag_len] == ' ') {
            if (strstr(g_line_buf, "OK") == NULL) {
                return -1;
            }
            break;
        }
    }
    
cleanup:
    return uid_count;
}

// 批量标记邮件为删除
int imap_mark_deleted(tls_context_t *tls_ctx, const uint32_t *uids, int count) {
    int ret;
    char tag[16];
    int pos = 0;
    int n;
    
    if (count <= 0) {
        return ERR_OK;
    }
    
    // 生成标签
    next_tag(tag, sizeof(tag));
    
    // 构建完整命令：标签 + 命令 + CRLF
    n = snprintf(g_cmd_buf + pos, sizeof(g_cmd_buf) - pos, "%s ", tag);
    if (n < 0 || (size_t)n >= sizeof(g_cmd_buf) - pos) return ERR_PARAM;
    pos += n;
    
    n = build_uid_store_cmd(g_cmd_buf + pos, sizeof(g_cmd_buf) - pos - 2, uids, count);
    if (n < 0) return ERR_PARAM;
    pos += n;
    
    // 添加CRLF
    g_cmd_buf[pos++] = '\r';
    g_cmd_buf[pos++] = '\n';
    g_cmd_buf[pos] = '\0';
    
    // 发送命令
    ret = send_command(tls_ctx, g_cmd_buf);
    if (ret != ERR_OK) {
        return ret;
    }
    
    return check_response(tls_ctx, tag);
}

// 永久删除已标记的邮件
int imap_expunge(tls_context_t *tls_ctx) {
    int ret;
    char tag[16];
    
    next_tag(tag, sizeof(tag));
    snprintf(g_cmd_buf, sizeof(g_cmd_buf), "%s EXPUNGE\r\n", tag);
    ret = send_command(tls_ctx, g_cmd_buf);
    if (ret != ERR_OK) {
        return ret;
    }
    
    return check_response(tls_ctx, tag);
}

// 登出IMAP服务器
int imap_logout(tls_context_t *tls_ctx) {
    int ret;
    char tag[16];
    
    next_tag(tag, sizeof(tag));
    snprintf(g_cmd_buf, sizeof(g_cmd_buf), "%s LOGOUT\r\n", tag);
    ret = send_command(tls_ctx, g_cmd_buf);
    if (ret != ERR_OK) {
        return ret;
    }
    
    // 读取响应，不检查结果（登出可能直接关闭连接）
    read_line(tls_ctx, g_line_buf, sizeof(g_line_buf));
    
    return ERR_OK;
}

// 列出所有IMAP文件夹
int imap_list_folders(tls_context_t *tls_ctx) {
    int ret;
    char tag[16];
    
    // 10086 日志：开始列出文件夹
    printf("[10086] IMAP: 开始列出所有文件夹\n");
    
    next_tag(tag, sizeof(tag));
    snprintf(g_cmd_buf, sizeof(g_cmd_buf), "%s LIST *\r\n", tag);
    
    // 10086 日志：发送LIST命令
    printf("[10086] IMAP: 发送命令: %s", g_cmd_buf);
    
    ret = send_command(tls_ctx, g_cmd_buf);
    if (ret != ERR_OK) {
        // 10086 日志：发送命令失败
        fprintf(stderr, "[10086] IMAP: 发送LIST命令失败，错误码: %d\n", ret);
        return ret;
    }
    
    size_t tag_len = strlen(tag);
    bool done = false;
    
    // 10086 日志：开始接收文件夹列表
    printf("[10086] IMAP: 开始接收文件夹列表\n");
    
    while (!done) {
        ret = read_line(tls_ctx, g_line_buf, sizeof(g_line_buf));
        if (ret <= 0) {
            return ERR_NETWORK;
        }
        
        // 10086 日志：服务器响应
        printf("[10086] IMAP: 服务器响应: %s\n", g_line_buf);
        
        // 检查是否是LIST响应行（格式: * LIST (...) "folder"）
        if (strncmp(g_line_buf, "* LIST", 6) == 0) {
            // 提取文件夹名称
            char *folder_start = strchr(g_line_buf, '"');
            if (folder_start) {
                folder_start++;
                char *folder_end = strchr(folder_start, '"');
                if (folder_end) {
                    *folder_end = '\0';
                    // 10086 日志：找到文件夹
                    printf("[10086] IMAP: 找到文件夹: %s\n", folder_start);
                }
            }
        }
        
        // 检查是否是标签响应行
        if (strncmp(g_line_buf, tag, tag_len) == 0 && g_line_buf[tag_len] == ' ') {
            // 检查是否是OK响应
            if (strstr(g_line_buf, "OK") != NULL) {
                // 10086 日志：列出文件夹成功
                printf("[10086] IMAP: 列出文件夹成功\n");
                return ERR_OK;
            } else {
                // 10086 日志：列出文件夹失败
                fprintf(stderr, "[10086] IMAP: 列出文件夹失败\n");
                return ERR_SELECT;
            }
        }
    }
    
    return ERR_OK;
}
