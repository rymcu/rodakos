# Task retirement host 验证

编译完整生产 `main/phone_os/task-retirement.cc`，以及本机已审阅 ESP-IDF 6.0.2
`idf_additions.c` 的完整 WithCaps 创建与删除函数链。准备脚本检查原始文件 SHA256，
并输出函数清单和编译输入指纹；来源变化会拒绝构建。
辅助 pin 同时覆盖 IDF wrapper header、kernel additions、heap 与 Xtensa 配置来源；
这些辅助文件用于核验模型前提，没有被当作真实 kernel/allocator 编译。`sdkconfig`
另记指纹并检查双核、1536 B idle stack、PSRAM 栈等假设。Host 的 100 B TCB 占位
不是重新测量目标 ABI；`xTaskCreateWithCaps` 只是转发到已编译的真实 pinned API。

```sh
cmake -S tests/task_retirement -B /tmp/rodakos-task-retirement \
  -DRODAKOS_IDF_PATH=/path/to/esp-idf
cmake --build /tmp/rodakos-task-retirement -j 4
ctest --test-dir /tmp/rodakos-task-retirement --output-on-failure
```

正例覆盖发布前任务已调度、业务局部对象析构阻塞、两个 Join 与 Pump 竞争、跨核收敛、
自主退出、Close/Drain、创建失败、固定容量、引用阻止槽位复用、旧代票据和 self Join。
退出资源账本要求零额外分配、零 cleanup task 创建、stack/TCB 各释放一次。

`legacy_self_delete` 在业务 Start 成功后只拒绝名为 `prvTaskDeleteWithCapsTask`
的创建，要求真实 IDF 分支打印指定失败并以 SIGABRT 终止子进程。不会抛 C++ 异常来
模拟删除，因此不会靠异常退栈替旧实现完成局部析构。六个完整生产 TU 变异负控分别
移除发布握手、提前 finished、取消独占领取、移除自主 Pump、提前复用有引用槽位、
允许已关闭 owner 准入；每个均须命中指定断言。编译错误、未知信号及进程超时不算通过。

其他服务可 `add_subdirectory` 并链接 `rodakos_task_retirement_host`，将本目录
`fakes` 放在 include 搜索路径首位，使用 `task_retirement_host.h`。子目录默认不注册
独立测试，也可显式设置 `RODAK_TASK_RETIREMENT_BUILD_TESTS=OFF`。
仅生产 retirement TU 与 pin IDF TU 的 heap 边界通过编译宏映射为专用账本函数，
不覆盖 Camera/Display 现有图像与编码内存注入。

验证边界：host 模型替换底层 scheduler、临界区、core-current 查询和 allocator；
真实 WithCaps 函数体没有改写。模拟 worker 在 suspend 边界停驻，外部删除必须先看到
core-current 收敛。host 线程最终由 `pthread_exit` 收尾，RAII 断言发生在此之前；底层
删除还会同步 join 宿主线程，确认它不再执行后，真实 WithCaps 才能获取和释放 stack/TCB。
这不证明真实双核抢占、cache-off、DMA、IRQ、硬件延迟、长期稳定性或固件发布通过。
