# File Manager UI regression

This target compiles the production `FileManagerApp`, `PhoneAppHost`, `PhoneUi`,
theme and components against LVGL 9.3's real 320×240 in-memory display. FileService
uses its production interface with controlled mount/list results; the image loader
is a fake returning owned LVGL descriptors or explicit failures. It does not test
the image decoder or physical SD card. Host fonts/icons are test substitutes.

The 15 cases cover initial read failure versus empty folders, missing service and
card recovery, partial/stale-list rejection, missing child retry at the original
path and navigation back, errno classification, preview retry and image detachment,
invalid descriptors, service/card rechecks, repeated teardown/recreation, deferred
Home cancellation/deduplication and rejected async scheduling, and ordinary file info.
Image destructors detect release while still attached to an LVGL object. The suite
also writes `files-read-error.ppm` and `files-preview-error.ppm` for visual inspection.

```sh
cmake -S tests/file_manager_ui -B "$HOME/.cache/rodakos-file-manager-ui" \
  -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$HOME/.cache/rodakos-file-manager-ui" -j 6
ctest --test-dir "$HOME/.cache/rodakos-file-manager-ui" --output-on-failure -V
```

For a separate ASan/UBSan build add both C and C++ flags
`-fsanitize=address,undefined -fno-omit-frame-pointer`, then run with
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1`.
Directory and preview reads remain synchronous; these checks do not establish
bounded slow-card cancellation, real OOM, physical touch, SD removal or persistence
through power loss. UI destruction waits for LVGL ownership before detaching images.
