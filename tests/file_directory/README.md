# Directory scan failure tests

This target compiles the production `ReadFileDirectory` used by the Board Manager-backed
`FileServiceImpl::ListDirectory`. It uses real temporary host directories and POSIX calls;
linker wrappers inject `opendir`, mid-`readdir`, `stat` and `closedir` failures. It does not
mount a card or instantiate Board Manager.

The eight cases distinguish an empty directory from failed storage access, preserve sorting
and metadata, discard partial/stale entries, preserve the first error during cleanup, and
verify a retry. An error from `stat` must not create a zero-byte playable entry. The caller
continues to own the mount lifetime and storage I/O lock.

```sh
cmake -S tests/file_directory -B build-host-music/file-directory-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host-music/file-directory-debug
ctest --test-dir build-host-music/file-directory-debug --output-on-failure -V
```

For ASan/UBSan add
`-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"` in a separate build
directory, then run CTest with `ASAN_OPTIONS=detect_leaks=1`.

These Linux host cases exercise software error propagation. They do not establish FATFS,
real-card removal, slow-card latency, memory exhaustion or device recovery acceptance.
