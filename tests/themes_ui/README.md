# Themes 应用 LVGL 回归

本目标编译生产 ThemesApp、theme_presets、PhoneUi、组件、主题色和 LVGL 9.3，并链接生产字体
（Montserrat、Ionicons 图标、普惠体中文子集），所以截图显示真实中文和图标。AppearanceService、
导航和 Settings 使用 host fake，不访问 NVS、SD、HTTP 或签名。

```sh
cmake -S tests/themes_ui -B build-host-themes-ui -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host-themes-ui -j 4
ctest --test-dir build-host-themes-ui --output-on-failure -V
```

ASan/UBSan 使用独立目录，并为 C/C++ 添加
`-fsanitize=address,undefined -fno-omit-frame-pointer`；执行时使用
`ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1`。CTest 超时为 60 秒。

20 项用例。主页覆盖：全部外观状态文字、预设默认主色、内置外观与外观包配色两种状态、750 ms
定时刷新下载进度与失败、选择预设后保存与异步刷新、浅色主题重建后的文字颜色、选中框在同色色块上
仍可见、保存失败不改变运行时主题、最长错误码不超出卡片、缺少外观服务、暂停/恢复时的轮询与隐藏、
壁纸缩放预览，以及应用描述符和别名。

发布端信任页覆盖：应用内打开与返回（标题栏返回和系统返回）、本机触摸只确认按下时显示的密钥、
远程输入和合成点击被拒绝、按下到释放之间密钥变化被拒绝、读取中/错误/已信任时禁用确认、
保存信任失败、信任页上的主题重建，以及缺少外观服务。触摸用例把 LVGL 测试指针设为主输入，
远程输入回调与 `main.cc` 形状相同。

截图写入 CTest 构建目录的 `themes-builtin.ppm`、`themes-pending.ppm`、`themes-failed.ppm`、
`themes-blue.ppm`、`themes-light.ppm`、`themes-wallpaper.ppm`、`themes-trust.ppm` 和
`themes-trust-light.ppm`。

这些测试不覆盖生产 AppearanceService 状态机、NVS/SD 持久化、真实导航与主题重建顺序、GT911
触摸、远程输入桥或 ST7789 显示效果。设备上的桌面位置、预设切换、设置入口、对照 Rodak 的本机信任
确认、远程控制下的拒绝和真实下载状态显示仍需单独检查。
