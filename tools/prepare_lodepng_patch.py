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
    original = "lv_draw_buf_t * decoded = lv_draw_buf_create_ex(image_cache_draw_buf_handlers, *w, *h, LV_COLOR_FORMAT_ARGB8888, 4 * *w);"
    require(source.count(original) == 1, "Unexpected decode allocation boundary")
    source = source.replace(original, """/* RodakOS: unfilter the original samples before RGBA8 conversion.
           RGB16/RGBA16 require 6/8 bytes per pixel, including Adam7 output. */
        unsigned raw_bpp = lodepng_get_bpp(&state->info_png.color);
        uint32_t raw_stride = (*w * raw_bpp + 7) / 8;
        uint32_t stride = raw_stride > 4 * *w ? raw_stride : 4 * *w;
        lv_draw_buf_t * decoded = lv_draw_buf_create_ex(image_cache_draw_buf_handlers, *w, *h, LV_COLOR_FORMAT_ARGB8888, stride);""")
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
