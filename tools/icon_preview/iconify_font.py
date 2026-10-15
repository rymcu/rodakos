"""Build a monochrome TrueType font from Iconify JSON icons.

Iconify publishes each icon set as JSON holding SVG bodies. lv_font_conv needs a font file, so
this module turns selected icons into TrueType glyphs at private-use codepoints.

Each SVG element becomes one outline. Shapes (circle, rect, ellipse, line, polyline, polygon)
and element transforms go through fontTools' PathBuilder. Strokes are expanded with Skia
(pathops), and every element is filled under its own fill rule before the union, because
TrueType renderers only fill with the nonzero rule. Bodies that use features this cannot
represent (masks, clip paths, opacity, gradients, nested groups with styles) are refused, so a
preview never silently shows a different shape than the icon set.
"""

import json
import re
import xml.etree.ElementTree as ElementTree
from pathlib import Path

from fontTools.fontBuilder import FontBuilder
from fontTools.misc.transform import Transform
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.svgLib.path import parse_path
from fontTools.svgLib.path.shapes import PathBuilder
from fontTools.svgLib.path.shapes import _transform as _svg_transform

UNITS_PER_EM = 1000
ASCENT = 875
FIRST_CODEPOINT = 0xF0000
UNSUPPORTED = re.compile(
    r"<(mask|clipPath|linearGradient|radialGradient|pattern|use|image|text)\b|"
    r"\b(opacity|fill-opacity|stroke-opacity|mask|clip-path|stroke-dasharray)=")
SHAPE_TAGS = {"path", "circle", "rect", "ellipse", "line", "polyline", "polygon"}


class UnsupportedIcon(ValueError):
    pass


def load_icon_set(icons_json: Path) -> dict:
    return json.loads(icons_json.read_text(encoding="utf-8"))


def resolve_icon(data: dict, name: str) -> dict:
    icon = data["icons"].get(name)
    if icon is not None:
        return icon
    alias = data.get("aliases", {}).get(name)
    if alias is None:
        raise KeyError(f"icon '{name}' is not in set '{data.get('prefix')}'")
    parent = resolve_icon(data, alias["parent"])
    return {**parent, **{key: value for key, value in alias.items() if key != "parent"}}


def _inherited(element, parents, attribute, default=None):
    if attribute in element.attrib:
        return element.attrib[attribute]
    for parent in reversed(parents):
        if attribute in parent.attrib:
            return parent.attrib[attribute]
    return default


def _element_transform(element, parents) -> Transform:
    combined = Transform()
    for node in [*parents, element]:
        value = node.attrib.get("transform")
        if value:
            combined = combined.transform(_svg_transform(value))
    return combined


def _outline(element) -> str:
    builder = PathBuilder()
    if not builder.add_path_from_element(element):
        raise UnsupportedIcon(f"cannot convert <{element.tag}>")
    return builder.paths[-1]


def _walk(element, parents):
    tag = element.tag.split("}")[-1]
    if tag in SHAPE_TAGS:
        yield element, parents
    elif tag == "g":
        for child in element:
            yield from _walk(child, [*parents, element])
    elif tag not in {"title", "desc"}:
        raise UnsupportedIcon(f"unsupported element <{tag}>")


def icon_outline(body: str, width: float, height: float, stroke_width: float | None = None):
    """Returns a pathops.Path in font units for one Iconify icon body.

    stroke_width, in the icon's own SVG units, replaces every stroke-width in the body. Outline
    sets are drawn for 24px and up; at 30px on the panel a heavier stroke can read better.
    """
    import pathops

    if UNSUPPORTED.search(body):
        raise UnsupportedIcon("uses masks, clips, opacity, gradients or dashes")
    root = ElementTree.fromstring(f'<svg xmlns="http://www.w3.org/2000/svg">{body}</svg>')
    # SVG y grows downward; TrueType y grows upward with the baseline ASCENT below the top.
    scale = UNITS_PER_EM / max(width, height)
    to_font = Transform(scale, 0, 0, -scale, 0, ASCENT)
    combined = pathops.Path()
    for element, parents in (pair for child in root for pair in _walk(child, [])):
        transform = to_font.transform(_element_transform(element, parents))
        part = pathops.Path()
        parse_path(_outline(element), TransformPen(part.getPen(), transform))
        fill = _inherited(element, parents, "fill", "currentColor")
        stroke = _inherited(element, parents, "stroke", "none")
        if fill != "none":
            filled = pathops.Path(part)
            rule = _inherited(element, parents, "fill-rule", "nonzero")
            filled.fillType = pathops.FillType.EVEN_ODD if rule == "evenodd" \
                else pathops.FillType.WINDING
            combined = pathops.op(combined, filled, pathops.PathOp.UNION)
        if stroke != "none":
            width_value = stroke_width if stroke_width is not None \
                else float(_inherited(element, parents, "stroke-width", "1"))
            caps = {"butt": pathops.LineCap.BUTT_CAP, "round": pathops.LineCap.ROUND_CAP,
                    "square": pathops.LineCap.SQUARE_CAP}
            joins = {"miter": pathops.LineJoin.MITER_JOIN, "round": pathops.LineJoin.ROUND_JOIN,
                     "bevel": pathops.LineJoin.BEVEL_JOIN}
            cap = caps[_inherited(element, parents, "stroke-linecap", "butt")]
            join = joins[_inherited(element, parents, "stroke-linejoin", "miter")]
            miter = float(_inherited(element, parents, "stroke-miterlimit", "4"))
            # Stroke in SVG units, then scale; the transform has no skew, so widths stay uniform.
            unscaled = pathops.Path()
            inverse = to_font.inverse()
            part.draw(TransformPen(unscaled.getPen(), inverse))
            unscaled.stroke(width_value, cap, join, miter)
            # Round caps and joins come back as conics, which the pen protocol cannot carry.
            unscaled.convertConicsToQuads()
            stroked = pathops.Path()
            unscaled.draw(TransformPen(stroked.getPen(), to_font))
            stroked.fillType = pathops.FillType.WINDING
            combined = pathops.op(combined, stroked, pathops.PathOp.UNION)
    return combined


def build_font(icon_sources: list[tuple[dict, str]], output: Path,
               stroke_width: float | None = None) -> dict[str, int]:
    """Writes a TTF holding one glyph per (icon set data, icon name); returns name -> codepoint."""
    glyphs = {".notdef": TTGlyphPen(None).glyph()}
    metrics = {".notdef": (UNITS_PER_EM, 0)}
    cmap = {}
    codepoints = {}
    for index, (data, name) in enumerate(icon_sources):
        icon = resolve_icon(data, name)
        outline = icon_outline(icon["body"], icon.get("width", data.get("width", 16)),
                               icon.get("height", data.get("height", 16)), stroke_width)
        pen = TTGlyphPen(None)
        outline.draw(Cu2QuPen(pen, max_err=0.5, reverse_direction=True))
        codepoint = FIRST_CODEPOINT + index
        glyph_name = f"u{codepoint:05X}"
        glyphs[glyph_name] = pen.glyph()
        cmap[codepoint] = glyph_name
        codepoints[f"{data['prefix']}:{name}"] = codepoint
    builder = FontBuilder(UNITS_PER_EM, isTTF=True)
    builder.setupGlyphOrder(list(glyphs))
    builder.setupCharacterMap(cmap)
    builder.setupGlyf(glyphs)
    # FreeType places a TrueType outline so that its xMin lands at the hmtx left side bearing.
    # The bearing must equal the real xMin, or every glyph is pushed to the left edge of its
    # advance instead of keeping the icon's designed position in the em square.
    glyf = builder.font["glyf"]
    for glyph_name in glyphs:
        glyph = glyf[glyph_name]
        metrics[glyph_name] = (UNITS_PER_EM, glyph.xMin if glyph.numberOfContours else 0)
    builder.setupHorizontalMetrics(metrics)
    builder.setupHorizontalHeader(ascent=ASCENT, descent=ASCENT - UNITS_PER_EM)
    builder.setupNameTable({"familyName": "RodakosIconPreview", "styleName": "Regular"})
    builder.setupOS2(sTypoAscender=ASCENT, sTypoDescender=ASCENT - UNITS_PER_EM,
                     usWinAscent=ASCENT, usWinDescent=UNITS_PER_EM - ASCENT)
    builder.setupPost()
    builder.save(output)
    return codepoints


def check_lvgl_font_placement(font_path: Path, lvgl_source: Path, size_px: int) -> list[str]:
    """Returns glyphs whose LVGL ofs_x differs from the icon's designed left edge.

    The designed left edge is the outline's xMin in the TTF, scaled to size_px. lv_font_conv
    rounds to whole pixels, so a difference above one pixel means the side bearing was lost.
    """
    from fontTools.ttLib import TTFont

    font = TTFont(font_path)
    glyf = font["glyf"]
    order = sorted(font.getBestCmap().items())
    source = lvgl_source.read_text(encoding="utf-8")
    offsets = [int(value) for value in re.findall(r"\.ofs_x = (-?\d+)", source)][1:]
    codepoints = []
    for cmap in re.finditer(r"\.range_start = (\d+), \.range_length = (\d+).*?"
                            r"\.unicode_list = (\w+)", source, re.S):
        start, length, unicode_list = int(cmap[1]), int(cmap[2]), cmap[3]
        if unicode_list == "NULL":
            codepoints += [start + step for step in range(length)]
        else:
            body = re.search(rf"{unicode_list}\[\] = \{{(.*?)\}}", source, re.S)[1]
            codepoints += [start + int(value, 16) for value in re.findall(r"0x[0-9a-fA-F]+", body)]
    names = dict(order)
    problems = []
    for codepoint, offset in zip(codepoints, offsets):
        glyph = glyf[names[codepoint]]
        expected = glyph.xMin * size_px / UNITS_PER_EM
        if abs(offset - expected) > 1.0:
            problems.append(f"U+{codepoint:X}: ofs_x={offset}, designed left edge {expected:.1f}px")
    return problems
