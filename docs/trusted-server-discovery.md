# Trusted server discovery

The trusted LAN transport starts from the serial-binding baseline `7101282`.
It keeps one Rodak authority across changing laptop addresses. USB establishes the
trust anchor; mDNS supplies routing candidates; TLS authenticates every credential
destination. A MAC address, cleartext server ID, or signed HTTP nonce alone is not
server authentication for transmitting the device secret.

## USB trust contract

Firmware advertises `RODAK_PROVISION_READY {"version":1,"server_trust":1}`. A desktop
must observe `server_trust:1` before sending this optional object in its ordinary
`RODAK_PROVISION_V1` frame:

```json
{
  "server_trust": {
    "version": 1,
    "server_id": "<lowercase SHA-256 of certificate SubjectPublicKeyInfo DER>",
    "tls_name": "rodak-<first 16 server_id hex characters>.local",
    "ca_pem": "-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----\n"
  }
}
```

The complete frame retains its 2048-byte limit. The certificate is bounded to
1536 bytes; the other fields and JSON escaping must also fit the complete frame.
The firmware parses one certificate, verifies its public-key digest, and requires
the exact deterministic TLS name. The private key never leaves Rodak.

`bootstrap_url` uses the canonical stable hostname:
`https://rodak-<id-prefix>.local:9443/api/v1/aiot/devices/bootstrap`. Numeric IP
addresses, HTTP, another hostname, or an incompatible pre-existing pin are rejected
in pin mode. A plain READY marker still permits legacy frames without `server_trust`.
The USB HMAC challenge remains independent device-to-desktop binding evidence.

The first USB installation explicitly trusts the operator-selected workspace. It
does not cryptographically prove which server previously operated an unauthenticated
HTTP endpoint. An existing bound device tests its existing secret only through this
newly pinned TLS channel; rejected authentication never starts a replacement pairing.

## Persistence and recovery

`device_cloud/server_auth` is one bounded, versioned JSON record with `active` and
`pending` endpoints. Version 3 stores their common trust object once at the root;
each endpoint retains the canonical URL, `requires_bound_identity`, and the
authenticated numeric `connect_address` route. An empty route uses normal resolution.
Readers still accept version 1 and 2 records, including their per-endpoint trust;
successful writes use version 3. Encoding rejects different active/pending pins
before sharing the trust. Output is limited to 3999 bytes, plus NVS's terminating
NUL, and the complete new value must fit before the old value is replaced.
The USB trust frame remains version 1. The existing secret and
registration flags remain in their existing keys. USB installs a candidate without
overwriting the last active endpoint.
Cached MQTT and voice capabilities are disabled while a USB candidate is pending.

For an existing binding, a successful TLS `/auth/token` response and a complete,
valid secure transport descriptor are required before promotion. Fresh devices may
complete the existing owner-confirmed pairing flow on the pinned candidate. The
guarded credential transaction writes its pending barrier, MQTT/voice caches, the
new authority record, and finally the completed identity. MQTT caches carry a
server-ID/URL/route key; mismatched or partially committed caches cannot become usable.
Authentication failures and exhausted discovery attempts retain the last authority.
Persistence failures restore the previous record and caches where possible and
report uncertainty when rollback cannot be established.

Once version 2 is written, package `20261007-010516` (006) and earlier version-1-only
readers fail closed on this record. Once version 3 is written, version-2-only 007/008
readers also fail closed. They are not supported network-recovery rollbacks.
Keep NVS and the immutable Recovery; repair and rebuild from a source that reads
the stored version, then use the normal preserved-NVS refresh workflow. Erasing
NVS to force compatibility discards the binding and is not part of this migration.

`device_cloud/server_pinned` is committed before the first authority record. Once
set, a missing, malformed, oversized, or unreadable authority fails closed instead
of selecting legacy HTTP. Required bound-identity reads are also strict; missing
secrets or failed NVS reads cannot generate a replacement device identity.

Interrupted serial provisioning still clears WiFi to prevent mixed old/new onboarding
data. It preserves an installed trust record and never restores a plaintext default
over that pin. An unreadable trust record leaves recovery pending. Hardware power-cut
and storage-fault recovery are separate acceptance gates.

The live cloud generation changes when USB stages an authority or authenticated
discovery changes its endpoint or route. Existing voice, OTA, and Appearance snapshots
must pass the current-generation check before further requests. Network discovery never
changes the pin. Explicit server-confirmed unbinding clears device credentials while
retaining the trusted server, allowing a new owner-confirmed pairing there.

## Discovery and transport

Before a route has been authenticated, connections use the lwIP `.local` resolver
with the stable hostname. If the stored HTTPS endpoint or route fails, bound devices
query `_rodak._tcp` through the pinned `espressif/mdns` 1.14.0 component.
The query has a 1500 ms timeout and at most
eight results. Only records with the expected hostname, exactly one `v=1`, and exactly
one matching full `id` contribute address/port candidates. Up to eight addresses
per result are inspected; at most six distinct address/port combinations are tried,
each with a six-second shared bootstrap/token deadline. This bounds candidate HTTP
attempts to 36 seconds, in addition to the initial attempt and discovery query.
IPv4 and non-scoped IPv6 addresses are supported. Unspecified, loopback, multicast,
IPv4-mapped IPv6 and link-local/scoped IPv6 routes are rejected. Scoped IPv6 needs a
separate interface-lifetime contract and remains open.

Each candidate uses its numeric address directly while retaining the installed
certificate and stable TLS name/SNI. Several addresses for the same hostname and
port are tested independently. TXT/SRV/A/AAAA records remain unauthenticated hints:
bootstrap success alone does not authorize promotion. Token authentication and a
complete secure descriptor must also succeed on the same route before persistence.
A TLS-valid credential rejection stops discovery without starting replacement
pairing. USB provisioning and refresh are serialized, and generation checks reject
stale requests and commits. Unconfirmed pairing is not moved by discovery.

| Channel | Pinned transport requirements |
| --- | --- |
| Bootstrap, token, pairing and unbind | HTTPS, installed certificate, fixed expected TLS name; redirects disabled |
| MQTT | `unifiedMqtt.transport: "mqtts"`, broker host equal to `tls_name`, validated port; same TLS trust anchor/name |
| Voice | WSS on the HTTPS origin; same certificate/name before the Bearer handshake |
| OTA API, ticket, manifest, artifact and result | HTTPS on the current logical origin; no cross-origin artifact or Bearer forwarding |
| Appearance API and artifact | HTTPS on the current logical origin; existing publisher trust remains a separate gate |

The default secure ports are HTTPS/WSS 9443 and MQTTS 8883. Ports come from the
authenticated descriptor after discovery. In pin mode, the public CA bundle and
global CA store are disabled, and common-name checks are never skipped. Certificate
and TLS-name storage outlives each asynchronous client and its reconnects.

The authenticated route is reused by bootstrap/token, MQTTS, WSS, OTA and Appearance,
including client recreation and reboot. Stored capability URLs and origin checks
remain logical URLs; only the connection URI contains the numeric address. HTTP
requests replace their single `Host` header with the logical hostname and port.
The current ESP WebSocket transport generates its wire `Host` from the numeric
connection URI. Fixed TLS SNI/name and the pin still authenticate Rodak, but this
does not support arbitrary virtual-host routing; its non-scoped IPv6 wire Host is
also a compatibility boundary. No second `Host` header is appended. This boundary
is accepted only for the private Rodak service and is not general WSS authority
compatibility. The checked WebSocket overlay rejects redirects before another
handshake; it must ship with numeric routing.

MQTT route changes count as session-identity changes even if credentials are
unchanged. They retain the existing controlled restart isolation. This is bounded
recovery, not seamless live migration. Separately, once the current explicit WiFi
Connect generation has obtained an IP, the adapter keeps retrying after AP loss
with a capped backoff and a 20-second connection deadline. Credentials that have
never connected in this generation still use the finite initial-attempt policy.
Explicit disconnect, new configuration and shutdown cancel the previous retry
generation. The timer daemon only posts retry events; driver calls remain
serialized by the API mutex. Late STA/IP events must match the current AP/netif state.

This firmware's `CONFIG_MBEDTLS_HAVE_TIME_DATE` remains disabled. It authenticates
the pinned key and expected TLS name; it does not claim certificate-expiry validation
from a trusted wall clock. Secure Boot, Flash Encryption, production certificate
rotation/revocation, and recovery from deliberately replaced trust storage require
their own policies and are not implied by LAN discovery.

Appearance trust binds publisher key and exact origin. The first HTTP-to-HTTPS
upgrade requires physical publisher confirmation for the new origin. Ordinary IP
changes retain the logical origin; an HTTPS port change remains an origin change.
The network migration never rewrites or grants publisher trust.

## Validation

`tests/server_trust` compiles the production trust codec, HTTP TLS configuration,
DNS-SD filtering and `DeviceCloudConfigService`. Its 29 Debug and ASan/UBSan tests
with leak detection cover public-key digest mismatch, URL confusion, downgrade,
TLS-open failure before secret writes, successful existing-binding promotion,
credential rejection without pairing, malicious MQTT descriptors, spoofed discovery
followed by a valid candidate, candidate exhaustion, reboot/repeat USB refresh,
authority/identity and transaction-barrier read faults, write failure, missing-record
downgrade rejection, same-port address failover, route persistence/reboot, v1/v2
records, compact shared-trust migration with bounded writes, conflicting pins and
ambiguous fields, fixed Host/SNI with numeric dialing, bounded address filtering and USB
refresh serialization. The MQTT targets additionally verify numeric route client
recreation and route-change restart isolation; voice-identity integration remains
green with the route source linked. The production WiFi adapter has 22 Debug and
ASan/UBSan/leak host cases covering retry/cancellation/deadline and stale events.
HTTP, mDNS and NVS are injected host boundaries; this suite does not implement a
real TLS handshake or prove physical flash power-loss behavior.

`tests/server_trust_nvs_storage` additionally compiles the production authority
codec with the reviewed ESP-IDF NVS Storage/Page/PageManager implementation and
official CRC source. Its six Debug and ASan/UBSan/leak cases use synthetic six-page
NOR storage and the public test certificate, never a device partition or secret.
They reproduce an old-format write failing despite aggregate free space, verify
the previous authority and unrelated sentry remain intact, then write the compact
record successfully on the identical layout. Thirty pending/active cycles exercise
60 updates, readback/reload and actual SDK garbage collection. SDK source drift is
rejected before building. These tests establish the storage regression and repair;
they do not retrospectively identify 007's unlogged hardware error or establish
physical power-cut behavior.

The numeric-route, redirect and continued WiFi-recovery changes are host-verified;
their identified package still needs independent-address and AP-loss hardware
acceptance. The dated 006 evidence below predates these changes. Additional 006
cross-network, MQTTS-negative and WSS observations are tracked in Rodak's
[network verification record](https://github.com/rymcu/rodak/blob/main/docs/trusted-network-verification.md).

```powershell
wsl -d Debian -- cmake -S /mnt/d/workspace/rodakos/tests/server_trust `
  -B /home/ronger/.cache/rodakos-server-trust -G Ninja -DCMAKE_BUILD_TYPE=Debug
wsl -d Debian -- cmake --build /home/ronger/.cache/rodakos-server-trust
wsl -d Debian -- ctest --test-dir /home/ronger/.cache/rodakos-server-trust --output-on-failure
```

The MQTT service target separately checks the MQTTS URI and certificate/name lifetime
after asynchronous setup returns. The release host runner includes both `server_trust`
and `serial_provisioning`. Build/package and COM3 evidence must identify the resulting
signed firmware. Package 006 passed the bounded COM3 gate for trusted USB refresh,
authenticated MQTT/telemetry and bidirectional SRV port recovery, as recorded below.
Actual WiFi/subnet/address changes, WSS interaction, extended resource-pressure soak,
independent A/AAAA probes and physical trust/power-loss recovery remain open. Earlier
hotspot/serial evidence predates this feature and cannot close those gates.

### Signed build package

Package `20261007-000651` was built from the uncommitted trusted-server changes on
`7101282`, using ESP-IDF 6.0.2 and `ninja -C build -j2`. The first unrestricted
parallel build encountered a GCC internal compiler error in the unchanged ESP-DSP
convolution source; the low-concurrency build and final incremental build completed.
The signed bundle reuses immutable Recovery from `20260930-231358` and preserves
the existing partition layout and OTA public key. This record is build evidence;
it does not assert that the package has been flashed or network-tested.

| Artifact | Identity |
| --- | --- |
| Main image | 7,080,096 bytes; SHA-256 `22fa8544be031a2c39a574f2d97b1af644cc3e2e17dc5e71c11651571538de2c` |
| Merged image | 16,777,216 bytes; SHA-256 `38179b2d7b6309d3768d333716616f4f4fe289e8b57c66046978b092f0694068` |
| Immutable Recovery | SHA-256 `ffa412ebe30c714c691bba73c8ab6e4efcaaab14fce5229f595707a8a08f75fd` |
| Version / signing task | `0.1.2-dev.1` / `trusted-server-lan-001`; development RSA-2048/SHA-256 signature |
| Flavor | Production app set; Home test population and release fault injection disabled |
| `sdkconfig` | SHA-256 `d241f837ae97109b3b7cd10d8c74e9d81a59c4e176bd7a77df3362fda6ba41fc` |
| `dependencies.lock` | SHA-256 `62261e93cf1d9df629c38af9ce54eae6a1b34e1318a1c8455d098059027f19b9` |

The bundle verifies the main image against `ota_0` (49% free) and the immutable
Recovery against its factory partition (87% free). The generic root-build warning
that the main image exceeds the Recovery partition is not a valid flash target;
use only the supported signed-bundle preservation workflow.

### First hardware attempt: failed acceptance

The `20261007-000651` attempt accepted trusted USB configuration on COM3 and briefly
reconnected with the existing binding and token version 4, but it repeatedly panicked
after connecting. The first log includes `LoadProhibited`, an interrupt watchdog,
and explicit `main` and `serial_prov` stack-overflow reports. These resets invalidate
that run's network acceptance; the old script's `ROUND_VERIFIED` output must not be
treated as a pass. The follow-up collector retains the complete redacted serial
log and fails immediately on a panic/overflow.

Evidence is retained in Rodak's `.codex-temp/trusted-network-hardware.log`,
`trusted-network-hardware-repro.log`, and `trusted-serial-live.log`. The original ELF
is `build/logs/trusted-network-first.elf`, SHA-256
`660f38d631845096319a670ed14e1e36ff9dc72354ceb9bfaf75e46e43f36924`.
The repeated serial failure resolves to FreeRTOS's stack-overflow hook; its final
NVS frame already has a corrupted stack pointer and is not a reliable faulting call.

Inspection found `app_main` polling `AppearanceService::OnNetworkReady` every five
seconds once MQTT is connected. Before the fix, its persistent main frame was 720
bytes, the stack-local cloud snapshot added an 816-byte frame, and the measured
`Load`/authority decode/X.509/EC public-key-import chain was at least 2160 bytes before
additional allocator/lock frames. This already exceeded the configured 3584-byte
main stack. Repeated serial provisioning nested the same validation under its 4096-byte
stack and likewise exhausted its margin.

The repair moves the Appearance notification's cloud snapshot to temporary heap
storage and budgets 8192 bytes for both main and serial provisioning. Every NVS trust
read still performs the same certificate/public-key validation; no trust cache or
validation bypass is introduced. Main reports stack minimum/internal heap every
30 seconds, and serial provisioning reports them after each request. These stack
budgets reserve 8704 additional internal bytes in total. The replacement signed
package and real minimum-free measurements must be recorded before declaring the
hardware gate passed. Cross-network, independent address candidates and damaged-trust
power-loss recovery remain separate open gates.

### Stack repair build

Replacement package `20261007-002717` uses signing task `trusted-server-stack-002`
and passed the full ESP-IDF 6.0.2 `ninja -C build -j2` build and the preserved-Recovery
bundle checks. Its main image is 7,079,472 bytes, SHA-256
`4cb272857b9acd664e2c59d8850af1c4aeaa65bef81c729490a8c091e4c976b9`;
the merged image SHA-256 is
`7445542f2b4d18de4439661512f963fad2ad81bc827a173d60c482dee6f617fe`.
It retains version `0.1.2-dev.1`, the same immutable Recovery, development signing,
the production app set, and disabled Home/fault-injection test modes.

The new `sdkconfig` SHA-256 is
`767b0e9a2a238a7e0557937c4b44ddaa940bcb5c1cab4775af37950b27f17077`;
the dependency-lock hash is unchanged. Generated `sdkconfig.h` confirms the main stack
constant and its compatibility alias both resolve to 8192. Disassembly confirms
`OnNetworkReady` now reserves 48 bytes rather than 816. The 19 trust-service tests
remain green under ASan/UBSan with leak detection; they do not establish the ESP task
stack margin. Its hardware attempt below did not pass network acceptance.

### Second hardware attempt: internal heap exhaustion

Package `20261007-002717` removed the observed stack panics. The serial task's
minimum free stack was 3976 bytes (4216 used), and main's was 2640 bytes (5552 used).
Wake initialization reported `enabled=1 listening=1`. However, after HTTPS token
refresh the SDK could not create its 6144-byte MQTT task: internal free heap was
10591 bytes with a largest contiguous block of only 4096 bytes. Subsequent retries
had about 9443 free bytes and the same 4096-byte largest block. A successful token
exchange therefore did not establish MQTTS or continuous shadow reporting.

Rodak retains the complete serial evidence in
`.codex-temp/trusted-network-hardware-serial-1791304199043.log`, with its summarized
collector output in `trusted-network-hardware-fixed.log`. The failed run preserves
the existing WiFi, pinned authority and device binding; neither erasing NVS nor
disabling the user's local wake listener is an acceptable repair.

The next repair keeps main at 8192 bytes, reduces serial provisioning to 6144
bytes using its measured 1928-byte remaining margin, and reserves 7168 contiguous
internal bytes during MQTT service startup before wake initialization. It releases
that block immediately before the authenticated SDK task starts, after its final
service/client/generation admission check. Allocation failure retains the existing
retry path, and service stop or setup failure releases the reservation exactly once.
This is an allocation-order repair: releasing a block does not make the subsequent
SDK allocation atomic across cores. Holding the block also reduces memory available
to wake initialization. Acceptance still requires a real run with wake listening,
MQTTS, repeated telemetry/shadow, no panic or allocation error, and recorded task
stack/heap margins at the same time.

### Internal-memory repair build

Package `20261007-004058`, signing task `trusted-server-memory-003`, contains the
allocation-order repair. ESP-IDF 6.0.2 completed the incremental `ninja -C build -j2`
build, followed by signed-bundle and preserved-Recovery verification. Its main image
is 7,080,496 bytes, SHA-256
`294e70c650efcfe23f220cc74fcce0f5937b0f9b4f5567097db4fa95dd6814d9`;
the merged image SHA-256 is
`0af84b91e1cda6a151f6b0c7a7bf74972018dc061bfe5498bd28c8d4fcc34056`.
The SDK configuration, immutable Recovery, version and development signing flavor
are unchanged from the stack-repair package.

All three MQTT host targets passed in both Debug and ASan/UBSan builds with leak
detection. Added cases cover failed reservation allocation, repeated unconfigured
start/stop, concurrent start/stop, and release before SDK start; the pinned MQTTS
certificate/name lifetime case remains included. These tests do not reproduce
ESP32 internal-heap fragmentation. The hardware attempt below failed, so this
package must not be used as evidence that resource-pressure acceptance passed.

### Third hardware attempt: rejected reservation strategy

Package `20261007-004058` exhausted internal DMA memory during local wake startup.
The log reports `allocate memory for dma descriptor failed`, followed by
`LoadProhibited`. The saved ELF resolves the crash to `i2s_tx_channel_start`, reached
through `i2s_channel_enable`, the codec I2S format change, `AudioCodecInput::OpenForOwner`,
and `VoiceAudioFrontend::StartListening`. The 7168-byte reservation therefore cannot
be kept: it trades away the required wake listener's DMA budget before MQTT starts.

The complete flash/boot evidence is
`.codex-temp/trusted-network-memory-flash.log`; the matching ELF is
`build/logs/trusted-network-memory-third.elf`, SHA-256
`b59a78bfe19b4c61f325d598511f27557aa28460b74c2625cf773bdc219a9649`.
The host runner rejected the run with exit code 3. Rollback to the second package
preserved NVS and passed the Recovery/main/OTA/Home boot checks, with wake
`listening=1`; see `.codex-temp/trusted-network-safe-rollback-flash.log`.

The reservation and its allocation-specific host fakes were removed. The
Start/Stop lifecycle lock and final SDK start admission check remain, along with
8192-byte main and 6144-byte serial stacks. The next run tests that smaller serial
stack by itself; previous runs do not establish whether its 2048-byte saving is
sufficient for a contiguous authenticated MQTT task allocation. Codec allocation
failure handling is being reviewed separately; reducing task memory does not by
itself repair an SDK path that proceeds after a failed DMA allocation.

### Serial-stack-only build

Package `20261007-004529`, signing task `trusted-server-serial-budget-004`, contains
main's 8192-byte stack, serial provisioning's 6144-byte stack, no early reservation,
and the MQTT lifecycle/admission fixes. It does not yet contain the separate codec
DMA failure-handling repair. The three MQTT host targets passed again after removal,
and ESP-IDF 6.0.2 incremental build and preserved-Recovery bundle verification passed.

The main image is 7,079,920 bytes, SHA-256
`dafcfb7e20a249c2731ce1aca405f49249e8e46d5e3bbd7b2db44daa107d79b7`;
the merged image SHA-256 is
`0f95cc9cb1ec9d4c1e9e615f2fcfa08520bc1703efcb0ee42872407e3703b882`.
Version, SDK configuration, immutable Recovery and development signing are unchanged.

This package passed NVS-preserving flash and the boot checks, with wake enabled and
listening. Serial provisioning's minimum free stack was 1932 bytes. After successful
HTTPS enrollment, internal free memory was 12451 bytes, but the largest block was
only 4608 bytes (later 4096), and MQTT task creation still failed. The full network
log is Rodak's `.codex-temp/trusted-network-hardware-serial-1791305225438.log`.
The matching ELF is `build/logs/trusted-network-serial-fourth.elf`, SHA-256
`628b730c5654afe65aa40617667137da80033dc63507079e86c7247702b7e779`.

The next budget adjustment reduces each of the two internal RGB565 display DMA
buffers from 40 rows to 32. With this board's configured width of 320 pixels, it
releases 10240 persistent internal bytes (51200 to 40960). Double buffering, display
resolution and wake functionality are retained; larger redraws may require more
flush fragments. Startup logs expose the row count and total byte budget. The next
hardware gate must therefore check display startup/navigation/refresh alongside
wake listening and stable MQTTS, and include the separately reviewed codec DMA
failure-handling repair.

### Display-DMA budget and codec repair build

Package `20261007-005456`, signing task `trusted-server-dma-budget-005`, combines the
32-row display buffers with the reviewed codec DMA failure-handling overlay. The
actual ESP-IDF compile commands include both generated `esp_codec_dev.c` and
`platform/audio_codec_data_i2s.c`. Its first compile rejected the obsolete
`SOC_I2S_NUM` macro; the final overlay uses IDF 6's `I2S_LL_GET(INST_NUM)` and the host
fake was corrected to expose that same interface. The final low-concurrency build
and signed preserved-Recovery bundle verification passed.

The main image is 7,080,224 bytes, SHA-256
`574be45b38c08026315c569809ee55a15e328c67b5ceb608f1c1205adcfc00c0`;
the merged image SHA-256 is
`eaaeeed0e07cbcec0f54b80ba3f91598b2e554cfc843849d4f80401cb099fd18`.
Its retained ELF is `build/logs/trusted-network-dma-fifth.elf`, SHA-256
`0b342706b63b7516a9640de05f5ec95d4a7093b1b1e5936567f6677569beb683`.
Version, SDK configuration, immutable Recovery and development signing are unchanged.

The final codec overlay passed 19 Debug and ASan/UBSan/leak cases plus 15 generator
checks. The unpatched upstream negative control failed 11 relevant cases, including
the two I2S failures. The current MQTT lifecycle passed all three host targets under
Debug and ASan/UBSan/leak checks after removal of the reservation. The codec fault
latch is scoped to the physical I2S port and survives codec/data-interface recreation;
only a reboot clears it. This prevents continuing with a damaged DMA chain but does
not establish stable audio under memory pressure. This package still requires its
own display, wake and authenticated-network hardware evidence.

### Fifth hardware attempt: protocol recovery works, TLS coexistence fails

The fifth package accepted two trusted USB refreshes with `bindingStatus=not_needed`,
kept token version 4, restored MQTTS across HTTPS/MQTTS port changes in both
directions, and produced fresh telemetry/shadow while wake remained enabled and
listening. Main and serial minimum free stacks were 2640 and 1896 bytes respectively.
These are protocol-function observations, not a stability pass.

The same full log, Rodak's
`.codex-temp/trusted-network-hardware-serial-1791305836753.log`, repeatedly reports
`esp-aes: Failed to allocate memory`, TLS read/write `-0x0084`, and HTTPS
`mbedtls_ssl_setup returned -0x008D`. The final wake report's internal minimum was
only 103 bytes, and the MQTT worker minimum reached 900 bytes. The first collector
still emitted `HARDWARE_TRUSTED_NETWORK_GATE_PASSED` because it checked panics and
main/serial/wake conditions but omitted crypto allocation errors; that result is
not valid resource-stability acceptance. The revised gate must reject any such
crypto allocation failure as well as MQTT task allocation failures.

In ESP-IDF 6.0.2, `-0x008D` is `PSA_ERROR_INSUFFICIENT_MEMORY` (-141), also used for
`MBEDTLS_ERR_SSL_ALLOC_FAILED`. SSL setup allocates I/O/handshake structures and
initializes handshake hashes; it does not create a task, obtain randomness or
allocate a key slot. I/O structures use the configured PSRAM allocator, whereas
the accelerated PSA SHA setup explicitly allocates internal DMA contexts. The
AES driver can also allocate a 1600-byte internal DMA bounce buffer for unaligned
PSRAM data. The AES allocation error is directly observed; the exact failing SSL
setup allocation cannot be identified from that error code alone.

The next layout uses two 24-row display buffers (30720 bytes total), saving another
10240 bytes over package 005, and increases the internal MQTT worker to 8192 bytes
for a net 8192-byte budget gain. Main stays at 8192, serial at 6144 and the MQTTS
SDK task at 6144. Full-screen flushes can increase from about eight to ten fragments;
pixel payload and double buffering stay the same. Main, serial and MQTT health
logs now include internal `DMA | 8BIT` free/largest blocks, and SDK callbacks report
new minimum stack values separately from the worker. The strict gate requires no
crypto/task allocation errors, wake listening, both port migrations and at least
1024 bytes of observed free stack for each measured task.

### TLS headroom build

Package `20261007-010516`, signing task `trusted-server-tls-headroom-006`, contains
the 24-row display/8 KiB worker layout and the separate DMA/SDK-stack diagnostics.
The main image is 7,081,040 bytes, SHA-256
`8f2af6a9155035cc56a6c1c9c73ab5f2f5d3dbb3f93ace1867088d663fec8dfa`;
the merged image SHA-256 is
`c7581cbd35c06cc3adf41a039368a4baae500fbd63ce83cb56584d96dd2000c8`.
The retained ELF is `build/logs/trusted-network-tls-sixth.elf`, SHA-256
`1c40e3e46ff4e697d16ad5c65149b109b54ce66f64854601958d33b5f2a46a10`.
The three MQTT host targets passed ASan/UBSan/leak checks, followed by the final
ESP-IDF 6.0.2 build and signed preserved-Recovery bundle checks. Version, SDK
configuration, immutable Recovery and development signing remain unchanged.
The bounded strict hardware gate passed for this package as recorded below.

### Sixth hardware attempt: bounded strict gate passed

On 2026-10-07, package `20261007-010516` passed NVS-preserving COM3 boot checks and
the strict trusted-network collector with exit code 0. The complete redacted serial
log is Rodak's `.codex-temp/trusted-network-hardware-serial-1791307010945.log`; the
collector transcript is `.codex-temp/trusted-network-hardware-final.log`. The run
lasted about five minutes, including a final one-minute observation window. It is
not the eight-hour release soak.

Device `44:1b:f6:c3:b4:30` stayed on the laptop's `RodakOS-Lab` hotspot at
`192.168.137.24`. Two trusted USB refreshes both returned `not_needed`. The device
ID and token version 4 remained unchanged, with no unbind or replacement pairing.
The gate observed new authenticated MQTT sessions, current shadow and at least two
telemetry reports per round. HTTPS/MQTTS ports changed from 9443/8883 to 9444/8884
and back; both routes recovered after authenticated DNS-SD candidate discovery.
Recovery still includes retries against the old port and a device-owned controlled
restart. It is not seamless live migration or proof of an actual WiFi/subnet change.

Home and local OTA boot confirmation remained healthy. Wake stayed enabled with
`listening=1` across the reboots, which establishes an armed listener rather than
spoken wake recognition. No panic, task-allocation error, AES allocation error,
TLS setup `-0x008D` or TLS read/write `-0x0084` occurred in the complete run. The
strict collector rejects these errors; it no longer accepts the fifth run's false
resource pass. Rodak reported no recent thing-model schema warnings for this device.

| Measurement across the complete run | Observed minimum |
| --- | --- |
| Main free stack / 8192-byte budget | 2640 bytes |
| Serial provisioning free stack / 6144-byte budget | 1904 bytes |
| MQTT worker free stack / 8192-byte budget | 3076 bytes |
| MQTTS SDK free stack / 6144-byte budget | 3128 bytes |
| Wake supervisor free stack | 2436 bytes |
| Sampled internal DMA free heap | 7083 bytes |
| Sampled largest internal DMA block | 6656 bytes |
| Reported internal heap low-water mark | 5799 bytes |

DMA samples are periodic observations, not an allocator-wide minimum or an
exhaustion proof. Display boot and touch polling remained active, but physical
navigation/readability and animation throughput were not measured in this gate.
WSS/voice turns, actual laptop address/network changes, multiple A/AAAA candidate
probing, power cuts, damaged-pin physical recovery and extended soak remain separate
open work in [the roadmap](roadmap.md). The HTTPS false-certificate result below is
separate negative evidence; this positive gate alone does not prove it.

### HTTPS wrong-certificate hardware rejection

The same package passed a separate COM3 negative gate with exit code 0. An HTTPS
listener on port 9443 presented the expected TLS name using a different self-signed
key. The device attempted a real handshake and rejected it with
`mbedtls_ssl_handshake returned -0x2700`. The listener recorded one TLS/certificate
failure and zero HTTP requests; neither a panic nor an allocation failure occurred.
Restoring the genuine server recovered the original device ID, token version 4,
bound state, MQTTS and two fresh telemetry reports without re-pairing.

Evidence is Rodak's `.codex-temp/trusted-negative-hardware-final.log` and
`.codex-temp/trusted-negative-serial-1791307651509.log`. The successful listener
ran in the already allowed Electron process. An earlier Node listener only caused
timeouts without a TLS handshake and is explicitly excluded from the passing result.
This checks the HTTPS wrong-key/certificate branch only. It does not establish WSS
or MQTTS negative cases, replay/expiry rejection, multiple-address probing, certificate
rotation/revocation or physical trust recovery.

### Numeric-route and WiFi recovery build

Package `20261007-020911`, signing task `trusted-server-route-recovery-007`, was
built after committing source `8188e7e22364ca9373cfb020ef834745cd80a609`, with no
tracked changes at packaging time. ESP-IDF 6.0.2 `ninja -C build -j2` and the signed
preserved-Recovery bundle checks both passed. This package adds numeric route
authentication/persistence, transport reuse, the checked WebSocket redirect
rejection and continued WiFi recovery with guarded STA/IP events. The production
compile commands include the generated WebSocket client and route source.

| Artifact | Identity |
| --- | --- |
| Main image | 7,093,088 bytes; SHA-256 `8e0beefcc725dc7b9651e27f1cf82ec0b4b38011ff18724c05c4f175e7cf704c` |
| Merged image | SHA-256 `d077e6e2bebed5ed9e8c3befdb93b84a19e9394de7aff4ce269299b7697bb80e` |
| Retained ELF | `build/logs/trusted-network-route-seventh.elf`; SHA-256 `bb1f7c91abc82f0af0bb736ee5e0333ea9089f8af89050e57585b8b6be44c699` |
| Immutable Recovery | Reused from `20260930-231358`; SHA-256 `ffa412ebe30c714c691bba73c8ab6e4efcaaab14fce5229f595707a8a08f75fd` |

Version remains `0.1.2-dev.1`, with development signing and the production app
flavor; Home population and release fault injection are disabled. `sdkconfig`
SHA-256 is `767b0e9a2a238a7e0557937c4b44ddaa940bcb5c1cab4775af37950b27f17077`,
and `dependencies.lock` remains
`62261e93cf1d9df629c38af9ce54eae6a1b34e1318a1c8455d098059027f19b9`.
The package leaves 49% of the main slot and 87% of the Recovery partition free.

The 26 trust cases, three MQTT targets, voice-identity integration, 22 production
WiFi cases and WebSocket overlay checks passed their recorded host checks. This is
build/software evidence only. Same-port multi-address recovery, preserved routes
after reboot, real AP disappearance and the new package's resource margins still
require their own hardware run. Once its authority v2 record is written, recover
with a v2-capable package preserving NVS; do not use 006 as a network rollback.

### Seventh hardware attempt: NVS write gate failed

007 passed preserved-NVS Recovery/Home boot, but its first trusted USB refresh
returned `nvs_state_uncertain`; it did not pass network acceptance. The failure
branch is after successful authority encoding/roundtrip validation, during NVS
pin/authority persistence. A protected read-only snapshot showed an intact v2
active authority with an empty route, so v1-to-v2 migration had already succeeded.
The existing device ID and server token version 4 remained unchanged.

The snapshot's active record was 994 bytes. Adding the pending endpoint duplicated
the certificate, producing a 1946-byte v2 record needing 62 contiguous NVS entries.
The active page had 43 tail entries; after GC the best page was exactly at the
62-entry boundary. This post-rollback snapshot alone cannot identify the failing
API/error: an in-memory run of the actual IDF NVS implementation could write that
candidate successfully. Separate synthetic pressure reproduced
`ESP_ERR_NVS_NOT_ENOUGH_SPACE` while preserving the old value, despite sufficient
aggregate free entries. It establishes a storage-design weakness, not the original
hardware error code. The host map fake had not modeled this single-page restriction.

Evidence is Rodak's `.codex-temp/trusted-network-seventh-hardware.log` and
`.codex-temp/trusted-network-hardware-serial-1791310298167.log`. The device's original
NVS backup stays private; no credential values or raw partition data belong in Git.

### NVS diagnostic build

Package `20261007-022059`, task `trusted-server-nvs-diagnostic-008`, was built from
`38d79bb3fb5f9a704d52d841a0c0ebab54b11d80`. It only adds diagnostic logs to the
production Settings writer: namespace/key, failed SDK result, string byte count,
size-read result and NVS entry statistics. It does not log values or change the
storage/identity policy and is not a repair or a successful hardware gate.

ESP-IDF 6.0.2 and signed preserved-Recovery packaging passed. Main is 7,094,080 bytes,
SHA-256 `05865dc206b40474d4c380bc7d4f3e1acc7c4f31653596024caa80f889964618`;
merged SHA-256 is `61fec43fc33726f7227ef4031710a2a831ef6d786305b13bd2b1a03558e0af1a`.
The retained ELF is `build/logs/trusted-network-nvs-eighth.elf`, SHA-256
`640748619c9690391510e4430132c17861af83a76ec9b5f750d9f36307493c30`.
Immutable Recovery, SDK configuration, dependencies, version and development signing
are unchanged; the production app flavor still disables fault/Home test injection.
