"""Build a source-checked LVGL LodePNG overlay; never change managed sources."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
PATCH = ROOT / "patches/lvgl/9.3.0"
REQUIRED_SOURCES = (
    "CMakeLists.txt",
    "env_support/cmake/esp.cmake",
    "src/draw/lv_draw_buf.c",
    "src/draw/lv_draw_buf.h",
    "src/draw/lv_image_dsc.h",
    "src/libs/lodepng/lodepng.c",
    "src/libs/lodepng/lodepng.h",
    "src/libs/lodepng/lv_lodepng.c",
    "src/lv_api_map_v9_0.h",
    "idf_component.yml",
    "LICENCE.txt",
)

def read(path):
    return path.read_bytes().replace(b"\r\n", b"\n")

def require(condition, message):
    if not condition:
        raise ValueError(message)

def prepare(component, lock, manifest, output):
    component, output = component.resolve(), output.resolve()
    require(component != output and component not in output.parents, "Output must be outside managed sources")
    provenance = json.loads((PATCH / "provenance.json").read_text())
    require(re.search(r'(?m)^  lvgl/lvgl: ["\']?9\.3\.0["\']?$', read(manifest).decode()), "Unreviewed LVGL project version")
    blocks = re.findall(r"(?m)^  lvgl/lvgl:\n((?:    [^\n]*\n)+)", read(lock).decode())
    require(len(blocks) == 1, "LVGL lock entry missing or ambiguous")
    for expected in ["    version: 9.3.0", "    component_hash: " + provenance["component_hash"],
                     "      type: service", "      registry_url: https://components.espressif.com/"]:
        require(expected in blocks[0].splitlines(), "Unreviewed LVGL lock identity")
    require((component / ".component_hash").read_text().strip() == provenance["component_hash"], "Unreviewed managed LVGL hash")
    source_hashes = provenance.get("source_hashes_lf")
    require(isinstance(source_hashes, dict) and set(source_hashes) == set(REQUIRED_SOURCES),
            "Incomplete reviewed LVGL source set")
    sources = {}
    for path in REQUIRED_SOURCES:
        digest = source_hashes[path]
        content = read(component / path)
        require(hashlib.sha256(content).hexdigest() == digest, "Unreviewed LVGL source: " + path)
        sources[path] = content.decode()
    source = sources["src/libs/lodepng/lodepng.c"]
    original = """    if(!state->error) {
        lv_draw_buf_t * decoded = lv_draw_buf_create_ex(image_cache_draw_buf_handlers, *w, *h, LV_COLOR_FORMAT_ARGB8888, 4 * *w);
        if(decoded) {
            *out = (unsigned char*)decoded;
            outsize = decoded->data_size;
        }
        else state->error = 83; /*alloc fail*/
    }
    if(!state->error) {
        lv_draw_buf_t * decoded = (lv_draw_buf_t *)*out;
        lodepng_memset(decoded->data, 0, outsize);
        state->error = postProcessScanlines(decoded->data, scanlines, *w, *h, &state->info_png);
    }
    lodepng_free(scanlines);"""
    require(source.count(original) == 1, "Unexpected decode allocation boundary")
    source = source.replace(original, """    /* RodakOS: RGBA8 scanlines can be unfiltered and compacted in place. Adopt
       the decompression allocation so large non-interlaced PNGs do not need a
       second full-size pixel buffer. Adam7 and color conversion keep the
       reviewed allocation path below. */
    if(!state->error && state->info_png.interlace_method == 0 &&
       state->info_png.color.colortype == LCT_RGBA && state->info_png.color.bitdepth == 8 &&
       lodepng_color_mode_equal(&state->info_raw, &state->info_png.color) &&
       scanlines_size <= 0xffffffffu &&
       lv_draw_buf_align(scanlines, LV_COLOR_FORMAT_ARGB8888) == scanlines) {
        lv_draw_buf_t * decoded;
        state->error = postProcessScanlines(scanlines, scanlines, *w, *h, &state->info_png);
        if(!state->error) {
            decoded = (lv_draw_buf_t *)lv_malloc_zeroed(sizeof(lv_draw_buf_t));
            if(decoded) {
                lv_result_t initialized = lv_draw_buf_init(decoded, *w, *h,
                                                           LV_COLOR_FORMAT_ARGB8888, 4 * *w,
                                                           scanlines, (uint32_t)scanlines_size);
                if(initialized != LV_RESULT_OK) {
                    lv_free(decoded);
                    decoded = 0;
                    state->error = 83;
                }
            }
            if(decoded) {
                decoded->header.flags = LV_IMAGE_FLAGS_MODIFIABLE | LV_IMAGE_FLAGS_ALLOCATED;
                *out = (unsigned char *)decoded;
                scanlines = 0;
            }
            else if(!state->error) state->error = 83; /*alloc fail*/
        }
    }
    else if(!state->error) {
        /* RodakOS: unfilter the original samples before RGBA8 conversion.
           RGB16/RGBA16 require 6/8 bytes per pixel, including Adam7 output. */
        unsigned raw_bpp = lodepng_get_bpp(&state->info_png.color);
        uint32_t raw_stride = (*w * raw_bpp + 7) / 8;
        uint32_t stride = raw_stride > 4 * *w ? raw_stride : 4 * *w;
        lv_draw_buf_t * decoded = lv_draw_buf_create_ex(image_cache_draw_buf_handlers, *w, *h,
                                                        LV_COLOR_FORMAT_ARGB8888, stride);
        if(decoded) {
            *out = (unsigned char*)decoded;
            outsize = decoded->data_size;
        }
        else state->error = 83; /*alloc fail*/
        if(!state->error) {
            lodepng_memset(decoded->data, 0, outsize);
            state->error = postProcessScanlines(decoded->data, scanlines, *w, *h, &state->info_png);
        }
    }
    lodepng_free(scanlines);""")
    boundary = "    /*the input filesize is a safe upper bound for the sum of idat chunks size*/"
    require(source.count(boundary) == 1, "Unexpected dimension validation boundary")
    source = source.replace(boundary, """    /* RodakOS: LVGL stores dimensions and stride in 16-bit fields. Validate
       before allocating/decompressing, including the larger raw RGB16/RGBA16 row. */
    {
        size_t raw_stride = ((size_t)*w * lodepng_get_bpp(&state->info_png.color) + 7) / 8;
        size_t rgba_stride = (size_t)4 * *w;
        size_t stride = raw_stride > rgba_stride ? raw_stride : rgba_stride;
        if(*w > 65535 || *h > 65535 || stride > 65535 ||
           (*h && stride > (0xffffffffu - LV_DRAW_BUF_ALIGN) / *h)) {
            CERROR_RETURN(state->error, 92);
        }
    }

""" + boundary)
    content = source.encode()
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_bytes() != content:
        temporary = output.with_suffix(".tmp")
        temporary.write_bytes(content)
        temporary.replace(output)
    return hashlib.sha256(content).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ["component-dir", "lock-file", "project-manifest", "output"]:
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    try:
        digest = prepare(args.component_dir, args.lock_file, args.project_manifest, args.output)
    except (OSError, ValueError) as error:
        print("LodePNG overlay refused: " + str(error), file=sys.stderr)
        return 1
    print("LodePNG overlay verified: " + digest)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
