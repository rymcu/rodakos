# Appearance Verification

As of 2026-10-02, source implementation, host gates, and the COM3 wireless custom-resource
acceptance pass. The latest device state is revision 14 with a remote Dark theme and the retained
wallpaper.

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

## Fresh live desktop acceptance

On 2026-10-02 a second, independent live session captured the first decoded frame without sending
the Home shortcut first. Six samples contained three distinct browser `blob:` URLs, confirming
renderer source replacement. The later Settings-to-Home interaction also verifies changing content.
All samples were 320×240 and contained the complete twelve-icon Home grid. The raw frame and
metrics are recorded in
`D:\workspace\rodak\build\logs\live-display-verification-frame.png` and
`D:\workspace\rodak\build\logs\live-display-verification.json`.

The same report carried `activeRevision=12`, `status=applied`, `pendingRevision=0`, and
`themeSource=local`. The decoded frame measured `bg=[8,24,40]`, 10,791 blue-dominant pixels,
zero magenta pixels, and 3,184 purple-classified pixels. The purple pixels are from the fixed
multicolour Home icon palette; the Settings capture shows the Blue (`B`) preset selected and no
magenta primary. This proves the live screen is blue, while also proving that revision 12's remote
theme is not the current source of the colours.

The final cleanup session restored Home and saved
`D:\workspace\rodak\build\logs\live-home-final.png`. It shows the status bar and twelve Home
icons, with no Lock Screen overlay. `PhoneShell` reads `lock_boot` with a default of `false`; a
startup lock therefore requires enabling **Settings → System Shell → Lock after startup**. The
live result is consistent with that preference and does not indicate a boot-animation failure.

## Theme and wallpaper editor recheck

On 2026-10-02 at 08:29–08:33 CST, the paired COM3 device was updated again through the real
Rodak editor: import a 320×240 PNG, reset crop/scale, select Blue with primary `#1976d2`, generate
the compiled wallpaper preview, and publish that preview. Only the desktop file-picker selection
was supplied by the test harness; compilation, persistence, signing, publication, device download,
boot adoption and reported state were real. Existing publisher trust was reused.

Revision 13 was published as deployment `e74c5cfa-9d70-4617-9e52-4ac670ad1f39`. Before the
controlled restart, the device reported `activeRevision=12`, `pendingRevision=13`,
`status=pending_reboot`; after the restart it reported `activeRevision=13`, `pendingRevision=0`,
`status=applied`, `themeSource=remote`. This newer deployment cleared the local theme override.
Firmware remained `0.1.2-dev.1`; no firmware image was written.

The compiled wallpaper and device frames have the same left-blue/right-green/bottom-gold regions.
Six successive 320×240 frames used six distinct JPEG ObjectURLs and retained the same Home body
hash. The last session measured 22,724 blue and 16,357 green wallpaper pixels. Settings was opened
over remote control and its primary brightness slider sampled `[25,117,216]`, matching the
RGB565/JPEG rendering of the Blue primary. Returning to Home restored the original body hash.
Remote control was then disabled; the final frame contains the wallpaper and twelve Home icons.
Resource loading took 182 ms and the animation took 2545 ms on the final confirmed boot.

Publication/preview evidence is in
`D:\workspace\rodak\build\logs\appearance-recheck\1790900963872\result.json`.
That run retained a failed extra Settings predicate: its global green-pixel limit also counted the
green theme selector. A subsequent whole-frame magenta check counted JPEG colour bleed from the
red remote-control status. These are harness false negatives, not claims of a passing original run.
The completed follow-up uses the actual slider colour and Home-return frame; it reports
`status=passed`, `hardwareVerified=true`, with server, application and serial cleanup complete:
`D:\workspace\rodak\build\logs\appearance-recheck-followup\1790901200050\result.json`.
Its `07-settings-remote-theme.png` and `08-final-home-readonly.png` are the final visual evidence.
Revision 13 and its wallpaper were left active instead of publishing an EDIX-only restore.

## Dark theme switch

At 08:56 CST, the editor published revision 14 with `preset=dark` and its default `#79cbff`
primary, retaining the existing wallpaper asset and 2500 ms EDIX animation. The device reported
active 13 / pending 14 / `pending_reboot` before a controlled restart, then active 14 / pending 0 /
`applied` / `themeSource=remote`. Loading took 182 ms; animation completion took 2520 ms.

Six new live Home frames confirmed wallpaper retention. The independent Settings frame sampled
black background `[0,0,0]`, dark-grey card `[24,24,24]`, and the light-blue primary slider
`[115,205,255]`. The Settings-to-Home return restored the original Home body hash and remote
control was disabled afterward. Black refers to the theme background; the colourful wallpaper
and fixed preset selectors remain visible.

`D:\workspace\rodak\build\logs\appearance-dark-test\1790902564325\result.json` records
`status=passed`, `hardwareVerified=true` and completed test cleanup. The independent theme image
is `07-settings-remote-theme.png`; the final Home image is `08-final-home-readonly.png`.
The device remains on revision 14 with unchanged firmware `0.1.2-dev.1`.

## Remaining physical limits

Device IP: `192.168.1.59`; persisted cloud endpoint: `http://192.168.1.5:9080`. Only endpoint strings
were inspected from NVS in RAM; private credential contents were not printed or stored.
Rodak listens on HTTP 9080 / MQTT 1883. Earlier attempts timed out and a firewall-rule creation
was denied; the later wireless and editor recheck sessions successfully used both services.

Power-cut boundaries, missing/slow SD, and abnormal-start rollback remain outside this run. The
wireless run proves the signed download, next-boot adoption, offline reuse, wallpaper/theme
rendering, and EDIX restoration paths on COM3; it does not replace those fault-injection gates.
