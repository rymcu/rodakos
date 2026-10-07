from pathlib import Path
import json
import struct
import sys
import zlib

root = Path(sys.argv[1])
root.mkdir(parents=True, exist_ok=True)
width, height = 17, 9
passes = (
    (0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4),
    (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)
)

def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)

def samples(x, y, channels):
    return [((x * 17 + y * 13 + channel * 41) % 256) for channel in range(channels)]

def pixel_bytes(x, y, depth, channels):
    values = samples(x, y, channels)
    if depth == 16:
        return b''.join(struct.pack('>H', value * 257) for value in values)
    return bytes(values)

def rgba(x, y, color, channels):
    values = samples(x, y, channels)
    if color == 0:
        return bytes([values[0], values[0], values[0], 255])
    if color == 4:
        return bytes([values[0], values[0], values[0], values[1]])
    return bytes(values + ([255] if color == 2 else []))

def paeth(a, b, c):
    estimate = a + b - c
    distance_a = abs(estimate - a)
    distance_b = abs(estimate - b)
    distance_c = abs(estimate - c)
    if distance_a <= distance_b and distance_a <= distance_c:
        return a
    if distance_b <= distance_c:
        return b
    return c

def filter_row(row, previous, filter_type, bytewidth):
    filtered = bytearray(len(row))
    for index, value in enumerate(row):
        left = row[index - bytewidth] if index >= bytewidth else 0
        above = previous[index] if previous else 0
        upper_left = previous[index - bytewidth] if previous and index >= bytewidth else 0
        predictor = {
            0: 0,
            1: left,
            2: above,
            3: (left + above) // 2,
            4: paeth(left, above, upper_left),
        }[filter_type]
        filtered[index] = (value - predictor) & 0xff
    return filtered

def create(name, depth, color, channels, interlace):
    raw = bytearray()
    if interlace:
        for start_x, start_y, step_x, step_y in passes:
            for y in range(start_y, height, step_y):
                row = [pixel_bytes(x, y, depth, channels) for x in range(start_x, width, step_x)]
                if row:
                    raw.append(0)
                    raw.extend(b''.join(row))
    else:
        for y in range(height):
            raw.append(0)
            raw.extend(b''.join(pixel_bytes(x, y, depth, channels) for x in range(width)))
    expected = b''.join(rgba(x, y, color, channels) for y in range(height) for x in range(width))
    ihdr = struct.pack('>IIBBBBB', width, height, depth, color, 0, 0, interlace)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
    (root / f'{name}.png').write_bytes(png)
    (root / f'{name}.rgba').write_bytes(expected)
    return {'name': name, 'bytes': len(png), 'depth': depth, 'color': color, 'interlace': interlace}

def create_rgba8_filters():
    raw = bytearray()
    previous = b''
    for y in range(height):
        row = b''.join(pixel_bytes(x, y, 8, 4) for x in range(width))
        filter_type = y % 5
        raw.append(filter_type)
        raw.extend(filter_row(row, previous, filter_type, 4))
        previous = row
    expected = b''.join(rgba(x, y, 6, 4) for y in range(height) for x in range(width))
    ihdr = struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
    (root / 'rgba8-filters.png').write_bytes(png)
    (root / 'rgba8-filters.rgba').write_bytes(expected)
    return {'name': 'rgba8-filters', 'bytes': len(png), 'depth': 8, 'color': 6, 'interlace': 0}

cases = [
    create('gray16', 16, 0, 1, 0),
    create('ga16', 16, 4, 2, 0),
    create('rgb8', 8, 2, 3, 0),
    create('rgba8', 8, 6, 4, 0),
    create_rgba8_filters(),
    create('rgb16', 16, 2, 3, 0),
    create('rgba16', 16, 6, 4, 0),
    create('rgba8-adam7', 8, 6, 4, 1),
    create('rgb16-adam7', 16, 2, 3, 1),
    create('rgba16-adam7', 16, 6, 4, 1),
]

def create_rejected_geometry(name, rejected_width, rejected_height):
    ihdr = struct.pack('>IIBBBBB', rejected_width, rejected_height, 8, 6, 0, 0, 0)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', zlib.compress(b'')) + chunk(b'IEND', b'')
    (root / f'{name}.png').write_bytes(png)

create_rejected_geometry('oversize', 65536, 1)
create_rejected_geometry('stride-overflow', 16384, 1)
(root / 'manifest.json').write_text(json.dumps(cases, indent=2) + '\n', encoding='utf-8')
