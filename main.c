// main.c - IMAP批量清理工具主程序
// IMAP批量清理工具
// 程序入口、参数解析、主流程控制

#include "config.h"
#include "utils.h"
#include "imap_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ==================== 全局变量 ====================
static uid_batch_t g_current_batch;  // 当前UID批次

// ==================== 搜索UID回调函数 ====================
// 将搜索到的UID收集到批次中，满了就写入临时文件
static int on_search_uid(uint32_t uid, void *ctx) {
    (void)ctx;  // 未使用上下文
    
    // 添加UID到当前批次
    g_current_batch.uids[g_current_batch.count] = uid;
    g_current_batch.count++;
    
    // 如果批次已满，写入临时文件
    if (g_current_batch.count >= BATCH_SIZE) {
        if (create_batch_file(&g_current_batch) != 0) {
            return -1;  // 写入失败，停止搜索
        }
        g_current_batch.count = 0;  // 重置批次
    }
    
    return 0;  // 继续搜索
}

// ==================== 参数解析函数 ====================
// 解析命令行参数
// 参数:
//   argc - 参数数量
//   argv - 参数数组
//   cfg - 输出配置结构
// 返回:
//   成功返回0，失败返回-1
static int parse_args(int argc, char *argv[], config_t *cfg) {
    int opt;
    
    // 初始化配置
    memset(cfg, 0, sizeof(config_t));
    cfg->has_keyword = false;
    cfg->keep_days = -1;
    cfg->port = -1;
    
    // 解析命令行参数
    while ((opt = getopt(argc, argv, "s:u:p:f:d:k:")) != -1) {
        switch (opt) {
            case 's': {
                // 解析服务器地址和端口（格式: server:port）
                char *colon = strchr(optarg, ':');
                if (colon == NULL) {
                    fprintf(stderr, "错误: 服务器地址格式错误，应为: server:port\n");
                    return -1;
                }
                // 复制服务器地址
                size_t server_len = colon - optarg;
                if (server_len >= MAX_SERVER_LEN) {
                    fprintf(stderr, "错误: 服务器地址过长\n");
                    return -1;
                }
                strncpy(cfg->server, optarg, server_len);
                cfg->server[server_len] = '\0';
                // 解析端口
                cfg->port = atoi(colon + 1);
                if (cfg->port <= 0 || cfg->port > 65535) {
                    fprintf(stderr, "错误: 端口号无效\n");
                    return -1;
                }
                break;
            }
            case 'u':
                if (strlen(optarg) >= MAX_USER_LEN) {
                    fprintf(stderr, "错误: 用户名过长\n");
                    return -1;
                }
                strncpy(cfg->username, optarg, MAX_USER_LEN - 1);
                break;
            case 'p':
                if (strlen(optarg) >= MAX_PASS_LEN) {
                    fprintf(stderr, "错误: 密码过长\n");
                    return -1;
                }
                strncpy(cfg->password, optarg, MAX_PASS_LEN - 1);
                break;
            case 'f':
                if (strlen(optarg) >= MAX_FOLDER_LEN) {
                    fprintf(stderr, "错误: 文件夹名称过长\n");
                    return -1;
                }
                strncpy(cfg->folder, optarg, MAX_FOLDER_LEN - 1);
                break;
            case 'd':
                cfg->keep_days = atoi(optarg);
                if (cfg->keep_days <= 0) {
                    fprintf(stderr, "错误: 保留天数必须大于0\n");
                    return -1;
                }
                break;
            case 'k':
                if (strlen(optarg) >= MAX_KEYWORD_LEN) {
                    fprintf(stderr, "错误: 关键词过长\n");
                    return -1;
                }
                strncpy(cfg->keyword, optarg, MAX_KEYWORD_LEN - 1);
                cfg->has_keyword = true;
                break;
            default:
                fprintf(stderr, "用法: %s -s <服务器:端口> -u <账号> -p <授权码> -f <文件夹> -d <保留天数> [-k <关键词>]\n", argv[0]);
                return -1;
        }
    }
    
    // 检查必需参数
    if (cfg->server[0] == '\0' || cfg->port == -1 ||
        cfg->username[0] == '\0' || cfg->password[0] == '\0' ||
        cfg->folder[0] == '\0' || cfg->keep_days == -1) {
        fprintf(stderr, "用法: %s -s <服务器:端口> -u <账号> -p <授权码> -f <文件夹> -d <保留天数> [-k <关键词>]\n", argv[0]);
        return -1;
    }
    
    return 0;
}

// ==================== 主函数 ====================
int main(int argc, char *argv[]) {
    int ret;
    config_t cfg;
    tls_context_t tls_ctx;
    char before_date[32];
    int total_found = 0;
    
    // 10086 日志：程序启动
    printf("[10086] 程序启动，参数数量: %d\n", argc);
    
    // ==================== 1. 解析参数 ====================
    if (parse_args(argc, argv, &cfg) != 0) {
        return 1;
    }
    
    // 10086 日志：参数解析成功
    printf("[10086] 参数解析成功: 服务器=%s, 端口=%d, 用户=%s, 文件夹=%s, 保留天数=%d\n", 
           cfg.server, cfg.port, cfg.username, cfg.folder, cfg.keep_days);
    
    // ==================== 2. 初始化临时文件管理器 ====================
    init_temp_file_manager();
    
    // 检查是否有未完成的批次文件
    int pending_batches = check_resume_state();
    if (pending_batches > 0) {
        fprintf(stderr, "警告: 发现 %d 个未完成的批次文件，将忽略并重新开始\n", pending_batches);
    }
    
    // 初始化当前批次
    g_current_batch.count = 0;
    
    // ==================== 3. 计算日期 ====================
    get_before_date(cfg.keep_days, before_date, sizeof(before_date));
    printf("搜索 %s 之前的邮件...\n", before_date);
    
    // ==================== 4. 连接并认证 ====================
    printf("正在连接到 %s:%d...\n", cfg.server, cfg.port);
    ret = imap_connect_and_auth(&tls_ctx, &cfg);
    if (ret != ERR_OK) {
        fprintf(stderr, "连接或认证失败，错误码: %d\n", ret);
        cleanup_all_batches();
        return 1;
    }
    printf("认证成功\n");
    
    // ==================== 5. 选择文件夹 ====================
    printf("正在选择文件夹: %s...\n", cfg.folder);
    ret = imap_select_folder(&tls_ctx, cfg.folder);
    if (ret != ERR_OK) {
        fprintf(stderr, "选择文件夹失败，错误码: %d\n", ret);
        imap_logout(&tls_ctx);
        tls_close(&tls_ctx);
        cleanup_all_batches();
        return 1;
    }
    printf("文件夹选择成功\n");
    
    // ==================== 6. 搜索UID ====================
    // 10086 日志：开始搜索邮件
    const char *keyword = cfg.has_keyword ? cfg.keyword : NULL;
    printf("[10086] 开始搜索邮件，日期: %s, 关键词: %s\n", before_date, keyword ? keyword : "无");
    printf("正在搜索符合条件的邮件...\n");
    total_found = imap_search_uids(&tls_ctx, before_date, keyword, on_search_uid, NULL);
    if (total_found < 0) {
        fprintf(stderr, "搜索失败\n");
        imap_logout(&tls_ctx);
        tls_close(&tls_ctx);
        cleanup_all_batches();
        return 1;
    }
    
    // 处理最后一个未满的批次
    if (g_current_batch.count > 0) {
        if (create_batch_file(&g_current_batch) != 0) {
            fprintf(stderr, "创建批次文件失败\n");
            imap_logout(&tls_ctx);
            tls_close(&tls_ctx);
            cleanup_all_batches();
            return 1;
        }
    }
    
    printf("共找到 %d 封符合条件的邮件\n", total_found);
    
    // 如果没有找到邮件，直接退出
    if (total_found == 0) {
        printf("没有需要删除的邮件\n");
        imap_logout(&tls_ctx);
        tls_close(&tls_ctx);
        return 0;
    }
    
    // ==================== 7. 批量删除 ====================
    // 10086 日志：开始批量删除
    printf("[10086] 开始批量删除，共 %d 个批次\n", get_batch_count());
    printf("开始删除邮件...\n");
    
    int batch_count = get_batch_count();
    for (int i = 0; i < batch_count; i++) {
        const char *batch_file = get_batch_file(i);
        if (batch_file == NULL) {
            continue;
        }
        
        // 10086 日志：处理批次文件
        printf("[10086] 处理批次文件: %s\n", batch_file);
        uid_batch_t batch;
        int uid_count = read_and_delete_batch(batch_file, &batch);
        if (uid_count <= 0) {
            fprintf(stderr, "读取批次文件失败: %s\n", batch_file);
            continue;
        }
        
        printf("正在删除第 %d/%d 批，共 %d 封邮件...\n", i + 1, batch_count, uid_count);
        
        // 标记为删除
        ret = imap_mark_deleted(&tls_ctx, batch.uids, batch.count);
        if (ret != ERR_OK) {
            fprintf(stderr, "标记删除失败，错误码: %d\n", ret);
            continue;
        }
        
        // 永久删除
        ret = imap_expunge(&tls_ctx);
        if (ret != ERR_OK) {
            fprintf(stderr, "永久删除失败，错误码: %d\n", ret);
            continue;
        }
        
        printf("第 %d 批删除成功\n", i + 1);
        
        // 如果不是最后一批，等待一段时间
        if (i < batch_count - 1) {
            printf("等待 %d 秒后继续...\n", BATCH_INTERVAL);
            sleep(BATCH_INTERVAL);
        }
    }
    
    // ==================== 8. 登出并关闭连接 ====================
    // 10086 日志：开始登出
    printf("[10086] 开始登出并关闭连接\n");
    printf("正在登出...\n");
    imap_logout(&tls_ctx);
    tls_close(&tls_ctx);
    
    printf("完成！\n");
    return 0;
}
