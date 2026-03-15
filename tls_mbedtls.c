// tls_mbedtls.c - Mbed TLS TLS连接封装实现
// IMAP批量清理工具
// 实现TCP连接和SSL/TLS握手的封装

#include "tls_mbedtls.h"
#include <string.h>
#include <stdio.h>

// ==================== 初始化TLS上下文 ====================
void tls_init(tls_context_t *ctx) {
    // 初始化所有Mbed TLS上下文
    mbedtls_net_init(&ctx->net_ctx);
    mbedtls_ssl_init(&ctx->ssl_ctx);
    mbedtls_ssl_config_init(&ctx->ssl_conf);
    mbedtls_entropy_init(&ctx->entropy);
    mbedtls_ctr_drbg_init(&ctx->ctr_drbg);
}

// ==================== 建立TLS连接 ====================
int tls_connect(tls_context_t *ctx, const char *server, int port, uint32_t timeout_ms) {
    int ret;
    char port_str[8];
    const char *pers = "imap_cleaner";  // 用于随机数生成的个性化字符串
    
    // 将端口号转换为字符串
    snprintf(port_str, sizeof(port_str), "%d", port);
    
    // ==================== 1. 初始化随机数生成器 ====================
    ret = mbedtls_ctr_drbg_seed(&ctx->ctr_drbg, mbedtls_entropy_func, 
                                 &ctx->entropy, (const unsigned char *)pers, 
                                 strlen(pers));
    if (ret != 0) {
        return ERR_TLS;
    }
    
    // ==================== 2. 建立TCP连接 ====================
    ret = mbedtls_net_connect(&ctx->net_ctx, server, port_str, MBEDTLS_NET_PROTO_TCP);
    if (ret != 0) {
        return ERR_CONNECT;
    }
    
    // ==================== 3. 配置SSL ====================
    // 设置SSL配置为客户端模式
    ret = mbedtls_ssl_config_defaults(&ctx->ssl_conf, 
                                       MBEDTLS_SSL_IS_CLIENT,
                                       MBEDTLS_SSL_TRANSPORT_STREAM,
                                       MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        mbedtls_net_free(&ctx->net_ctx);
        return ERR_TLS;
    }
    
    // 设置验证模式：NONE，跳过证书验证（嵌入式设备通常无CA证书）
    mbedtls_ssl_conf_authmode(&ctx->ssl_conf, MBEDTLS_SSL_VERIFY_NONE);
    
    // 设置随机数生成器
    mbedtls_ssl_conf_rng(&ctx->ssl_conf, mbedtls_ctr_drbg_random, &ctx->ctr_drbg);
    
    // 设置超时时间
    mbedtls_ssl_conf_read_timeout(&ctx->ssl_conf, timeout_ms);
    
    // ==================== 4. 设置SSL上下文 ====================
    ret = mbedtls_ssl_setup(&ctx->ssl_ctx, &ctx->ssl_conf);
    if (ret != 0) {
        mbedtls_net_free(&ctx->net_ctx);
        return ERR_TLS;
    }
    
    // 绑定网络IO
    mbedtls_ssl_set_bio(&ctx->ssl_ctx, &ctx->net_ctx, 
                        mbedtls_net_send, mbedtls_net_recv, 
                        mbedtls_net_recv_timeout);
    
    // ==================== 5. 执行SSL握手 ====================
    while ((ret = mbedtls_ssl_handshake(&ctx->ssl_ctx)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            mbedtls_net_free(&ctx->net_ctx);
            return ERR_TLS;
        }
    }
    
    return ERR_OK;
}

// ==================== 通过TLS连接发送数据 ====================
int tls_send(tls_context_t *ctx, const unsigned char *buf, size_t len) {
    int ret = mbedtls_ssl_write(&ctx->ssl_ctx, buf, len);
    if (ret < 0) {
        return -1;
    }
    return ret;
}

// ==================== 从TLS连接接收数据 ====================
int tls_recv(tls_context_t *ctx, unsigned char *buf, size_t len) {
    int ret = mbedtls_ssl_read(&ctx->ssl_ctx, buf, len);
    if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
        return 0;  // 对端正常关闭连接
    }
    if (ret < 0) {
        return -1;
    }
    return ret;
}

// ==================== 关闭TLS连接并清理资源 ====================
void tls_close(tls_context_t *ctx) {
    // 发送关闭通知（忽略错误）
    mbedtls_ssl_close_notify(&ctx->ssl_ctx);
    
    // 释放所有资源
    mbedtls_net_free(&ctx->net_ctx);
    mbedtls_ssl_free(&ctx->ssl_ctx);
    mbedtls_ssl_config_free(&ctx->ssl_conf);
    mbedtls_ctr_drbg_free(&ctx->ctr_drbg);
    mbedtls_entropy_free(&ctx->entropy);
}
