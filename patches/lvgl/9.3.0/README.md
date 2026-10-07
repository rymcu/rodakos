# LVGL 9.3.0 LodePNG decode overlay

RodakOS keeps the component-manager resolution at `lvgl/lvgl` 9.3.0. Its bundled LodePNG fork
creates an ARGB8888 draw buffer before removing PNG filters. That buffer is four bytes per pixel,
while 16-bit RGB and RGBA input needs six or eight bytes per pixel until color conversion. The
unfilter step can therefore write beyond the allocation.

The build-local overlay allocates the larger of the raw PNG row and the final RGBA8 row, validates
the 16-bit LVGL width/height/stride fields before allocation, then preserves the existing conversion
to a compact ARGB8888 draw buffer. It supports normal and Adam7 input instead of rejecting all
16-bit color PNGs.

For non-interlaced RGBA8, the overlay adopts the unfiltered decompression allocation as the
draw buffer. Its predicted inflate reserve includes the 260 spare bytes required by the pinned
Huffman loop, including after an end symbol. Both size additions are checked for overflow; a
failed reserve returns error 83 immediately. This prevents a valid image from triggering the
vector's approximately 50% capacity growth just before completion. The source check also verifies
the exact upstream 260-byte requirement before applying this transformation.

`tools/prepare_lodepng_patch.py` verifies the component-manager pin, package hash, and an exact
fail-closed set of normalized hashes covering the decoder, draw-buffer implementation and API map,
the LVGL build entry and ESP-IDF source enumeration, metadata, and MIT license. It writes only under the build
directory; managed component files remain unchanged. `cmake/lodepng_patch.cmake` replaces exactly
one source entry on the resolved LVGL target for both ESP-IDF and host tests.

`tests/lodepng_decode` generates valid fixtures with the Python standard library and verifies 8/16
bit grayscale, grayscale-alpha, RGB and RGBA, non-interlaced RGBA filters 0-4, RGBA8 and 16-bit
Adam7 RGB/RGBA, exact output pixels, and both oversized-dimension and oversized-stride rejection
under ASan/UBSan.
The 471x423 RGBA8 fixture additionally checks a 1,081,344-byte contiguous-allocation ceiling and
real allocator rejection at IDAT, inflate reserve and adopted-buffer descriptor boundaries.
Every rejection must release the decoder's allocations, and the next decode must recover with
exact pixels. These checks are host evidence, not a device fragmentation or arbitrary-OOM claim.
