# Device Appearance Resources

RodakOS loads a signed local package containing a boot logo, Home wallpaper and theme. Rodak
compiles text/TTF/PNG sources on the desktop; ESP32-S3 receives pixels and a bounded timeline.
The firmware retains its built-in EDIX `RODAKOS` animation. Appearance resources do not change
the main firmware, immutable Recovery, or partitions.

This describes the current implementation. Host tests, compiler checks and a firmware build do
not prove wireless hardware acceptance. Physical trust confirmation, download, reboot adoption,
interruption recovery, SD timing and display readability need separate device evidence.

## Device workflow and publisher pinning

1. Pair BigSmart with Rodak and configure a reachable device-service origin. The supported
   product key is `rymcu-bigsmart`.
2. In Rodak, design, generate a preview and publish the previewed release. The default is EDIX
   `RODAKOS`, 2500 ms, with a letter entrance.
3. Open device **Settings → Appearance**, tap **读取/刷新**, and compare **发布端指纹** with Rodak.
   Exit remote control, then physically touch **指纹一致，确认信任**. Both press and click must
   originate from physical touch, and the displayed key must remain unchanged. Remote pointer
   and serial events cannot grant trust.
4. Wait for `pending_reboot`, then restart. Downloading does not restart an active application.
   Use the device's `applied` and `activeRevision` report to confirm adoption.

RSA-2048/SHA-256 signs deployments. `keyId` is SHA-256 of SPKI DER; the displayed fingerprint
contains the first 32 hex characters in groups of four. The device persists the full key ID,
public key and origin in `appearance` NVS. This is public-key pinning, not a numeric PIN. A new
key or origin requires another physical confirmation. Identity revocation forgets publisher trust.

Rodak's legacy service uses HTTP on a trusted LAN. Signatures authenticate resources but do not
encrypt Bearer tokens, download tickets or contents. There is no private-IP enforcement; public
HTTP is not an accepted production transport. The optional
[trusted server transport](trusted-server-discovery.md) now provides a direct Rodak HTTPS listener
and a USB-installed certificate/name pin. In this mode every Appearance API/artifact request uses
the pinned HTTPS origin, without a public-CA or plaintext fallback.

The first HTTP-to-HTTPS transition changes the publisher origin and still needs physical
confirmation. Ordinary IP changes preserve the stable logical hostname; changing the HTTPS port
changes the origin again. Server discovery never updates `appearance` trust on the owner's behalf.

Every request requires device authentication. Tickets bind device, deployment and `tokenVersion`,
expire after ten minutes, and reject superseded deployments. The firmware constructs fixed paths
under its configured origin and disables redirects:

- `GET /api/v1/aiot/appearance/publisher`
- `POST /api/v1/aiot/appearance/download-ticket`
- `GET /api/v1/aiot/appearance/manifests/:deploymentId?ticketId=…`
- `GET /api/v1/aiot/appearance/artifacts/:deploymentId?ticketId=…`

The server supports a single Range. The firmware starts with a full GET, then resumes a valid
partial with `Range: bytes=N-`. A 206 must match exact start/end/total and the remaining length;
a 200 resets the file before restarting. Transient failures retry at 5/15/60 seconds, at most
three retries. A 401 refreshes authentication once; a second rejection is terminal. Exhaustion
requires a new published revision after fixing the cause.

## RAP1 and signed manifest

A package has a 16-byte header, UTF-8 JSON metadata and contiguous pixels. Resource offsets are
relative to the pixel payload. All header fields after the ASCII magic are little-endian.

| Offset | Bytes | Value |
| --- | ---: | --- |
| 0 | 4 | ASCII `RAP1` |
| 4 | 2 | Version `1` |
| 6 | 2 | Width `320` |
| 8 | 2 | Height `240` |
| 10 | 2 | Resource count |
| 12 | 4 | JSON byte count |

Schema `rodak-appearance-v1` defines animation kind/template/colors/duration, units, optional
wallpaper, theme and resources. Units contain `resourceId/x/y/startMs/durationMs`, with upper-left
coordinates. Entrance uses SmoothStep. `letters`/`rise` move from `y + 10`; `fade` does not move.
The root fades for 220 ms after its configured display interval and system readiness. The LVGL
timer uses actual elapsed ticks, every 33 ms; preview timing is not a startup-time guarantee.

| Format | Layout |
| --- | --- |
| `a4` | `ceil(width/2)` bytes per row; left pixel in high nibble; expand each nibble × 17 to A8 |
| `rgb565` | Little-endian RGB565, two bytes per pixel |
| `rgb565a8` | Full RGB565 color plane, then full one-byte alpha plane |

Manifest envelope `{ signedPayload, signature, keyId }` signs the original Base64-decoded UTF-8
payload bytes with RSA-2048/SHA-256. Do not parse and reserialize before verification. Payload
binds deployment, device ID/Key, product, revision, mode, release, package size/SHA-256, dimensions
and key. Digest and metadata bounds are checked again when loading from SD.

Desired shadow carries `appearance.{deploymentId,releaseId,revision,mode,keyId}`. Reported shadow
carries capabilities, active/pending revision, status/progress, publisher, failure reason, theme
source and timings. Partial desired updates preserve appearance when light controls change.
Resource state and keys are independent of firmware OTA.

## Startup, memory and SD budgets

| Limit | Value |
| --- | --- |
| Display | 320 × 240 |
| Package / JSON | 512 KiB / 16 KiB |
| Decoded resource data | ≤384 KiB, including animation and wallpaper |
| Private JSON allocations | ≤96 KiB shared across appearance JSON nodes and print buffers, including the 16-byte allocation headers |
| Input / staging buffers | Metadata ≤16 KiB, small file / HTTP input ≤8193 bytes each, download IO 4096 bytes |
| Desired appearance JSON | ≤1024 bytes; exactly five protocol fields; MQTT projects into a fixed 384-byte buffer |
| Animation units | 32, plus one wallpaper |
| Duration | 1000–5000 ms; default 2500 ms |
| Wallpaper | RGB565, 153600 bytes (150 KiB) |
| Boot-load acceptance | 1500 ms from `BeginBootLoad()` |
| Free SD space before download | Remaining package bytes + 8192 bytes |

Desktop imports allow 16 MiB per source and images up to 4096 × 4096. Text is a single line of
at most 32 Unicode code points, 16–80 px, with a font covering every character. PNG is required
for a logo; wallpaper accepts PNG/JPEG. No font parser or script engine runs on the device.

`BeginBootLoad()` starts after Board Manager and overlaps display setup. Built-in mode does not
wait for SD. `WaitBootAssets()` uses only the remaining budget. Missing, invalid or slow resources
fall back for the current boot; late results cannot replace the current UI. This budget limits
waiting and acceptance, not cancellation of blocking SD-driver calls. Resources allocate in PSRAM.
Non-wallpaper buffers release after playback; `PhoneUi` keeps the shared Home wallpaper.

The private, bounded JSON allocator does not change process-wide cJSON hooks. RGB resources read
directly into final PSRAM buffers; A4 expands with a ≤160-byte internal row buffer. Decoding does
not retain an entire package or package JSON tree. Boot verification also releases its cloud
configuration, manifest and signature result before allocating pixels. The download worker waits
until boot loading and animation playback finish, so HTTP/download buffers do not overlap the
complete boot animation data. State persistence and reporting can still allocate private JSON
while pixels are retained; the two controlled budgets sum to 480 KiB. Full-screen RGB565A8 logo
plus RGB565 wallpaper uses 384000 bytes, within the decoded limit.

These are resource and private-JSON allocation limits, not a hard limit on total physical PSRAM
use. Descriptors, STL strings/containers, serialized JSON copies, global MQTT JSON, SDK transport,
allocator bookkeeping and other firmware services are outside those counters. This build permits
ordinary malloc allocations over 512 bytes to use PSRAM. Actual heap overhead and peak use need
device free-PSRAM measurements.

SD directory `/rodakos/appearance/` contains `a.rap/b.rap`, matching `.manifest` files and temporary
`.rap.part` files. `a.part.meta/b.part.meta` store origin and a signed manifest. New tickets are
checked against that binding before resuming. A durable prefix can survive retries/reboots;
invalid signatures, hashes, ranges or superseded deployments discard it. The inactive slot is
verified, synced and renamed before pending NVS state is saved.
Two packages take up to about 1 MiB plus manifests; a temporary download may require one more
package. Namespace `appearance`, key `state`, stores publisher, active/pending/previous revision,
trial/rejected revision and local theme.

A pending version becomes a persisted boot trial before use. The main loop confirms after system
startup and animation completion. An unconfirmed trial recovers at the next boot. Invalid resources
can use the prior valid package or built-in resources. File-pair/NVS transitions still require
hardware power-interruption verification.

## States and local theme

States are `idle`, `awaiting_trust`, `waiting_device`, `deferred_busy`, `downloading`, `pending_reboot`,
`applied`, `failed`, `fallback`, `superseded`. Only `applied` confirms adoption; inspect `error` and
`fallbackReason` on failure. Downloads defer during boot trials, OTA, voice/recording, screen/camera
streams, or when the largest free internal block is below 8192 bytes. The MQTT-connected main loop
retries scheduling periodically.

Selecting a theme in Settings immediately applies that preset/default primary and saves a local
override without removing animation/wallpaper. Ordinary later boots retain it. A newer online
version clears that override on adoption. Global RodakOS and `PhoneUi` share colors; primary button
foreground is selected for contrast.

Restore Built-in is a signed builtin deployment without a package download. Current built-in
appearance is EDIX, no custom wallpaper, dark preset and primary `#79cbff`; it clears a previous
local theme override. A local theme can be selected again afterward.

## Implementation and verification

- [AppearanceService](../main/phone_os/appearance_service.h): worker, pinning, SD and NVS state.
- `components/rodak_appearance/`: metadata, signature and revision policy.
- [BootAnimation](../main/phone_ui/boot_animation.h): LVGL surface and elapsed-time rendering.
- [PhoneUi](../main/phone_ui/phone_ui.h), Home and Settings: wallpaper, unified theme, physical trust.
- `tests/home_ui/`: production LVGL rendering/lifetime tests; the built-in logo font is a host fake.

Rodak's `docs/appearance-customization.md` contains the desktop usage guide. Host suites do not
prove physical confirmation, ST7789 output, slow-card timing or wireless delivery.
