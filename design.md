
## IMAP批量清理工具 - 开发设计文档

### 项目概述
开发一个用于MIPSel架构资源受限设备（路由器/嵌入式设备）的IMAP邮箱清理工具：
- **二进制体积目标**: < 500KB（极致优化）
- **运行时内存目标**: < 512KB（严格控制）
- **SSL/TLS库**: 动态链接 Mbed TLS 3.6.5

---

### 1. 命令行参数设计

**参数格式:**
```
./imap_cleaner -s <服务器:端口> -u <账号> -p <授权码> -f <文件夹> -d <保留天数> -k <关键词>
```

**参数说明:**
| 参数 | 必需 | 说明 | 示例 |
|------|------|------|------|
| `-s` | 是 | IMAP服务器地址和端口 | `imap.gmail.com:993` |
| `-u` | 是 | 邮箱账号 | `user@gmail.com` |
| `-p` | 是 | 授权码/密码 | `xxxxxxxx` |
| `-f` | 是 | 目标文件夹 | `INBOX/Ads` 或 `"INBOX/Ads"` |
| `-d` | 是 | 保留天数（删除N天前的邮件） | `30` |
| `-k` | 否 | 标题关键词过滤 | `promotion` |

**使用示例:**
```bash
# 基础用法 - 删除30天前的所有邮件
./imap_cleaner -s imap.gmail.com:993 -u user@gmail.com -p xxxxx -f "INBOX" -d 30

# 高级用法 - 删除30天前标题含promotion的邮件
./imap_cleaner -s imap.qq.com:993 -u 12345@qq.com -p xxxxx -f "INBOX/广告" -d 30 -k "promotion"
```

---

### 2. 批量处理流程

```
┌─────────────────────────────────────────────────────────────────┐
│                        主流程                                   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 1. 解析命令行参数                                               │
│    - 验证所有必需参数                                           │
│    - 解析服务器地址和端口                                       │
│    - URL解码文件夹名称（处理中文/特殊字符）                      │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 2. 建立TLS连接                                                  │
│    - TCP连接到IMAP服务器                                        │
│    - Mbed TLS SSL握手（跳过证书验证）                           │
│    - 设置30秒读写超时                                           │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 3. IMAP认证与会话                                               │
│    - CAPABILITY检查                                             │
│    - LOGIN认证                                                  │
│    - SELECT目标文件夹                                           │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 4. 搜索符合条件的邮件                                           │
│    - 构建SEARCH命令: BEFORE <date> [SUBJECT <keyword>]          │
│    - 流式解析UID响应                                            │
│    - 每收集100个UID写入临时文件                                 │
│    - 临时文件命名: /tmp/imap_batch_XXXXXX                       │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 5. 批量删除处理                                                 │
│    FOR each 临时文件:                                           │
│    ├─ 读取100个UID                                              │
│    ├─ UID STORE +FLAGS (\Deleted)                               │
│    ├─ EXPUNGE 永久删除                                          │
│    ├─ 删除临时文件                                              │
│    └─ sleep(30)  // 批次间隔                                    │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 6. 清理与退出                                                   │
│    - LOGOUT关闭会话                                             │
│    - 关闭TLS连接                                                │
│    - 清理所有临时文件（即使异常退出）                           │
└─────────────────────────────────────────────────────────────────┘
```

---

### 3. 资源约束详细设计

#### 3.1 内存布局（目标 < 512KB）

```
内存使用分布:
┌─────────────────────────────────────┐
│ 代码段 (.text)       ~ 80-120KB     │
│ 只读数据 (.rodata)   ~ 10-20KB      │
│ 已初始化数据 (.data) ~ 5-10KB       │
│ 未初始化数据 (.bss)  ~ 50-100KB     │
├─────────────────────────────────────┤
│ 运行时栈             ~ 32-64KB      │
│ Mbed TLS动态库       ~ 200-300KB    │
├─────────────────────────────────────┤
│ 总计                 ~ 400-500KB    │
└─────────────────────────────────────┘
```

**静态缓冲区分配策略:**
```c
// 全局静态缓冲区（在 .bss 段，不占用运行时堆）
static char g_cmd_buf[2048];        // IMAP命令缓冲区
static char g_resp_buf[8192];       // IMAP响应缓冲区（8KB足够解析单行响应）
static char g_line_buf[1024];       // 行读取缓冲区
static uint32_t g_uid_batch[100];   // UID批次数组（400字节）
static char g_temp_path[64];        // 临时文件路径
```

**禁止动态内存分配:**
- ❌ 不使用 `malloc/free`
- ❌ 不使用 `strdup`
- ❌ 不使用变长数组(VLA)
- ✅ 所有缓冲区使用静态分配或栈分配

#### 3.2 二进制体积优化（目标 < 500KB）

**编译优化选项:**
```makefile
# 极致体积优化
CFLAGS = -Os                    # 优化大小
CFLAGS += -s                    # 去除符号表
CFLAGS += -fno-stack-protector  # 禁用栈保护（节省体积）
CFLAGS += -fomit-frame-pointer  # 省略帧指针
CFLAGS += -ffunction-sections   # 函数独立段
CFLAGS += -fdata-sections       # 数据独立段
CFLAGS += -fno-unwind-tables    # 禁用异常展开表
CFLAGS += -fno-asynchronous-unwind-tables
CFLAGS += -fmerge-all-constants # 合并常量
CFLAGS += -fno-ident            # 去除编译器标识
CFLAGS += -Wl,--gc-sections     # 链接时移除未使用段
CFLAGS += -Wl,--strip-all       # 去除所有符号
CFLAGS += -Wl,-z,norelro        # 禁用RELRO（节省少量空间）
CFLAGS += -fno-plt              # 禁用PLT（如果支持）
```

**代码层面优化:**
- 使用错误码代替错误字符串
- 合并相似功能的函数
- 避免使用标准库的高级别函数（如`sprintf`，使用`snprintf`）
- 手动内联关键小函数

---

### 4. Mbed TLS 动态链接配置

#### 4.1 目标设备库信息
```
目标库文件（设备已存在）:
- /usr/lib/libmbedtls.so.3.6.5      (SSL/TLS协议层)
- /usr/lib/libmbedx509.so.3.6.5     (X509证书处理)
- /usr/lib/libmbedcrypto.so.3.6.5   (加密算法)
```

#### 4.2 Mbed TLS 最小化配置
```c
// mbedtls_config.h - 最小化配置
#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

// 必需的基础功能
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_MAX_CONTENT_LEN 4096  // 减小SSL缓冲区

// 仅启用必要的密码套件（优先选择体积小、速度快的）
#define MBEDTLS_SSL_CIPHERSUITES \
    MBEDTLS_TLS_RSA_WITH_AES_128_GCM_SHA256, \
    MBEDTLS_TLS_RSA_WITH_AES_128_CBC_SHA256

// 禁用不需要的功能以减小依赖体积
#undef MBEDTLS_SSL_MAX_FRAGMENT_LENGTH
#undef MBEDTLS_SSL_ENCRYPT_THEN_MAC
#undef MBEDTLS_SSL_EXTENDED_MASTER_SECRET
#undef MBEDTLS_SSL_SESSION_TICKETS
#undef MBEDTLS_SSL_MAX_FRAGMENT_LENGTH

// 证书验证 - 禁用（嵌入式设备通常无CA证书）
#undef MBEDTLS_X509_CRT_PARSE_C
#undef MBEDTLS_X509_CSR_PARSE_C
#undef MBEDTLS_X509_CRL_PARSE_C

// 仅保留基础加密
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_MD_C
#define MBEDTLS_SHA256_C

// 基础工具
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY

// 网络层
#define MBEDTLS_NET_C

// 错误处理简化
#define MBEDTLS_ERROR_C

#include "mbedtls/check_config.h"
#endif
```

#### 4.3 编译链接配置
```makefile
# Makefile 链接配置
CC = mipsel-linux-gcc

# 编译选项
CFLAGS = -Os -fno-stack-protector -fomit-frame-pointer \
         -ffunction-sections -fdata-sections -fno-unwind-tables \
         -I. -I$(MBEDTLS_INCLUDE)

# 链接选项 - 动态链接Mbed TLS
LDFLAGS = -Wl,--gc-sections -Wl,--strip-all \
          -lmbedtls -lmbedx509 -lmbedcrypto \
          -Wl,-rpath,/usr/lib

# 或者使用全路径链接
LDFLAGS = -Wl,--gc-sections -Wl,--strip-all \
          /usr/lib/libmbedtls.so.3.6.5 \
          /usr/lib/libmbedx509.so.3.6.5 \
          /usr/lib/libmbedcrypto.so.3.6.5
```

---

### 5. 核心模块设计

#### 5.1 模块划分
```
项目结构:
├── main.c              # 程序入口、参数解析、主流程控制
├── imap_client.c/h     # IMAP协议实现（精简版）
├── tls_mbedtls.c/h     # Mbed TLS封装（TCP+SSL）
├── utils.c/h           # 工具函数（日期、文件操作）
├── config.h            # 编译时配置常量
└── Makefile            # 交叉编译配置
```

#### 5.2 关键数据结构
```c
// config.h - 编译时常量
#ifndef CONFIG_H
#define CONFIG_H

#define BATCH_SIZE          100         // 每批次UID数量
#define CMD_BUF_SIZE        2048        // 命令缓冲区
#define RESP_BUF_SIZE       8192        // 响应缓冲区
#define LINE_BUF_SIZE       1024        // 行缓冲区
#define MAX_SERVER_LEN      256
#define MAX_USER_LEN        128
#define MAX_PASS_LEN        128
#define MAX_FOLDER_LEN      256
#define MAX_KEYWORD_LEN     128
#define TEMP_DIR            "/tmp"
#define TEMP_PREFIX         "imap_batch_"
#define BATCH_INTERVAL      30          // 批次间隔秒数
#define NETWORK_TIMEOUT     30000       // 网络超时毫秒

#endif

// imap_client.h
#ifndef IMAP_CLIENT_H
#define IMAP_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include "mbedtls/ssl.h"

typedef struct {
    char server[MAX_SERVER_LEN];
    int port;
    char username[MAX_USER_LEN];
    char password[MAX_PASS_LEN];
    char folder[MAX_FOLDER_LEN];
    int keep_days;
    char keyword[MAX_KEYWORD_LEN];
    bool has_keyword;
} config_t;

typedef struct {
    uint32_t uids[BATCH_SIZE];
    int count;
} uid_batch_t;

// IMAP客户端函数
int imap_connect(const char *server, int port, mbedtls_ssl_context **ssl);
int imap_login(mbedtls_ssl_context *ssl, const char *user, const char *pass);
int imap_select(mbedtls_ssl_context *ssl, const char *folder);
int imap_search(mbedtls_ssl_context *ssl, const char *before_date, 
                const char *keyword, int (*on_uid)(uint32_t uid, void *ctx), void *ctx);
int imap_delete_uids(mbedtls_ssl_context *ssl, const uint32_t *uids, int count);
int imap_logout(mbedtls_ssl_context *ssl);
void imap_disconnect(mbedtls_ssl_context *ssl);

#endif
```

---

### 6. 关键代码实现规范

#### 6.1 临时文件管理（原子性 + 安全清理）
```c
// utils.c - 临时文件管理
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include "config.h"

static char g_batch_files[100][64];  // 最多100个批次文件
static int g_batch_count = 0;

// 信号处理：清理所有临时文件
static void cleanup_temp_files(int sig) {
    (void)sig;
    for (int i = 0; i < g_batch_count; i++) {
        unlink(g_batch_files[i]);
    }
    _exit(1);
}

void init_temp_file_manager(void) {
    signal(SIGINT, cleanup_temp_files);
    signal(SIGTERM, cleanup_temp_files);
    signal(SIGPIPE, SIG_IGN);  // 忽略管道破裂信号
    g_batch_count = 0;
}

// 创建批次临时文件
int create_batch_file(uid_batch_t *batch) {
    if (g_batch_count >= 100) return -1;
    
    char template[64];
    snprintf(template, sizeof(template), "%s/%sXXXXXX", TEMP_DIR, TEMP_PREFIX);
    
    int fd = mkstemp(template);
    if (fd < 0) return -1;
    
    // 二进制写入UID（紧凑格式）
    ssize_t written = write(fd, batch->uids, batch->count * sizeof(uint32_t));
    close(fd);
    
    if (written != (ssize_t)(batch->count * sizeof(uint32_t))) {
        unlink(template);
        return -1;
    }
    
    strncpy(g_batch_files[g_batch_count], template, 63);
    g_batch_files[g_batch_count][63] = '\0';
    g_batch_count++;
    
    return 0;
}

// 读取并删除批次文件
int read_and_delete_batch(const char *filename, uid_batch_t *batch) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) return -1;
    
    ssize_t n = read(fd, batch->uids, sizeof(batch->uids));
    close(fd);
    
    if (n < 0) return -1;
    
    batch->count = n / sizeof(uint32_t);
    unlink(filename);  // 立即删除
    return batch->count;
}

// 清理所有批次文件
void cleanup_all_batches(void) {
    for (int i = 0; i < g_batch_count; i++) {
        unlink(g_batch_files[i]);
    }
    g_batch_count = 0;
}
```

#### 6.2 日期计算（精简版）
```c
// utils.c - 日期计算
#include <time.h>

// 获取N天前的日期，格式化为IMAP SEARCH格式: "01-Jan-2024"
void get_before_date(int days_ago, char *out, size_t out_len) {
    time_t now = time(NULL);
    time_t before = now - (days_ago * 24 * 3600);
    struct tm *tm = gmtime(&before);
    
    static const char *months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    
    snprintf(out, out_len, "%02d-%s-%04d",
             tm->tm_mday, months[tm->tm_mon], tm->tm_year + 1900);
}
```

#### 6.3 IMAP命令构建（避免sprintf溢出）
```c
// imap_client.c - 构建UID STORE命令
static int build_uid_store_cmd(char *buf, size_t buf_size, 
                                const uint32_t *uids, int count) {
    int pos = 0;
    int n;
    
    n = snprintf(buf + pos, buf_size - pos, "UID STORE ");
    if (n < 0 || (size_t)n >= buf_size - pos) return -1;
    pos += n;
    
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
    
    n = snprintf(buf + pos, buf_size - pos, " +FLAGS (\\Deleted)");
    if (n < 0 || (size_t)n >= buf_size - pos) return -1;
    pos += n;
    
    return pos;
}
```

---

### 7. 错误处理与健壮性

#### 7.1 错误码定义
```c
// config.h
enum {
    ERR_OK = 0,
    ERR_PARAM = -1,          // 参数错误
    ERR_CONNECT = -2,        // 连接失败
    ERR_TLS = -3,            // TLS错误
    ERR_AUTH = -4,           // 认证失败
    ERR_SELECT = -5,         // 选择文件夹失败
    ERR_SEARCH = -6,         // 搜索失败
    ERR_DELETE = -7,         // 删除失败
    ERR_FILE = -8,           // 文件操作错误
    ERR_MEMORY = -9,         // 内存不足（预留）
    ERR_TIMEOUT = -10,       // 超时
    ERR_NETWORK = -11,       // 网络错误
};
```

#### 7.2 网络超时处理
```c
// tls_mbedtls.c - 设置超时
void tls_set_timeout(mbedtls_ssl_context *ssl, uint32_t timeout_ms) {
    mbedtls_ssl_conf_read_timeout(ssl->conf, timeout_ms);
    mbedtls_ssl_set_bio(ssl, &server_fd, mbedtls_net_send, 
                        mbedtls_net_recv, mbedtls_net_recv_timeout);
}
```

#### 7.3 断点续传支持（可选）
```c
// 检查是否存在未完成的批次文件
int check_resume_state(void) {
    DIR *dir = opendir(TEMP_DIR);
    if (!dir) return 0;
    
    struct dirent *entry;
    int count = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, TEMP_PREFIX, strlen(TEMP_PREFIX)) == 0) {
            count++;
        }
    }
    closedir(dir);
    
    return count;  // 返回未完成的批次数量
}
```

---

### 8. 完整 Makefile

```makefile
# MIPSel IMAP Cleaner Makefile

# 交叉编译器
CC = mipsel-linux-gcc
STRIP = mipsel-linux-strip
SIZE = mipsel-linux-size

# 目标
TARGET = imap_cleaner

# 源文件
SRCS = main.c imap_client.c tls_mbedtls.c utils.c
OBJS = $(SRCS:.c=.o)

# Mbed TLS 路径（根据实际环境调整）
MBEDTLS_INCLUDE = /usr/include
MBEDTLS_LIB = /usr/lib

# 编译选项 - 极致体积优化
CFLAGS += -fno-stack-protector
CFLAGS += -fomit-frame-pointer
CFLAGS += -ffunction-sections
CFLAGS += -fdata-sections
CFLAGS += -fno-unwind-tables
CFLAGS += -fno-asynchronous-unwind-tables
CFLAGS += -fmerge-all-constants
CFLAGS += -fno-ident
CFLAGS += -Wall -Wextra
CFLAGS += -I. -I$(MBEDTLS_INCLUDE)

# 链接选项
LDFLAGS = -Wl,--gc-sections
LDFLAGS += -Wl,--strip-all
LDFLAGS += -L$(MBEDTLS_LIB)
LDFLAGS += -lmbedtls -lmbedx509 -lmbedcrypto
LDFLAGS += -Wl,-rpath,/usr/lib

# 默认目标
all: $(TARGET)

# 编译规则
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "=== 二进制体积统计 ==="
	$(SIZE) $@
	@ls -lh $@
	@echo "====================="

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 清理
clean:
	rm -f $(OBJS) $(TARGET)

# 安装（可选）
install: $(TARGET)
	cp $(TARGET) /usr/bin/

# 分析体积
analyze: $(TARGET)
	$(SIZE) -A -x $@
	mipsel-linux-nm --print-size --size-sort $@ | tail -20

.PHONY: all clean install analyze
```

---

### 9. 测试验证方案

#### 9.1 功能测试矩阵
| 测试项 | 测试内容 | 预期结果 |
|--------|----------|----------|
| 参数解析 | 测试各种参数组合 | 正确解析，错误参数给出错误码 |
| 连接测试 | 连接不同IMAP服务器 | 成功建立TLS连接 |
| 认证测试 | 正确/错误密码 | 正确密码通过，错误密码返回ERR_AUTH |
| 搜索测试 | 不同日期和关键词组合 | 返回正确的UID列表 |
| 批次拆分 | 生成150个UID | 生成2个临时文件（100+50） |
| 删除测试 | 删除指定批次 | UID被标记删除并EXPUNGE |
| 间隔测试 | 多批次删除 | 批次间间隔30秒 |

#### 9.2 资源测试
```bash
# 内存监控（在目标设备上运行）
while true; do
    cat /proc/$(pidof imap_cleaner)/status | grep -E "VmRSS|VmSize"
    sleep 1
done

# 二进制体积检查
ls -lh imap_cleaner
mipsel-linux-size imap_cleaner
```

#### 9.3 边界测试
- 空文件夹（无邮件）
- 无匹配邮件（搜索条件无结果）
- 正好100封匹配邮件（单批次边界）
- 101封匹配邮件（双批次）
- 1000+封匹配邮件（多批次）
- 超长文件夹名称（接近256字节）
- 特殊字符文件夹名称（中文、空格等）

---

### 10. 交付物清单

1. **源代码文件**
   - `main.c` - 主程序
   - `imap_client.c/h` - IMAP协议实现
   - `tls_mbedtls.c/h` - TLS封装
   - `utils.c/h` - 工具函数
   - `config.h` - 配置常量

2. **构建文件**
   - `Makefile` - 交叉编译配置

3. **文档**
   - `design.md` - 本设计文档
   - `README.md` - 使用说明

4. **测试脚本**（可选）
   - `test.sh` - 自动化测试脚本

---

### 11. 关键优化总结

| 优化项 | 原设计 | 优化后 | 收益 |
|--------|--------|--------|------|
| 二进制体积 | < 800KB | < 500KB | 节省37.5% |
| 运行时内存 | < 1MB | < 512KB | 节省50% |
| SSL缓冲区 | 16KB | 4KB | 节省75% |
| 响应缓冲区 | 16KB | 8KB | 节省50% |
| 命令缓冲区 | 4KB | 2KB | 节省50% |
| Mbed TLS | 未明确 | 动态链接3.6.5 | 减少静态体积 |
| 内存分配 | 部分malloc | 零malloc | 避免堆碎片 |

---
