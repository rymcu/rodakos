#!/usr/bin/env python3
"""Generates the RodakOS camera calibration chart, version 2 (A4 landscape).

Writes an A4 landscape PDF (vector, real millimetres), an SVG with the same geometry, and the
printable web page the device serves at /calibration-chart (main/phone_os/camera_chart_page.html).
Print at 100% / actual size; the 50 mm scale bar checks that the printer did not scale it.

Landscape matches the 4:3 camera frame better than portrait: the QR code sits on the left and the
slanted-edge square on the right, so the whole chart fills more of the frame and the code is larger.

The geometry must match kCameraChartV2 in main/phone_os/camera_calibration.h:
  - QR code body (no quiet zone) is 100 mm square, payload "RODAKCAL:V2".
  - A dark square of side 70 mm, rotated 5 degrees, centred 130 mm right of the QR centre.
The firmware still reads version 1 (portrait) charts printed earlier.

Usage:
  python -m venv build/caltool-venv && build/caltool-venv/Scripts/pip install segno fpdf2
  build/caltool-venv/Scripts/python tools/camera_calibration_chart.py <out-dir> [--page main/phone_os/camera_chart_page.html]
"""
import math
import pathlib
import sys

import segno

PAYLOAD = "RODAKCAL:V2"
PAGE_W, PAGE_H = 297.0, 210.0
QR_SIZE = 100.0
# The QR + square group is centred on the page: QR body x 33..133, square centre x 213.
QR_CENTER = (83.0, 108.0)
EDGE_CENTER_DX = 130.0
EDGE_CENTER_DY = 0.0
EDGE_HALF = 35.0
EDGE_ANGLE_DEG = 5.0


def qr_modules():
    qr = segno.make(PAYLOAD, error="m", micro=False, boost_error=False)
    rows = [list(row) for row in qr.matrix_iter(scale=1, border=0)]
    return qr.version, rows


def edge_square():
    cx, cy = QR_CENTER[0] + EDGE_CENTER_DX, QR_CENTER[1] + EDGE_CENTER_DY
    a = math.radians(EDGE_ANGLE_DEG)
    pts = []
    for dx, dy in ((-EDGE_HALF, -EDGE_HALF), (EDGE_HALF, -EDGE_HALF), (EDGE_HALF, EDGE_HALF), (-EDGE_HALF, EDGE_HALF)):
        pts.append((cx + dx * math.cos(a) - dy * math.sin(a), cy + dx * math.sin(a) + dy * math.cos(a)))
    return pts


def qr_origin():
    return QR_CENTER[0] - QR_SIZE / 2, QR_CENTER[1] - QR_SIZE / 2


# Orientation marks: an "L" at the top-left corner and an up arrow at the top-right corner.
L_MARK = [(14, 14), (30, 14), (30, 17.2), (17.2, 17.2), (17.2, 30), (14, 30)]
UP_ARROW = [(277, 14), (283, 23), (279, 23), (279, 31), (275, 31), (275, 23), (271, 23)]
SCALE_X, SCALE_Y = 123.5, 190.0


def write_svg(path, rows):
    n = len(rows)
    cell = QR_SIZE / n
    x0, y0 = qr_origin()
    pts = lambda poly: " ".join(f"{x:.3f},{y:.3f}" for x, y in poly)
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{PAGE_W}mm" height="{PAGE_H}mm" viewBox="0 0 {PAGE_W} {PAGE_H}">',
           f'<rect width="{PAGE_W}" height="{PAGE_H}" fill="#fff"/>',
           '<text x="36" y="22" font-size="6" font-family="sans-serif" font-weight="700">RodakOS 相机校准图 · V2</text>',
           '<text x="36" y="29" font-size="3.6" font-family="sans-serif" fill="#444">A4 横向，按实际大小（100%）打印；打印后下方刻度应正好 50 mm。</text>']
    for r, row in enumerate(rows):
        for c, dark in enumerate(row):
            if dark:
                out.append(f'<rect x="{x0 + c * cell:.3f}" y="{y0 + r * cell:.3f}" width="{cell:.3f}" height="{cell:.3f}"/>')
    out.append(f'<polygon points="{pts(edge_square())}"/>')
    out += [f'<polygon points="{pts(L_MARK)}"/>',
            f'<polygon points="{pts(UP_ARROW)}"/>',
            '<text x="277" y="37" font-size="4.2" font-family="sans-serif" font-weight="700" text-anchor="middle">上</text>',
            f'<rect x="{SCALE_X}" y="{SCALE_Y}" width="50" height="1.2"/>',
            f'<rect x="{SCALE_X}" y="{SCALE_Y - 3}" width="0.5" height="4.2"/><rect x="{SCALE_X + 49.5}" y="{SCALE_Y - 3}" width="0.5" height="4.2"/>',
            f'<text x="{SCALE_X + 25}" y="{SCALE_Y + 8}" font-size="3.4" font-family="sans-serif" text-anchor="middle">50 mm</text>',
            f'<text x="283" y="203" font-size="3" font-family="monospace" text-anchor="end" fill="#777">{PAYLOAD}</text>',
            '</svg>']
    path.write_text("\n".join(out), encoding="utf-8")


PAGE_TEMPLATE = """<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>RodakOS 相机校准图</title>
<style>
@page {{ size: A4 landscape; margin: 0; }}
:root {{ color-scheme: light; }}
body {{ margin: 0; background: #e9edf0; font: 14px/1.5 system-ui, "PingFang SC", "Microsoft YaHei", sans-serif; color: #17212a; }}
.bar {{ max-width: 297mm; margin: 16px auto; display: flex; gap: 12px; align-items: center; flex-wrap: wrap; padding: 0 12px; }}
.bar p {{ margin: 0; flex: 1 1 260px; }}
button {{ font: inherit; padding: 8px 14px; border-radius: 8px; border: 0; background: #2d7dff; color: #fff; cursor: pointer; }}
.sheet {{ width: 297mm; height: 210mm; margin: 0 auto 24px; background: #fff; box-shadow: 0 4px 18px rgb(0 0 0 / 0.15); }}
.sheet svg {{ width: 297mm; height: 210mm; display: block; }}
@media print {{ body {{ background: #fff; }} .bar {{ display: none; }} .sheet {{ margin: 0; box-shadow: none; }} }}
</style>
</head>
<body>
<div class="bar">
  <p>打印设置选 A4、横向、“实际大小”或缩放 100%，不要“适合页面”。打印后量下方刻度，应正好 50 mm。然后在设备上打开 相机 → 校准，让整张纸进入画面。</p>
  <button type="button" onclick="window.print()">打印</button>
</div>
<div class="sheet">{svg}</div>
</body>
</html>
"""


def write_page(path, svg_text):
    path.write_text(PAGE_TEMPLATE.format(svg=svg_text), encoding="utf-8")


def write_pdf(path, rows):
    from fpdf import FPDF

    pdf = FPDF(orientation="L", unit="mm", format="A4")
    pdf.set_auto_page_break(False)
    pdf.add_page()
    pdf.set_fill_color(0, 0, 0)
    # ASCII only: no font file is embedded, so the PDF prints the same everywhere.
    pdf.set_font("Helvetica", "B", 16)
    pdf.text(36, 22, "RodakOS camera calibration chart  V2")
    pdf.set_font("Helvetica", "", 9)
    pdf.text(36, 29, "A4 landscape. Print at 100% / actual size. The scale bar below must measure exactly 50 mm.")
    n = len(rows)
    cell = QR_SIZE / n
    x0, y0 = qr_origin()
    for r, row in enumerate(rows):
        for c, dark in enumerate(row):
            if dark:
                pdf.rect(x0 + c * cell, y0 + r * cell, cell, cell, style="F")
    pdf.polygon(edge_square(), style="F")
    pdf.polygon(L_MARK, style="F")
    pdf.polygon(UP_ARROW, style="F")
    pdf.set_font("Helvetica", "B", 10)
    pdf.text(273.5, 37, "UP")
    pdf.rect(SCALE_X, SCALE_Y, 50, 1.2, style="F")
    pdf.rect(SCALE_X, SCALE_Y - 3, 0.5, 4.2, style="F")
    pdf.rect(SCALE_X + 49.5, SCALE_Y - 3, 0.5, 4.2, style="F")
    pdf.set_font("Helvetica", "", 9)
    pdf.text(SCALE_X + 20, SCALE_Y + 8, "50 mm")
    pdf.set_font("Courier", "", 8)
    pdf.text(264, 203, PAYLOAD)
    pdf.output(str(path))


def main():
    args = sys.argv[1:]
    page = None
    if "--page" in args:
        index = args.index("--page")
        page = pathlib.Path(args[index + 1])
        del args[index:index + 2]
    out = pathlib.Path(args[0] if args else ".")
    out.mkdir(parents=True, exist_ok=True)
    version, rows = qr_modules()
    svg_path = out / "rodakos-camera-chart-v2.svg"
    write_svg(svg_path, rows)
    write_pdf(out / "rodakos-camera-chart-v2.pdf", rows)
    if page is not None:
        write_page(page, svg_path.read_text(encoding="utf-8"))
    print(f"QR version {version}, {len(rows)} modules, {QR_SIZE / len(rows):.2f} mm per module")


if __name__ == "__main__":
    main()
