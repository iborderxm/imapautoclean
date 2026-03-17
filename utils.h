// utils.h - 工具函数头文件
// IMAP批量清理工具
// 包含日期计算、临时文件管理等通用工具函数

#ifndef UTILS_H
#define UTILS_H

#include "config.h"
#include <stddef.h>

// ==================== UID批次数据结构 ====================
typedef struct {
    uint32_t uids[BATCH_SIZE];  // UID数组，静态分配避免动态内存
    int count;                    // 当前批次UID数量
} uid_batch_t;

// ==================== 日期处理函数 ====================

// 获取N天前的日期，格式化为IMAP SEARCH格式: "01-Jan-2024"
// 参数:
//   days_ago - 要计算的天数前
//   out - 输出缓冲区
//   out_len - 输出缓冲区大小
void get_before_date(int days_ago, char *out, size_t out_len);

// ==================== 临时文件管理函数 ====================

// 初始化临时文件管理器，注册信号处理函数
void init_temp_file_manager(void);

// 创建批次临时文件，将UID批次写入
// 参数:
//   batch - UID批次数据
// 返回:
//   成功返回0，失败返回-1
int create_batch_file(uid_batch_t *batch);

// 读取并删除批次文件
// 参数:
//   filename - 批次文件路径
//   batch - 输出UID批次数据
// 返回:
//   成功返回读取到的UID数量，失败返回-1
int read_and_delete_batch(const char *filename, uid_batch_t *batch);

// 清理所有批次文件
void cleanup_all_batches(void);

// 检查是否存在未完成的批次文件
// 返回:
//   未完成的批次文件数量
int check_resume_state(void);

// 获取当前批次文件数量
// 返回:
//   批次文件数量
int get_batch_count(void);

// 获取指定索引的批次文件路径
// 参数:
//   index - 批次索引
// 返回:
//   批次文件路径，失败返回NULL
const char *get_batch_file(int index);

// 重置批次计数（清空所有批次记录）
void reset_batch_count(void);

// ==================== 编码转换函数 ====================

// 将UTF-8字符串编码为IMAP UTF-7格式
// 参数:
//   utf8 - 输入的UTF-8字符串
//   out - 输出缓冲区
//   out_len - 输出缓冲区大小
// 返回:
//   成功返回0，失败返回-1
int utf8_to_imap_utf7(const char *utf8, char *out, size_t out_len);

// 转义IMAP字符串中的特殊字符（引号和反斜杠）
// 参数:
//   input - 输入字符串
//   out - 输出缓冲区
//   out_len - 输出缓冲区大小
// 返回:
//   成功返回0，失败返回-1
int imap_escape_string(const char *input, char *out, size_t out_len);

#endif // UTILS_H
