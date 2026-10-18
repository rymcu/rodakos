#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// 主机测试用确定性随机源：按顺序吐出 lan_trust_setup_queue_random 排入的字节，
// 用于从 esp_srp 生成可复现的 SRP 测试向量。队列耗尽视为测试错误。
void esp_fill_random(void* buffer, size_t length);
void lan_trust_setup_queue_random(const unsigned char* bytes, size_t length);
size_t lan_trust_setup_random_remaining(void);
#ifdef __cplusplus
}
#endif
