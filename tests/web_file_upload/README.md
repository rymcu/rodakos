# Web 文件上传租约测试

该 host target 编译生产 `WebFileSystemService::UploadHandler`，以假 HTTP 请求和宿主文件目录覆盖实际状态码与文件生命周期：录音目录租约冲突返回 409；持租约期间删除/重命名被拒绝；成功上传返回 200；截断请求、ferror、flush 失败和 close 失败返回 500 并清理部分文件。

测试不证明 ESP-IDF 网络栈、SD 卡驱动或设备文件系统行为。录音服务的真实文件生命周期由 `tests/recording_service` 覆盖；本 target 的 stdio faults 只用于验证 UploadHandler 的失败分支。

```powershell
wsl.exe -d Debian --exec cmake -S /mnt/d/workspace/rodakos/tests/web_file_upload `
  -B /home/ronger/.cache/rodakos-web-file-upload -G Ninja -DCMAKE_BUILD_TYPE=Debug
wsl.exe -d Debian --exec cmake --build /home/ronger/.cache/rodakos-web-file-upload
wsl.exe -d Debian --exec ctest --test-dir /home/ronger/.cache/rodakos-web-file-upload --output-on-failure
```
