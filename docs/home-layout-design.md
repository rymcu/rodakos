# Home Layout And Folder Design

Status: phase 4 residency is implemented in source. Pure policy tests and a production-HomeApp host
LVGL suite pass; multi-page hardware, physical interaction/readability, and true out-of-memory
recovery gates remain open.

Home resolves a stable layout into 4x3 LVGL tileview pages, supports non-nested folder browsing, and
provides draft-only root and folder editing without moving Home into a dynamic runtime or weakening
the native app model.

## Data Contract

Persist one versioned JSON document in NVS namespace `home`, key `layout`:

```json
{
  "v": 1,
  "rev": 4,
  "items": [
    {"type": "app", "id": "settings"},
    {
      "type": "folder",
      "id": "f_7a31c4e2",
      "name": "Tools",
      "apps": ["files", "gyro", "system"]
    }
  ]
}
```

- Root items are dense and ordered. Page and slot are derived from index, never persisted.
- `rev` starts at zero. A user save serializes the next revision, but the live in-memory revision
  advances only when the store returns confirmed `kSaved`. Registry reconciliation and app-ID
  migration do not increment it or write NVS. An explicit reset of an exhausted `UINT32_MAX`
  revision starts a new baseline at zero.
- App entries store the exact canonical descriptor ID, not title, alias, icon, or color.
- Folders cannot nest and hold at most 12 apps.
- Folder IDs use `f_` plus eight random hexadecimal digits and are unique within the document.
- Folder names are valid UTF-8 and at most 24 bytes. Rendering may elide but must not split UTF-8.
- Empty folders are removed. A one-item folder is retained because it can represent user intent.

## Storage Rules

- Reject input larger than 3072 bytes before JSON parsing. NVS remains a small settings store, not an app database.
- Use a short-lived `Settings("home", true)` and require both `SetString()` and explicit `Commit()` to succeed.
- Validate and serialize a candidate model before writing. Replace the live in-memory model only
  after a confirmed `kSaved` result.
- Before saving, compare the current NVS value with the exact value observed at load time and require
  the candidate revision to match the current session revision. A conflict freezes normal writes for
  that Home session instead of overwriting a newer document.
- ESP-IDF may persist during `nvs_set_str()` even though callers must still invoke `Commit()`. If a
  write/remove or commit result is uncertain, NVS may already contain the complete next-revision
  candidate while the live model still holds the previous revision. Freeze the session and resolve
  it on the next load; never report that state as a rollback to the previous Flash value.
- Do not write while editing or paging. Root moves, folder create/rename/member moves/dissolve, and
  any nested editor navigation modify one draft; the outermost Done performs at most one guarded
  save. Cancel or a repeated Home request discards the entire draft without writing.
- A newer unknown schema is read-only. Older firmware must not overwrite it after an OTA rollback.
- Malformed current-schema data requires an explicit reset. A wrong-type NVS value remains read-only
  because it cannot participate in the exact-value compare-and-swap guard. Oversized,
  unknown-version, and storage-I/O failures also remain read-only because they may contain data an
  older firmware cannot interpret safely.

## Registry Reconciliation

At Home startup, resolve the saved model against `ListHomeApps()` using exact descriptor IDs:

1. Keep the first occurrence of each currently visible app ID.
2. Remove missing, duplicate, and no-longer-visible app IDs.
3. Remove empty folders and dissolve duplicate folder IDs without losing valid members.
4. Move members beyond a folder's 12-item limit back to the root; never discard them.
5. Append newly registered apps in registry order.
6. Keep reconciliation in RAM until the user edits the layout, avoiding flash writes on every upgrade.

The first folder ID wins. Members from a later duplicate folder are dissolved into root entries at
that folder's position. Folder members beyond the 12-item limit are inserted immediately after the
folder in member order. Both rules run through the same global first-occurrence app-ID filter.

Aliases are not automatic migrations for persisted IDs. App ID changes use the versioned
`kHomeAppIdMigrations` graph before deduplication. The resolver follows the full graph to a terminal
ID so repeated boots are idempotent; conflicting sources and cycles are ignored with their dependent
chains. With no valid explicit entry, the old ID is removed and the new app is appended in Registry order.

## Capacity And UI

- The first managed layout supports eight pages of 12 root items.
- If more than 96 items exist, slot 96 becomes `All Apps` and exposes items 96 onward with a count.
- Folder views live inside the Home body, not on `lv_layer_top()`, so the system Shell stays authoritative.
- Long-pressing a real root App or Folder opens Arrange. Previous/next changes only the draft order;
  Done performs one guarded save and Cancel or a repeated Home request discards the draft.
- Root Apps can create or join a Folder. Folders can be renamed or dissolved, and members can be
  reordered, moved Home, or moved to another non-full Folder. These commands stay in the same draft
  until the outermost Done.
- `All Apps` is a read-only overflow view; items beyond the managed boundary cannot be edited there.
- Save failure keeps the previous live layout. Conflict, stale revision, revision overflow, or an
  indeterminate write freezes further layout writes for that Home session.
- Root items cannot move across the synthetic `All Apps` boundary. Free drag is deferred until
  tileview paging and the 20 ms touch bridge are proven stable together.
- Tile actions record the pointer origin and cancel both tap launch and long-press Arrange after
  10 pixels of movement, even when a one-page or edge-bound tileview has no valid scroll target.
- A long press suppresses launch until the finger is released.
- The current page anchor is module-level session state in RAM. It survives Home instance changes and
  theme rebuilding, clamps after layout changes, and resets after a device reboot; it is never stored
  in NVS.
- Every projected page keeps a stable tile shell so the LVGL tileview geometry and page indices do not
  change during a swipe. Only the active page and its existing immediate neighbors retain their grid,
  buttons, icons, and labels; the resident window is therefore one to three page child trees.
- The render plan always populates the active page first, then the previous page, then the next page.
  On scroll end, an LVGL async refresh releases far-page child trees before populating the new window.
- While that async refresh is pending, the tileview enables only directions whose adjacent page is
  already populated. This prevents a second swipe into an empty page. Teardown cancels the pending
  callback before navigation destruction or theme rebuilding invalidates Home or its LVGL objects.
- Each window refresh logs resident/total pages, internal-SRAM free space, and largest free internal
  block. Home also logs final free/largest values after the remaining UI overlays are created.
- Icon colors derive from stable app ID hashes. Battery information is hidden until a real service exists.

## Delivery Phases

1. Done in source: pure model types, strict cJSON codec, reconciliation, guarded NVS store, boundary
   tests, resolved rendering, read-only folder access, and the eight-page `All Apps` projection.
2. Done in source and host tests: RAM page-anchor restoration, typed root-item keys, adjacent
   reordering, one-shot save/cancel, and explicit save failure handling. Hardware interaction remains
   to be verified.
3. Done in source and host tests: folder create/rename/dissolve, root-to-folder moves, member
   reordering, move-to-Home, cross-folder moves, and single-save draft semantics. Hardware
   interaction remains to be verified.
4. Done in source: stable tile shells, active-plus-neighbors child-tree residency, asynchronous
   far-page release, active/previous/next population order, guarded scroll directions, pending-refresh
   cancellation during teardown, and final SRAM metrics. Pure model tests cover the page-window and
   render-plan policy, while host LVGL tests execute the production Home UI. Multi-page device
   behavior and true embedded OOM recovery remain open.

## Phase 4 Validation Boundary

- Pure model tests pass across one through eight pages, including empty/clamped inputs, a maximum
  three-page resident window, and active/previous/next population order.
- The host LVGL 9.3 target compiles the production `HomeApp`, Home model/store, Registry, `PhoneUi`,
  layout, theme, components, and `SoftKeyboard`. Its in-memory 320x240 display and LVGL test pointer
  execute the real widget/event tree. The suite reports 13 tests and 0 failures for tap slop,
  one-page and multi-page drag suppression, bidirectional boundaries/page swipes, long-press Arrange,
  Cancel/Done semantics, repeated Home, theme rebuild, keyboard geometry, the 96/97-app `All Apps`
  boundary, and async active-plus-neighbors residency. The normal suite also passed 20 consecutive
  runs; ASan/UBSan with leak detection passed.
- The latest protected COM3 refresh reached Home with 11 visible apps, `1/1` resident pages,
  65,387 bytes of final internal SRAM free, and a 43,008-byte largest free block.
  Because the current device exposes only one page, multi-page swiping and lazy turnover remain
  unverified on hardware.
- GT911 touch interaction and ST7789 screen readability require manual observation or an external
  fixture; the host display and test pointer cannot close that gate.
- True out-of-memory recovery is unproven. LVGL uses CLIB allocation with malloc assertions enabled,
  so a real exhaustion path may assert before Home can recover; the SRAM logs are measurements, not
  a recovery test. Host sanitizer and leak checks do not reproduce that embedded allocation path.

Tests must cover round trips, malformed JSON, unknown versions, duplicate/missing/hidden IDs, new app
append, folder overflow, the 3072-byte limit, commit failure, 0/11/13/97 apps, OTA reconciliation,
long-press launch suppression, and reboot persistence.
