# OpenOS Comparison And RodakOS Decisions

This document records the July 21, 2026 source review of
[openplace1/OpenOS](https://github.com/openplace1/OpenOS). It is a design input, not a dependency
proposal. The reviewed revision is
[`9205751506fea3fca57ce2f16f0f4ccaff6745eb`](https://github.com/openplace1/OpenOS/commit/9205751506fea3fca57ce2f16f0f4ccaff6745eb),
committed on July 20, 2026.

## Executive Decision

RodakOS will keep its ESP-IDF, Board Manager, LVGL, PhoneServices, and Recovery/OTA foundations.
It will implement Lock Screen, Control Center, and Home organization as native RodakOS features.
It will not import the OpenOS OSA runtime, package format, direct-drawing SDK, or writable-SD
privilege model.

OpenOS is best understood as an Arduino firmware that runs replaceable UI scripts from SD on a
classic ESP32 CYD. RodakOS is an ESP32-S3 firmware with explicit board adapters, services, native
app lifecycle, and a recovery-aware flash layout. Their useful overlap is product behavior and
install transaction design, not implementation architecture.

## Platform Comparison

| Area | OpenOS at reviewed revision | RodakOS direction |
| --- | --- | --- |
| Target | Classic ESP32 CYD / ESP32-2432S028R | ESP32-S3 RYMCU BigSmart |
| Framework | Arduino with PlatformIO | ESP-IDF 6.0.2 |
| Display/input | ILI9341, TFT_eSPI, XPT2046 | ST7789, LVGL 9.3, cached GT911 bridge |
| Memory assumption | No PSRAM required | 8 MiB PSRAM available |
| App delivery | SD-hosted OSA scripts and bytecode | Statically linked native apps; future MiniApps separate |
| Hardware access | Runtime builtins call WiFi, SD, Bluetooth, and backlight directly | Apps use PhoneServices; adapters own raw board APIs |
| Update layout | 3 MiB `huge_app.csv`, no OTA path | immutable Recovery, OTA journal, large `ota_0`, SD staging |
| Bluetooth | Classic Bluetooth SPP | ESP32-S3 cannot use Classic Bluetooth |

OpenOS hardware and build facts are stated in its
[README](https://github.com/openplace1/OpenOS/blob/9205751506fea3fca57ce2f16f0f4ccaff6745eb/README.md#L52-L63)
and [PlatformIO configuration](https://github.com/openplace1/OpenOS/blob/9205751506fea3fca57ce2f16f0f4ccaff6745eb/platformio.ini#L11-L19).
The framework package version is not pinned. During a prior PlatformIO 6.1.19 validation,
`oplatform_packages` was reported as an ignored option, confirming that the misspelled toolchain
override does not apply.

## Runtime And Lifecycle

OpenOS routes four global states: `LOCKSCREEN`, `HOMESCREEN`, `IN_APP`, and `CONTROLCENTER` in
[`main.cpp`](https://github.com/openplace1/OpenOS/blob/9205751506fea3fca57ce2f16f0f4ccaff6745eb/src/main.cpp#L222-L230).
One reusable `OSAApp` instance is recycled between Home, Lock Screen, and normal apps. A second
`OSAApp` is allocated on demand for Control Center
([overlay path](https://github.com/openplace1/OpenOS/blob/9205751506fea3fca57ce2f16f0f4ccaff6745eb/src/main.cpp#L564-L637)).
Closing Control Center reruns setup for the underlying app; there is no native
Create/Resume/Pause/Destroy lifecycle
([`OSAApp`](https://github.com/openplace1/OpenOS/blob/9205751506fea3fca57ce2f16f0f4ccaff6745eb/src/Applications/OSAApp.cpp#L14-L90)).

OSA is a custom interpreter/bytecode VM with fixed limits including 512 lines, 96 variables,
24 functions, 8 KiB bytecode, and an instruction/time budget
([runtime limits](https://github.com/openplace1/OpenOS/blob/9205751506fea3fca57ce2f16f0f4ccaff6745eb/src/Runtime/OSARuntime.h#L6-L19)).
At the reviewed revision, `OSARuntime.cpp` is about 6,213 lines and `PackageManager.cpp` about
1,586 lines. Importing that runtime would add a second UI, lifecycle, service, and storage model to
RodakOS.

RodakOS therefore uses these rules:

- PhoneAppHost is the only owner of `OnCreate`, `OnResume`, `OnPause`, and `OnDestroy`.
- Candidate app creation is transactional; failure keeps the current app alive.
- Lock Screen and Control Center are PhoneSystem-owned `lv_layer_top()` overlays, not PhoneApps.
- Music, recording, synchronization, and other continuous behavior belongs to services, not
  retained background UI instances.

## Home And Shell Features

OpenOS Home has useful interaction ideas: long-press actions, drag ordering, folders, and uninstall.
Its top level is fixed to 16 4x4 slots with no pagination, while folders hold up to 12 entries
([`Home.h`](https://github.com/openplace1/OpenOS/blob/9205751506fea3fca57ce2f16f0f4ccaff6745eb/src/Applications/Home.h#L34-L50)).
Layout is serialized by display name rather than stable package ID
([`Home.cpp`](https://github.com/openplace1/OpenOS/blob/9205751506fea3fca57ce2f16f0f4ccaff6745eb/src/Applications/Home.cpp#L177-L263)),
which makes duplicate names and renames unsafe.

RodakOS will apply the product ideas with different invariants:

- 4x3 LVGL pages are derived from the registry and have real dynamic page indicators.
- Future ordering and folders persist stable descriptor IDs in a versioned schema.
- Registry reconciliation removes missing/duplicate IDs and appends newly registered apps.
- Long press and launch events remain distinct so releasing after an edit gesture does not launch.
- Overflow is reported explicitly instead of silently dropping apps.

## Security And Capability Boundary

OpenOS sandboxing is API-level path rewriting, not process, task, MPU, or memory isolation
([filesystem sandbox](https://github.com/openplace1/OpenOS/blob/9205751506fea3fca57ce2f16f0f4ccaff6745eb/src/Runtime/OSARuntime.cpp#L3402-L3443)).
Sensitive runtime builtins still directly reach WiFi, SD, Bluetooth, backlight, and system actions.
Its privilege checks restrict root to fixed system paths and official OPK entries
([privilege path](https://github.com/openplace1/OpenOS/blob/9205751506fea3fca57ce2f16f0f4ccaff6745eb/src/Runtime/OSARuntime.cpp#L3605-L3628)),
but system scripts on writable removable storage remain vulnerable to physical replacement. Stored
WiFi credentials and the lock code use fixed-key XOR obfuscation
([`Crypto.h`](https://github.com/openplace1/OpenOS/blob/9205751506fea3fca57ce2f16f0f4ccaff6745eb/src/Applications/Crypto.h#L5-L30)).

RodakOS native `PhoneCapability` values remain descriptive metadata. They must not be presented as
an enforcement boundary. A future MiniApp implementation requires:

- a capability broker between untrusted code and services;
- separate storage roots and settings namespaces;
- bounded memory, execution time, UI object count, and network use;
- no raw `PhoneServices`, board handles, LVGL roots, or privileged navigation object;
- signed manifests and packages rooted in an immutable verification key;
- explicit policy for camera, microphone, network, notifications, and background work.

The first RodakOS swipe Lock Screen is only a privacy cover and accidental-touch barrier. It is not
authentication and does not protect the early USB MSC boot path, removable SD contents, flash, or
NVS without additional secure-boot, flash-encryption, and credential policies.

## Package Management Lessons

OpenOS package handling contains several sound transaction ideas: stable package ID and version
fields, path traversal checks, per-file and total-size limits, CRC/SHA checks, staging, backup, and
rollback before activation
([ZIP validation](https://github.com/openplace1/OpenOS/blob/9205751506fea3fca57ce2f16f0f4ccaff6745eb/src/Runtime/PackageManager.cpp#L810-L856)).

Its current trust chain is not sufficient for RodakOS:

- Catalog and package downloads call `WiFiClientSecure::setInsecure()`
  ([TLS path](https://github.com/openplace1/OpenOS/blob/9205751506fea3fca57ce2f16f0f4ccaff6745eb/src/Runtime/PackageManager.cpp#L692-L700)).
- The expected SHA-256 is delivered by the same unauthenticated catalog as the package URL.
- Packages have no publisher or platform signature.
- A URL-prefix allowlist cannot prevent a network attacker from replacing the catalog, hash, and
  package together.

RodakOS may reuse the transaction pattern, independently implemented, only after adding authenticated
transport, signed metadata, signed payloads, rollback state, and an immutable trust root.

## Project Maturity And Reproducibility

At review time, OpenOS had 23 commits, one star, no forks, no releases, no CI, and no tests. The
repository was created in April 2026 and had undergone recent delete/re-upload restructuring. Its
controlled `sd_content/` no longer contains the required Lock Screen and Home scripts, while boot
still requires those paths
([boot loading](https://github.com/openplace1/OpenOS/blob/9205751506fea3fca57ce2f16f0f4ccaff6745eb/src/main.cpp#L715-L725)).
Following the current clean-SD instructions therefore does not appear sufficient to reach a Home
screen and bootstrap OpenStore. This is consistent with the `1.0-alpha` tag and makes OpenOS a
design reference rather than a production dependency.

A prior PlatformIO 6.1.19 validation built OpenOS successfully. The resulting `firmware.bin` was
2,024,560 bytes; the link report used 100,780 of 327,680 bytes of RAM and 2,017,977 of 3,145,728
bytes of flash. The ignored `oplatform_packages` warning means that result does not prove the
repository pins its intended toolchain. No CYD device was flashed or tested.

## License Handling

OpenOS uses the [MIT License](https://github.com/openplace1/OpenOS/blob/9205751506fea3fca57ce2f16f0f4ccaff6745eb/LICENSE).
RodakOS is independently implementing the selected ideas and is not copying OSA runtime or system
script code. Any future copied or adapted OpenOS code must retain the OpenOS copyright and MIT text.

RodakOS currently has no root license file and includes components under their own licenses. Before
accepting third-party code, add a project license decision and a `THIRD_PARTY_NOTICES` inventory
without implying that one root license replaces component-specific terms.
