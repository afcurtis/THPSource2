"""Primitive-stream oracle files (`OPENTONY_VIDEO_TRACE`, docs/HAL.md "Video").

opentony.dll records the game's Direct3D 7 device calls per frame
(src/hooks/hal/video_trace.cpp):

- ``frame_NNNNN.otvs``: "OTVS", u32 version (bit 31 = truncated), u32 frame,
  u32 record count, then records ``u16 type, u16 flags, u32 size, payload``;
- ``frame_NNNNN.ottx`` / ``textures/<hash>.ottx``: "OTTX", u32 version,
  width, height, bits, red/green/blue/alpha masks, pixel-format flags,
  2 reserved words, then top-down rows of ``width * bits / 8`` bytes.

This module decodes both, summarizes streams, and writes PNG screenshots.
"""

from __future__ import annotations

import dataclasses
import struct
import zlib
from collections import Counter
from collections.abc import Iterator
from pathlib import Path
from typing import Any

RECORD_NAMES = {
    1: "begin_scene",
    2: "end_scene",
    3: "clear",
    4: "render_state",
    5: "stage_state",
    6: "texture",
    7: "draw",
    8: "draw_indexed",
    9: "viewport",
}
FROM_VERTEX_BUFFER = 1
SNAPSHOT = 2  # state read back at the start of the frame

# D3DPRIMITIVETYPE
PRIMITIVES = {1: "points", 2: "lines", 3: "line_strip", 4: "triangles", 5: "triangle_strip", 6: "triangle_fan"}

# The D3DRENDERSTATETYPE values THPS2 sets (d3dtypes.h numbering).
RENDER_STATES = {
    1: "texture_handle", 2: "antialias", 3: "texture_address", 4: "texture_perspective", 5: "wrap_u",
    6: "wrap_v", 7: "z_enable", 8: "fill_mode", 9: "shade_mode", 10: "line_pattern", 14: "z_write_enable",
    15: "alpha_test_enable", 16: "last_pixel", 17: "texture_mag", 18: "texture_min", 19: "src_blend",
    20: "dest_blend", 21: "texture_map_blend", 22: "cull_mode", 23: "z_func", 24: "alpha_ref",
    25: "alpha_func", 26: "dither_enable", 27: "alpha_blend_enable", 28: "fog_enable",
    29: "specular_enable", 34: "fog_color", 35: "fog_table_mode", 36: "fog_start", 37: "fog_end",
    38: "fog_density", 40: "edge_antialias", 41: "color_key_enable", 47: "z_bias", 48: "range_fog_enable",
    136: "lighting", 137: "extents", 138: "ambient", 139: "fog_vertex_mode", 140: "color_vertex",
}
# D3DTEXTURESTAGESTATETYPE
STAGE_STATES = {
    1: "color_op", 2: "color_arg1", 3: "color_arg2", 4: "alpha_op", 5: "alpha_arg1", 6: "alpha_arg2",
    7: "bump00", 11: "tex_coord_index", 12: "address", 13: "address_u", 14: "address_v", 15: "border",
    16: "mag_filter", 17: "min_filter", 18: "mip_filter", 19: "mip_bias", 20: "max_mip", 21: "max_aniso",
    24: "transform_flags",
}


class VideoTraceError(RuntimeError):
    pass


@dataclasses.dataclass
class Record:
    kind: str
    flags: int
    fields: dict[str, Any]


@dataclasses.dataclass
class Frame:
    frame: int
    truncated: bool
    records: list[Record]


@dataclasses.dataclass
class Image:
    width: int
    height: int
    bits: int
    masks: tuple[int, int, int, int]
    format_flags: int
    pixels: bytes  # top-down rows, width * bits / 8 bytes each

    def rgba(self) -> bytes:
        """8-bit RGBA, expanding each channel mask to 0..255."""

        if self.bits not in (16, 24, 32):
            raise VideoTraceError(f"unsupported pixel depth {self.bits}")
        step = self.bits // 8
        shifts = []
        for mask in self.masks:
            if mask == 0:
                shifts.append(None)
                continue
            low = (mask & -mask).bit_length() - 1
            shifts.append((mask, low, mask >> low))
        out = bytearray(self.width * self.height * 4)
        data = self.pixels
        cursor = 0
        for index in range(0, len(data) - step + 1, step):
            value = int.from_bytes(data[index:index + step], "little")
            for channel, shift in enumerate(shifts):
                if shift is None:
                    out[cursor + channel] = 255 if channel == 3 else 0
                else:
                    mask, low, top = shift
                    out[cursor + channel] = ((value & mask) >> low) * 255 // top
            cursor += 4
        return bytes(out)


def read_image(path: str | Path) -> Image:
    data = Path(path).read_bytes()
    if data[:4] != b"OTTX":
        raise VideoTraceError(f"{path}: not an OTTX image")
    (_version, width, height, bits, red, green, blue, alpha, flags, _r0, _r1) = struct.unpack_from("<11I", data, 4)
    pixels = data[48:48 + width * height * (bits // 8)]
    return Image(width, height, bits, (red, green, blue, alpha), flags, pixels)


def write_png(path: str | Path, width: int, height: int, rgba: bytes) -> None:
    def chunk(tag: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    stride = width * 4
    raw = b"".join(b"\0" + rgba[row * stride:(row + 1) * stride] for row in range(height))
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    Path(path).write_bytes(png)


def fvf_stride(fvf: int) -> int:
    position = fvf & 0x00E
    stride = {0x002: 12, 0x004: 16}.get(position, 12 + 4 * ((position - 0x004) // 2) if position >= 0x006 else 0)
    stride += 12 if fvf & 0x010 else 0
    stride += 4 if fvf & 0x020 else 0
    stride += 4 if fvf & 0x040 else 0
    stride += 4 if fvf & 0x080 else 0
    return stride + ((fvf >> 8) & 0xF) * 8


def _vertices(data: bytes, offset: int, count: int, stride: int, fvf: int) -> list[dict[str, Any]]:
    """Pre-transformed vertices (D3DFVF_XYZRHW...), in OtHalVertex terms."""

    vertices = []
    for index in range(count):
        base = offset + index * stride
        vertex: dict[str, Any] = {}
        cursor = base
        if fvf & 0x00E == 0x004:
            vertex["x"], vertex["y"], vertex["z"], vertex["rhw"] = struct.unpack_from("<4f", data, cursor)
            cursor += 16
        else:
            vertex["xyz"] = struct.unpack_from("<3f", data, cursor)
            cursor += 12
        if fvf & 0x010:
            cursor += 12
        if fvf & 0x020:
            cursor += 4
        if fvf & 0x040:
            vertex["color"] = struct.unpack_from("<I", data, cursor)[0]
            cursor += 4
        if fvf & 0x080:
            vertex["specular"] = struct.unpack_from("<I", data, cursor)[0]
            cursor += 4
        if (fvf >> 8) & 0xF:
            vertex["u"], vertex["v"] = struct.unpack_from("<2f", data, cursor)
        vertices.append(vertex)
    return vertices


def _decode(kind: int, flags: int, payload: bytes) -> Record:
    name = RECORD_NAMES.get(kind, f"unknown_{kind}")
    fields: dict[str, Any] = {}
    if kind == 3:
        count, clear_flags, color = struct.unpack_from("<3I", payload)
        (z,) = struct.unpack_from("<f", payload, 12)
        (stencil,) = struct.unpack_from("<I", payload, 16)
        rects = [struct.unpack_from("<4i", payload, 20 + 16 * i) for i in range((len(payload) - 20) // 16)]
        fields = {"count": count, "flags": clear_flags, "color": color, "z": z, "stencil": stencil, "rects": rects}
    elif kind == 4:
        state, value = struct.unpack_from("<2I", payload)
        fields = {"state": state, "name": RENDER_STATES.get(state, f"rs{state}"), "value": value}
    elif kind == 5:
        stage, state, value = struct.unpack_from("<3I", payload)
        fields = {"stage": stage, "state": state, "name": STAGE_STATES.get(state, f"tss{state}"), "value": value}
    elif kind == 6:
        stage, status = struct.unpack_from("<2I", payload)
        (texture_hash,) = struct.unpack_from("<Q", payload, 8)
        fields = {"stage": stage, "status": ("none", "hashed", "unreadable")[min(status, 2)], "hash": texture_hash,
                  "surface": struct.unpack_from("<I", payload, 16)[0] if len(payload) >= 20 else 0}
    elif kind in (7, 8):
        if kind == 7:
            primitive, fvf, vertex_count, index_count, stride = struct.unpack_from("<5I", payload)
            index_count = 0
            offset = 20
        else:
            primitive, fvf, vertex_count, index_count, _flags, stride = struct.unpack_from("<6I", payload)
            offset = 24
        vertex_bytes = stride * vertex_count if fvf else 0
        fields = {
            "primitive": PRIMITIVES.get(primitive, str(primitive)),
            "fvf": fvf,
            "vertex_count": vertex_count,
            "index_count": index_count,
            "stride": stride,
            "vertices": _vertices(payload, offset, vertex_count, stride, fvf) if vertex_bytes else [],
            "indices": list(struct.unpack_from(f"<{index_count}H", payload, offset + vertex_bytes)) if index_count else [],
            "vertex_buffer": bool(flags & FROM_VERTEX_BUFFER),
        }
    elif kind == 9:
        x, y, width, height = struct.unpack_from("<4I", payload)
        min_z, max_z = struct.unpack_from("<2f", payload, 16)
        fields = {"x": x, "y": y, "width": width, "height": height, "min_z": min_z, "max_z": max_z}
    return Record(name, flags, fields)


def read_frame(path: str | Path) -> Frame:
    data = Path(path).read_bytes()
    if data[:4] != b"OTVS":
        raise VideoTraceError(f"{path}: not an OTVS stream")
    version, frame, count = struct.unpack_from("<3I", data, 4)
    if version & 0x7FFFFFFF != 1:
        raise VideoTraceError(f"{path}: unsupported version {version & 0x7FFFFFFF}")
    records = []
    offset = 16
    for _ in range(count):
        kind, flags, size = struct.unpack_from("<HHI", data, offset)
        offset += 8
        records.append(_decode(kind, flags, data[offset:offset + size]))
        offset += size
    if offset != len(data):
        raise VideoTraceError(f"{path}: {len(data) - offset} trailing bytes")
    return Frame(frame, bool(version & 0x80000000), records)


def frames(directory: str | Path) -> Iterator[Path]:
    yield from sorted(Path(directory).glob("frame_*.otvs"))


def summarize(frame: Frame) -> dict[str, Any]:
    kinds = Counter(record.kind for record in frame.records)
    primitives: Counter[str] = Counter()
    fvfs: Counter[int] = Counter()
    states: dict[str, Counter[int]] = {}
    textures = set()
    vertices = 0
    for record in frame.records:
        if record.kind in ("draw", "draw_indexed"):
            primitives[record.fields["primitive"]] += 1
            fvfs[record.fields["fvf"]] += 1
            vertices += record.fields["vertex_count"]
        elif record.kind in ("render_state", "stage_state"):
            key = record.fields["name"] if record.kind == "render_state" else f"stage{record.fields['stage']}.{record.fields['name']}"
            states.setdefault(key, Counter())[record.fields["value"]] += 1
        elif record.kind == "texture" and record.fields["hash"]:
            textures.add(record.fields["hash"])
    return {
        "frame": frame.frame,
        "truncated": frame.truncated,
        "records": dict(kinds),
        "primitives": dict(primitives),
        "fvf": {f"0x{fvf:03x}": count for fvf, count in fvfs.items()},
        "vertices": vertices,
        "textures": len(textures),
        "states": {name: {str(value): count for value, count in counter.items()} for name, counter in sorted(states.items())},
    }


# ------------------------------------------------------------------ image oracle

# Pass marks for `tony hal video replay` (docs/HAL.md, "Video"): measured on
# 5-6-5 values, the precision of retail's render target. Retail renders
# through wined3d/OpenGL and the HAL through its own GL 3.3 shaders, so exact
# equality is not expected; edges and filtering differ by a few levels.
MIN_PSNR = 30.0
MIN_WITHIN_TOLERANCE = 0.95
TOLERANCE = 16  # per 8-bit channel


def _rgb565(rgba: bytes) -> list[tuple[int, int, int]]:
    """8-bit RGB quantized to 5-6-5 and expanded back as retail's target stores it."""

    return [
        (((rgba[i] >> 3) * 255) // 31, ((rgba[i + 1] >> 2) * 255) // 63, ((rgba[i + 2] >> 3) * 255) // 31)
        for i in range(0, len(rgba), 4)
    ]


def compare_images(retail: Image, replay: Image, diff_png: str | Path | None = None) -> dict[str, Any]:
    """Per-pixel comparison at the retail target's precision."""

    import math

    if (retail.width, retail.height) != (replay.width, replay.height):
        raise VideoTraceError(f"size differs: {retail.width}x{retail.height} vs {replay.width}x{replay.height}")
    left = _rgb565(retail.rgba())
    right = _rgb565(replay.rgba())
    squared = 0
    absolute = 0
    within = 0
    worst = 0
    diff = bytearray(len(left) * 4) if diff_png else None
    for index, (a, b) in enumerate(zip(left, right)):
        dr, dg, db = abs(a[0] - b[0]), abs(a[1] - b[1]), abs(a[2] - b[2])
        largest = max(dr, dg, db)
        worst = max(worst, largest)
        within += largest <= TOLERANCE
        squared += dr * dr + dg * dg + db * db
        absolute += dr + dg + db
        if diff is not None:
            level = min(255, largest * 4)
            diff[index * 4:index * 4 + 4] = bytes((level, level, level, 255))
    samples = len(left) * 3
    mse = squared / samples
    result = {
        "pixels": len(left),
        "psnr": round(99.0 if mse == 0 else 10 * math.log10(255 * 255 / mse), 2),
        "mean_abs": round(absolute / samples, 3),
        "within_tolerance": round(within / len(left), 4),
        "max_difference": worst,
    }
    result["pass"] = result["psnr"] >= MIN_PSNR and result["within_tolerance"] >= MIN_WITHIN_TOLERANCE
    if diff is not None:
        write_png(diff_png, retail.width, retail.height, bytes(diff))
    return result
