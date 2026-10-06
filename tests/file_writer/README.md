# 文件保存结果宿主测试

本目标编译生产 `WriteFileBytes`，使用真实临时目录和 libc 文件操作，通过 linker wrapper 注入
打开、短写、stream error、flush、close 和清理失败。10 项用例验证新建文件的排他性、并发单一
胜者、完整内容、失败清理及重试，并保留普通替换/追加语义。

```sh
cmake -S tests/file_writer -B build-host-media-save/file-writer-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host-media-save/file-writer-debug
ctest --test-dir build-host-media-save/file-writer-debug --output-on-failure -V
```

ASan/UBSan 使用独立目录并添加
`-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"`；执行时设置
`ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1`。CTest 超时 20 秒。

只有完整写入、无流错误、flush 和 close 均成功才返回成功。独占创建失败不能覆盖或删除原有文件；
清理失败保留首个写入错误，可能留下本次未完成文件，不能变成成功。替换/追加失败可能已经部分改写
目标，因此不自动删除该文件。FileService 调用方保持挂载及 I/O 锁所有权。

这不验证实际 FAT/SD、断电恢复、落盘持久性或驱动永久阻塞时的有界完成时间。
