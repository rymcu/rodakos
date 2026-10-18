#pragma once
// 主机构建用 POSIX 套接字代替 lwIP：server_trust_route.cc 只需要 inet_pton/inet_ntop。
#include <arpa/inet.h>
#include <sys/socket.h>
