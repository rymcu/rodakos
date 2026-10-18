#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// 主机测试用随机源：排队时按顺序吐出 lan_trust_setup_queue_random 排入的字节，用于从
// esp_srp 生成可复现的向量（排队后取超出视为测试错误）；未排队时读取 /dev/urandom。
void esp_fill_random(void* buffer, size_t length);
void lan_trust_setup_queue_random(const unsigned char* bytes, size_t length);
size_t lan_trust_setup_random_remaining(void);
#ifdef __cplusplus
}
#endif
