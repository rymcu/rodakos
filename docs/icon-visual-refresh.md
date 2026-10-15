# Home Icon Visual Refresh

Status: implemented, host-verified, and checked on COM3 for colors, shapes, press feedback and
the Arrange header. Media coral shows slight RGB565 banding, accepted as is. Swipe smoothness with
the 25-app population is not yet verified; see [Verification Status](#verification-status).

Goal: replace hash-random circular icon tiles with a coherent icon system: category-stable colors,
46px rounded-square tiles, subtle vertical gradients, and press feedback. The desktop tile stays
inside the existing draw-event renderer, so the Home LVGL object count is unchanged.

## Scope

- Desktop Home grid icons, All Apps overflow tile, folder tiles and their previews.
- All Apps / folder collection list row icons.
- Arrange editor header preview (`home_app.cc` layout editor icon, currently a 40px circle). It
  also calls the hash helper, so it must move with the rest.
- The first batch kept the Font Awesome glyphs. A second batch replaced every UI icon with
  Ionicons outline glyphs; see [Icon Glyphs](#icon-glyphs).
- No PNG assets, no Appearance package integration, no new firmware dependency, no
  `sdkconfig` or `lv_conf.h` change.

## Verified LVGL 9.3 Facts

The project pins `lvgl/lvgl 9.3.0` (`dependencies.lock`). Checked against the v9.3.0 tag:

- **No gradient dithering exists.** v8's `LV_DITHER_GRADIENT` / `dither_mode` were removed in v9
  (lvgl/lvgl#6013). There is no dither option in `lv_conf_template.h`, `lv_draw_sw_grad.c`, or
  `lv_draw_sw_fill.c`. The RGB565 path truncates (`& 0xF8` / `& 0xFC`) with no rounding.
- **Vertical two-stop gradients with radius work in the current config.** `LV_GRAD_DIR_VER` is a
  simple gradient. It does not need `LV_USE_DRAW_SW_COMPLEX_GRADIENTS` (off in `sdkconfig`; that
  option is only for linear-angle, radial and conical gradients). It needs `LV_DRAW_SW_COMPLEX=1`
  for the radius mask: on in `sdkconfig`, and the default for the host `lv_conf.h`.
  `LV_GRADIENT_MAX_STOPS=2` is enough.
- **Draw descriptor:** `lv_draw_rect_dsc_t::bg_grad` is an embedded `lv_grad_dsc_t` (not a pointer)
  with `stops[]`, `stops_count`, and `dir`.
- **Style path:** `lv_obj_set_style_bg_grad_color` + `lv_obj_set_style_bg_grad_dir(LV_GRAD_DIR_VER)`
  need no descriptor lifetime. Avoid `bg_grad`, which must point to static storage.
- **Color helpers:** `lv_color_darken(c, lvl)` is `lv_color_mix(black, c, lvl)`. `lvl = 31` is
  about 12 percent; `lvl = 26` is about 10 percent.
- **Transient allocation per gradient draw:** the SW renderer `lv_malloc`s a per-row color map
  (about `h * 4` bytes plus a header) for every gradient fill and frees it after the draw. The
  firmware uses `CONFIG_LV_USE_CLIB_MALLOC`. The object count and persistent heap are unchanged,
  but each tile redraw adds one short-lived allocation of about 200 bytes. The radius mask
  already allocates in the same way today.
- **Pressed state does not invalidate this tile.** `update_obj_state` returns early with no
  invalidate when no style differs between states (`LV_STYLE_STATE_CMP_SAME`). Desktop tiles
  call `lv_obj_remove_style_all` and have no pressed styles, so the draw event must invalidate
  explicitly.
- **Scrolling clears the pressed state without `PRESS_LOST`.** When a press turns into a scroll,
  `lv_indev_scroll_handler` removes `LV_STATE_PRESSED` from the active object directly.
  `PRESS_LOST` is only sent when the pointer moves onto a different object, or in the
  wait-until-release path. Children have `LV_OBJ_FLAG_PRESS_LOCK` by default, so page swipes take
  the scroll path. The pressed look must therefore come from the object state, not from a
  payload flag.
- **An input reset also clears the pressed state silently.** `lv_indev_reset` sends
  `LV_EVENT_INDEV_RESET`, and the object base handler removes `LV_STATE_PRESSED` without an
  invalidate. Production calls it from the touch bridge (`main.cc`), the Shell and `PhoneUi`, so the
  tile also invalidates on `LV_EVENT_INDEV_RESET`.
- **Host framebuffer is XRGB8888** (`lv_test_display.c`, asserted in
  `tests/home_ui/framebuffer_test_helpers.h`). Host tests cannot observe RGB565 banding.

## Design

- Desktop tile: 46x46, radius 14. Arrange header: 40x40, radius 12. Collection row icon: 28x28,
  radius 8.
- Background: vertical two-stop gradient. Top = slot color; bottom = `lv_color_darken(top, 31)`.
- Glyph: white, centered. Desktop uses `PhoneIconFontLarge()`; rows and the Arrange header use
  `PhoneIconFont()`.
- Folder previews (16px on the desktop, 12px in Arrange) stay flat slot colors. A gradient across
  16 rows is mostly quantization noise.
- Folder tile: radius 14, flat `bg_tertiary`, 2x2 previews with radius 5 (desktop) / 3 (Arrange).
- All Apps overflow tile: radius 14, primary gradient. The glyph and count use the Home theme's
  `accent_text` (already used for primary buttons), not hard-coded white, because a custom
  primary can be light.

### Palette Slots

The slot index becomes semantic. Add named constants in `rodakos_theme.h` (for example
`RODAKOS_ICON_SLOT_SYSTEM`) instead of using literal indexes in Home. The array stays 8 entries,
so `rodakos_theme_set_custom` and `rodakos_theme_apply_preset_primary` keep copying it unchanged.

| Slot | Role | dark | light | blue | green |
| --- | --- | --- | --- | --- | --- |
| 0 | System, slate | `5A6E8C` | `52637E` | `667A98` | `62759A` |
| 1 | Media, coral | `D0644E` | `C4553E` | `D46A52` | `D2674F` |
| 2 | Tools, blue | `3D7DD3` | `2F6CC2` | `4A8BE6` | `4082D8` |
| 3 | Games, green | `3A9A5F` | `2E8550` | `3E9E63` | `2F9C86` |
| 4 | Neutral, warm gray | `7A7068` | `6E645C` | `857A71` | `847970` |
| 5 | Warm, amber | `B87A22` | `A86C17` | `BC7E26` | `BA7C24` |
| 6 | Cool, teal | `23919F` | `1C7F8C` | `2496A0` | `2A8FB0` |
| 7 | Alert, rose | `C23A6E` | `B2305F` | `C8406F` | `C63E6E` |

The green preset shifts Games toward teal-green and Cool toward cyan so they separate from the
green background. Neutral is warm gray and Alert is rose, so they stay distinct from System slate
and Media coral.

These values were checked offline with the LVGL mix math and RGB565 truncation:

- White glyph on the top stop: at least 3.35:1 in every preset.
- Darkest stop against `bg_primary`: at least 2.49:1 on the dark presets, at least 4.35:1 on light.
- Closest pair of slots within one preset: Euclidean RGB distance at least 44.

### App Mapping

Add `enum class PhoneAppIconTint { kDefault, kNeutral, kWarm, kCool, kAlert }` and
`PhoneAppIconTint icon_tint = PhoneAppIconTint::kDefault;` after `category` in
`PhoneAppDescriptor`. `kDefault` resolves to the category slot.

Category-only coloring would make 7 of the 13 visible apps the same blue, so the tint spreads
Tools and Media:

| Color | Apps |
| --- | --- |
| System slate | settings, system |
| Media coral | music, camera, recorder |
| Tools blue | calendar, gyro |
| Cool teal | assistant, smart |
| Warm amber | clock, photos |
| Alert rose | wol |
| Neutral warm gray | files, hardware test population |
| Games green | (no current app) |

The rule is: category gives the default, and a tint is a deliberate per-app identity choice. IDs
that are not in the registry (for example a folder preview of a removed app) fall back to the
Neutral slot.

### Press Feedback

While the tile has `LV_STATE_PRESSED`, draw it at 44x44 centered (1px inset) and darken both
stops with `lv_color_darken(..., 26)`. The name label does not move. No timers, animations, or
style transforms (`transform_scale` would also scale the label).

## Implementation Notes

- Add one internal helper in `home_app.cc` that resolves `PhoneAppDescriptor*` (or null) to a slot
  and returns `{top, bottom}` colors, with an optional pressed adjustment. Each call site applies
  the result its own way:
  - Desktop draw event: fill `lv_draw_rect_dsc_t::bg_grad` (`dir = LV_GRAD_DIR_VER`,
    `stops_count = 2`, fracs 0 / 255, opa cover).
  - Collection rows, Arrange header, All Apps overflow tile: local `bg_color`, `bg_grad_color`,
    and `bg_grad_dir` styles.
  - Folder previews: flat `bg_color` only.
- Delete `IconColorIndex`. Folder previews look up each ID with `registry().FindById` so they use
  the same category and tint mapping as the desktop.
- `TilePayload::icon_color` becomes `icon_slot` (or the two stop colors). Theme changes rebuild
  Home, so either is safe; the slot index is smaller.
- In `AppButtonEvent`, call `lv_obj_invalidate(target)` on `LV_EVENT_PRESSED`,
  `LV_EVENT_RELEASED`, `LV_EVENT_PRESS_LOST`, and `LV_EVENT_INDEV_RESET`. In
  `LV_EVENT_DRAW_MAIN_END`, read `lv_obj_has_state(obj, LV_STATE_PRESSED)` instead of a payload
  flag. This covers the scroll case, where LVGL clears the state silently and the moving tile is
  redrawn anyway.
- Keep `rodakos_theme_icon_color(int)` as the accessor; only the meaning of the index changes.
- Banding fallback: there is no runtime dithering in LVGL 9.3, and pre-dithered images are out of
  scope. If hardware shows visible bands, switch the helper to a flat top color plus a 1px
  `lv_color_lighten(top, 20)` highlight row drawn inside the radius. This is a helper-only change.

## Test Plan

Home UI host suite (`tests/home_ui`):

- Gradient: on a desktop tile, the pixel 4px below the top is brighter than the pixel 4px above
  the bottom. The bottom matches `lv_color_darken(top, 31)` within interpolation tolerance.
- Radius: the tile's corner pixel matches the page background; the center pixel is the slot color.
- Mapping: settings and system share a color. music and camera share a color. assistant (kCool)
  differs from calendar (kDefault Tools). Two apps with the same category and no tint have equal
  colors even though their IDs differ, which proves the hash is gone.
- Press: after a simulated press the drawn tile is 44px wide and its center is darker. Release
  restores 46px. A press that turns into a page swipe also ends at 46px with the undarkened color
  once the swipe settles.
- Arrange header and collection rows use the new radius and slot color.
- Palette: for each preset, every slot meets the contrast thresholds listed under Palette Slots
  (glyph at least 3.0:1; tile at least 2.0:1 on dark presets and 3.0:1 on light), and the slots
  are pairwise distinct. Do not assert that presets differ from each other.
- All Apps overflow glyph color equals the Home theme `accent_text` for a light custom primary.
- Regression: all existing Home UI tests still pass, including tap slop, paging, long-press
  editing, theme rebuild, and `lv_obj_create` count wrappers.

Build: `. .\activate_idf.ps1 -Version v6.0.2`, then `idf.py build`.

Hardware (the authority for RGB565 appearance):

- Corners, glyph centering, and press feedback on the 320x240 panel in all four presets.
- Banding: offline estimates give 5 to 10 distinct RGB565 rows per 46px tile, with flat runs up
  to 14px (worst case Media coral). Judge whether the steps read as smooth shading. If not, apply
  the flat-plus-highlight fallback.
- Page swipe smoothness with the hardware test population (25 apps), since each page now draws up
  to 16 rounded gradients.

## Verification Status

Host (2026-10-15, WSL Debian, `tests/home_ui`): 53 tests, 0 failures in Debug and in
ASan/UBSan with leak detection. `tests/app_model`, which compiles the changed descriptor and
Registry, reports 278 tests and 0 failures under the same sanitizers. The Home suite includes the
43 earlier tests and 10 icon tests:

- Rounded vertical gradient: corner pixels match the page background, the row 4px below the top
  is at least 36px wide (a circle would be narrower), the top matches the slot color, the middle
  matches the midpoint mix, and the bottom row matches `lv_color_darken(top, 31)`.
- Mapping for all 13 built-in apps with production categories and tints, including the page-2
  `files` tile. Default-category apps with different IDs share one color.
- Press: 46px idle, 44px and darker while pressed, 46px and the idle color after release, after a
  press that turns into a two-way page swipe, and after `lv_indev_reset` during a press.
- Folder previews (flat slot colors, radius 5 / 3), collection row (28px, radius 8, gradient),
  Arrange header (40px, radius 12, gradient), and the Neutral fallback for an unregistered
  preview ID.
- All Apps overflow tile on a light custom primary: radius 14, primary gradient, and glyph and
  count in `accent_text` (black).
- Every preset palette: glyph contrast at least 3.0:1, darkest stop at least 2.0:1 (dark) /
  3.0:1 (light), pairwise RGB distance at least 40, the out-of-range fallback, and copying through
  `rodakos_theme_apply_preset_primary`.

Each of these production regressions makes at least one icon test fail: no invalidate on press,
no invalidate on input reset, circular radius, flat fill, white All Apps foreground, and an
ID-based slot.

Firmware (2026-10-15, ESP-IDF 6.0.2, `idf.py build`): passed. `build/rodakos.bin` is 0x6DC890
bytes in the 0xD50000 `ota_0` slot.

### 2026-10-15 COM3 hardware check

Package `build/packages/ota/20261015-163959`: development-signed release-test flavor, the same
flavor as the previous device package `20261014-100759`. Main image SHA-256 `6c0952a0…`. A
read-only check matched the installed bootloader, partition table and Recovery. The refresh then
wrote only `otadata` and `ota_0`, and all boot markers passed, including
`Phone desktop ready with 13 apps on 2 page(s)` and `Local boot confirmation complete`.

Visual result, reported by the operator on the panel:

- Slot colors match the App Mapping table, and the tile shapes are rounded squares.
- Press feedback works.
- Media coral (music) shows slight banding. This matches the offline estimate, which named coral
  as the worst case.

Serial window: about 11 minutes without a reset (`build/logs/icon-visual-refresh-20261015-com3.log`,
SHA-256 `d4e4518059fbe1ca…`). It covers 30 app launches, 15 Home rebuilds, and theme changes to
Blue, Light and Green. The window has no panic, abort, watchdog, or error outside the network
stack. The MQTT and TLS errors come from an unreachable cloud server (`DeviceCloud: Server
unreachable`). The last `Main health` sample shows `internal_free=23747` and
`internal_largest=10240`. There is no baseline from before the change in the same device state,
so this window does not prove that heap use is unchanged.

The operator also confirmed the Arrange header on the panel. Decision: keep the gradient. The
coral banding is slight and does not justify the flat-plus-highlight fallback for every tile.

Still open: swipe smoothness with the 25-app population.

## Icon Glyphs

The first batch kept Font Awesome Light 300 for the 30px desktop font and Regular 400 for the
20px UI font. Light is a paid Pro weight, and on the panel its 2px strokes read thin inside the
new tiles. The second batch replaced both fonts with Ionicons 8.0.13 outline glyphs (MIT).

### Choosing the set

`tools/icon_preview/` renders the production Home on a PC (LVGL 9.3, RGB565, real HomeApp and
theme) with candidate desktop fonts. It compared Phosphor Fill and Bold, Material Symbols Rounded
Fill 400 and 600, Carbon, Ionicons filled and outline, and Font Awesome Light. The operator chose
Ionicons outline at the original stroke width (32 of 512 units), with
`ion:calendar-number-outline` for Calendar.

Candidates other than plain font files come from [Iconify](https://icon-sets.iconify.design/)
JSON. `tools/icon_preview/iconify_font.py` turns SVG bodies into TrueType glyphs: it converts
shapes and transforms, expands strokes with Skia, and applies each element's fill rule before the
union, because TrueType renders with the nonzero rule only. It refuses masks, clips, opacity,
gradients and dashes.

### Production fonts

`components/rodakos_fonts/icons.json` is the source of truth: one semantic name per icon, the
Iconify name it maps to, and the subset for each font size. `tools/generate_icon_fonts.py`
generates:

- `include/rodakos_icons.h`: `ICON_*` UTF-8 macros. It replaces `font_awesome.h`; every
  `FONT_AWESOME_*` call site now uses an `ICON_*` name.
- `src/rodakos_icons_20_4.c`: all 48 glyphs, the `PhoneIconFont()` font and the fallback of
  `phone_font_12/14/18`.
- `src/rodakos_icons_30_4.c`: the 14 desktop glyphs, `PhoneIconFontLarge()`.

Codepoints start at U+F0000 (Supplementary Private Use Area-A), so they cannot collide with
Montserrat, `LV_SYMBOL_*` or the CJK subset. Names that map to the same Iconify icon share one
glyph. Music's shuffle and repeat and Recorder's stop button moved from `LV_SYMBOL_*` to
`ICON_*` so the whole UI uses one style.

Ionicons has no direct match for some old icons. The operator chose the nearest Ionicons glyph:

| Old icon | New icon | Effect |
| --- | --- | --- |
| WiFi slash | `cloud-offline-outline` | Disconnected WiFi shows a crossed-out cloud. |
| Battery 3/4, 1/4 | `battery-full`, `battery-half` | The status bar shows three fill levels instead of five. Low-battery color still comes from the 15 percent threshold. |
| Battery slash | `battery-dead-outline` | An unavailable battery shows an empty outline in the tertiary color. |
| WiFi signal strong/good/fair/weak | `cellular-outline` | One glyph; the WiFi list keeps its existing green/blue/amber/red signal colors. |
| SD card | `save-outline` | Storage rows show a disk. The Files app tile uses `folder-outline`. |
| Microchip AI | `hardware-chip-outline` | System info memory and runtime rows. |
| User robot | `chatbubble-ellipses-outline` | Assistant tile and avatar. |
| Smart (power) | `bulb-outline` | Smart and Wake no longer share the power glyph. |

The old fonts carried 135 glyphs each, of which the firmware used 50. The new fonts carry only
used glyphs. Glyph bitmap data drops from 25,835 to 6,095 bytes at 20px and from 56,086 to
4,415 bytes at 30px.

### Verification (2026-10-16)

- Host, ASan/UBSan with leak detection, every suite that compiles the icon header or fonts:
  `home_ui` 53, `assistant_ui` 15, `camera_ui` 14, `file_manager_ui` 15, `music_ui` 18,
  `navigation_ui` 13, `photos_ui` 25, `recorder_ui` 14, `app_model` 278. All pass with no
  sanitizer report. These suites use the Montserrat font fake, so they prove the macro rename,
  not the glyph pixels.
- The icon preview renders the production `rodakos_icons_30_4` desktop grid pixel-identical to
  the chosen `ion_outline` candidate. The status bar differs as intended (new 20px icons).
- ESP-IDF 6.0.2 `idf.py build` passes. `build/rodakos.bin` shrinks from 0x6DC890 to 0x6CA2A0
  bytes (75,248 bytes smaller).

On COM3 the first Ionicons build (package `20261016-011501`) drew every icon left of center.
Cause: `iconify_font.py` wrote a left side bearing of 0 to `hmtx` for every glyph. FreeType
places a TrueType outline so that its `xMin` lands on that bearing, so each icon moved to the
left edge of its advance; `lv_font_conv` then emitted `ofs_x = 0`. In the 30px font, 13 of 14
glyphs were off by 1 to 5px. The host suites did not catch it because they use a Montserrat
font fake, and the PC preview compared candidates built the same way.

Fix: the bearing now equals each glyph's `xMin`. `generate_icon_fonts.py` compares every LVGL
`ofs_x` with the designed left edge and fails on a difference above one pixel. `home_ui` adds
`icon_font_test.cc`, which links the real fonts and checks that every `ICON_*` glyph exists and
has left and right gaps within one pixel. Against the left-shifted 30px font it fails; with the
fix it passes (55 tests, 0 failures). The corrected package is `20261016-043851`, main image
SHA-256 `e48fb825…`, 0x6CA3E0 bytes.

## Assumptions

- The first batch skipped glyph replacement, image-based icons, and Appearance package
  integration. The second batch replaced the glyphs; the other two remain out of scope.
- Keeping the 8-entry palette array preserves custom theme copying with a minimal diff.
- Host framebuffer tests prove geometry, mapping, and press behavior; only hardware can judge
  RGB565 banding.

## References

- LVGL v9.3.0 sources: `lv_conf_template.h`, `src/misc/lv_grad.h`, `src/draw/lv_draw_rect.h`,
  `src/draw/sw/lv_draw_sw_fill.c`, `src/draw/sw/lv_draw_sw_grad.c`, `src/core/lv_obj.c`
  (`update_obj_state`), `src/core/lv_obj_style.c` (`lv_obj_style_state_compare`),
  `src/indev/lv_indev_scroll.c`, `src/misc/lv_color.c`.
- Dither removal in v9: https://github.com/lvgl/lvgl/issues/6013
- v8 dithering origin: https://github.com/lvgl/lvgl/pull/2872
