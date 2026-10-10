# Camera capture host tests

This target compiles the production `CameraService`, `FileServiceImpl`, directory reader and
`WriteFileBytes` helper. A fake V4L2 device delivers a 2×2 RGB565 frame through the real preview task;
the real frame snapshot/conversion/capture path calls a fake JPEG encoder. Board Manager mounts a
unique temporary host directory, and photo selection, exclusive creation, writing, flushing,
closing and cleanup use real host filesystem operations.

The 47 tests cover:

- Publishing `saved_path` and `last_saved_path` only after a successful save, including a blocked
  write where the candidate path must remain unpublished.
- The JPEG encoder is opened and closed only inside `ScreenJpegAllocationScope` for a capture,
  a failed encode and the JPEG stream worker. The host scope is a thread-local recorder; production
  allocator routing is covered by `tests/screen_jpeg_allocator` and the final-ELF audit.
- Missing frame/service/storage/directory, encoder open/process/empty output and input allocation
  failures, with an empty per-call result and a retry after recovery.
- `FileServiceImpl::ListDirectory` clears stale entries and reports `ENODEV` when
  unmounted, and preserves a real missing-directory `ENOENT` across adapter logging.
- Short writes, flush and close failures, cleanup of failed newly created files, and preservation
  of the previous successful photo and its historical `last_saved_path`.
- An external writer winning the exclusive-create race, preserving its file and selecting a new
  name on retry; concurrent captures with a frozen clock; two cameras sharing one FileService.
- Draining an in-flight capture before service destruction, including a JPEG callback that calls
  `CapturePhoto`, retaining local/remote preview leases across capture failures and retries, and
  releasing both owners before service destruction. The target also exercises FileService path leases
  rejecting overlapping save/delete operations while allowing reads.
- Final preview stop and unexpected dequeue failure revoke the last frame and free its allocation;
  stopping either owner alone preserves the other owner's live preview. A restarted preview must
  deliver a new frame before capture can resume. Already copied frames remain valid across stop.
- A failed `VIDIOC_STREAMON` never enters the running-stream teardown path: mapped buffers, the fd
  and Board Manager ownership are released without `VIDIOC_STREAMOFF`, and a later preview can start.
  A stream that did start still retains all ownership when `VIDIOC_STREAMOFF` fails.
- A separate production-TU probe compiles the default-off GC0308 test-pattern path, verifies the
  V4L2 control is applied before streaming, and confirms normal worker/resource retirement.
- A second production-TU probe compiles the default-off GC0308 register diagnostic, verifies the
  configured/streaming/first-frame page 0 and page 1 snapshots, page restoration and normal
  worker/resource retirement without enabling the test pattern.
- Concurrent snapshot readers and a blocked final worker state read verify that stop cannot publish
  completion while the worker still accesses service state. The allocation wrappers track the real
  preview worker's 2×2 RGB565 payload, so clearing vector size without freeing its capacity fails.

Run under Linux or WSL:

```sh
cmake -S tests/camera_capture -B "$HOME/.cache/rodakos-camera-capture" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build "$HOME/.cache/rodakos-camera-capture"
ctest --test-dir "$HOME/.cache/rodakos-camera-capture" --output-on-failure

cmake -S tests/camera_capture -B "$HOME/.cache/rodakos-camera-capture-asan" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined -no-pie"
cmake --build "$HOME/.cache/rodakos-camera-capture-asan"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir "$HOME/.cache/rodakos-camera-capture-asan" --output-on-failure
```

Debug and ASan/UBSan with leak detection passed all 47 tests after the JPEG PSRAM scope change;
the scope case fails against the pre-082 CameraService (`unscoped_encoder_calls` is non-zero).
With the updated tests,
the pre-fix CameraService fails six cases. A separate mutation retaining vector capacity with
`clear()` instead of returning the frame allocation also fails six cases, confirming that these
  checks cover allocation lifetime as well as stopped-state flags. The encoder returns deterministic
test bytes: these checks do not prove JPEG validity/quality, physical camera behavior, FAT/SD
performance, power-loss durability or CameraApp/LVGL completion delivery. A blocked underlying I/O
operation can keep capture/destruction waiting; no bounded hardware shutdown is claimed. No device,
serial port, NVS, flashing or packaging is used.

## Allocation-failure recovery (018 source)

The additional 12 cases compile the complete production `camera_service.cc`, including both worker
entries and the snapshot/JPEG/photo paths. Link wrappers reject a selected allocation by byte size,
occurrence and caller/preview/JPEG thread; static libstdc++ linkage makes string and callback-copy
allocations visible to the same wrappers. Semaphore fakes abort after a bounded wait when production
code leaks a lock, and aligned buffers, encoder handles and driver dequeue/requeue counts are tracked.

Covered boundaries include first/subsequent preview-frame allocation failure, caller and worker
snapshot failure, packed-frame/JPEG-output failure, state/error-string copies, callback copies and
callback delivery. Rejected preview frames return to the driver while existing owner leases and
published snapshots remain usable. Persistent callback-copy failure still permits Stop; a callback
that throws after accepting a sequence does not receive that sequence again while the source is
unchanged. Failed photo-path allocation retains the previous successful photo, and a successful
write publishes its preallocated result strings even when all later caller allocations are rejected.

On 2026-10-07 all 33 tests passed in Debug and ASan/UBSan with leak detection. Six selected cases were
also run against the complete pre-fix 017 CameraService translation unit: five aborted on an
unhandled allocation exception or leaked semaphore, and the post-write publication case failed on
`std::bad_alloc`. The working source was not replaced for these negative controls.

These results cover recoverable `std::bad_alloc` at the named service boundaries. The short
`Camera OOM` fallback uses libstdc++ small-string storage, but firmware still has a zero-sized C++
exception emergency pool: total heap exhaustion can fail before a catch is reached. Parameter/error
paths outside these catches, CameraApp/LVGL allocations and storage/DMA implementation failures are
separate boundaries. The task-deletion fake does not validate ESP-IDF `vTaskDeleteWithCaps(nullptr)`:
its temporary cleanup task still requires internal stack/TCB allocation and can abort at low heap.
No arbitrary-OOM or physical shutdown guarantee follows from this host suite.

## STREAMOFF 与日志阻塞诊断（022）

新增四项独立 CTest 使用同一份生产 `CameraService`、真实固定 DRAM 记录模块与 host
V4L2/日志边界。每项启动新进程，不向记录模块添加 reset：分别阻塞 STREAMOFF 内部或
返回后的 complete 日志，并各自覆盖返回 `0` 和 `-1`。在阻塞期间从另一线程读取 snapshot，
应分别只见 ioctl enter，或见 ioctl returned / before-log 而没有 after-log；释放后必须看到
四个标记、原始有符号返回值、preview 停止与帧内存归还。异常清理先释放 gate，再等待 worker。

原 33 项与新增 4 项在 Debug、ASan/UBSan/leak 下通过。共 5 项 CTest（原 suite 为一个
CTest）。这些测试证明生产调用点能区分两类受控阻塞，不证明实机 STREAMOFF 故障的原因。
`ioctl` 的 `-1` 不包含 errno；标记中的 core 只表示该调用点采样。enter 前仍有普通 begin
日志，缺少 enter 本身不能证明程序未进入 CloseStream。真实下层驱动分段由独立
`camera_teardown_patch` suite 验证。
