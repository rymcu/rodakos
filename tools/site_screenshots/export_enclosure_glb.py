"""Export the 6 real build123d STEP parts from 3d/enclosure/out/step/ into a single compact GLB.

Run with:
  uv run --python 3.12 --with build123d --with numpy python tools/site_screenshots/export_enclosure_glb.py
"""

from __future__ import annotations

import json
import struct
from pathlib import Path

import numpy as np
from build123d import import_step

ROOT = Path(__file__).resolve().parents[2]
STEP_DIR = ROOT / "3d" / "enclosure" / "out" / "step"
OUT_GLB = ROOT / "site" / "public" / "hardware" / "rodakos-enclosure.glb"

SCALE = 0.01  # mm -> cm-like Three.js scene units (106 mm -> 1.06 units)
PART_NAMES = (
    "front_shell",
    "mid_frame",
    "rear_shell",
    "camera_clamp",
    "rear_retainer",
    "desktop_cradle",
)


def tessellate_step(step_path: Path, is_desk_coords: bool) -> tuple[np.ndarray, np.ndarray]:
    shape = import_step(str(step_path))
    solids = shape.solids()
    assert len(solids) == 1, f"{step_path.name}: expected 1 solid, got {len(solids)}"
    verts_raw, tris_raw = solids[0].tessellate(tolerance=0.14, angular_tolerance=0.28)

    pts = np.array([(v.X, v.Y, v.Z) for v in verts_raw], dtype=np.float32) * SCALE
    if is_desk_coords:
        # Desk coords (X right, Y back, Z up) -> Three.js Y-up (X right, Y up, Z front)
        x = pts[:, 0].copy()
        y = pts[:, 2].copy()
        z = -pts[:, 1].copy()
        pts = np.stack([x, y, z], axis=1)

    indices = np.array(tris_raw, dtype=np.uint32).reshape(-1)
    return np.ascontiguousarray(pts, dtype=np.float32), np.ascontiguousarray(indices, dtype=np.uint32)


def build_glb(parts: list[tuple[str, np.ndarray, np.ndarray]], out_path: Path) -> None:
    bin_chunks: list[bytes] = []
    byte_offset = 0
    buffer_views: list[dict] = []
    accessors: list[dict] = []
    meshes: list[dict] = []
    nodes: list[dict] = []

    def append_bytes(data: bytes, target: int) -> int:
        nonlocal byte_offset
        pad = (4 - (len(data) % 4)) % 4
        view_idx = len(buffer_views)
        buffer_views.append(
            {
                "buffer": 0,
                "byteOffset": byte_offset,
                "byteLength": len(data),
                "target": target,
            }
        )
        bin_chunks.append(data + (b"\x00" * pad))
        byte_offset += len(data) + pad
        return view_idx

    for name, pts, indices in parts:
        pos_view = append_bytes(pts.tobytes(), 34962)  # ARRAY_BUFFER
        idx_view = append_bytes(indices.tobytes(), 34963)  # ELEMENT_ARRAY_BUFFER

        pos_acc = len(accessors)
        accessors.append(
            {
                "bufferView": pos_view,
                "componentType": 5126,  # FLOAT
                "count": int(pts.shape[0]),
                "type": "VEC3",
                "min": [float(v) for v in pts.min(axis=0)],
                "max": [float(v) for v in pts.max(axis=0)],
            }
        )

        idx_acc = len(accessors)
        accessors.append(
            {
                "bufferView": idx_view,
                "componentType": 5125,  # UNSIGNED_INT
                "count": int(indices.shape[0]),
                "type": "SCALAR",
                "min": [int(indices.min())],
                "max": [int(indices.max())],
            }
        )

        mesh_idx = len(meshes)
        meshes.append(
            {
                "name": name,
                "primitives": [
                    {
                        "attributes": {"POSITION": pos_acc},
                        "indices": idx_acc,
                        "mode": 4,  # TRIANGLES
                    }
                ],
            }
        )
        nodes.append({"name": name, "mesh": mesh_idx})

    gltf = {
        "asset": {"version": "2.0", "generator": "rodakos-enclosure-glb-exporter"},
        "scene": 0,
        "scenes": [{"name": "RodakOSEnclosure", "nodes": list(range(len(nodes)))}],
        "nodes": nodes,
        "meshes": meshes,
        "accessors": accessors,
        "bufferViews": buffer_views,
        "buffers": [{"byteLength": byte_offset}],
    }

    json_bytes = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    json_pad = (4 - (len(json_bytes) % 4)) % 4
    json_bytes += b" " * json_pad

    bin_bytes = b"".join(bin_chunks)
    total_len = 12 + 8 + len(json_bytes) + 8 + len(bin_bytes)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total_len))  # glTF header
        f.write(struct.pack("<II", len(json_bytes), 0x4E4F534A))  # JSON chunk
        f.write(json_bytes)
        f.write(struct.pack("<II", len(bin_bytes), 0x004E4942))  # BIN chunk
        f.write(bin_bytes)


def main() -> None:
    exported: list[tuple[str, np.ndarray, np.ndarray]] = []
    total_tris = 0
    for name in PART_NAMES:
        step_path = STEP_DIR / f"{name}.step"
        pts, idx = tessellate_step(step_path, is_desk_coords=(name == "desktop_cradle"))
        tris = len(idx) // 3
        total_tris += tris
        print(f"[glb] {name:16s}: {len(pts):6d} verts, {tris:6d} tris")
        exported.append((name, pts, idx))

    build_glb(exported, OUT_GLB)
    size_kb = OUT_GLB.stat().st_size / 1024.0
    print(f"Wrote {OUT_GLB} ({size_kb:.1f} KiB, {total_tris} triangles total)")


if __name__ == "__main__":
    main()
