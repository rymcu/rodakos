# Wake UI regression

This target compiles the production `WolApp`, `WolDeviceStore`, the device codec and packet
parsing, the soft keyboard and the production fonts against LVGL 9.3's real 320×240 in-memory
display. NVS is the `tests/home_ui` Settings fake, so the real store and JSON codec run on top of
it; `WakeOnLanService` is a fake that records every send and returns a scripted result; Wi‑Fi is
a fake with a settable status and SSID. The test source uses `-fno-access-control` to read page
state.

The 13 cases cover the list (Wi‑Fi strip with the device count, one card per device, a separate
46×40 power target), sending with a 2 s green confirmation on the card, a failed send, the offline
strip (warning text, a 去设置 link that opens Settings on the Wi‑Fi page through `LaunchFrom` and
fits on screen, power blocked, editing still allowed), the empty list and its add button, adding a
device through the single-field input pages (trimmed name, MAC normalised to upper case with
colons, the 7 port preset), invalid input shown on the input page (multicast or short MAC, port 0,
missing name on save), a duplicate MAC, editing with back stepping input → form → list → Home and a
confirmed delete, a corrupt list that resets only after confirmation, a newer-schema list that stays
read-only, a failed save that keeps the form, and a theme change that keeps an open editor's draft.
The suite writes `wol-*.ppm` screenshots.

```sh
cmake -S tests/wol_ui -B "$HOME/.cache/rodakos-wol-ui" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$HOME/.cache/rodakos-wol-ui" -j 6
ctest --test-dir "$HOME/.cache/rodakos-wol-ui" --output-on-failure
```
