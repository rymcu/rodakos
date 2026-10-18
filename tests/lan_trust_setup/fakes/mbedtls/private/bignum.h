#pragma once
// ESP-IDF 6.0.2 的 mbedTLS 4 把 bignum 放在 private/ 下；主机 mbedTLS 3.6 仍是公开头文件。
#include <mbedtls/bignum.h>
