#pragma once
using DWORD = unsigned long;
struct FATFS { unsigned csize = 1; };
constexpr int FR_OK = 0;
inline int f_getfree(const char*, DWORD* free_clusters, FATFS** filesystem) {
    static FATFS value;
    *free_clusters = 1024;
    *filesystem = &value;
    return FR_OK;
}
