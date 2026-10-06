# Camera capture host tests

This target compiles the production `CameraService`, `FileServiceImpl`, directory reader and
`WriteFileBytes` helper. A fake V4L2 device delivers a 2×2 RGB565 frame through the real preview task;
the real frame snapshot/conversion/capture path calls a fake JPEG encoder. Board Manager mounts a
unique temporary host directory, and photo selection, exclusive creation, writing, flushing,
closing and cleanup use real host filesystem operations.

The 14 tests cover:

- Publishing `saved_path` and `last_saved_path` only after a successful save, including a blocked
  write where the candidate path must remain unpublished.
- Missing frame/service/storage/directory, encoder open/process/empty output and input allocation
  failures, with an empty per-call result and a retry after recovery.
- Short writes, flush and close failures, cleanup of failed newly created files, and preservation
  of the previous successful photo and its historical `last_saved_path`.
- An external writer winning the exclusive-create race, preserving its file and selecting a new
  name on retry; concurrent captures with a frozen clock; two cameras sharing one FileService.
- Draining an in-flight capture before service destruction, including a JPEG callback that calls
  `CapturePhoto`, retaining local/remote preview leases across capture failures and retries, and
  releasing both owners before service destruction. The target also exercises FileService path leases
  rejecting overlapping save/delete operations while allowing reads.

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

Debug and ASan/UBSan with leak detection passed on 2026-10-06. The encoder returns deterministic
test bytes: these checks do not prove JPEG validity/quality, physical camera behavior, FAT/SD
performance, power-loss durability or CameraApp/LVGL completion delivery. A blocked underlying I/O
operation can keep capture/destruction waiting; no bounded hardware shutdown is claimed. No device,
serial port, NVS, flashing or packaging is used.
