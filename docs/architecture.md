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

The board layer is esp-brookesia plus Board Manager generated code. RodakOS does not keep a hand-written `main/board/` implementation anymore.

- Board YAML lives under `components/brookesia_hal_boards/boards/rymcu/rymcu_bigsmart/`.
- `generate_board_config.ps1` owns Board Manager generation, including cold-bootstrap passes,
  generated-path normalization, and reconfiguration.
- Device handles are acquired through `esp_board_manager_get_device_handle()` and adapted by RodakOS services.

## Phone OS

`phone_os` owns app metadata, lifecycle, navigation, and system services.

Important pieces:

- `PhoneAppDescriptor`: static metadata, role, aliases, category, descriptive requirements, and app factory.
- `PhoneAppRegistry`: validates app identities and the unique Home role, then freezes before launch.
- `PhoneAppHost`: is the sole lifecycle owner and calls `OnCreate`, `OnResume`, `OnPause`, and
  `OnDestroy`; it also dispatches the optional repeated-Home request hook without exposing app
  instances to navigation policy.
- `PhoneNavigation`: app launch, return-home, theme, lock, Control Center, and Shell-preference routing.
- `PhoneShell`: owns Lock Screen and Control Center on `lv_layer_top()` without replacing or destroying the current app.
- `PhoneServices`: dependency container for hardware and system services.
- `DeviceCloudConfigService`: binding, credential refresh, and optional USB-installed TLS
  authority. A single active/pending record guards trusted endpoint promotion; bounded
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
Only a confirmed store save advances the live revision; uncertain writes freeze editing for that Home
session. Descriptor metadata remains static; user order and folders never mutate `PhoneAppDescriptor`
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
eight pages, including boundary clamping and active/previous/next order; this target isolates policy.

The same target now compiles production `AudioOutputService`, `AudioService`, and
`AudioCodecOutput` with fake lower-level board/codec APIs. It verifies volume API error propagation,
configuration-cache retention, atomic relative updates and initial-open cleanup/retry. It also
compiles the production volume MCP dispatcher. `tests/voice_volume_service/` compiles the real
`VoiceAssistantService`, inbound queue, I/O loop and reconnect coordinator with host dependencies,
covering early initialize, Stop cancellation, stale generations and session ledger reset.
`tests/codec_volume/` separately
compiles the real dependency source with the project's volume correction and real software-volume
implementation, injecting failures at the lower-level driver callbacks. See
[dependency maintenance](dependency-maintenance.md) for source identity and build integration,
and [local audio validation](ota-release-readiness.md#2026-10-06-local-audio-volume-validation)
for the earlier service fixture's results and hardware limits. Current wire behavior and
software-receipt boundaries live in [voice volume MCP](voice-volume-mcp.md).

`tests/home_ui/` complements the model suite by compiling the production `HomeApp`, Home
model/store, Registry, `PhoneUi`, layout, theme, components, `BootAnimation`, appearance metadata
codec and `SoftKeyboard` against LVGL 9.3's
in-memory 320x240 display. LVGL's test pointer drives the real widget/event tree while platform
services, Settings, navigation, and fonts use host fakes. The suite covers tap slop,
one-page and multi-page drag suppression, boundary and bidirectional page swipes, long-press Arrange,
Cancel/Done persistence, repeated Home, theme rebuilding, keyboard geometry, 96/97-app `All Apps`,
and asynchronous active-plus-neighbors residency, boot-image formats/timing/lifetime, touch recovery,
Home wallpaper and unified theme colors. The current WSL run reports 43 tests and 0 failures in
Debug and ASan/UBSan with leak detection. The logo font fake does not verify built-in EDIX typography.

The latest 2026-10-01/02 COM3 evidence also covers the integrated media peers: six display sessions
(320x240, four rounds, 283 JPEG samples), camera/display mutual exclusion, and clean stop/navigation
cleanup. Remote text and explicit screen-control authorization were exercised against the real LVGL
widget tree. This proves the WebRTC peer path on the tested device; it does not provide an HTTP
camera snapshot/MJPEG endpoint or establish sustained high-frame-rate video. The 13-app/two-page
Home population still proves boot only; page gestures, Arrange, page restoration, GT911/ST7789
interaction, three-page far-page release, and graceful LVGL out-of-memory recovery remain physical
gates. The host suite validates Home behavior and object lifetime but cannot replace those gates.

## Phone UI

`phone_ui` wraps LVGL conventions for this 320x240 screen:

- `PhoneUi` and `PhoneUiLock` centralize LVGL access.
- `rodakos_theme` defines dark/light/blue/green theme tokens.
- `rodakos_layout` provides fixed-screen helpers for header/body/footer, grids, and flex rows.
- `phone_fonts` initializes the RodakOS-owned CJK and Font Awesome UI font subset.
- `BootAnimation` renders the built-in EDIX fallback or compiled appearance units with elapsed LVGL
  ticks. `PhoneUi` retains a shared RGB565 Home wallpaper and follows the global theme colors.
- `image_library` scans and loads JPG/PNG/BMP images from FileService-backed storage, preferring SPIRAM.

GT911 touch is registered through a cached polling bridge in `main.cc`: a low-priority task reads the touch controller and LVGL reads cached coordinates. This avoids doing I2C reads directly inside the LVGL task. `PhoneUi` retains the primary LVGL input device so system gestures can observe LVGL events without accessing GT911 or I2C.

## Built-In Apps

Built-in apps are registered in `main/apps/built_in_apps.cc`:

- Home: phone desktop, status area, app grid, dock/page affordances.
- Settings: WiFi, display/theme/brightness, USB disk mode, web/cloud file tools, time sync.
- Photos: scans `/photos`, `/DCIM`, and fallback roots for images, with separate storage/read
  failure and empty-library states, explicit retry and owned image/thumbnail cleanup.
- Camera: opens camera service on demand, previews and captures to storage. Capture completion is
  published only after exclusive `WriteNewFile` creation, complete output, flush and close. The
  short FileService I/O lock protects the write; a UI teardown can revoke the shared result guard
  while an already admitted capture finishes independently. See [media save boundaries](media-save.md).
- Clock: local display and network time sync entry points.
- Calendar: local month navigation and current-day selection.
- File Manager: browses FileService-backed storage, clears stale/partial lists after read failures
  and retries the attempted folder or image. See [media browsing](media-browsing.md) for error
  propagation and the synchronous I/O boundary.
- Gyro: motion capability surface for gyroscope/accelerometer samples.
- System Info: firmware, WiFi, memory, and storage status.
- Music: scans `/music` and plays supported audio through the music/audio services. Separate library/error snapshots, worker-backed retry, revision-bound selection and asynchronous playback results are described in [music playback](music-playback.md).
- Recorder: captures microphone audio through the recording service and stores it through FileService.
  Start admission is separate from Saved; final WAV header, flush and close failures remain errors,
  and `library_error` does not overwrite a completed save. Path leases cover creation, data, final
  header and cleanup without holding the entire storage I/O lock. See [media save boundaries](media-save.md).
- Assistant: configuration and status for the local "你好达克" monitor; interaction runs as a
  system service rather than an app-owned Talk/Stop session.
- Smart: light/smart-device control surface.
- Wake: persists network devices and sends validated Wake-on-LAN magic packets over UDP broadcast.

## Service Notes

- Appearance resources use `components/rodak_appearance` for metadata/signature/revision policy and
  `/rodakos/appearance/` on SD for two verified slots. A background loader overlaps display setup;
  only results accepted within the total 1500 ms budget may enter the boot UI. The device requires
  physical touch to pin a publisher key/origin. Runtime downloads defer during media/voice/OTA and
  activate through a next-boot trial. See [Appearance resources](appearance-customization.md) for
  the RAP1 layout, HTTP limitations, limits and still-required hardware evidence.

- SD storage mounts on demand through FileService; USB MSC mode is an early-boot path and does not start normal UI/services.
- FileService write leases normalize paths, reject NUL/parent traversal and conflicting mutations
  on the same, parent or child path; unrelated reads and writes remain available. FileService and
  CameraService are static services in `main.cc`; callers must keep them alive until workers and
  leases exit. FileService unmount does not drain active leases.
- Audio, music, voice assistant, camera, web file server, and cloud services are initialized as services but open heavy hardware paths only when needed.
- `AudioOutputService` owns the shared output configuration. With an open codec it commits a new
  volume only after `AudioCodecOutput::SetVolume` succeeds. `AudioService` reads that shared value
  for playback/UI, and atomic set/up/down operations serialize calculation, hardware application,
  revision and receipt under the output mutex. With the codec closed, a setter accepts configuration
  for the next open. Failure of the initial codec volume API call closes that open attempt and
  clears its format state so a later request can retry. This preserves on-demand hardware ownership.
- MQTT desired-volume application checks the setter result and reports the retained configuration
  on failure. A source-verified build overlay for esp_codec_dev 1.5.7 propagates the selected
  codec/software-volume driver's exact error and only commits the dependency cache on success.
  It preserves software priority and no-codec software output. Other dependency API error paths
  remain outside this focused correction; success does not establish hardware or acoustic success.
  Ordinary shadow reports contain no volume effect ID or applied desired version; see the
  [AIoT shadow contract](rodak-aiot-contract-v1.md#5-shadow-state-and-device-properties).
  Correlated MQTT volume effects use a separate `effects/receipt` publication and a bounded
  authority-scoped ledger. Real connection epochs travel through fragment assembly and the
  worker queue; the scope check and `ApplyVolume` share the MQTT service lock with cancellation.
  Receipt enqueue runs in the SDK custom-event callback to preserve SDK/service lock order.
  See [MQTT volume effects](mqtt-volume-effects.md) for the contract and host service tests.
- `LightService` shares one atomic patch path between Smart UI setters and MQTT. A driver error
  retains accepted configuration/revision, updates availability/error, and leaves hardware
  application unverified. Correlated MQTT light patches carry actual discovered IDs, reuse the
  authenticated epoch/SDK receipt boundary, and keep 64 recent outcomes plus a monotonic
  authority version watermark. Only requested fields execute. `tests/light_service` and the
  extended real MQTT service target compile production light/board adapter code with SDK fakes;
  see [MQTT light effects](mqtt-light-effects.md). LCD backlight remains a separate local service.
- Voice wake monitoring uses local MultiNet without a cloud connection. A wake match takes exclusive
  audio focus and opens one Rodak WebSocket session. Every non-terminal reply drains TTS and starts
  the next input turn on that same session; a user saying “再见” results in `session.end`, while 30 seconds of follow-up silence, errors, or a
  connection/listening watchdog disconnect and re-arm local monitoring. Active TTS playback is not
  terminated by that watchdog. See [Voice assistant integration](voice-assistant.md) and the
  [realtime voice v1 contract](rodak-realtime-voice-contract-v1.md).
- WiFi credentials are stored in NVS by `WiFiConfig`; auto-connect starts after PhoneSystem is up so UI boot is not blocked.
- MotionService exposes a stable app-facing motion API. The BigSmart QMI8658 adapter samples over the shared Board Manager I2C peripheral in a background task so apps never perform I2C work in the LVGL thread.
- UnifiedMqttService consumes Rodak bootstrap credentials, reports device state, and routes OTA
  notifications. OtaUpdateService stages and verifies the main image on SD; the separate factory
  Recovery project is the only runtime allowed to rewrite `ota_0`.
  With USB server trust, MQTT uses MQTTS with the fixed expected server name, and OTA Bearer
  requests remain on the pinned HTTPS origin. The separate Appearance publisher signature/origin
  approval is never granted or rewritten by network discovery.
- Ordinary command ACKs and camera/display sidebands retain the original MQTT generation, epoch
  and ACK topic. Their bounded queue drains in the SDK user-event callback with direct QoS 0
  publish, without storing an outbox item. Epoch changes discard queued results and late callbacks;
  a separate operation mutex serializes stream Start/Stop/signaling, while connection changes
  revoke instance leases and defer peer cleanup outside MQTT callbacks. A latest-64 authority
  cache replays immutable command results. `RemoteInputController` owns bounded input queues and
  stream/control grants checked at final LVGL execution; `DisplayControlAckTracker` binds replies
  to an opaque peer instance. Admitted actions may finish; eviction/reboot are outside the replay
  guarantee. See the [command result boundary](rodak-aiot-contract-v1.md#command-results-and-replay-boundary).
- Shell preferences use the short-lived `shell` NVS namespace. Settings exposes explicit commit results so a failed save cannot be reported as successful.
- ButtonBindingService encodes Lock and Control Center as stable actions; persisted `btnbind` values override compiled defaults.
- WakeOnLanService creates a UDP socket only for a user-requested wake, requires active WiFi, and
  leaves device-list persistence to the Wake app's versioned `wol/devices` NVS document.

### Video and voice task retirement

The five Camera Preview/JPEG, Display JPEG and Camera/Display peer task paths use
`task-retirement.{h,cc}`. A bounded, lazily allocated PSRAM registry owns numeric
owner/generation records. Each Start reserves a record before creating its WithCaps
task; the common entry waits for handle publication before entering the service.
Business cleanup and the complete return of callbacks/local destructors are separate
states. Stop captures one exact generation and joins outside service locks, allowing
a callback destructor to start a replacement without making the old Stop wait for it.
Creation failure preserves the previous ticket; shutdown closes admission and drains
all generations before releasing service state.

The external reaper calls the pinned IDF WithCaps delete path, which suspends the
worker, waits for all cores to leave it, then frees its TCB/stack. A periodic call in
the permanent main loop also reclaims autonomous peer failures. It does not create
an exit-time cleanup task or allocate a retirement record at exit. This is not a
deadline, zero-cost or whole-device OOM guarantee. The 031 source also uses this registry
for Assistant I/O, frontend Capture and wake supervisor. Ordinary voice Deinit remains
restartable; only service destruction closes and drains its owner. Capture returns through
its vector destructors before retirement, Assistant closes its transport before joining,
and Wake coalesces Stop/Disable/Deinit completion by operation epoch. AFE fetch retains its
existing external delete and wake notification retains its ordinary internal stack.
See [030 video contract](task-retirement.md) and [031 voice contract](voice-task-retirement.md).
The 032 diagnostic is compiled only with `RODAKOS_RELEASE_TESTS=ON`. It admits a canonical
request ID into one accepting/pending/executing slot; the permanent internal-stack main task
calls Wake Deinit, observes task-name absence without reinitializing Wake, then attempts one
Wake Start. Listening with all three workers and idle cycles have different result categories.
Callbacks run outside the slot mutex; busy checks and serial-write blocking do not arbitrate
UI/MQTT operations globally. The ordinary OFF ELF excludes the TU, hooks, command and fault
markers. Test-flavor device observations and ordinary restoration have separate identities in
[032 evidence](ota-release-readiness.md#2026-10-08-voice-lifecycle-diagnostic-and-restoration-032).
Independent review records one idle and three Listening cycles on the test image, followed by
ordinary OFF guarded boot and one voice-session stop/rearm window. The latter does not execute
the absent lifecycle diagnostic; physical full-resource recovery and production gates remain open.

033 keeps that retirement and diagnostic contract. The audited mn5q8_cn model alone owns
its command registry. AFE classifies post-fetch cancellation under the lifecycle mutex and
continues draining outstanding feed before exit; current errors still invalidate continuity.
Credential refresh runs sequential noinline exchange/parse/persist stages (and pairing only
under the existing gate), with shorter NVS-key temporary lifetimes on the same internal
wake_notify task. Final ELF frame reductions are separate from runtime headroom. See
[033 evidence](ota-release-readiness.md#2026-10-08-voice-health-and-credential-refresh-033).

034 gates current AFE fetch on one complete output frame using successful SDK output
bytes. Credits remain a conservative lower bound after a transient read failure; a
ledger of four uncertain frames, a zero feed with uncertain inventory, or a malformed
nonzero feed/credit overflow triggers quiescent buffer/VAD resynchronization while
outstanding feed leases can still drain. Input epochs fence old reads and local tails;
raw and AFE diagnostic gaps stay distinct. DSP residual state is explicitly retained.
Only terminal recorder failure ends Running, and Assistant cleanup rechecks both
interaction and transport generations. The locked phase-only getter is observational;
existing admission and wake-generation checks remain authoritative. See
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
replay. Startup timer allocation still has LVGL's configured malloc-assert boundary;
arbitrary app rollback or UI OOM recovery is not established.

`tests/navigation_ui` compiles the actual System, Navigation, Registry, Host,
CameraApp and PhoneUi against real LVGL. Its 12 cases include three child-process
marker/SIGABRT checks for unsafe lifecycle failures; current run identities and results
are recorded in the test README and 030 contract.
Camera hardware, audio focus, Shell and Home content remain fixtures. These cases
do not establish the cause of the earlier 028 Home enqueue failure or a 030 device
result. See [serial launch](serial-provisioning.md#030-bounded-navigation-admission)
and [navigation host coverage](../tests/navigation_ui/README.md).

## Related Docs

- [Firmware build and flash](firmware-download.md)
- [BigSmart WebRTC peer integration](esp-peer-integration.md)
- [Appearance verification](appearance-verification.md)
- [Project roadmap](roadmap.md)
- [OpenOS comparison and design decisions](openos-comparison.md)
- [Home layout and folder design](home-layout-design.md)
- [Rodak AIoT v1 contract](rodak-aiot-contract-v1.md)
- [Rodak realtime voice v1 contract](rodak-realtime-voice-contract-v1.md)
- [Voice assistant integration](voice-assistant.md)
- [Voice task retirement and software evidence](voice-task-retirement.md)
- [Serial provisioning](serial-provisioning.md)
- [Video task retirement and deferred navigation](task-retirement.md)
- [Rodak MQTT and SD Recovery OTA](mqtt-ota-sd-recovery.md)
- [Troubleshooting](../TROUBLESHOOTING.md)
