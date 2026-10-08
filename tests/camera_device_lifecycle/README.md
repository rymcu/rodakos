# Camera 外层清理传播回归

此目标编译真实的 dev_camera.c、esp_board_device.c 和
main/rodakos_adapters/camera_device.cc。fixture 将 board-manager API 委托给真实
esp_board_device 实现，并用可控的 Camera subtype 回调模拟底层失败。

覆盖：

- subtype deinit 失败沿 dev_camera → esp_board_device → CameraDevice::Release
  传播，并保留 board handle/ref 供下一次 Acquire 重试；
- 初始化成功但设备 handle 查询失败时，清理失败仍保留 ownership；
- 缺少 subtype deinit callback 返回错误并支持回调恢复后的重试；
- legacy dev_camera_deinit 旧实现的三个负控。

patched 生成文件原样复制真实生产源；legacy 仅在完整 reviewed block 精确匹配
后替换旧实现，源码漂移会使配置失败。managed esp_video 的阶段性 DVP teardown
由独立 dvp_deinit 目标验证。

验证命令：

    cmake -S tests/camera_device_lifecycle -B build/host-evidence/camera-device -G Ninja -DCMAKE_BUILD_TYPE=Debug
    cmake --build build/host-evidence/camera-device -j 4
    ctest --test-dir build/host-evidence/camera-device --output-on-failure
