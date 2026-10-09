# 板级生成与路径回归

在仓库根目录运行：

```powershell
python -m unittest discover -s tests/board_generation
```

需要 Python 的 PyYAML、CMake 和 PowerShell 7 (`pwsh`)。这些工具在 ESP-IDF 开发环境中可用；没有 CMake 或 pwsh 时相应用例会显示跳过，不能记作完整通过。

测试调用真实 `BoardConfigGenerator.setup_gen_bmgr_codes_component`，在名称含空格的临时项目中验证：

- BigSmart 的 `setup_device.c` 只由 `rodakos_hal_boards` 注册，生成的设备表仍由 `gen_bmgr_codes` 注册。
- 重复生成结果一致，其他板型的源文件仍纳入生成组件。
- 复制并运行真实 `fix_gen_paths.ps1` 后，CMake / manifest 不含临时项目绝对路径，PCA9557 的相对路径可以解析，重复修正和重新生成后修正均保持一致。

组件源集通过 `cmake -P` 解释真实 CMake 文件，仅替换 ESP-IDF 注册入口以记录结果。测试不编译固件、不运行 IDF 配置、不修改仓库生成目录、不访问设备。它覆盖构建输入和路径行为；最终链接、固件大小和硬件运行仍由独立验证确认。
