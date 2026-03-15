# MIPSel IMAP Cleaner Makefile
# IMAP批量清理工具 - 交叉编译配置
# 用于MIPSel架构资源受限设备

# ==================== 交叉编译器配置 ====================
# 根据实际环境调整交叉编译器路径
CC = mipsel-openwrt-linux-gcc
STRIP = mipsel-openwrt-linux-strip
SIZE = mipsel-openwrt-linux-size

# ==================== 目标与源文件 ====================
TARGET = imap_cleaner
SRCS = main.c imap_client.c tls_mbedtls.c utils.c
OBJS = $(SRCS:.c=.o)

# ==================== Mbed TLS 路径配置 ====================
# 根据实际环境调整Mbed TLS的头文件和库路径
MBEDTLS_INCLUDE = /usr/include
MBEDTLS_LIB = /usr/lib

# ==================== 编译选项 - 极致体积优化 ====================
CFLAGS = -Os                       # 优化大小
CFLAGS += -s                       # 去除符号表
CFLAGS += -mips32                  # MIPS32指令集
CFLAGS += -fno-stack-protector     # 禁用栈保护（节省体积）
CFLAGS += -fomit-frame-pointer     # 省略帧指针
CFLAGS += -ffunction-sections      # 函数独立段
CFLAGS += -fdata-sections          # 数据独立段
CFLAGS += -fno-unwind-tables       # 禁用异常展开表
CFLAGS += -fno-asynchronous-unwind-tables
CFLAGS += -fmerge-all-constants    # 合并常量
CFLAGS += -fno-ident               # 去除编译器标识
CFLAGS += -fno-plt                 # 禁用PLT（如果支持）
CFLAGS += -mno-shared              # 优先静态链接内部代码
CFLAGS += -Wall -Wextra            # 启用警告
CFLAGS += -I. -I$(MBEDTLS_INCLUDE) # 头文件搜索路径

# ==================== 链接选项 ====================
LDFLAGS = -Wl,--gc-sections        # 链接时移除未使用段
LDFLAGS += -Wl,--strip-all         # 去除所有符号
LDFLAGS += -Wl,-z,norelro          # 禁用RELRO（节省少量空间）
LDFLAGS += -L$(MBEDTLS_LIB)        # 库文件搜索路径
LDFLAGS += -lmbedtls -lmbedx509 -lmbedcrypto  # 动态链接Mbed TLS
LDFLAGS += -Wl,-rpath,/usr/lib     # 运行时库搜索路径

# ==================== 默认目标 ====================
all: $(TARGET)

# ==================== 编译规则 ====================
$(TARGET): $(OBJS)
	@echo "正在链接: $@"
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo ""
	@echo "=== 二进制体积统计 ==="
	$(SIZE) $@
	@ls -lh $@
	@echo "====================="
	@echo ""

%.o: %.c
	@echo "正在编译: $<"
	$(CC) $(CFLAGS) -c $< -o $@

# ==================== 清理目标 ====================
clean:
	@echo "正在清理..."
	rm -f $(OBJS) $(TARGET)
	@echo "清理完成"

# ==================== 安装目标（可选） ====================
install: $(TARGET)
	@echo "正在安装..."
	cp $(TARGET) /usr/bin/
	@echo "安装完成"

# ==================== 体积分析目标 ====================
analyze: $(TARGET)
	@echo "=== 详细体积分析 ==="
	$(SIZE) -A -x $@
	@echo ""
	@echo "=== 符号大小排序（前20个） ==="
	mipsel-openwrt-linux-nm --print-size --size-sort $@ | tail -20
	@echo "====================="

.PHONY: all clean install analyze
