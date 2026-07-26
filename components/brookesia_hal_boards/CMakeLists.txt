set(board_dir "boards/rymcu/rymcu_bigsmart")

idf_component_register(
    SRCS "${board_dir}/setup_device.c"
    INCLUDE_DIRS "${board_dir}"
    REQUIRES
        esp_board_manager
        esp_io_expander_pca9557
        esp_lcd
        esp_lcd_touch_gt911
)
