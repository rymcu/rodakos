# Appearance Verification

As of 2026-10-02, source implementation, host gates, and the COM3 wireless custom-resource
acceptance pass. Source changes remain uncommitted and unpushed.

## Software evidence

| Gate | Result |
| --- | --- |
| Appearance codec / RSA / policy / allocator host suite | 26 tests, 0 failures |
| App-model host suite | 221 tests, 0 failures |
| Production Home / animation LVGL suite | 43 tests, 0 failures, Debug and ASan/UBSan with leak checks |
| Real desktop package contract | 5 packages accepted by the latest production codec |
| ESP-IDF 6.0.2 build | Passed, `build/logs/appearance-idf-budget.log` |

Cross-repository input came from real Electron compilation; its default EDIX is 5306 bytes on
wire / 8008 decoded bytes. Four preset wallpaper packages decode to 161608 bytes. Evidence:
`build/logs/appearance-contract-check-384k-1790868599392-0595ffbe.jsonl`.

The decoder caps pixels at 384 KiB and the private parser at 96 KiB. These are controlled resource
budgets; shared MQTT, STL/LVGL and SDK overhead are outside these counters. A total physical-PSRAM
hard bound has not been proven. Custom-resource peak use still needs device measurement.

## Latest COM3 package and boot

Package: `build/packages/ota/20261001-234748`, development-signed, production Home population.
The guarded non-erasing refresh writes only otadata and ota_0 and retains NVS, OTA journal,
partition table, bootloader and the matched immutable Recovery from `20261001-062518`.

| Artifact | Size / SHA-256 |
| --- | --- |
| Main | 6897584 bytes; `ec7f8d726f34d3a1a75681fa365304c407f9aeec0fda906f8d80e1daafc304d0` |
| Recovery | `ffa412ebe30c714c691bba73c8ab6e4efcaaab14fce5229f595707a8a08f75fd` |
| Main slot | 0xd50000 bytes; 51% free |

Flash and first-boot gates passed:
`build/logs/appearance-flash-final.log` and `build/logs/first-boot-20261001-234802.log`.
Board Manager, cached touch, LVGL, backlight, PhoneSystem, Home, OTA local confirmation and
`RodakOS started successfully` were observed.

Ten further controlled boots used one persistent USB serial session. Every boot displayed builtin
EDIX, reached Home, completed its animation and restored input. Durations were 2505–2605 ms,
mean 2563 ms. Startup free PSRAM samples were 2982424–2983444 bytes; these are baseline samples,
not a custom-resource peak measurement. Records:
`build/logs/appearance-builtin-ten-boots.json` and `appearance-builtin-ten-boots.log`.

Windows usbser.sys requires a DTR update after changing RTS to send the new USB control state.
The repeated-reset collector and desktop hardware runner use that order. Opening the port can
also cause a reset; never describe serial open as reset-free.

## COM3 wireless acceptance

The complete real-device runner passed with `hardwareVerified=true`:
`D:\workspace\rodak\build\logs\appearance-hardware\1790897257641\result.json`.
The run reused the trusted publisher fingerprint `23bc b548 aa3e cb68 31f6 0253 62fb bc62`.

- Ten online EDIX boots used revision 10, with 2.52–2.61 s animation duration and 105–108 ms
  appearance loading. Home and input recovered on every boot.
- Revision 11 applied the 320×240 wallpaper and Blue theme with the native `#1976d2` primary;
  the saved device frame measured 22,726 blue wallpaper pixels, 16,370 green wallpaper pixels,
  and 75 primary-blue pixels.
- Two Rodak-offline boots kept revision 11 active and healthy. After reconnect, revision 12
  restored EDIX and the device firmware version remained unchanged.
- The device frame is a Home desktop, not a lock screen. The runner now checks the decoded frame
  before applying wallpaper thresholds, and accounts for Home's 50% wallpaper overlay.

## Remaining physical limits

Device IP: `192.168.1.59`; persisted cloud endpoint: `http://192.168.1.5:9080`. Only endpoint strings
were inspected from NVS in RAM; private credential contents were not printed or stored.
Rodak listens on HTTP 9080 / MQTT 1883. Device transport currently times out. The PC's WLAN is
Public, and creating a device-scoped firewall rule was denied for lack of administrator rights.
Rodak's `docs/appearance-verification.md` contains the proposed rule and restartable hardware runner.

Power-cut boundaries, missing/slow SD, and abnormal-start rollback remain outside this run. The
wireless run proves the signed download, next-boot adoption, offline reuse, wallpaper/theme
rendering, and EDIX restoration paths on COM3; it does not replace those fault-injection gates.
