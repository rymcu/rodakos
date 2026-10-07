# LVGL 9.3.0 LodePNG decode overlay

RodakOS keeps the component-manager resolution at `lvgl/lvgl` 9.3.0. Its bundled LodePNG fork
creates an ARGB8888 draw buffer before removing PNG filters. That buffer is four bytes per pixel,
while 16-bit RGB and RGBA input needs six or eight bytes per pixel until color conversion. The
unfilter step can therefore write beyond the allocation.

The build-local overlay allocates the larger of the raw PNG row and the final RGBA8 row, validates
the 16-bit LVGL width/height/stride fields before allocation, then preserves the existing conversion
to a compact ARGB8888 draw buffer. It supports normal and Adam7 input instead of rejecting all
16-bit color PNGs.

`tools/prepare_lodepng_patch.py` verifies the component-manager pin, package hash, and an exact
fail-closed set of normalized hashes covering the decoder, draw-buffer implementation and API map,
the LVGL build entry and ESP-IDF source enumeration, metadata, and MIT license. It writes only under the build
directory; managed component files remain unchanged. `cmake/lodepng_patch.cmake` replaces exactly
one source entry on the resolved LVGL target for both ESP-IDF and host tests.

`tests/lodepng_decode` generates valid fixtures with the Python standard library and verifies 8/16
bit grayscale, grayscale-alpha, RGB and RGBA, non-interlaced RGBA filters 0-4, RGBA8 and 16-bit
Adam7 RGB/RGBA, exact output pixels, and both oversized-dimension and oversized-stride rejection
under ASan/UBSan.
