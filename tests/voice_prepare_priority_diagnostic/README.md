# Prepare 优先级诊断输出测试

本独立 host 目标直接编译生产 `voice_prepare_priority_diagnostic.cc`，仅将 completed
snapshot provider 替换为 fake take；不复制格式化实现，也不加载设备或网络 SDK。

5 个 Python 用例核验 JSON 输出：完整四点/两类提前退出、unavailable 与消费后不重放、
非法 count 不输出半条快照、16 字节任务名中的引号/控制/非 ASCII 字节安全 hex 编码、
两个记录的 scope/index/self/count 与 complete 一致。宽整数以 Python 整数读取。

```sh
cmake -S tests/voice_prepare_priority_diagnostic -B /tmp/rodakos-prepare-diagnostic -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/rodakos-prepare-diagnostic
ctest --test-dir /tmp/rodakos-prepare-diagnostic --output-on-failure
```

可添加 `-fsanitize=address,undefined -fno-omit-frame-pointer` 并以
`ASAN_OPTIONS=detect_leaks=1` 运行 CTest；本目标也纳入 `tools/run_release_host_checks.sh`。

fake take 的消费行为不是 observer 并发证明，真实模块状态见相邻 observer suite；
transport 的锁和 Cloud 接线见 transport suite。本目标不是现场串口收集器，不证明字节
原子性或输出交付。真实 take 在输出前已消费，丢行不得冒称可以重新读取；现场必须按
boot/package/capture、scope/index 和 complete 拼装完整证据。
