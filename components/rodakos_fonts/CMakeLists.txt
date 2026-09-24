idf_component_register(
    SRCS
        "src/font_puhui_16_4.c"
        "src/font_awesome_20_4.c"
        "src/font_awesome_30_4.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES lvgl
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC LV_LVGL_H_INCLUDE_SIMPLE)
