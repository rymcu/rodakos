# Serial Provisioning

This document defines the USB serial provisioning path for WiFi and
Device Cloud bootstrap configuration. It is intentionally separate from the
runtime voice protocol and from the Rodak server API.

## Scope

Rodak is the operator-side sender. RodakOS is the device-side receiver. A
successful provisioning session writes only:

- WiFi SSID and password to the existing `wifi` NVS namespace.
- Device Cloud bootstrap URL to the existing `device_cloud` NVS namespace
  (`prov_url` key).

MQTT credentials, WebSocket tokens, broker settings, and topic names are not
sent over serial. RodakOS obtains those values from the configured bootstrap
endpoint after WiFi connects.

The existing Settings pages remain supported as a manual fallback.

### NVS namespace ownership

The service label is **Device Cloud**, but the persisted NVS namespace is
`device_cloud` (not `cloud`). Related namespaces are:

| Namespace      | Keys written or refreshed          | Source                      |
| -------------- | ---------------------------------- | --------------------------- |
| `wifi`         | `ssid`, `password`                 | Serial provisioning request |
| `device_cloud` | `prov_url`                         | Serial provisioning request |
| `websocket`    | `url`, `token`, `version`          | Bootstrap response          |
| `unified_mqtt` | Broker, credential, and topic keys | Bootstrap response          |

Changing `device_cloud/prov_url` clears cached `websocket` and `unified_mqtt`
values before the next bootstrap. MQTT and WebSocket secrets are never accepted
as serial request fields.

The firmware does not expose textual commands such as `cloud bootstrap set` or
`wifi provision`. The only host-to-device command is the framed
`RODAK_PROVISION_V1` line below; the namespace names above describe persistence
only.

## Wire Format

The first implementation uses newline-delimited UTF-8 JSON with an explicit
framing marker:

```text
RODAK_PROVISION_V1 {"ssid":"...","password":"...","bootstrap_url":"..."}\n
```

The device replies with one JSON result per line. Because ESP-IDF log output and
the provisioning stream share USB Serial/JTAG, the host must also recognize a
marker that is adjacent to a log prefix in the same USB packet/line:

When the service is running it emits `RODAK_PROVISION_READY {"version":1}`
immediately and approximately every five seconds. The sender must wait for this
marker (a plain `RODAK_PROVISION_READY` is also accepted) before sending a
request. The service uses the existing USB Serial/JTAG console VFS and does not
install a second UART or USB driver.
Do not start an ESP-IDF REPL or another reader on the same console while a
provisioning session is active; both readers would consume the same RX stream.

```text
RODAK_PROVISION_RESULT {"ok":true}\n
```

Errors use a stable code and do not echo credentials:

```text
RODAK_PROVISION_RESULT {"ok":false,"error":"invalid_bootstrap_url"}\n
```

The receiver must reject oversized frames, malformed JSON, control characters
inside values, unknown protocol versions, and URLs using schemes other than
`http` or `https`. Bootstrap URLs also require a non-empty authority, a valid
port when present, and no embedded user information. Credentials must never be
printed in replies or logs.
An empty `password` is valid for an open WiFi network; non-empty passwords are
limited to 63 bytes and SSIDs to 32 bytes.

The 2048-byte frame limit is the complete wire size: prefix, JSON payload, and
terminating LF are all included. LF and CRLF are accepted; with CRLF, both line
ending bytes count toward the same limit. After rejecting an oversized line,
the receiver discards bytes through its terminating LF and then accepts the
next frame normally.

## Transaction Rules

1. Validate the complete request before changing NVS.
2. Save WiFi credentials and bootstrap URL as one provisioning transaction.
3. Clear cached WebSocket and MQTT credentials for every accepted provisioning
   request, including a repeat request for the same bootstrap URL.
4. Return success only after NVS commit succeeds.
5. Start or restart WiFi connection using the saved credentials.
6. Once connected, wait until the success result has drained to USB, then call
   the cloud refresh callback. When the unified MQTT service is already running,
   that callback restarts the device so an in-flight old bootstrap task and MQTT
   outbox cannot cross the new configuration boundary.
7. After the restart, call Device Cloud bootstrap and persist its WebSocket and
   unified MQTT response.

When MQTT authentication is rejected later, RodakOS refreshes bootstrap credentials
without accepting any secret over serial. An unchanged session identity with an
empty outbox may update the running client in place; identity, routing, outbox,
configuration, or reconnect failures restart the device. A transient bootstrap
failure keeps the existing client so the next authentication rejection can retry;
only MQTT connection events update the logical connected state while HTTP refresh is
pending.

Partial requests must not erase an existing working configuration. A failed
bootstrap refresh may retain the previous cloud credentials, but an accepted
provisioning request always clears them before the next bootstrap attempt.

### Interrupted transaction recovery

Before the first provisioning NVS mutation, RodakOS commits
`serial_prov/pending=true`. It clears that marker only after the WiFi credentials,
bootstrap URL, and cloud credential invalidation have all committed. If power is
lost while the marker is set, the next boot does not try to combine old and new
fields: it clears the saved WiFi credentials, restores the default bootstrap URL,
invalidates cached WebSocket and MQTT credentials, and then clears the marker.
If any recovery write fails, the marker remains set so the same conservative
cleanup is retried on the next boot.

This recovery deliberately requires the operator to provision the device again.
It prevents a partially written WiFi/cloud pair from reaching the server. A
failure after the marker has been cleared, such as a live WiFi disconnect timeout
or connection-start failure, does not erase the fully committed new values; the
next normal boot can use them.

## Security

The serial link is assumed to be physically local. It is not an authenticated
remote management channel. The sender must require an explicit operator action
and should use a short-lived provisioning session. The device must not echo the
WiFi password, MQTT password, JWT, or WebSocket token. Production deployments
should add an operator confirmation or pairing challenge before enabling the
receiver continuously.

RodakOS reuses the ESP-IDF USB Serial/JTAG console VFS; it does not create a
second serial or USB interface. The service backs that VFS with Espressif's
ring-buffer driver because the no-driver hardware RX FIFO is only 64 bytes and
a complete provisioning JSON frame is normally longer. The RX ring is 4096
bytes, leaving headroom above the 2048-byte protocol limit. Provisioning RX
reads the installed driver ring directly with a zero-wait call, so it does not
depend on `/dev/console`, `stdin`, or VFS descriptor flags; stdout and logs keep
using the console VFS on the same COM port. RodakOS refuses to take over a USB
Serial/JTAG driver already installed by another service. The provisioning task
is pinned to the core that installs the driver so its shutdown path can drain
and uninstall the driver on the same core, as required by ESP-IDF.

## Rodak Integration

Rodak should expose a device configuration action that reads `WIFI_SSID`,
`WIFI_PASSWORD`, and `RODAK_BOOTSTRAP_URL` from a local, ignored `.env`
file, shows the target serial port, and requires confirmation before sending.
The action must report only validation, transport, and device result status.

The server-side bootstrap endpoint remains the source of truth for MQTT and
WebSocket credentials. It must not accept MQTT secrets from the desktop
provisioning flow.

## Acceptance

- A wiped device accepts a valid frame and auto-connects to WiFi.
- A valid bootstrap URL is persisted and used after reboot.
- After a successful result, the running device schedules the cloud refresh only
  after the result has drained to USB, then performs one device-owned restart.
- A malformed or oversized frame leaves existing NVS unchanged.
- A reset during NVS mutation is detected on the next boot and cannot bring up a
  mixed WiFi/cloud configuration.
- Every accepted provisioning request, including a repeated URL, clears stale cloud credentials.
- Serial output contains no password or token.
- Rodak reports the device result and subsequently observes the device's
  bootstrap request and MQTT connection.

## Hardware Acceptance

The wired BigSmart gate has verified settings-preserving firmware updates and
serial provisioning on the board's USB Serial/JTAG port. The captured boot log
contained `RODAK_PROVISION_READY`, a successful provisioning result, a WiFi
address, and `Unified MQTT connected`. Rodak subsequently received periodic
telemetry for the device, confirming the bootstrap and MQTT path.

The firmware log also reports MultiNet loaded with TDM slot 2 (MIC2) and
`listening=1`. No one was at the device to speak the wake phrase during this
gate, so this is an armed/listening result, not a claim of a successful
real-person wake. A Rodak near-field recording preset is not required for the local
MultiNet gate; audio fixtures only exercise the post-wake cloud path.

## Device Cloud E2E Acceptance

Rodak's Playwright hardware gate passed in one serial session after verifying the
immutable Recovery image and partition table against the installed baseline.
The device accepted provisioning, returned the result before its own restart, and
then restored WiFi and the bootstrap URL from NVS. The resulting bootstrap, MQTT
connect, and telemetry events were observed in order. A second host-controlled
restart reused the persisted cloud cache. A formal `token_version` rotation from
4 to 5 caused the cached MQTT credential to be rejected; RodakOS refreshed bootstrap
credentials, updated the running MQTT client, and reconnected.

The gate recorded three host-controlled resets plus one provisioning-owned restart.
The 300,013 ms soak received 9 telemetry frames at 29,321..30,311 ms intervals and
20 successful Rodak health samples (maximum latency 8 ms). It detected no unexpected
reset, panic, MQTT task creation failure, or disconnect. The redacted report is under
the ignored Playwright `test-results` directory.
This remains a non-voice acceptance result; it does not claim a real-person local
MultiNet wake.

## Interrupted-Recovery Hardware Gate (2026-09-02)

The first same-value provisioning smoke exposed a real recovery failure. The
request left `serial_prov/pending=true`; the next boot cleared WiFi, then
overflowed the 3,584-byte `main` task stack while nested cloud-config snapshots
were being created.

The fix keeps full `DeviceCloudConfig` rollback snapshots in short-lived heap
objects and persists the empty WebSocket/MQTT state directly. The compiled
`SaveProvisioningUrl()` frame decreased from `0x420` (1,056 bytes) to `0xC0`
(192 bytes) without increasing a resident task stack.

The corrected firmware passed an NVS-preserving refresh after its bootloader,
partition table, and immutable Recovery were verified. The boot log records the
pending marker being detected, WiFi/cloud state being conservatively cleared,
and `Interrupted provisioning recovery complete`, followed by Home startup and
local OTA confirmation without a panic. A second provisioning request returned
an explicit success response. Rodak then observed bootstrap request/response,
MQTT connect, and telemetry at approximately 30-second intervals. A subsequent
no-reset capture contained the expected readiness and MQTT downlink markers with
no reset, panic, or stack-overflow marker. The server-side `token_version`
remained unchanged; this gate did not perform credential rotation.

This is a non-voice provisioning and Device Cloud result. It does not claim a
real-person MultiNet wake.
