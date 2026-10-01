# RodakOS UI fonts

This local component contains the generated LVGL font sources and the
Font Awesome symbol map required by the RodakOS UI. It is intentionally a
small, RodakOS-owned subset rather than a dependency on an assistant or
device-firmware project.

The generated sources were imported from the `78/xiaozhi-fonts` 1.6.0
component. The upstream component metadata declares MIT licensing. The
Font Awesome glyphs retain the licensing obligations of the Font Awesome
font distribution; this repository does not include the original font files.

The EDIX boot-logo subset was generated from `.tmp/EDIX.ttf`. Its font metadata
states: `Copyright © Maoweicheng. All rights reserved.` This repository does
not assert an open-source license for EDIX; confirm redistribution permission
before shipping the font in a product image.
