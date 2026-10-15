# Desktop Icon Preview

Renders the production Home desktop on a PC with candidate desktop icon fonts, so glyph sets
can be compared before one replaces the production `rodakos_icons_30_4`. The set named
`current` in `iconsets.json` is the production font; the others are candidates. To adopt a
candidate, change `components/rodakos_fonts/icons.json` and run `tools/generate_icon_fonts.py`.

The program compiles the real `HomeApp`, theme, layout and production font sources against
LVGL 9.3's in-memory display. `lv_conf.h` matches the device options that affect icon pixels:
RGB565 output, complex software drawing, two gradient stops, mix rounding offset 128, and the
large text-font format. Only `PhoneIconFontLarge()` changes between candidates.

## Files

- `iconsets.json`: candidate sets. Each set names its source and one icon per built-in app:
  - A font file in a pinned npm package (for variable fonts, also the axis instance). Icons
    are hex codepoints.
  - An [Iconify](https://icon-sets.iconify.design/) set (`"iconify": true`, package
    `@iconify-json/<prefix>`). Icons are the names shown on the Iconify site, for example
    `information-filled`.
- `iconify_font.py`: builds a TrueType font from Iconify SVG bodies. It converts shapes and
  transforms, expands strokes, and applies each element's fill rule before the union, because
  TrueType renders only with the nonzero rule. It refuses masks, clips, opacity, gradients and
  dashes instead of drawing a different shape.
- `generate_fonts.py`: downloads the pinned packages, prepares each font, converts each set to a
  30px 4bpp LVGL font with `lv_font_conv`, and writes `generated/glyphs.json` with the resolved
  codepoints. It writes only into `--work-dir`.
- `icon_preview.cc`: renders, for each set, the Home page in all four presets, the first eight
  tiles at 4x, one 3x crop per app, the pressed Music tile at 6x, and page 2. Output is PPM.

## Run

Font generation needs Python with `fonttools`, `brotli` and `skia-pathops`, plus Node.js for
`npm` and `npx`. The preview builds under WSL or Linux. It does not need the ESP-IDF tree.

```bash
python tools/icon_preview/generate_fonts.py --work-dir /tmp/icon-preview
cmake -S tools/icon_preview -B ~/.cache/rodakos-icon-preview -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DRODAKOS_ICON_PREVIEW_FONT_DIR=/tmp/icon-preview/generated
cmake --build ~/.cache/rodakos-icon-preview
~/.cache/rodakos-icon-preview/rodakos_icon_preview /tmp/icon-preview/generated/glyphs.json /tmp/icon-preview/out
```

To try another Iconify set, add its `@iconify-json/<prefix>` package under `packages`, add a
set with `"source": {"package": "<name>", "iconify": true}`, and list the icon names.

## Limits

The output is RGB565 quantized like the panel input, but it does not model the ST7789 gamma,
viewing angle or the GT911 cover glass. Use it to choose a glyph set; confirm the choice on
hardware. The status bar shows no WiFi or battery because those services are absent.
