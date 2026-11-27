# RodakOS Architecture

RodakOS is split into three practical layers: board/HAL integration, Phone OS services, and LVGL-facing UI/apps.

```mermaid
flowchart TD
  "app_main" --> "NVS"
  "app_main" --> "USB MSC boot gate"
  "app_main" --> "esp_board_manager"
  "esp_board_manager" --> "display_lcd"
  "esp_board_manager" --> "lcd_touch"
  "esp_board_manager" --> "audio_dac/audio_adc"
  "esp_board_manager" --> "fs_sdcard"
  "esp_board_manager" --> "camera"
  "app_main" --> "LVGL port"
  "LVGL port" --> "PhoneUi"
  "app_main" --> "PhoneServices"
  "PhoneServices" --> "Backlight/WiFi/FileService"
  "PhoneServices" --> "Audio/Music/Voice"
  "PhoneServices" --> "Camera/WebFiles/Time/Buttons/Lights/Motion/Wake-on-LAN"
  "PhoneServices" --> "WebRTC Camera/Display peers"
  "PhoneServices" --> "Unified MQTT/SD OTA"
  "Unified MQTT/SD OTA" --> "Factory Recovery"
  "PhoneUi" --> "PhoneSystem"
  "PhoneSystem" --> "PhoneAppRegistry"
  "PhoneSystem" --> "PhoneAppHost"
  "PhoneSystem" --> "PhoneNavigation"
  "PhoneSystem" --> "PhoneShell"
  "PhoneShell" --> "Lock Screen overlay"
  "PhoneShell" --> "Control Center overlay"
  "PhoneAppRegistry" --> "Built-in Apps"
```

## Board And HAL

The board layer combines the RodakOS-owned `rodakos_hal_boards` component with Espressif Board
Manager and its generated code. Board definitions and setup hooks stay in the board component.
Runtime adaptation stays in `main/rodakos_adapters/`. There is no parallel `main/board/` implementation.

- Board YAML, board defaults, setup hooks and the local PCA9557 driver live under
  `components/rodakos_hal_boards/boards/rymcu/rymcu_bigsmart/`.
- Only the RYMCU BigSmart board is retained and supported today. Future vendors and boards use
  `boards/<vendor>/<board>/`. Each port needs explicit build selection and hardware acceptance.
- `generate_board_config.ps1` owns Board Manager generation, including cold-bootstrap passes,
  generated-path normalization, and reconfiguration.
- `rodakos_hal_boards` compiles the BigSmart `setup_device.c` once. The generated component
  depends on it and compiles only generated tables, without compiling the setup hooks again.
- Board Manager owns the generated device/peripheral tables and device lifecycle. Device handles
  are acquired through `esp_board_manager_get_device_handle()` and wrapped by the existing
  RodakOS adapters before injection into services.
- The board-component migration preserves `rymcu_bigsmart`, device names, pins and configuration.
  Recovery has its own build and depends on `rodak_ota_state`, not this board component.

## Phone OS

`phone_os` owns app metadata, lifecycle, navigation, and system services.

Important pieces:

- `PhoneAppDescriptor`: static metadata, role, aliases, category, descriptive requirements, and app factory.
- `PhoneAppRegistry`: validates app identities and the unique Home role, then freezes before launch.
- `PhoneAppHost`: is the sole lifecycle owner and calls `OnCreate`, `OnResume`, `OnPause`, and
  `OnDestroy`. It also dispatches the optional repeated-Home request hook without exposing app
  instances to navigation policy.
- `PhoneNavigation`: app launch, return-home, theme, lock, Control Center, and Shell-preference routing.
- `PhoneShell`: owns Lock Screen and Control Center on `lv_layer_top()` without replacing or destroying the current app.
  It also owns the bottom voice bar (turn progress and reply captions), which yields to the Shell
  modals, a soft keyboard, an app that asks for the bottom area, a toast and any display share.
- `PhoneServices`: dependency container for hardware and system services.
- `DeviceCloudConfigService`: binding, credential refresh, and optional USB-installed TLS
  authority. A single active/pending record guards trusted endpoint promotion. Bounded
  DNS-SD results only suggest routes for TLS verification. HTTPS, MQTT, voice, OTA and
  Appearance share the installed server trust. See
  [trusted server discovery](trusted-server-discovery.md) for the remaining address and recovery gates.
- `AppearanceService`: publisher pinning, signed SD resources, bounded boot loading, revision trials
  and local theme overrides. It is independent of firmware OTA and exposed through `PhoneServices`.

RodakOS still uses statically linked apps. "Pluggable" means apps are modular at firmware architecture level: a new app registers a descriptor and factory, and Home discovers it from the registry.

Native app capabilities are descriptive metadata, not a security boundary. A future untrusted MiniApp runtime must use a separate capability broker, per-app storage, resource limits, and signed packages instead of receiving `PhoneServices` directly.

Home layout is app-owned state under `main/apps/home/`: a strict versioned JSON codec, pure exact-ID
model, Registry reconciliation, eight-page projection, guarded short-lived NVS store, discrete root
reordering, non-nested folder commands, single-save draft sessions, and a runtime-only page anchor.
Only a confirmed store save advances the live revision. Uncertain writes freeze editing for that Home
session. Descriptor metadata remains static. User order and folders never mutate `PhoneAppDescriptor`
or Registry order.

Home creates one stable tile shell per projected page, but retains each page's grid, buttons, and
labels only for the active page and its existing immediate neighbors. The pure
`HomePageRenderWindow` and `HomePageRenderPlan` policy selects that window and fixes population order
as active, previous, then next. After `LV_EVENT_SCROLL_END`, `HomeApp` queues an LVGL async refresh
that releases far-page child trees before populating the new window. The tileview exposes a scroll
direction only when that adjacent page is already populated. UI teardown cancels the queued refresh
before navigation destruction or a theme rebuild can invalidate its `HomeApp` or LVGL objects.
Per-window and final-ready logs report resident page count, internal-SRAM free space, and the largest
free internal block.

`tests/app_model/` compiles the production Home codec/model/store and Registry, Host, and Navigation
sources against small host fakes. It verifies layout boundaries and persistence failures, identity
validation, transactional lifecycle order, theme recreation, re-entrancy guards, and navigation
forwarding without entering the firmware image. Its pure-model page-window tests pass for one through
eight pages, including boundary clamping and active/previous/next order. This target isolates policy.

The same target now compiles production `AudioOutputService`, `AudioService`, and
`AudioCodecOutput` with fake lower-level board/codec APIs. It verifies volume API error propagation,
configuration-cache retention, atomic relative updates and initial-open cleanup/retry, plus the
fixed four-slot playback frame and its sample expansion. `tests/codec_volume/` runs the patched
esp_codec_dev I2S negotiation and checks that capture and playback opens keep the shared TX DMA ring size. `tests/voice_volume_service/` compiles the real
`VoiceAssistantService`, inbound queue, I/O loop, playback task and reconnect coordinator with host
dependencies. It covers early initialize, Stop cancellation, stale generations, session ledger
reset, and playback that blocks on the speaker while the uplink continues.
`tests/codec_volume/` separately
compiles the real dependency source with the project's volume correction and real software-volume
implementation, injecting failures at the lower-level driver callbacks. See
[dependency maintenance](dependency-maintenance.md) for source identity and build integration,
and [local audio validation](ota-release-readiness.md#2026-10-06-local-audio-volume-validation)
for the earlier service fixture's results and hardware limits. Current wire behavior and
software-receipt boundaries live in [MQTT volume effects](mqtt-volume-effects.md).

`tests/home_ui/` complements the model suite by compiling the production `HomeApp`, Home
model/store, Registry, `PhoneUi`, layout, theme, components, `BootAnimation`, appearance metadata
codec and `SoftKeyboard` against LVGL 9.3's
in-memory 320x240 display. LVGL's test pointer drives the real widget/event tree while platform
services, Settings, navigation, and fonts use host fakes. The suite covers tap slop,
one-page and multi-page drag suppression, boundary and bidirectional page swipes, long-press Arrange,
Cancel/Done persistence, repeated Home, theme rebuilding, keyboard geometry, 96/97-app `All Apps`,
and asynchronous active-plus-neighbors residency, boot-image formats/timing/lifetime, touch recovery,
Home wallpaper, unified theme colors, and the category-based icon gradients, radius and press
feedback ([icon visual refresh](icon-visual-refresh.md)). The current WSL run reports 55 tests and
0 failures in Debug and ASan/UBSan with leak detection. The logo font fake does not verify built-in EDIX typography.

The latest 2026-10-01/02 COM3 evidence also covers the integrated media peers: six display sessions
(320x240, four rounds, 283 JPEG samples), camera/display mutual exclusion, and clean stop/navigation
cleanup. Remote text and explicit screen-control authorization were exercised against the real LVGL
widget tree. This proves the WebRTC peer path on the tested device. It does not provide an HTTP
camera snapshot/MJPEG endpoint or establish sustained high-frame-rate video. The 13-app/two-page
Home population still proves boot only. Page gestures, Arrange, page restoration, GT911/ST7789
interaction, three-page far-page release, and graceful LVGL out-of-memory recovery remain physical
gates. The host suite validates Home behavior and object lifetime but cannot replace those gates.

## Phone UI

`phone_ui` wraps LVGL conventions for this 320x240 screen:

- `PhoneUi` and `PhoneUiLock` centralize LVGL access.
- `rodakos_theme` defines dark/light/blue/green theme tokens.
- `rodakos_layout` provides fixed-screen helpers for header/body/footer, grids, and flex rows.
- `phone_fonts` initializes the RodakOS-owned CJK and icon font subset. UI icons are `ICON_*`
  macros from `rodakos_icons.h`, generated from `components/rodakos_fonts/icons.json` by
  `tools/generate_icon_fonts.py`.
- `BootAnimation` renders the built-in EDIX fallback or compiled appearance units with elapsed LVGL
  ticks. `PhoneUi` retains a shared RGB565 Home wallpaper and follows the global theme colors.
- `image_library` scans and loads JPG/PNG/BMP images from FileService-backed storage, preferring SPIRAM.

`main.cc` registers GT911 touch through a cached polling bridge: a low-priority task reads the touch controller and LVGL reads cached coordinates. This avoids doing I2C reads directly inside the LVGL task. `PhoneUi` retains the primary LVGL input device so system gestures can observe LVGL events without accessing GT911 or I2C.

## Built-In Apps

`main/apps/built_in_apps.cc` registers the built-in apps:

- Home: phone desktop, status area, paged app grid, folders and page indicators.
- Settings: WiFi, brightness, USB disk mode, web/cloud file tools, time sync, and an entry row
  that opens Themes.
- Themes (`themes`): appearance preview, local color presets, appearance package status and the
  physical-touch publisher trust page. See [theme store design](theme-store-design.md#phase-1-themes-app).
- Photos: scans `/photos`, `/DCIM`, and fallback roots for images, with separate storage/read
  failure and empty-library states, explicit retry and owned image/thumbnail cleanup.
- Camera: opens camera service on demand, previews and captures to storage. Capture completion is
  published only after exclusive `WriteNewFile` creation, complete output, flush and close. The
  short FileService I/O lock protects the write. A UI teardown can revoke the shared result guard
  while an already admitted capture finishes independently. See [media save boundaries](media-save.md).
- Clock: local display and network time sync entry points.
- Calendar: whole-month grid with lunar dates, solar terms and festivals in each cell (offline
  `lunar_calendar`, 1900–2100), a day page, swipe or ‹ › to change month, and an in-app SNTP
  sync when the clock is not set.
- Files (`file_manager`): browses FileService-backed storage, clears stale/partial lists after read failures
  and retries the attempted folder or image. Images open full screen; the info page offers a
  confirmed delete that calls `DeleteFile` directly (never under `/rodakos`). Free space is read
  by a short task. See [media browsing](media-browsing.md) for error propagation and the
  synchronous I/O boundary.
- Gyro: motion capability surface for gyroscope/accelerometer samples.
- System (`system_info`): firmware, WiFi, memory, and storage status.
- Music: scans `/music` and plays supported audio through the music/audio services. Separate library/error snapshots, worker-backed retry, revision-bound selection and asynchronous playback results are described in [music playback](music-playback.md).
- Recorder: captures microphone audio through the recording service and stores it through FileService.
  Start admission is separate from Saved. Final WAV header, flush and close failures remain errors,
  and `library_error` does not overwrite a completed save. Path leases cover creation, data, final
  header and cleanup without holding the entire storage I/O lock. See [media save boundaries](media-save.md).
- Assistant: status card (wake word, session phase, turn progress, cloud diagnosis with a fix
  button) and rows for wake, the voice status light and 连接服务. Interaction runs as a system
  service rather than an app-owned Talk/Stop session. See [voice assistant](voice-assistant.md).
- Smart: light control. One light opens its control page; two or more open an overview. Every
  write is a one-field `ApplyLightPatch` by light id; brightness commits on release, and choosing a
  colour while the light is off saves it without switching the light on. A 500 ms poll shows MQTT
  changes and the voice status light overlay.
- Wake: persists network devices and sends validated Wake-on-LAN magic packets over UDP broadcast.
- Messages (`messages`): notices and todo reminders from `InboxService`, a RAM-only copy of the
  server records synced over pinned HTTPS after an `inbox/changed` signal. Todos can be completed
  or snoozed 10/30/60 minutes with an idempotent `opId`; text is hidden during any display share.
  Home shows only a red status dot after the clock while something is unread; the dot opens the app.
  See [AIoT contract §7.6](rodak-aiot-contract-v1.md).

## Service Notes

- Appearance resources use `components/rodak_appearance` for metadata/signature/revision policy and
  `/rodakos/appearance/` on SD for two verified slots. A background loader overlaps display setup.
  Only results accepted within the total 1500 ms budget may enter the boot UI. The device requires
  physical touch to pin a publisher key/origin. Runtime downloads defer during media/voice/OTA and
  activate through a next-boot trial. See [Appearance resources](appearance-customization.md) for
  the RAP1 layout, HTTP limitations, limits and still-required hardware evidence.

- SD storage mounts on demand through FileService. USB MSC mode is an early-boot path and does not start normal UI/services.
- FileService write leases normalize paths, reject NUL/parent traversal and conflicting mutations
  on the same, parent or child path. Unrelated reads and writes remain available. FileService and
  CameraService are static services in `main.cc`. Callers must keep them alive until workers and
  leases exit. FileService unmount does not drain active leases.
- `main.cc` initializes audio, music, voice assistant, camera, web file server, and cloud services as services. They open heavy hardware paths only when needed.
- `AudioOutputService` owns the shared output configuration. With an open codec it commits a new
  volume only after `AudioCodecOutput::SetVolume` succeeds. `AudioService` reads that shared value
  for playback/UI, and atomic set/up/down operations serialize calculation, hardware application,
  revision and receipt under the output mutex. With the codec closed, a setter accepts configuration
  for the next open. Failure of the initial codec volume API call closes that open attempt and
  clears its format state so a later request can retry. This preserves on-demand hardware ownership.
  `AudioCodecOutput` applies a three-point volume curve (1→-40 dB, 50→-12 dB, 100→0 dB) so
  mid-range settings are audible on the BigSmart speaker. Volume 100 still maps to 0 dB, keeping
  maximum output and clipping headroom unchanged. It converts each user volume to the matching
  value on esp_codec_dev's default linear -50..0 dB curve before writing it, rather than calling
  `esp_codec_dev_set_vol_curve()`. That call keeps a small internal-RAM allocation for the device
  lifetime. When the first playback made that call, it split a free block and left too little
  contiguous RAM for the voice WebSocket task. The stored and reported volume remains the user's 0–100 value.
- ES8311 playback and ES7210 capture share I2S0. `AudioCodecOutput` opens every playback stream in
  capture's four-slot 16-bit frame with slot mask `0x3`, expanding each mono or stereo 16-bit sample
  into both halves of the two 32-bit TX slots. esp_codec_dev reprograms the TX channel for
  whichever side opens. A mono 16-bit playback frame shrank the TX DMA ring from 6 × 1,920 B to
  6 × 480 B, and the next capture open regrew it. Each size change frees and reallocates the ring
  in internal RAM. After a spoken reply, the regrown buffers split the contiguous block the voice
  WebSocket task needs, so the next wake failed with "Device memory low". Playback accepts only
  1–2 channel 16-bit PCM. It stages conversions in a reusable PSRAM buffer. Both sides must also
  share one sample rate, so media playback releases wake capture before it opens the bus; see
  [music playback](music-playback.md).
- MQTT desired-volume application checks the setter result and reports the retained configuration
  on failure. A source-verified build overlay for esp_codec_dev 1.5.7 propagates the selected
  codec/software-volume driver's exact error and only commits the dependency cache on success.
  It preserves software priority and no-codec software output. Other dependency API error paths
  remain outside this focused correction. Success does not establish hardware or acoustic success.
  Ordinary shadow reports contain no volume effect ID or applied desired version. See the
  [AIoT shadow contract](rodak-aiot-contract-v1.md#9-shadow-and-telemetry).
  Correlated MQTT volume effects use a separate `effects/receipt` publication and a bounded
  authority-scoped ledger. Real connection epochs travel through fragment assembly and the
  worker queue. The scope check and `ApplyVolume` share the MQTT service lock with cancellation.
  Receipt enqueue runs in the SDK custom-event callback to preserve SDK/service lock order.
  See [MQTT volume effects](mqtt-volume-effects.md) for the contract and host service tests.
- `LightService` shares one atomic patch path between Smart UI setters and MQTT. A driver error
  retains accepted configuration/revision, updates availability/error, and leaves hardware
  application unverified. Correlated MQTT light patches carry actual discovered IDs, reuse the
  authenticated epoch/SDK receipt boundary, and keep 64 recent outcomes plus a monotonic
  authority version watermark. Only requested fields execute. `tests/light_service` and the
  extended real MQTT service target compile production light/board adapter code with SDK fakes.
  See [MQTT light effects](mqtt-light-effects.md). LCD backlight remains a separate local service.
- Voice wake monitoring uses local MultiNet without a cloud connection. A wake match takes exclusive
  audio focus and opens one Rodak WebSocket session. Every non-terminal reply drains TTS and starts
  the next input turn on that same session. When the user says “再见”, the session receives
  `session.end`. Thirty seconds of follow-up silence, errors, or a connection/listening watchdog
  disconnect the session and re-arm local monitoring. Active TTS playback is not
  terminated by that watchdog. See [Voice assistant integration](voice-assistant.md) and the
  [realtime voice v1 contract](rodak-realtime-voice-contract-v1.md).
- The `assistant_io` task decodes TTS frames into `VoicePlaybackQueue` (at most 50 frames, about
  3 s). A separate `assistant_play` task (core 1, priority 5, 6 KiB PSRAM stack) writes them to the
  codec. The I/O task thus never blocks on the speaker, and uplink frames stay fresh for barge-in.
  Wake monitoring runs a synchronous ESP-SR AEC and AGC in front of MultiNet and plays a short
  chime after a match. See [wake word evaluation](voice-wake-evaluation.md).
- The MQTT command `voice.announce` asks for a playback-only voice session. The MQTT thread only
  queues the request on the voice frontend's command task and sends the ACK. That task opens the
  session. The firmware registers no MCP tools. See
  [realtime voice §8](rodak-realtime-voice-contract-v1.md#8-server-initiated-announce).
- `WiFiConfig` stores WiFi credentials in NVS. Auto-connect starts after PhoneSystem is up, so it does not block UI boot.
- MotionService exposes a stable app-facing motion API. The BigSmart QMI8658 adapter samples over the shared Board Manager I2C peripheral in a background task. Thus apps never perform I2C work in the LVGL thread.
- UnifiedMqttService consumes Rodak bootstrap credentials, reports device state, and routes OTA
  notifications. OtaUpdateService stages and verifies the main image on SD. The separate factory
  Recovery project is the only runtime allowed to rewrite `ota_0`.
  With USB server trust, MQTT uses MQTTS with the fixed expected server name, and OTA Bearer
  requests remain on the pinned HTTPS origin. The separate Appearance publisher signature/origin
  approval is never granted or rewritten by network discovery.
- Ordinary command ACKs and camera/display sidebands retain the original MQTT generation, epoch
  and ACK topic. Their bounded queue drains in the SDK user-event callback with direct QoS 0
  publish, without storing an outbox item. Epoch changes discard queued results and late callbacks.
  A separate operation mutex serializes stream Start/Stop/signaling, while connection changes
  revoke instance leases and defer peer cleanup outside MQTT callbacks. A latest-64 authority
  cache replays immutable command results. `RemoteInputController` owns bounded input queues and
  stream/control grants checked at final LVGL execution. `DisplayControlAckTracker` binds replies
  to an opaque peer instance. Admitted actions may finish. Eviction/reboot are outside the replay
  guarantee. See the [command result boundary](mqtt-command-semantics.md#command-results-and-replay-boundary).
- Shell preferences use the short-lived `shell` NVS namespace. Settings exposes explicit commit results so a failed save cannot be reported as successful.
- ButtonBindingService encodes Lock and Control Center as stable actions. Persisted `btnbind` values override compiled defaults.
- WakeOnLanService creates a UDP socket only for a user-requested wake, requires active WiFi, and
  leaves device-list persistence to the Wake app's versioned `wol/devices` NVS document.

### Video and voice task retirement

The five Camera Preview/JPEG, Display JPEG and Camera/Display peer task paths use
`task-retirement.{h,cc}`. A bounded, lazily allocated PSRAM registry owns numeric
owner/generation records. Each Start reserves a record before creating its WithCaps
task. The common entry waits for handle publication before entering the service.
Business cleanup and the complete return of callbacks/local destructors are separate
states. Stop captures one exact generation and joins outside service locks, allowing
a callback destructor to start a replacement without making the old Stop wait for it.
Creation failure preserves the previous ticket. Shutdown closes admission and drains
all generations before releasing service state.

The external reaper calls the pinned IDF WithCaps delete path, which suspends the
worker, waits for all cores to leave it, then frees its TCB/stack. A periodic call in
the permanent main loop also reclaims autonomous peer failures. It does not create
an exit-time cleanup task or allocate a retirement record at exit. This is not a
deadline, zero-cost or whole-device OOM guarantee. The 031 source also uses this registry
for Assistant I/O, frontend Capture and wake supervisor. Ordinary voice Deinit remains
restartable. Only service destruction closes and drains its owner. Capture returns through
its vector destructors before retirement, Assistant closes its transport before joining,
and Wake coalesces Stop/Disable/Deinit completion by operation epoch. AFE fetch retains its
existing external delete and wake notification retains its ordinary internal stack.
See [030 video contract](task-retirement.md) and [031 voice contract](voice-task-retirement.md).
The 032 diagnostic is compiled only with `RODAKOS_RELEASE_TESTS=ON`. It admits a canonical
request ID into one accepting/pending/executing slot. The permanent internal-stack main task
calls Wake Deinit, observes task-name absence without reinitializing Wake, then attempts one
Wake Start. Listening with all three workers and idle cycles have different result categories.
Callbacks run outside the slot mutex. Busy checks and serial-write blocking do not arbitrate
UI/MQTT operations globally. The ordinary OFF ELF excludes the TU, hooks, command and fault
markers. Test-flavor device observations and ordinary restoration have separate identities in
[032 evidence](ota-release-readiness.md#2026-10-08-voice-lifecycle-diagnostic-and-restoration-032).
Independent review records one idle and three Listening cycles on the test image, followed by
ordinary OFF guarded boot and one voice-session stop/rearm window. The latter does not execute
the absent lifecycle diagnostic. Physical full-resource recovery and production gates remain open.

033 keeps that retirement and diagnostic contract. The audited mn5q8_cn model alone owns
its command registry. AFE classifies post-fetch cancellation under the lifecycle mutex and
continues draining outstanding feed before exit. Current errors still invalidate continuity.
Credential refresh runs sequential noinline exchange/parse/persist stages (and pairing only
under the existing gate), with shorter NVS-key temporary lifetimes on the same internal
wake_notify task. Final ELF frame reductions are separate from runtime headroom. See
[033 evidence](ota-release-readiness.md#2026-10-08-voice-health-and-credential-refresh-033).

034 gates current AFE fetch on one complete output frame using successful SDK output
bytes. Credits remain a conservative lower bound after a transient read failure. A
ledger of four uncertain frames, a zero feed with uncertain inventory, or a malformed
nonzero feed/credit overflow triggers quiescent buffer/VAD resynchronization while
outstanding feed leases can still drain. Input epochs fence old reads and local tails.
Raw and AFE diagnostic gaps stay distinct. DSP residual state is explicitly retained.
Only terminal recorder failure ends Running, and Assistant cleanup rechecks both
interaction and transport generations. The locked phase-only getter is observational.
Existing admission and wake-generation checks remain authoritative. See
[034 evidence](ota-release-readiness.md#2026-10-08-afe-output-readiness-and-voice-recovery-034).


### Deferred serial and Camera navigation

`PhoneNavigation` owns one `DeferredNavigation` ring: four pending canonical IDs in
PSRAM and a permanent 30 ms LVGL timer created during `PhoneSystem::Start()`. Serial
launch registration follows successful startup. Serial `RequestLaunch` resolves
aliases without a temporary normalized string and enqueues without taking the LVGL
lock or allocating a per-request async object. Camera Back/Home uses `RequestHome`
and shows a retry toast on rejection. Other apps and async paths are unchanged.

The timer executes at most one request per callback. Admission is distinct from app
completion, and the timer period is not a completion deadline. Shutdown revokes
admission, cancels pending callbacks once and synchronizes with LVGL before app state
is destroyed. The app host restores its transition flag with RAII and reports
factory/preallocation or successfully cleaned partial `OnCreate` failures. Unknown
lifecycle exceptions and teardown failures abort rather than continue with possibly
dangling timer userdata. Completion-notification exceptions are isolated without
replay. Startup timer allocation still has LVGL's configured malloc-assert boundary.
Arbitrary app rollback or UI OOM recovery is not established.

`tests/navigation_ui` compiles the actual System, Navigation, Registry, Host,
CameraApp and PhoneUi against real LVGL. Its 12 cases include three child-process
marker/SIGABRT checks for unsafe lifecycle failures. Current run identities and results
are recorded in the test README and 030 contract.
Camera hardware, audio focus, Shell and Home content remain fixtures. These cases
do not establish the cause of the earlier 028 Home enqueue failure or a 030 device
result. See [serial launch](serial-provisioning.md#030-bounded-navigation-admission)
and [navigation host coverage](../tests/navigation_ui/README.md).

## Related Docs

- [Firmware build and flash](firmware-download.md)
- [BigSmart WebRTC peer integration](esp-peer-integration.md)
- [Appearance verification](appearance-verification.md)
- [Theme store design](theme-store-design.md)
- [Project roadmap](roadmap.md)
- [OpenOS comparison and design decisions](openos-comparison.md)
- [Home layout and folder design](home-layout-design.md)
- [Rodak AIoT v1 contract](rodak-aiot-contract-v1.md), [MQTT command semantics](mqtt-command-semantics.md),
  [AIoT implementation notes](aiot-implementation-notes.md), [machine-readable contract](../contracts/rodak-aiot/v1/README.md)
- [Rodak realtime voice v1 contract](rodak-realtime-voice-contract-v1.md), [machine-readable contract](../contracts/rodak-realtime-voice/v1/README.md)
- [Voice assistant integration](voice-assistant.md)
- [Voice task retirement and software evidence](voice-task-retirement.md)
- [Serial provisioning](serial-provisioning.md)
- [LAN first setup without USB](lan-onboarding-design.md)
- [Wake word evaluation](voice-wake-evaluation.md)
- [Video task retirement and deferred navigation](task-retirement.md)
- [Rodak MQTT and SD Recovery OTA](mqtt-ota-sd-recovery.md)
- [Troubleshooting](../TROUBLESHOOTING.md)
