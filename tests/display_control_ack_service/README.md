# Display Control ACK 服务验证

此目标直接编译完整生产 `main/phone_os/webrtc_display_service.cc`，使用生产 ACK tracker、
`components/esp_peer/include` 头文件与托管 cJSON。没有复制或重写 sender，也没有提取函数。
配置时生成的 `production-sources.json` 记录服务源码、服务头与 tracker 的 SHA-256；源码变化
会触发 CMake 重新配置。测试不会修改生产文件。

21 项用例覆盖：正常 ACK 的真实 JSON、peer 与 stream ID；显式和默认拒绝原因；Stop/Start
期间的旧 reply、旧排队结果和已经 `Take()` 的旧批次；新实例复用序号；等待发送锁时失效；
已通过最终检查的发送与 Stop 并发；五种终止状态和通道关闭；解析/序号拒绝；临时发送压力的
FIFO 重试、1 秒 / 50 次耗尽与致命错误终止、32 项队列溢出及分配失败；不完整 JSON 不发送、
队首 reason 拷贝失败保留预算；压力下生产 peer loop 继续泵浦并暂停 JPEG；六个启动资源失败
入口；服务析构后保留 reply 的弱引用失效。

重试仅发送同一 peer instance 的 ACK，不重复设备动作，也不转移到重建的 peer。队首只有在
SCTP 接受后出队；WOULD_BLOCK / NO_MEM 在后续主循环重试，其余错误或资源预算耗尽使用原
终止路径撤销控制。宿主 fake 区分发送尝试与成功，分配故障覆盖 cJSON 的 12 个分配位置和
C++ reason 拷贝 / 入队；另将生产 RemoteInputController 的取消入口连接到真实 MakeReply，
验证末端入队 OOM 只关闭原 peer，不从输入回调抛异常。这些软件证据不能直接归因 017 的实机 UI 超时。

测试从真实注册的 peer 回调进入 `HandleControlData`，调用它交付的真实 `MakeReply` closure，
然后调用生产 `FlushControlAcks` / `SendControlAck`。`esp_peer_send_data` fake 只记录实际调用
传入的 peer、stream、类型和编码字节，可阻塞这次调用以稳定重现 Stop 竞态。已经获准的
发送允许完成，但只属于旧 peer，`esp_peer_close` 不得与它重叠；Stop 完成之前新 Start 必须失败。

为确定性控制 flush 时点，任务 fake 分配真实非空 task handle 和 host thread，先暂停线程，
在 Stop 第一次 `vTaskDelay` 时放行，让生产 `PeerTask` 观察停止请求并执行真实 `FinishStop`。
除专门的泵浦回归外，正常运行中的 peer 主循环由此不自行消费 ACK；测试文件仅开放 private 可见性来调度真实
flush、取出批次、占用发送锁和观测 Stop admission。生产翻译单元使用原始头文件单独编译。
所有线程在每个 fixture 结束时 join；显示捕获、JPEG、时钟、FreeRTOS 调度和 peer transport
仍为 host 替身。此目标不证明 SCTP/WebRTC 出线、设备屏幕、LVGL 或物理输入行为。

```sh
cmake -S tests/display_control_ack_service \
  -B ~/.cache/rodakos-display-control-ack-service -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build ~/.cache/rodakos-display-control-ack-service
ctest --test-dir ~/.cache/rodakos-display-control-ack-service --output-on-failure
```

ASan/UBSan 检查可另设 build 目录并传入：

```sh
-DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
-DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
-DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
```

运行时使用 `ASAN_OPTIONS=detect_leaks=1`。项目根目录含空格时请给路径加引号；Windows 可通过
WSL Debian 执行上述命令，源码路径使用 `/mnt/d/workspace/rodakos/tests/display_control_ack_service`。
