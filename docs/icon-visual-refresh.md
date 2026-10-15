# Home Icon Visual Refresh

Status: planned, reviewed against LVGL 9.3.0 sources. No implementation work has started.

Goal: replace hash-random circular icon tiles with a coherent icon system: category-stable colors,
46px rounded-square tiles, subtle vertical gradients, and press feedback. The desktop tile stays
inside the existing draw-event renderer, so the Home LVGL object count is unchanged.

## Scope

- Desktop Home grid icons, All Apps overflow tile, folder tiles and their previews.
- All Apps / folder collection list row icons.
- Arrange editor header preview (`home_app.cc` layout editor icon, currently a 40px circle). It
  also calls the hash helper, so it must move with the rest.
- Keep the current Font Awesome glyphs and 30px desktop icon font; no glyph redesign.
- No PNG assets, no icon pack, no Appearance package integration, no new dependency, no
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
  `LV_EVENT_RELEASED`, and `LV_EVENT_PRESS_LOST`. In `LV_EVENT_DRAW_MAIN_END`, read
  `lv_obj_has_state(obj, LV_STATE_PRESSED)` instead of a payload flag. This covers the scroll
  case, where LVGL clears the state silently and the moving tile is redrawn anyway.
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

## Assumptions

- The first batch skips glyph replacement, image-based icons, and Appearance package integration.
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
