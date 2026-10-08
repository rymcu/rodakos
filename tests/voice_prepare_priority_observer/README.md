# Prepare 调用者自身优先级观察测试

本独立 host 目标编译生产 `voice_prepare_priority_observer.cc`。FreeRTOS 与时钟仅由
小型 host SDK 替代，测试断言所有 getter 位于 observer 临界区之外，priority/name getter
只接受 `nullptr` 查询当前任务。

9 个场景覆盖四点和 RAII、active/ready 拒绝覆盖、取得 open 前后的提前退出、非 owner
与 stale ticket、阶段顺序/四条预算、采样期间重入、时钟与名称边界、ID/碰撞计数饱和、
无当前任务。ready take 复制并消费，active take 不修改输出，scope ID 高水位不重置。

```sh
cmake -S tests/voice_prepare_priority_observer -B /tmp/rodakos-prepare-observer -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/rodakos-prepare-observer
ctest --test-dir /tmp/rodakos-prepare-observer --output-on-failure
```

可使用常规 ASan/UBSan 与 leak 检查；本目标也纳入 `tools/run_release_host_checks.sh`。
脚本化 getter 数值不模拟目标 FreeRTOS PI，不证明实际调度、ISR、锁等待或栈余量。
真实 transport TU 接线、输出协议和 OFF/目标链接分别由相邻 suite 与独立构建审核承接。
