# Photos and ImageLibrary host regression

This target compiles production `PhotosApp` and `ImageLibrary` with the production FileService
interface, real LVGL 9.3 at 320 × 240, its BMP/LodePNG decoders and stdio filesystem driver.
The controlled FileService supplies directory entries, mounting outcomes and errno failures.
Images are temporary host files read by the production C stdio path. Only the ESP JPEG decoder
and allocator platform APIs are fakes: the small JPEG SOF fixture verifies loader/ownership
behavior, not actual JPEG compression quality or the device decoder.

The 23 cases cover missing service, mount/read/missing-directory errors, truly empty albums,
strict recursive failure propagation, optional-album fallback, recursion limits and sorted format
selection; visible error/retry and repaired thumbnail state; timer/Home scheduling failure;
preview/back/rescan/destruction ownership; and actual PNG/BMP decode paths. BMP cases include
24/32-bit direct pixels, RGB565 bitfields, invalid magic/offsets/dimensions, truncated pixel ranges,
unsupported palette/compression, pixel-read failure, and same-path replacement with header caching
enabled. PNG cases include valid decode, 24-byte header-only input, damaged IDAT, repeated
failure/recovery and rendering after preflight closes. File seek/read/close and confirmed allocation
failures retain separate statuses; unknown decode failures use general wording.

The tests drive real LVGL Retry buttons and produce four `photos-*.ppm` frames in the build
directory for layout inspection. Host font fixtures do not establish device glyph/icon coverage or
physical readability. Only the Photos test source uses `-fno-access-control` to inspect
ownership and exercise lifecycle boundaries; production visibility is unchanged.

```sh
cmake -S tests/photos_ui -B ~/.cache/rodakos-photos-ui -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build ~/.cache/rodakos-photos-ui -j4
ctest --test-dir ~/.cache/rodakos-photos-ui --output-on-failure
```

Configure a separate directory with
`-DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"` and
`-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"`, then run CTest with
`ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1`.

Scanning, retry, file reads and decode remain synchronous; navigation does not interrupt an
in-flight SD/library call. Destroy waits for the LVGL lock before canceling callbacks and releasing
image sources; no bounded teardown latency is claimed. BMP remains a filesystem source after
validating its current headers and all pixel bytes, so later removal or modification of that file
is outside this point-in-time load result. PNG preflight is an actual no-cache decode, but a later
draw still needs decoder memory. Generic LVGL decode failures do not identify every OOM cause.
No arbitrary LVGL/CLIB exhaustion, device SD, physical touch, hardware JPEG, NVS or firmware flash
is exercised. These remain separate #28 acceptance gates.
