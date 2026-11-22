#pragma once
typedef struct {
    char version[32];
    char project_name[32];
    char time[16];
    char date[16];
} esp_app_desc_t;
inline const esp_app_desc_t* esp_app_get_description() {
    static const esp_app_desc_t desc = {"0.1.3", "rodakos", "18:02:11", "Nov 21 2026"};
    return &desc;
}
