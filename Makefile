# MIPSel IMAP Cleaner Makefile
# IMAP批量清理工具 - 交叉编译配置
# 用于MIPSel架构资源受限设备

# ==================== 交叉编译器配置 ====================
ifneq ($(CROSS_COMPILE),)
CC := $(CROSS_COMPILE)gcc
STRIP := $(CROSS_COMPILE)strip
SIZE := $(CROSS_COMPILE)size
else
# 退出编译
$(error "CROSS_COMPILE 未定义，请设置交叉编译工具链")
endif

# ==================== 目标与源文件 ====================
TARGET := imap_cleaner
SRCS := main.c imap_client.c tls_mbedtls.c utils.c
OBJS := $(SRCS:.c=.o)

# ==================== Mbed TLS 路径配置 ====================
# 根据实际环境调整Mbed TLS的头文件和库路径
# 注意：使用交叉编译工具链中的mbedTLS，而不是主机系统的
# MBEDTLS_INCLUDE = /usr/include
# MBEDTLS_LIB = /usr/lib

# 如果交叉编译工具链有自带的mbedTLS，请改为：
MBEDTLS_INCLUDE := $(STAGING_DIR)/usr/include
MBEDTLS_LIB := $(STAGING_DIR)/usr/lib

# ==================== 编译选项 - 极致体积优化 ====================
CFLAGS := -Os
CFLAGS += -fno-stack-protector
CFLAGS += -fomit-frame-pointer
CFLAGS += -ffunction-sections
CFLAGS += -fdata-sections
CFLAGS += -fno-unwind-tables
CFLAGS += -fno-asynchronous-unwind-tables
CFLAGS += -fmerge-all-constants
CFLAGS += -fno-ident
CFLAGS += -fno-plt
CFLAGS += -Wall -Wextra
CFLAGS += -I. -I$(MBEDTLS_INCLUDE)
# 追加从外部传入的额外编译选项
CFLAGS += $(EXTRA_CFLAGS)

# ==================== 链接选项 ====================
LDFLAGS := -Wl,--gc-sections
LDFLAGS += -Wl,--strip-all
LDFLAGS += -Wl,-z,norelro
LDFLAGS += -L$(MBEDTLS_LIB)
LDFLAGS += -lmbedtls -lmbedx509 -lmbedcrypto
LDFLAGS += -Wl,-rpath,/usr/lib
# 追加从外部传入的额外链接选项
LDFLAGS += $(EXTRA_LDFLAGS)

.PHONY: all clean install analyze

# ==================== 默认目标 ====================
all: $(TARGET)

# ==================== 编译规则 ====================
$(TARGET): $(OBJS)
	@echo "正在链接: $@"
	@echo "使用的LDFLAGS: $(LDFLAGS)"
	$(CC) -o $@ $(OBJS) $(LDFLAGS)
	@echo ""
	@echo "=== 二进制体积统计 ==="
	$(SIZE) $@
	@ls -lh $@
	@echo "====================="
	@echo ""

%.o: %.c
	@echo "正在编译: $<"
	$(CC) $(CFLAGS) -o $@ -c $<

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
	mipsel-linux-nm --print-size --size-sort $@ | tail -20
	@echo "====================="
