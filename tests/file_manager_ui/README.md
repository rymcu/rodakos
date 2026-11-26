# File Manager UI regression

This target compiles the production `FileManagerApp`, `PhoneAppHost`, `PhoneUi`,
theme and components against LVGL 9.3's real 320×240 in-memory display with the
production fonts (Montserrat → Ionicons → PuHui). FileService uses its production
interface with controlled mount/list/capacity/delete results; the image loader is a
fake returning owned LVGL descriptors or explicit failures. `xTaskCreate` runs the
capacity probe inline unless a test holds it. It does not test the image decoder or
physical SD card.

The 25 cases cover initial read failure versus empty folders, missing service and
card recovery, partial/stale-list rejection, missing child retry at the original
path and navigation back, errno classification, preview retry and image detachment,
invalid descriptors, service/card rechecks, repeated teardown/recreation, deferred
Home cancellation/deduplication and rejected async scheduling, ordinary file info,
the root free-space strip (drawn before and after a held probe, and when the probe
fails), subfolder titles, the full-screen preview with tap-to-hide chrome and its info
page, delete confirmation calling `DeleteFile` directly, a busy (`EBUSY`) file, no
delete under `/rodakos`, and back closing the dialog first. Image destructors detect
release while still attached to an LVGL object. The suite writes `files-*.ppm`
screenshots for visual inspection.

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
