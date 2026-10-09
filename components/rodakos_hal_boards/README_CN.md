# RodakOS HAL Boards

[English](README.md)

本组件维护 RodakOS 板级定义，当前仅支持 ESP32-S3 RYMCU BigSmart。
Board Manager 继续负责生成配置表、注册设备、共享外设和设备生命周期。

```text
boards/
└── rymcu/
    └── rymcu_bigsmart/
        ├── board_info.yaml
        ├── board_devices.yaml
        ├── board_peripherals.yaml
        ├── sdkconfig.defaults.board
        ├── setup_device.c
        └── components/esp_io_expander_pca9557/
```

本组件独占编译 BigSmart 的 `setup_device.c`。`gen_bmgr_codes` 编译生成配置表并依赖
本组件，不重复编译板级初始化代码。PCA9557 驱动暂随板型保存，等其他板型需要复用时再提取。

在仓库根目录激活 ESP-IDF 6.0.2，运行 `./generate_board_config.ps1`，再运行
`idf.py build`。移动或修改板级定义后必须重新生成；`gen_bmgr_codes` 不纳入版本控制。

以后按 `boards/<厂商>/<板型>/` 增加硬件。新增目录并不会自动启用板型：需要同步调整
本组件 CMake、manifest、项目生成入口的板型选择，以及 Board Manager 生成器中的源码
编译归属，并完成配置与实机验证。当前构建入口仍只接受 `rymcu_bigsmart`。

## 来源与许可证

本组件提取自 ESP-Brookesia 的 `hal/brookesia_hal_boards` 0.7.5，原随库元数据记录的
上游提交为 `02d1db90d4cdb232850c50c3b179fefccfaba420`。BigSmart 定义还包含 RodakOS
适配修改，现由 RodakOS 维护；原上游校验清单与组件身份元数据不再适用于精简后的组件。

移植文件保留原版权及 SPDX 声明，Apache-2.0 条款见 [license.txt](license.txt)。
独立的 `esp_board_manager` 组件继续保留自己的许可证与来源记录。
