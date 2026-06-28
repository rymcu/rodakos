# GT911 触摸问题诊断与修复笔记

## 问题现象

GT911 触摸屏在 LVGL 任务中读取时导致 I2C 死锁，引发 Watchdog 超时：

```
E (6147) task_wdt: Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:
E (6147) task_wdt:  - IDLE0 (CPU 0)
E (6147) task_wdt: Tasks currently running:
E (6147) task_wdt: CPU 0: taskLVGL
```

**堆栈追踪显示**：
```
taskLVGL 卡在:
→ lvgl_port_touchpad_read
→ esp_lcd_touch_gt911_read_data
→ touch_gt911_i2c_read
→ i2c_ll_is_bus_busy (I2C 总线一直忙，无限等待)
```

## 根本原因

1. **硬件限制**：RYMCU BigSmart 没有连接 GT911 的中断引脚 (`int_gpio_num: -1`)
2. **轮询模式问题**：GT911 驱动在无中断模式下使用轮询，但在 LVGL 任务中轮询时 I2C 总线陷入死循环
3. **`esp_lvgl_port` 限制**：当前使用的 `esp_lvgl_port` 组件在处理轮询模式触摸时有问题

## 临时解决方案（已实施）

**在 `main.cc` 中禁用触摸注册到 LVGL**：

```cpp
// 暂时不注册触摸输入到 LVGL - GT911 轮询模式有 I2C 死锁问题
/*
const lvgl_port_touch_cfg_t touch_cfg = {
    .disp = disp,
    .handle = touch_handles->touch_handle,
};
lv_indev_t *touch_indev = lvgl_port_add_touch(&touch_cfg);
*/
ESP_LOGW(TAG, "Touch input disabled - GT911 polling mode causes I2C deadlock in LVGL task");
```

**效果**：
- ✅ 系统正常运行
- ✅ 屏幕显示正常
- ✅ HomeApp 成功渲染
- ❌ 触摸输入不可用

## 参考实现对比

### esp-brookesia 官方示例

位置：`D:\workspace\esp-brookesia\examples\agent\chatbot`

**关键差异**：
1. 使用 `esp_lvgl_adapter` (v0.5.*) 而不是 `esp_lvgl_port`
2. 触摸初始化代码：
   ```cpp
   esp_lv_adapter_touch_config_t touch_config = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(
       reinterpret_cast<lv_display_t *>(lvgl_display_),
       reinterpret_cast<esp_lcd_touch_handle_t>(touch_driver_specific.touch_handle)
   );
   lvgl_indev_ = esp_lv_adapter_register_touch(&touch_config);
   ```
3. **相同的硬件配置** (`int_gpio_num: -1`)，但**能正常工作**

## 永久解决方案（待实施）

### 方案 1：迁移到 `esp_lvgl_adapter`（推荐）

**优点**：
- 官方示例验证可用
- 更好的轮询模式支持
- 更现代的 API

**缺点**：
- 需要重构 LVGL 初始化代码
- 可能需要调整其他 UI 代码

**实施步骤**：
1. 在 `idf_component.yml` 中替换依赖：
   ```yaml
   espressif/esp_lvgl_adapter:
     version: "0.5.*"
   ```
2. 移除 `espressif/esp_lvgl_port` 依赖
3. 参考 `chatbot` 示例重构 `main.cc` 中的 LVGL 初始化
4. 测试触摸功能

### 方案 2：自定义触摸轮询任务

**思路**：不使用 `lvgl_port_add_touch`，而是创建独立任务轮询触摸并手动发送事件到 LVGL

**实施步骤**：
1. 创建低优先级任务定期读取 GT911
2. 使用 `lv_indev_set_read_cb` 注册回调
3. 在回调中返回缓存的触摸数据（不直接读取 I2C）

### 方案 3：添加硬件中断引脚（需要硬件修改）

如果硬件支持，连接 GT911 INT 引脚到可用的 GPIO，然后：
1. 更新 `board_devices.yaml`: `int_gpio_num: <gpio>`
2. 添加中断引脚定义到 `board_peripherals.yaml`
3. 重新生成板级配置

## 配置文件对比

### board_devices.yaml - 触摸配置

**参考实现**（esp-brookesia）：
```yaml
lcd_touch:
  chip: gt911
  type: lcd_touch
  sub_type: i2c
  config:
    touch_config:
      x_max: 240  # 原始
      y_max: 320  # 原始
      int_gpio_num: -1  # 无中断
      flags:
        swap_xy: true
        mirror_x: true
        mirror_y: false
```

**我们的配置**（已修正）：
```yaml
lcd_touch:
  chip: gt911
  type: lcd_touch
  sub_type: i2c
  config:
    touch_config:
      x_max: 320  # 已修正为实际分辨率
      y_max: 240  # 已修正为实际分辨率
      int_gpio_num: -1
      flags:
        swap_xy: true
        mirror_x: true
        mirror_y: false
```

## LCD 显示配置（已修复）

**关键参数**：
- `invert_color: false` - 正确的颜色显示
- `swap_bytes: true` - 正确的 RGB565 字节序
- `mirror_x: true`
- `mirror_y: false`
- `swap_xy: true`

## 测试记录

### 2026-06-28 测试结果

| 配置 | `invert_color` | `swap_bytes` | 触摸 | 结果 |
|------|----------------|--------------|------|------|
| 1 | `true` | `true` | 启用 | 白屏，UI lock 超时 |
| 2 | `false` | `true` | 启用 | 白屏，Watchdog 超时 |
| 3 | `false` | `false` | 启用 | 绿色背景，UI lock 超时 |
| 4 | `false` | `true` | **禁用** | ✅ **正常显示** |

**结论**：触摸轮询导致问题，禁用后系统正常。

## 下一步行动

1. ✅ 系统已可正常运行（触摸禁用）
2. ⏳ 待选择并实施永久解决方案
3. ⏳ 实现其他待修复功能（Audio、Battery 等）

## 相关文件

- `main/main.cc` - LVGL 和触摸初始化
- `components/brookesia_hal_boards/boards/rymcu/rymcu_bigsmart/board_devices.yaml` - 触摸配置
- `components/brookesia_hal_boards/boards/rymcu/rymcu_bigsmart/setup_device.c` - 触摸工厂函数
- `D:\workspace\esp-brookesia\examples\agent\chatbot` - 参考实现
