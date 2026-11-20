# One-off generator; the PGM outputs are committed, so host tests need no QR encoder.
import sys, segno
out = sys.argv[1]
payload = "RODAK:V:1;S:HomeNet;T:WPA;P:s3cret-pass;U:https://ic.example.com;;"
def write_pgm(path, rows):
    h = len(rows); w = len(rows[0])
    with open(path, "wb") as f:
        f.write(b"P5\n%d %d\n255\n" % (w, h))
        for r in rows: f.write(bytes(r))
qr = segno.make(payload, error="m", micro=False, boost_error=False)
print("version", qr.version, "error", qr.error)
matrix = [[0 if m else 255 for m in row] for row in qr.matrix_iter(scale=4, border=4)]
write_pgm(f"{out}/rodak_payload_v{qr.version}_scale4.pgm", matrix)
# Same code on a 320x240 grey background, as a camera preview would see it.
W, H = 320, 240
canvas = [[170]*W for _ in range(H)]
qr3 = [[0 if m else 255 for m in row] for row in qr.matrix_iter(scale=3, border=4)]
oy = (H-len(qr3))//2; ox = (W-len(qr3[0]))//2
for y,row in enumerate(qr3):
    canvas[oy+y][ox:ox+len(row)] = row
write_pgm(f"{out}/rodak_payload_320x240.pgm", canvas)
print(len(qr3), "px code in 320x240")
