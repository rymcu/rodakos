# Gyro UI regression

This target compiles the production `GyroApp` and `MotionService` against LVGL 9.3's real 320×240
in-memory display with the production fonts. The sensor is a scripted `MotionSensorBackend`
(start, read and failure are controlled per test); `heap_caps_malloc` can be made to fail.

The 9 cases cover the level page reading 已水平 in the success colour within ±1.5°, a tilted
reading, 归零 making the current attitude zero (with yaw wrapping across ±180°) and 复位
returning to absolute angles, 定格 holding the reading (also through a failing read) until 继续,
the six-axis page with signed values, centred bars, a warning colour at full scale and the shared
freeze state, back navigation from the six-axis page, a sensor that will not start (error page,
重试 recovers), a read failure mid-session switching to the error page and back, the dial taking
the page background in the light theme and surviving a theme rebuild, a canvas allocation
failure, and stopping the motion service on destroy. The suite writes `gyro-*.ppm` screenshots.

```sh
cmake -S tests/gyro_ui -B "$HOME/.cache/rodakos-gyro-ui" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$HOME/.cache/rodakos-gyro-ui" -j 6
ctest --test-dir "$HOME/.cache/rodakos-gyro-ui" --output-on-failure
```
