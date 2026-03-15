// utils.c - 工具函数实现
// IMAP批量清理工具
// 实现日期计算、临时文件管理等功能

#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

// ==================== 全局静态变量 ====================
// 存储所有批次文件路径，静态分配避免动态内存
static char g_batch_files[MAX_BATCH_FILES][TEMP_PATH_LEN];
static int g_batch_count = 0;

// ==================== 信号处理函数 ====================

// 清理所有临时文件的信号处理函数
// 参数:
//   sig - 信号编号（未使用）
static void cleanup_temp_files(int sig) {
    (void)sig;  // 避免未使用参数警告
    
    // 遍历所有批次文件并删除
    for (int i = 0; i < g_batch_count; i++) {
        unlink(g_batch_files[i]);
    }
    
    // 立即退出，不执行清理代码
    _exit(1);
}

// ==================== 日期处理函数实现 ====================

// 获取N天前的日期，格式化为IMAP SEARCH格式: "01-Jan-2024"
void get_before_date(int days_ago, char *out, size_t out_len) {
    time_t now = time(NULL);
    // 计算N天前的时间戳（每天86400秒）
    time_t before = now - ((time_t)days_ago * 24 * 3600);
    struct tm *tm = gmtime(&before);  // 使用GMT时间
    
    // 月份英文缩写数组
    static const char *months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    
    // 格式化为IMAP要求的日期格式
    snprintf(out, out_len, "%02d-%s-%04d",
             tm->tm_mday, months[tm->tm_mon], tm->tm_year + 1900);
}

// ==================== 临时文件管理函数实现 ====================

// 初始化临时文件管理器，注册信号处理函数
void init_temp_file_manager(void) {
    // 注册中断信号处理
    signal(SIGINT, cleanup_temp_files);
    // 注册终止信号处理
    signal(SIGTERM, cleanup_temp_files);
    // 忽略管道破裂信号（避免网络断开时程序崩溃）
    signal(SIGPIPE, SIG_IGN);
    // 重置批次计数
    g_batch_count = 0;
}

// 创建批次临时文件，将UID批次写入
int create_batch_file(uid_batch_t *batch) {
    // 检查批次数量是否超限
    if (g_batch_count >= MAX_BATCH_FILES) {
        return -1;
    }
    
    char template[TEMP_PATH_LEN];
    // 构造临时文件模板
    snprintf(template, sizeof(template), "%s/%sXXXXXX", TEMP_DIR, TEMP_PREFIX);
    
    // 创建唯一临时文件，mkstemp会修改template为实际文件名
    int fd = mkstemp(template);
    if (fd < 0) {
        return -1;
    }
    
    // 二进制方式写入UID数组（紧凑存储，节省空间）
    ssize_t written = write(fd, batch->uids, batch->count * sizeof(uint32_t));
    close(fd);
    
    // 检查写入是否完整
    if (written != (ssize_t)(batch->count * sizeof(uint32_t))) {
        unlink(template);  // 写入失败，删除不完整的文件
        return -1;
    }
    
    // 保存文件路径到全局数组
    strncpy(g_batch_files[g_batch_count], template, TEMP_PATH_LEN - 1);
    g_batch_files[g_batch_count][TEMP_PATH_LEN - 1] = '\0';  // 确保字符串结束
    g_batch_count++;
    
    return 0;
}

// 读取并删除批次文件
int read_and_delete_batch(const char *filename, uid_batch_t *batch) {
    // 以只读方式打开文件
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    
    // 读取UID数组
    ssize_t n = read(fd, batch->uids, sizeof(batch->uids));
    close(fd);
    
    if (n < 0) {
        return -1;
    }
    
    // 计算UID数量
    batch->count = n / sizeof(uint32_t);
    unlink(filename);  // 读取完成后立即删除文件
    
    return batch->count;
}

// 清理所有批次文件
void cleanup_all_batches(void) {
    for (int i = 0; i < g_batch_count; i++) {
        unlink(g_batch_files[i]);
    }
    g_batch_count = 0;
}

// 检查是否存在未完成的批次文件
int check_resume_state(void) {
    DIR *dir = opendir(TEMP_DIR);
    if (!dir) {
        return 0;  // 无法打开目录，视为无待处理文件
    }
    
    struct dirent *entry;
    int count = 0;
    size_t prefix_len = strlen(TEMP_PREFIX);
    
    // 遍历目录中的所有文件
    while ((entry = readdir(dir)) != NULL) {
        // 检查文件名是否以指定前缀开头
        if (strncmp(entry->d_name, TEMP_PREFIX, prefix_len) == 0) {
            count++;
        }
    }
    
    closedir(dir);
    return count;
}

// 获取当前批次文件数量
int get_batch_count(void) {
    return g_batch_count;
}

// 获取指定索引的批次文件路径
const char *get_batch_file(int index) {
    if (index < 0 || index >= g_batch_count) {
        return NULL;
    }
    return g_batch_files[index];
}

// 重置批次计数（清空所有批次记录）
void reset_batch_count(void) {
    g_batch_count = 0;
}
