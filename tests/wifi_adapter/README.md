# Wi-Fi adapter 恢复回归

直接编译生产 `main/rodakos_adapters/wifi_adapter.cc` 和 connection policy，fake 仅替换
ESP-IDF / FreeRTOS 边界。覆盖持续 AP 缺失、1/2/4/8/16/30 秒封顶退避、SDK 失败、事件队列
拥塞、20 秒关联 / DHCP watchdog、超时断开屏障、首次错误凭据的有限失败、手动断开、
SSID / 同 SSID 新配置换代、迟到 STA / IP 事件，以及重试与断开 / Deinit 的所有者竞争。

仅当前显式 Connect 代次成功取得过 IP 才持续恢复；保存过但本轮从未连通的配置仍执行
原有限重试策略。成功恢复重新通知原 callback（若存在），恢复资格不依赖 callback。
手动 Disconnect、Connect 新代次、Deinit 都撤销旧资格；断开超时也不会恢复旧自动尝试。

不新增线程或任务栈。一个 FreeRTOS 周期 timer 每秒检查截止时间并向默认 event loop
投递携带代次的事件；daemon 不调用驱动，也不等待队列。所有驱动命令由同一个 api mutex
串行执行。Deinit 先在所有者锁下失效代次，释放锁后删除 timer，并通过同一 daemon FIFO
上的 barrier 等待已选中的回调结束，再注销事件及销毁资源。

主机 timer fake 模拟已选中回调与 delete/barrier 的等待关系；事件 payload 按值入队。
这些测试验证生产控制流，真实 AP 恢复、DHCP、MQTT / WSS 重建仍须单独实机确认。

```sh
cmake -S tests/wifi_adapter -B build/wifi-host -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/wifi-host
ctest --test-dir build/wifi-host --output-on-failure
```
