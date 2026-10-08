# 单串口 Camera 短窗口冒烟

`tools/run_serial_camera_smoke.py` 复用 `Session`、release `Evidence`、现有首次启动日志
校验器和 `ota_security.py verify-package`。它不刷写、不复位、不配置 WiFi，也不更改绑定。

## 执行前置

- 原长稳/monitor 进程必须已经退出并释放串口。不得并发运行其他串口工具，也不得杀进程抢占。
- 对明确的普通 OFF 签名包完成 `VerifyOnly` 和保留 NVS 的 `flash_and_test.ps1` 刷写，
  留存完整刷写记录及 Recovery → Main → OTA confirmation → Home 启动日志。
  `VerifyOnly` 会复位，且不校验已安装 main；它也必须等待原采集退出。
- 已用现有 Rodak 实例记录 DeviceCloud 身份基线，保留设备 ID、绑定和 tokenVersion；
  本工具不验证这些状态，不应启动另一个使用真实数据库的 Electron 实例。
- 停止外部屏幕/Camera 流，避免另一控制方与本工具同时导航设备。

只有上述步骤完成后才能使用以下模板；`flash-record`、`boot-log` 必须替换为**同一次已验证
刷写**的实际文件，不能挪用历史启动日志。`output` 必须是尚不存在的新目录。

```powershell
python tools/run_serial_camera_smoke.py --port COM3 `
  --package build/packages/ota/20261009-063127 `
  --flash-record .codex-temp/<new-window>/flash.log `
  --boot-log build/logs/first-boot-<new-window>.log `
  --output .codex-temp/<new-camera-window> --cycles 3 --observe-seconds 65
```

包预检要求普通 `production` flavor、fault 和 Home population 均为 OFF；开发签名包仍只
是开发候选。工具仅将包和刷写/启动记录作为操作者提供的上下文，并写明
`identity.installed_image_verified=false`、`identity.binding_verified=false`。
静态文件校验不会证明当前串口设备运行该镜像。

## 行为与结果

先确认 Home，再逐轮执行 Camera 导航、当前实例的软件首帧提交、Home 导航和六个关闭日志，
随后观察至少 60 秒（默认 65 秒，减少遥测周期边界影响）。每轮要求新鲜 MQTT ≥ 2、Main ≥ 1、
Voice ≥ 1，且 Voice 已恢复监听；沿用 release 解析器的错误、断线与资源余量门槛。

失败后停止后续轮次。首次预览失败仍尝试一次 Home；若 Home 请求已经发出，收尾仅对该请求
做有界等待，不补发、不复位。中断也保留原失败并尝试同会话 Home 清理。无法恢复时记录未确认
状态并关闭串口，不把清理成功改写为测试成功。输出为原始 `serial.log` 和 `summary.json`。

`software-smoke-observed` 只表示这个短窗口的软件路径和健康日志满足要求。
`Camera preview image updated` 是 LVGL 图像提交；`STREAMOFF complete` / `fd close complete`
是调用返回日志，均不能证明物理成像、画质、驱动完全释放或硬件资源恢复。
短窗口的 release 结果仍是 `incomplete` 或 `no-go`，`eight_hour_gate_passed` 始终为 `false`。

## 纯离线回归

```powershell
python -m unittest discover -s tests/camera_serial_tool -v
```

测试只使用内存串口 fake（无 `open` 方法），不执行主 CLI，也不打开任何设备。
覆盖重复轮次、首帧缺失、deferred release、缺 Voice、断线、低栈、旧关闭日志、延迟/缺失
Home completion、KeyboardInterrupt 后同会话清理，以及包/启动上下文预检失败。
