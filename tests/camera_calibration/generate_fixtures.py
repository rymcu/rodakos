# One-off generator; the PGM outputs are committed, so host tests need no QR encoder.
# Renders the current calibration chart (V2, A4 landscape) as a 320x240 camera frame would see it:
# the whole sheet in view at 1.0 px per mm, centred, on a grey background. Writes a sharp frame,
# a blurred frame and a mirror image. The chart_v1_* fixtures were made the same way from the
# portrait V1 chart and stay as they are, so the firmware keeps reading charts printed earlier.
#
# build/caltool-venv/Scripts/python tests/camera_calibration/generate_fixtures.py tests/camera_calibration/fixtures
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "tools"))
import camera_calibration_chart as chart  # noqa: E402

W, H = 320, 240
PX_PER_MM = 1.0
SS = 4  # supersampling per axis


def sheet_dark(x_mm, y_mm, rows, edge):
    if not (0 <= x_mm < chart.PAGE_W and 0 <= y_mm < chart.PAGE_H):
        return None  # off the sheet
    n = len(rows)
    x0, y0 = chart.qr_origin()
    if x0 <= x_mm < x0 + chart.QR_SIZE and y0 <= y_mm < y0 + chart.QR_SIZE:
        c = int((x_mm - x0) / chart.QR_SIZE * n)
        r = int((y_mm - y0) / chart.QR_SIZE * n)
        return bool(rows[r][c])
    for i in range(4):
        ax, ay = edge[i]
        bx, by = edge[(i + 1) % 4]
        if (bx - ax) * (y_mm - ay) - (by - ay) * (x_mm - ax) < 0:
            return False
    return True


def render(rows):
    edge = chart.edge_square()
    cx_mm, cy_mm = chart.PAGE_W / 2, chart.PAGE_H / 2
    img = []
    for py in range(H):
        row = []
        for px in range(W):
            acc = 0
            for sy in range(SS):
                for sx in range(SS):
                    x_mm = cx_mm + ((px + (sx + 0.5) / SS) - W / 2) / PX_PER_MM
                    y_mm = cy_mm + ((py + (sy + 0.5) / SS) - H / 2) / PX_PER_MM
                    d = sheet_dark(x_mm, y_mm, rows, edge)
                    acc += 110 if d is None else (30 if d else 225)
            row.append(acc // (SS * SS))
        img.append(row)
    return img


def box_blur(img, radius):
    h, w = len(img), len(img[0])
    out = [[0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            s = n = 0
            for dy in range(-radius, radius + 1):
                for dx in range(-radius, radius + 1):
                    yy, xx = y + dy, x + dx
                    if 0 <= yy < h and 0 <= xx < w:
                        s += img[yy][xx]
                        n += 1
            out[y][x] = s // n
    return out


def write_pgm(path, img):
    with open(path, "wb") as f:
        f.write(b"P5\n%d %d\n255\n" % (len(img[0]), len(img)))
        for row in img:
            f.write(bytes(row))


def main():
    out = pathlib.Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    _, rows = chart.qr_modules()
    sharp = render(rows)
    write_pgm(out / "chart_v2_sharp_320x240.pgm", sharp)
    write_pgm(out / "chart_v2_blur_320x240.pgm", box_blur(sharp, 2))
    write_pgm(out / "chart_v2_mirrored_320x240.pgm", [list(reversed(r)) for r in sharp])
    print("QR width px:", chart.QR_SIZE * PX_PER_MM)


if __name__ == "__main__":
    main()
