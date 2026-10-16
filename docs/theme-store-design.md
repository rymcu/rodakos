# Theme Store Design

Status: Phase 1 is implemented and host-verified; Phases 2 and 3 are proposed. This design
extends the signed appearance pipeline in [Device Appearance Resources](appearance-customization.md);
read that page first.

Goal: a standalone **Themes** app that shows the current appearance, switches local presets, and
later browses and installs themes from a Rodak-hosted store. Every installed theme still arrives
as a per-device, signed appearance deployment. The store adds discovery and a device-initiated
install request. It does not add a second trust or download path.

## Non-goals

- Third-party publishers. The device pins one publisher key; every store entry is signed by it.
- Payments, user accounts, ratings or download counts.
- Offline theme switching. Switching to another theme always needs a fresh signed deployment.
- Fonts, scripts or executable content in packages. The device still parses no fonts and runs no
  script engine.
- Relaxing physical-touch publisher confirmation. It moves into Themes with the same checks.

## Current baseline

| Area | Today | Source |
| --- | --- | --- |
| Theme picker | Card on the Settings main page with four preset swatches (dark, light, blue, green). A tap saves a local override and refreshes the UI. | `main/apps/settings/settings_main_page.cc` |
| Preset primaries | Duplicated as literals in the Settings tap handler and in `rodakos_theme.cc` | `settings_main_page.cc`, `main/phone_ui/rodakos_theme.cc` |
| Package theme | `rodak-appearance-v1` carries one preset name plus one primary color. The device accepts only the four preset names. | `components/rodak_appearance/rodak_appearance.cc` (`IsAppearancePreset`) |
| Wallpaper | Only from a signed package, applied at boot | `main/main.cc` (`SetWallpaper`) |
| Package status | `AppearanceService` reports status, progress and errors to the shadow only; no UI shows them | `main/phone_os/appearance_service.cc` (`ReportedJson`) |
| Trust | Settings → Appearance showed the publisher fingerprint and accepted physical touch only. Phase 1 moved it into Themes. | Formerly `main/apps/settings/settings_appearance_page.cc`; now `main/apps/themes/themes_app.cc` |
| Rodak data | `appearance_releases` are device-independent; `appearance_deployments` are per device, signed and revisioned | Rodak `src/main/server/appearance-service.ts` |
| SD layout | Two package slots `a`/`b` for active and pending/previous | `/rodakos/appearance/` |

The Rodak split between releases and deployments makes the store possible. A release is a
catalog item. Installing it means creating a deployment for one device through the existing
`createDeployment` path, so signing, revision allocation, supersession and the device's
download/trial/confirm flow stay unchanged.

## Decisions

1. **Standalone app, ID `themes`.** Themes owns the presets, the package status and the publisher
   trust page. The Settings theme card is replaced by a row that launches Themes, and the Settings
   外观定制与信任 page is removed. Moving the page avoids a launch argument for a Settings deep
   link. It also gives phase 2 one place to switch between the store and the trust prompt. Home appends the new app at the end of the layout
   through its existing registry reconciliation; no layout migration is needed.
2. **Rodak is the only publisher.** A store listing points at a Rodak release. Unlisted releases
   can still be published from the Rodak desktop, but the device cannot install them from the store.
3. **The device requests installs; the server allocates deployments.** The device never chooses a
   revision. The install response returns the same five-field desired object that MQTT delivers,
   and the device feeds it to `ApplyDesiredJson`. If MQTT delivers the same revision later, the
   duplicate is a no-op (`revision == latest_revision_` with a stored desired returns early).
4. **The store requires the trusted server transport.** Catalog JSON and thumbnails are not
   signed. The pinned HTTPS origin authenticates them, so the store tab is hidden in legacy
   HTTP mode.
5. **Install is not a trust decision.** The publisher key is already pinned, and each package is
   still verified against it. Remote control may browse and install. Only publisher trust keeps
   the physical-touch rule.
6. **The last action wins for colors.** The existing precedence stays: a local preset overrides
   the package colors, and adopting a newer deployment clears that override.
7. **Formal adoption stays at boot.** Phase 3 adds a RAM-only live preview, not a new adoption
   path; see [Live preview](#live-preview).

## Phases

| Phase | Scope | Repositories | Persistent-format change | Blocked by |
| --- | --- | --- | --- | --- |
| 1. Themes app | Current appearance, presets, package status, trust shortcut | RodakOS | None | — |
| 2. Store MVP | Catalog, thumbnails, install, restore built-in | RodakOS + Rodak | SD thumbnail cache (disposable) | Trusted HTTPS route. Appearance rows in the [#29](https://github.com/rymcu/rodakos/issues/29) power-cut matrix before default enablement. |
| 3. Theme v2 | Full color tokens, live preview, content-addressed package reuse | RodakOS + Rodak | `rodak-appearance-v2`, package library | Phase 2 hardware evidence |

### Precedence

| Action | Colors | Wallpaper and boot animation | Persisted as |
| --- | --- | --- | --- |
| Select a preset in Themes | Immediately | Unchanged | Local override in NVS `appearance/state` |
| Install from store | Next boot (phase 3: live preview) | Next boot | New deployment; adoption clears the local override |
| Restore built-in | Built-in dark at next boot | Removed at next boot | Built-in deployment; clears the local override |
| Publish from Rodak desktop | Same as install | Same as install | Same as install |

## Phase 1: Themes app

Status: implemented in source and host-verified on 2026-10-16. Not yet flashed or checked on a
device; see [Phase 1 verification](#phase-1-verification).

### Device changes

- `main/apps/themes/` holds `ThemesApp`, registered last by `RegisterThemesApp` with ID `themes`,
  category `kSystem` and tint `kWarm`. Aliases: `theme`, `appearance`, `wallpaper`, `主题`,
  `外观`, `壁纸`. Home appends it to existing layouts through registry reconciliation.
- `ICON_PALETTE` (`ion:color-palette-outline`) is in both icon fonts, generated by
  `tools/generate_icon_fonts.py`; see [Icon Glyphs](icon-visual-refresh.md#icon-glyphs).
- `AppearanceService::GetStatusSnapshot()` returns `AppearanceStatusSnapshot`
  (`main/phone_os/appearance_status.h`): status, progress, error, fallback reason, active and
  pending revision, whether colors are local, and whether a publisher key is pinned. It copies
  fields under the service mutex and does not touch NVS or SD.
- `rodakos_theme_preset_primary()` returns each preset's default primary. The literal primaries
  in Settings are gone.
- `main/phone_ui/theme_presets.{h,cc}` owns the preset table and `SelectLocalThemePreset()`:
  `SetLocalTheme`, then the runtime theme and `PhoneUi::SyncThemeName`. A failed save leaves the
  runtime theme unchanged. Themes then calls `RefreshTheme` asynchronously.
- The Settings main page replaces its four-swatch theme card with a 主题 row that launches Themes.
  Settings no longer has an 外观定制与信任 row or an Appearance page.
- The publisher trust page moved from Settings into Themes as a second page with the same checks.
  A press records the displayed key ID only when `PhoneUi::IsPhysicalInput()` is true. The click
  confirms only that key, and only if it is still the key displayed by `AppearanceService`. Remote
  pointer, serial or synthetic events cannot grant trust. Opening the page requests the publisher
  once, and 读取/刷新 requests it again.

### Layout (320 × 240)

```text
┌──────────────────────────────────────┐
│ ←            主题                 ⌂ │  header
├──────────────────────────────────────┤
│ ┌─────────┐ 配色：本地 / 外观包      │  112 × 84 preview
│ │ preview │ 外观包 r4 → r5 / 内置外观 │  revisions
│ └─────────┘ 重启后生效                │  status, two lines at most
│ 🎨 配色         [L] [D] [B] [G]      │  preset chips
│ 🔒 发布端：已信任                  > │  opens the trust page
└──────────────────────────────────────┘
```

The preview stretches the shared `PhoneUi` wallpaper with `LV_IMAGE_ALIGN_STRETCH` and allocates
no pixels. Over it, the preview draws a status bar, three icon-slot tiles, a primary button and
sample text in the current theme colors. A local preset gets a `text_primary` selection ring,
which stays visible on the blue and green swatches. With package colors, no chip is selected.

The trust row opens the in-app trust page. That page shows the guide text, the 32-hex-digit
fingerprint in two groups of four, the trust state, any publisher error, and the 读取/刷新 and
指纹一致，确认信任 buttons. The header back button and the system back request both return to
the main page, and a second back returns Home. A theme rebuild keeps the current page.

### Status strings

| `status` | Display |
| --- | --- |
| `idle`, `applied` | 已生效 |
| `awaiting_trust` | 需要确认发布端 |
| `waiting_device` | 等待下载, or 等待重试：`error` when a retry is scheduled |
| `deferred_busy` | 设备忙，稍后下载 |
| `downloading` | 下载中 N% |
| `pending_reboot` | 重启后生效 (primary color) |
| `failed` | 失败：`error` (warning color) |
| `fallback` | 已回退：`fallbackReason` (warning color) |
| `superseded` | 已被新版本取代 |

The page polls the snapshot every 750 ms while it is visible. `OnPause` pauses the timer and hides
the root; `OnResume` refreshes immediately. A theme rebuild keeps a hidden page hidden and paused.

### Phase 1 verification

`tests/themes_ui` compiles the production `ThemesApp`, `theme_presets`, `PhoneUi`, theme and
component code, and the production fonts (Montserrat, Ionicons, PuHui), against LVGL 9.3.
`AppearanceService` and navigation are fakes. Its 20 cases cover:

- Main page: every status string, preset primaries, the local and package-color states, timer
  refresh, a failed save, the light-theme rebuild, the selection ring on its own swatch, the
  longest emitted error code staying inside the card, a missing service, pause/resume polling,
  wallpaper scaling and the descriptor.
- Trust page: opening it in the app, header and system back, and a physical touch that confirms
  exactly the displayed key. Confirmation is refused for remote pointer input, a synthetic click
  and a key that changes between press and release. Loading, publisher errors and an already
  trusted key disable confirmation. The page also covers a failed trust save, a theme rebuild on
  the trust page and a missing service.

Touch cases use the LVGL test pointer as the primary input, with the same remote-input callback
shape as `main.cc`. Screenshots are written as `themes-*.ppm` in the CTest build directory.

The suite does not exercise the production `AppearanceService` state machine, NVS or SD, GT911
touch, the remote-input bridge, the ST7789 panel or real navigation. A device check still needs to
confirm Home placement, preset switching, the Settings row, a physical trust confirmation
against Rodak, refusal under remote control, and the status display during a real package
download.

## Phase 2: Store MVP

### Rodak data

A new table separates listing metadata from immutable releases:

```sql
CREATE TABLE appearance_store_listings (
  id TEXT PRIMARY KEY,              -- lst_<uuid>
  release_id TEXT NOT NULL UNIQUE,
  title TEXT NOT NULL,
  author TEXT NOT NULL,
  summary TEXT NOT NULL,
  product_key TEXT NOT NULL,        -- rymcu-bigsmart
  schema_version INTEGER NOT NULL,
  thumbnail_sha256 TEXT NOT NULL,
  sort_order INTEGER NOT NULL,
  listed INTEGER NOT NULL,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  FOREIGN KEY (release_id) REFERENCES appearance_releases(id)
);
```

The Rodak desktop adds a "List in store" action on a release. When the operator lists a release,
Rodak renders the thumbnail from the compiled package (wallpaper plus a theme color strip) through
its existing Electron canvas compiler. Before storing the JPEG, Rodak checks the size limit and
that it is baseline, not progressive.

The device font is a glyph subset. Rodak rejects a listing whose `title`, `author` or `summary`
contains characters outside the published device glyph set. The device still renders unknown
glyphs safely.

### Device API

All endpoints use the existing device authentication, the `{ code, message, data }` envelope,
fixed paths under the pinned origin, and no redirects.

**`GET /api/v1/aiot/appearance/store/listings?cursor=<opaque>&limit=<1..8>`**

Returns listed releases that match the device's product key and reported capabilities
(`schemaVersion`, `maxPackageBytes`, `maxUnits`, `maxDurationMs`). The response must fit in the
device's existing 8 KiB JSON limit.

```json
{
  "items": [
    {
      "listingId": "lst_…",
      "releaseId": "…",
      "title": "Ocean",
      "author": "RYMCU",
      "summary": "深海蓝配色与波纹开机动画",
      "packageSize": 312345,
      "schemaVersion": 1,
      "thumbnailSha256": "…",
      "updatedAt": "2026-10-16T08:00:00Z"
    }
  ],
  "nextCursor": "…"
}
```

The device bounds `title` and `author` to 32 code points and `summary` to 120. It rejects invalid
UTF-8 and unknown keys.

**`GET /api/v1/aiot/appearance/store/listings/:listingId/thumbnail`**

Returns a baseline JPEG (`esp_new_jpeg` does not decode progressive JPEG), 192 × 144, at most
24 KiB. The device streams it to the SD cache and checks it against `thumbnailSha256` before
decoding. A mismatch deletes the file.

**`POST /api/v1/aiot/appearance/store/installs`**

```json
{ "releaseId": "…" }
```

or `{ "mode": "builtin" }` to restore built-in. The server:

1. Requires a listed release for the device's product key, or `builtin`.
2. Runs the same capability checks as desktop `publish`.
3. Returns the non-terminal latest deployment when it already targets the same release, so retries
   do not consume revisions.
4. Otherwise calls `createDeployment`, which signs the manifest, supersedes the older deployment
   and patches the desired shadow.
5. Allows at most one new deployment per device every 10 s (`429`).

Response `data` is exactly the desired object: `deploymentId`, `releaseId`, `revision`, `mode`,
`keyId`. Errors: `400` malformed, `403` unlisted or wrong product, `409` already active or
revision exhausted, `413` package too large, `429` rate limited.

### Device client

- `ThemeStoreService` in `main/phone_os/`, exposed through `PhoneServices`. It owns one bounded
  worker task. It never runs while `AppearanceService::IsBusy()` and obeys the same busy gate
  (OTA, voice, recording, screen or camera streams). While blocked, the UI shows placeholders.
- An install calls the endpoint and then `AppearanceService::ApplyDesiredJson` with the returned
  object. The existing worker then downloads, verifies and reports `pending_reboot`.
- Thumbnail cache: `/rodakos/appearance/store-cache/<listingId>.jpg`, at most 64 files and 1 MiB,
  evicted by oldest modification time. The cache is disposable and is never consulted for trust.
- The store tab is visible only with a trusted-server HTTPS route, a trusted publisher and a
  listings response. Without a trusted publisher it shows the trust shortcut instead.

### Store layout

Grid: 3 columns × 2 rows of 96 × 72 thumbnails, each with a one-line title, decoded near that
size from the 192 × 144 JPEG through `ImageLibrary::LoadThumbnail`. Only the visible page keeps
decoded images. Detail page: the full 192 × 144 thumbnail, title, author, summary, package size,
and one button: 安装, 下载中 N%, 重启后生效 or 使用中. The button state comes from comparing
`releaseId` with the snapshot's active and pending release IDs.

## Phase 3: Theme v2 and package reuse

### Schema `rodak-appearance-v2`

The `theme` object carries a full token set instead of `preset` + `primary`:

- `base`: `dark` or `light`. This selects the `PhoneUi` component theme, like
  `rodakos_theme_is_light_name` today.
- Every `rodakos_theme_t` color field and `iconPalette[8]`, as 24-bit RGB.

Rodak checks the contrast at compile time: `textPrimary` on `bgPrimary` and on `bgSecondary` must
reach at least 4.5:1. The device only checks bounds and applies the tokens through
`rodakos_theme_set_custom`.

Negotiation: firmware that decodes v2 reports `capabilities.schemaVersion: 2` and still accepts
v1. Rodak lists and deploys v2 releases only to devices that report 2. After an OTA rollback to
v1 firmware, a v2 package in a slot fails schema validation and the device uses the existing
fallback path (previous valid package or built-in).

### Live preview

After a v2 package reaches `pending_reboot`, Themes offers 立即预览:

1. Read the pending package from SD and verify its SHA-256 against the stored release, as at boot.
2. Release the current wallpaper, then decode the new one (153,600 bytes). The peak stays at one
   wallpaper.
3. Apply the color tokens and call `RefreshTheme`. Apps without `OnThemeChanged` are recreated, as
   today.

The preview is RAM-only. The persisted state stays `pending_reboot`, and formal adoption still
happens through the boot trial and confirmation, so a crash during preview recovers through the
existing trial path. The boot animation always waits for the next boot.

### Package reuse

Store packages in a content-addressed library,
`/rodakos/appearance/library/<packageSha256>.rap`, with at most 8 entries (4 MiB). Switching
back to a theme still needs a new signed deployment from the store. The download step is skipped
when a library file matches the signed `packageSha256` and size. The `a`/`b` slot semantics and
the NVS trial state are unchanged; slots reference library files instead of private copies. This
step changes the SD layout and needs its own power-interruption rows.

## Budgets

| Item | Limit |
| --- | --- |
| Listing page JSON | ≤ 8 KiB per page, ≤ 8 items, using the existing private JSON budget |
| Thumbnail transfer | ≤ 24 KiB JPEG, streamed to SD through a 4 KiB buffer |
| Decoded grid thumbnails | 6 × 96 × 72 × 2 = 82,944 bytes PSRAM, visible page only |
| Detail preview | 192 × 144 × 2 = 55,296 bytes PSRAM; grid images are released while detail is open |
| Thumbnail cache | ≤ 64 files and ≤ 1 MiB on SD |
| Live preview | One wallpaper (153,600 bytes), with the old one released first |
| Package library (phase 3) | ≤ 8 packages, ≤ 4 MiB on SD |

These are application limits, not a proof of total heap headroom. Store browsing during camera or
voice use is blocked by the busy gate rather than budgeted.

## Security notes

- Packages, manifests and revisions keep the existing RSA-2048/SHA-256 verification and
  anti-rollback policy. The store adds no new key or signature format.
- Catalog text and thumbnails come only from the pinned HTTPS origin. Thumbnails are
  hash-checked against the listing, size-bounded and limited to baseline JPEG before decoding.
- The install endpoint only accepts listed releases. A compromised device credential can at most
  install listed themes on its own device, rate-limited.
- Remote control can install but cannot grant publisher trust.

## Verification plan

Host:

- `tests/appearance`: snapshot fields across all states, the install handoff with duplicate MQTT
  desired, listing codec bounds (UTF-8, unknown keys, oversize page), thumbnail hash mismatch,
  cache eviction.
- New `tests/themes_ui` (production LVGL, like `tests/music_ui`): status strings, preset refresh,
  navigation away while a request is in flight, grid residency.
- Rodak (vitest): listing filters by product and capability, unlisted rejection, idempotent
  install, rate limit, supersession, built-in restore.

Hardware:

- Browse and install with and without an SD card, through to `applied` after reboot.
- Install during a camera stream (`deferred_busy`, then resume).
- Remote-control browse and install. Confirm that trust still refuses remote input.
- Power cuts during install download, slot rename and NVS commit, added to the
  [#29](https://github.com/rymcu/rodakos/issues/29) matrix before the store is enabled by default.

## Open questions

- Should Themes offer 立即重启 for `pending_reboot`? It would need the same busy gate as OTA, so
  it does not interrupt recording, voice or an OTA write.
- Whether Rodak should report per-release install counts to the operator. The device would need
  to add `activeReleaseId` to the reported shadow.
