// tls_mbedtls.h - Mbed TLS TLS连接封装头文件
// IMAP批量清理工具
// 提供TCP连接和SSL/TLS握手的封装接口

#ifndef TLS_MBEDTLS_H
#define TLS_MBEDTLS_H

#include "config.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/net_sockets.h"

// ==================== TLS连接上下文结构 ====================
typedef struct {
    mbedtls_net_context net_ctx;      // 网络连接上下文
    mbedtls_ssl_context ssl_ctx;      // SSL上下文
    mbedtls_ssl_config ssl_conf;      // SSL配置
    mbedtls_entropy_context entropy;  // 熵源上下文（用于随机数生成）
    mbedtls_ctr_drbg_context ctr_drbg;// 确定性随机数生成器
} tls_context_t;

// ==================== TLS连接函数 ====================

// 初始化TLS上下文
// 参数:
//   ctx - TLS上下文指针
void tls_init(tls_context_t *ctx);

// 建立TLS连接（TCP连接 + SSL握手）
// 参数:
//   ctx - TLS上下文指针
//   server - 服务器地址
//   port - 服务器端口
//   timeout_ms - 超时时间（毫秒）
// 返回:
//   成功返回0，失败返回错误码
int tls_connect(tls_context_t *ctx, const char *server, int port, uint32_t timeout_ms);

// 通过TLS连接发送数据
// 参数:
//   ctx - TLS上下文指针
//   buf - 数据缓冲区
//   len - 数据长度
// 返回:
//   成功返回发送的字节数，失败返回-1
int tls_send(tls_context_t *ctx, const unsigned char *buf, size_t len);

// 从TLS连接接收数据
// 参数:
//   ctx - TLS上下文指针
//   buf - 接收缓冲区
//   len - 缓冲区大小
// 返回:
//   成功返回接收的字节数，失败返回-1，连接关闭返回0
int tls_recv(tls_context_t *ctx, unsigned char *buf, size_t len);

// 关闭TLS连接并清理资源
// 参数:
//   ctx - TLS上下文指针
void tls_close(tls_context_t *ctx);

#endif // TLS_MBEDTLS_H
