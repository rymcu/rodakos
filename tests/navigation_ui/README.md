# 预建导航队列与真实 LVGL 生命周期验证

本目标完整编译生产 `PhoneSystem`、`PhoneNavigation`、`DeferredNavigation`、`PhoneAppRegistry`、
`PhoneAppHost`、`CameraApp`、`PhoneUi` 及相关 UI 组件，并链接仓库锁定的真实 LVGL。
相机设备、音频焦点、Shell 行为、Home 内容和设备字体为 fixture；不连接硬件或串口。

049 当前 13 项用例在 Debug 与 ASan/UBSan/leak 下通过：

- Camera Start 持有外层 UI 锁时，旧式 1000 ms 加锁准入会被拒绝，新队列仍保留 Home；
  Camera Stop 尚未完成时，后续 Home 可独立准入，完成回调等实际退出后才执行。
- 真实 LVGL display flush 事件被阻塞时，串口请求可入队，释放事件后再执行。
- 四个 pending 槽满后不覆盖旧请求；空、未知、过长、内含 NUL 的 ID 均不产生后续执行。
- 实际 Camera Back/Home 事件、重复 Home、快速 Camera/Home 切换，最终对象和 timer 数恢复
  基线；切换时 Camera 在 Home `OnCreate` 前删除旧 UI/timer、释放预览像素、停止预览并释放
  音频焦点，销毁阶段仍执行幂等停止以保留待重试释放；队满拒绝显示“返回桌面失败，请重试”，
  后续新点击可恢复。
- 关闭队列逐一取消待执行请求，禁止新准入；完成回调可重入 Close，不重复执行或取消。
- factory 和部分 `OnCreate` 抛异常后，失败完成、transition RAII 恢复、旧 app 保持；部分
  UI/timer 清理后没有遗留 tick。旧 Camera 在候选 `OnCreate` 失败后恢复预览；completion 抛
  异常不会阻断后续 dispatch 或 Close 取消。
- 3 个子进程分别进入旧 app teardown 失败、部分创建的 teardown 失败、未知 dispatch 异常；
  必须先观察指定 `T` marker，再以 SIGABRT 结束。任意崩溃或正常继续均不算通过。

测试从生产 `RequestLaunch`/`RequestHome` 和真实 LVGL 事件进入，未复制导航 dispatcher。
gate 使用真实宿主线程和 UI mutex，精确阻塞服务生命周期或 flush；相机底层仍是替身。
主机准入耗时断言只是回归检查，不是固件截止时间。timer 每次至多取一个请求，30 ms 是调度周期，
不是 Home 完成保证；真实 LVGL 启动分配仍可能触发其 malloc assert。

```sh
cmake -S tests/navigation_ui -B "$HOME/.cache/rodakos-navigation-ui-debug" \
  -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$HOME/.cache/rodakos-navigation-ui-debug" -j 2
ctest --test-dir "$HOME/.cache/rodakos-navigation-ui-debug" --output-on-failure -V

cmake -S tests/navigation_ui -B "$HOME/.cache/rodakos-navigation-ui-asan" \
  -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build "$HOME/.cache/rodakos-navigation-ui-asan" -j 2
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir "$HOME/.cache/rodakos-navigation-ui-asan" --output-on-failure -V
```

049 本地复用构建目录 `~/.cache/rodakos-navigation-ui-048-debug` 与
`~/.cache/rodakos-navigation-ui-048-asan`。一个 CTest 内执行 13 项用例，其中一项运行
3 个预期中止子进程，不是 13 个 CTest 或 3 次普通成功退出；030 的旧封存记录保持原身份。

只有 factory/预分配，以及部分 `OnCreate` 成功清理后的失败才可恢复。未知生命周期异常或
teardown 失败必须中止；completion 通知异常仍隔离且不重试。测试不把任意异常一律解释为可恢复。

该结果不证明 028 `queued:false` 的实机根因，也不覆盖 USB 收发、GT911 触摸、真实相机/显示
并发、任意 UI OOM 或 030 制品/部署。迁移仅包含串口和 Camera 两类入口，其他 app 与
`lv_async_call` 仍有各自的生命周期边界。合同见 [任务回收与导航](../../docs/task-retirement.md)，
Camera 保存结果和主题重建回归见 [camera_ui](../camera_ui/README.md)。
