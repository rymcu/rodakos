# Native light service host target

The target compiles production `LightService` and the entire production `board_device_adapter`.
Fake Board Manager descriptors expose `board_rgb` on LEDs 0–2 and `accent` on LED 3. Only SDK
discovery/LED/button calls are replaced; merge logic, setters, scaling and result creation are real.

```powershell
wsl -d Debian -- bash -lc '
  cmake -S /mnt/d/workspace/rodakos/tests/light_service \
    -B ~/.cache/rodakos-light-service -G Ninja -DCMAKE_BUILD_TYPE=Debug &&
  cmake --build ~/.cache/rodakos-light-service &&
  ctest --test-dir ~/.cache/rodakos-light-service --output-on-failure
'
```

For a separate ASan/UBSan build, add
`-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"` and
`-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"`; run CTest with
`ASAN_OPTIONS=detect_leaks=1`. The test timeout is 30 seconds.

The fake driver can reject an exact pixel write, refresh, discovery clear, or missing handle, and
block refresh to exercise concurrent operations. A mid-pixel failure intentionally leaves a
partially changed fake driver buffer. Tests assert unchanged accepted service configuration and
`unverified` application, not a fictitious hardware rollback. The 13 cases are software evidence;
they do not test RMT timing, physical RGB output, persistence or board task scheduling.
