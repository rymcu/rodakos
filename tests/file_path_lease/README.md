# File path lease host tests

This target exercises the path policy used by `FileService::WithWriteLease` without requiring an
SD card or Board Manager. It verifies mount-relative and full-mount spellings, FAT-style ASCII case
folding, slash and backslash normalization, root paths, NUL and parent traversal rejection, exact
versus ancestor/descendant conflicts, release behavior, and concurrent lease admission. NUL-bearing
paths are invalid because POSIX/FAT C-string APIs would otherwise truncate them before opening.

Run under Linux or WSL:

```sh
cmake -S tests/file_path_lease -B "$HOME/.cache/rodakos-file-path-lease" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build "$HOME/.cache/rodakos-file-path-lease"
ctest --test-dir "$HOME/.cache/rodakos-file-path-lease" --output-on-failure
```

The tests cover path policy only. They do not prove FAT Unicode collation, SD controller behavior,
power-loss durability, or the lifetime of a callback that uses a real FileService mount.
