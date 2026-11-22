"""The static data of another PC build of THawk2.exe, rebuilt at the canonical build's addresses.

The standalone reads the canonical build's initialised `.rdata` / `.data` / `.rsrc` at their canonical addresses
(tony/static_data.py). The original 2000 retail release (re/xref/config.yml `variants.original_2000`, the no-CD build PARTYMOD and
the Cumulative Patch target) holds the same tables and strings, laid out a little differently. A *recipe*
(`re/xref/<variant>_static.yml`, written by `tony xref static-recipe`) turns that executable's data into the canonical image:

1. ``copy``: byte ranges of the variant's data sections, each placed at its canonical address;
2. ``translate``: every 4-byte-aligned word of a copied range whose value is an address inside the variant's image is moved to the
   canonical address (``x + delta`` of the last breakpoint at or below ``x``);
3. ``zero``: canonical ranges that are zero, or masked (``masked``: bytes the standalone never reads; see the recipe);
4. ``bytes``: what is left, written last.

The recipe holds addresses, the canonical import tables (Win32 names and RVAs, which the linker ordered differently) and the few
bytes the variant does not have (about 240: the CD check's strings, a debug format, the build date); every table and string the game
uses comes from the player's own executable. The result is checked against ``image_sha256``: the canonical image with the masked
ranges zeroed.
"""

from __future__ import annotations

import bisect
import hashlib
import json
import struct
from dataclasses import dataclass
from difflib import SequenceMatcher
from pathlib import Path

from .common import load_yaml, resolve

IMAGE_BASE = 0x00400000
_CODE = 0x00000020
_PLACEHOLDER = b"\xfe\xed\xfa\xce"  # an in-image address while aligning: both builds' pointers compare equal


@dataclass
class Section:
    name: str
    va: int
    data: bytes  # initialised bytes: min(raw size, virtual size)
    virtual_size: int
    code: bool


@dataclass
class Executable:
    sha256: str
    timestamp: int
    size_of_image: int
    sections: list[Section]
    directories: list[tuple[int, int]]  # the optional header's data directories: (rva, size)

    def data_sections(self) -> list[Section]:
        return [s for s in self.sections if not s.code]

    def section(self, name: str) -> Section:
        return next(s for s in self.sections if s.name == name)

    def in_image(self, value: int) -> bool:
        return IMAGE_BASE <= value < IMAGE_BASE + self.size_of_image


def read_executable(path: Path) -> Executable:
    blob = path.read_bytes()
    if len(blob) < 0x40 or blob[:2] != b"MZ":
        raise ValueError(f"{path} is not a PE file")
    pe = struct.unpack_from("<I", blob, 0x3C)[0]
    if blob[pe:pe + 4] != b"PE\0\0":
        raise ValueError(f"{path} is not a PE file")
    count = struct.unpack_from("<H", blob, pe + 6)[0]
    timestamp = struct.unpack_from("<I", blob, pe + 8)[0]
    optional = struct.unpack_from("<H", blob, pe + 20)[0]
    base = struct.unpack_from("<I", blob, pe + 24 + 28)[0]
    size_of_image = struct.unpack_from("<I", blob, pe + 24 + 56)[0]
    if base != IMAGE_BASE:
        raise ValueError(f"{path}: image base {base:#x}, expected {IMAGE_BASE:#x}")
    sections = []
    for k in range(count):
        at = pe + 24 + optional + 40 * k
        name, vsize, rva, rsize, raw = struct.unpack_from("<8sIIII", blob, at)
        flags = struct.unpack_from("<I", blob, at + 36)[0]
        n = min(rsize, vsize)
        sections.append(Section(name.rstrip(b"\0").decode("ascii", "replace"), base + rva, blob[raw:raw + n], vsize, bool(flags & _CODE)))
    directory_count = struct.unpack_from("<I", blob, pe + 24 + 92)[0]
    directories = [struct.unpack_from("<II", blob, pe + 24 + 96 + 8 * k) for k in range(min(directory_count, 16))]
    return Executable(hashlib.sha256(blob).hexdigest(), timestamp, size_of_image, sections, directories)


def import_area(exe: Executable) -> list[tuple[int, int]]:
    """the IAT, and the import directory up to the end of its section (lookup tables, hint/name entries, DLL names follow it)"""
    out = []
    if len(exe.directories) > 12 and exe.directories[12][1]:
        out.append((IMAGE_BASE + exe.directories[12][0], exe.directories[12][1]))
    if len(exe.directories) > 1 and exe.directories[1][1]:
        start = IMAGE_BASE + exe.directories[1][0]
        section = next((s for s in exe.sections if s.va <= start < s.va + len(s.data)), None)
        if section is not None:
            out.append((start, section.va + len(section.data) - start))
    return sorted(out)


def otsd(timestamp: int, size_of_image: int, parts: list[tuple[int, bytes]]) -> bytes:
    """The static-data image format of tony/static_data.py."""
    header = b"OTSD" + struct.pack("<5I", 1, IMAGE_BASE, timestamp, size_of_image, len(parts))
    return header + b"".join(struct.pack("<2I", va, len(b)) for va, b in parts) + b"".join(b for _, b in parts)


# ------------------------------------------------------------------------------------------------------------------- applying


class Translator:
    def __init__(self, breakpoints: list[tuple[int, int]]):
        self.starts = [v for v, _ in breakpoints]
        self.deltas = [d for _, d in breakpoints]

    def __call__(self, value: int) -> int:
        k = bisect.bisect_right(self.starts, value) - 1
        return value if k < 0 else (value + self.deltas[k]) & 0xFFFFFFFF


def apply(variant: Executable, recipe: dict) -> bytes:
    """The canonical static-data image made from `variant`. ValueError when the result is not the one the recipe promises."""
    if variant.sha256 != recipe["variant_sha256"]:
        raise ValueError(f"not the {recipe['variant']} build the recipe was made for (sha256 {variant.sha256})")
    sources = variant.data_sections()

    def source(va: int, n: int) -> bytes:
        for s in sources:
            if s.va <= va and va + n <= s.va + len(s.data):
                return s.data[va - s.va:va - s.va + n]
        raise ValueError(f"copy {va:#x}+{n:#x} is outside the variant's data")

    out = {va: bytearray(size) for va, size in recipe["sections"]}
    starts = sorted(out)

    def target(va: int, n: int) -> tuple[bytearray, int]:
        k = bisect.bisect_right(starts, va) - 1
        if k < 0 or va + n > starts[k] + len(out[starts[k]]):
            raise ValueError(f"{va:#x}+{n:#x} is outside the canonical sections")
        return out[starts[k]], va - starts[k]

    translate = Translator([(int(v), int(d)) for v, d in recipe["breakpoints"]])
    for c, v, n in recipe["copy"]:
        chunk = bytearray(source(v, n))
        for o in range((-v) % 4, n - 3, 4):
            x = struct.unpack_from("<I", chunk, o)[0]
            if variant.in_image(x):
                struct.pack_into("<I", chunk, o, translate(x))
        buf, at = target(c, n)
        buf[at:at + n] = chunk
    for c, n in recipe["zero"]:
        buf, at = target(c, n)
        buf[at:at + n] = bytes(n)
    for c, text in recipe["bytes"]:
        b = bytes.fromhex(text)
        buf, at = target(c, len(b))
        buf[at:at + len(b)] = b
    image = otsd(int(recipe["canonical_timestamp"]), int(recipe["canonical_size_of_image"]), [(va, bytes(out[va])) for va, _ in recipe["sections"]])
    found = hashlib.sha256(image).hexdigest()
    if found != recipe["image_sha256"]:
        raise ValueError(f"the rebuilt image has sha256 {found}, expected {recipe['image_sha256']}")
    return image


# ------------------------------------------------------------------------------------------------------------------- deriving


def _masked_pointers(section: Section, exe: Executable) -> bytes:
    b = bytearray(section.data)
    for o in range((-section.va) % 4, len(b) - 3, 4):
        if exe.in_image(struct.unpack_from("<I", b, o)[0]):
            b[o:o + 4] = _PLACEHOLDER
    return bytes(b)


def _unique_grams(s: bytes, k: int) -> dict[bytes, int]:
    seen: dict[bytes, int] = {}
    for i in range(len(s) - k + 1):
        g = s[i:i + k]
        seen[g] = -1 if g in seen else i
    return {g: i for g, i in seen.items() if i >= 0 and len(set(g)) > 1}


def _longest_chain(pairs: list[tuple[int, int]]) -> list[tuple[int, int]]:
    """longest chain increasing in both coordinates; pairs sorted by the second"""
    tails: list[int] = []
    tail_index: list[int] = []
    prev: list[int | None] = [None] * len(pairs)
    for p, (i, _) in enumerate(pairs):
        k = bisect.bisect_left(tails, i)
        if k == len(tails):
            tails.append(i)
            tail_index.append(p)
        else:
            tails[k] = i
            tail_index[k] = p
        prev[p] = tail_index[k - 1] if k else None
    chain = []
    p = tail_index[-1] if tail_index else None
    while p is not None:
        chain.append(pairs[p])
        p = prev[p]
    return chain[::-1]


def _align(a: bytes, b: bytes, ao: int, bo: int, out: list[tuple[int, int, int]]) -> None:
    """append the equal runs (a position, b position, length) of an alignment of a and b"""
    if not a or not b:
        return
    p = 0
    m = min(len(a), len(b))
    while p < m and a[p] == b[p]:
        p += 1
    s = 0
    while s < m - p and a[len(a) - 1 - s] == b[len(b) - 1 - s]:
        s += 1
    if p:
        out.append((ao, bo, p))
    mid_a, mid_b = a[p:len(a) - s], b[p:len(b) - s]
    if mid_a and mid_b:
        if len(mid_a) * len(mid_b) <= 1 << 22:
            for x, y, n in SequenceMatcher(None, mid_a, mid_b, autojunk=False).get_matching_blocks():
                if n:
                    out.append((ao + p + x, bo + p + y, n))
        else:
            _align_large(mid_a, mid_b, ao + p, bo + p, out)
    if s:
        out.append((ao + len(a) - s, bo + len(b) - s, s))


def _align_large(a: bytes, b: bytes, ao: int, bo: int, out: list[tuple[int, int, int]]) -> None:
    for k in (16, 8, 4):
        ua, ub = _unique_grams(a, k), _unique_grams(b, k)
        pairs = sorted(((ua[g], ub[g]) for g in ua.keys() & ub.keys()), key=lambda t: t[1])
        chain = _longest_chain(pairs)
        if chain:
            break
    else:
        # nothing in common that is unique: same-length spans compare in place, others are left to the literal bytes
        if len(a) == len(b):
            i = 0
            while i < len(a):
                if a[i] == b[i]:
                    j = i
                    while j < len(a) and a[j] == b[j]:
                        j += 1
                    out.append((ao + i, bo + i, j - i))
                    i = j
                else:
                    i += 1
        return
    ai = bj = 0
    for i, j in chain:
        if i < ai or j < bj:
            continue
        _align(a[ai:i], b[bj:j], ao + ai, bo + bj, out)
        n = 0
        while i + n < len(a) and j + n < len(b) and a[i + n] == b[j + n]:
            n += 1
        out.append((ao + i, bo + j, n))
        ai, bj = i + n, j + n
    _align(a[ai:], b[bj:], ao + ai, bo + bj, out)


MIN_MOVED = 8  # the shortest run the second pass copies from anywhere in the variant


def _section_blocks(cs: Section, vs: Section, canon: Executable, variant: Executable) -> list[tuple[int, int, int]]:
    """(canonical va, variant va, length) runs of equal bytes of one section pair, pointers masked, aligned in order"""
    ca, va = _masked_pointers(cs, canon), _masked_pointers(vs, variant)
    raw: list[tuple[int, int, int]] = []
    _align(va, ca, vs.va, cs.va, raw)
    return sorted((c, v, n) for v, c, n in raw if n)


def _moved_blocks(canon: Executable, variant: Executable, blocks: list[tuple[int, int, int]]) -> list[tuple[int, int, int]]:
    """second pass: canonical bytes no in-order run covers, found anywhere in the variant's data (tables the linker reordered)"""
    covered: dict[int, bytearray] = {s.va: bytearray(len(s.data)) for s in canon.data_sections()}
    starts = sorted(covered)
    for c, _, n in blocks:
        k = bisect.bisect_right(starts, c) - 1
        covered[starts[k]][c - starts[k]:c - starts[k] + n] = b"\1" * n
    pool = [(s.va, _masked_pointers(s, variant)) for s in variant.data_sections()]
    index: dict[bytes, int] = {}
    for base, b in pool:
        for i in range(len(b) - MIN_MOVED + 1):
            g = b[i:i + MIN_MOVED]
            if len(set(g)) > 1:
                index.setdefault(g, base + i)
    flat = {base: b for base, b in pool}

    def variant_at(va: int) -> tuple[bytes, int]:
        for base, b in flat.items():
            if base <= va < base + len(b):
                return b, va - base
        raise KeyError(va)

    out = []
    for s in canon.data_sections():
        cb = _masked_pointers(s, canon)
        cov = covered[s.va]
        i = 0
        while i < len(cb):
            if cov[i]:
                i += 1
                continue
            v = index.get(cb[i:i + MIN_MOVED]) if i + MIN_MOVED <= len(cb) and not any(cov[i:i + MIN_MOVED]) else None
            if v is None:
                i += 1
                continue
            vb, vo = variant_at(v)
            n = 0
            while i + n < len(cb) and not cov[i + n] and vo + n < len(vb) and cb[i + n] == vb[vo + n]:
                n += 1
            out.append((s.va + i, v, n))
            i += n
    return out


def derive(canonical_path: Path, variant_path: Path, address_map: dict, variant_name: str, masked: list[dict]) -> dict:
    canon, variant = read_executable(canonical_path), read_executable(variant_path)
    if address_map["canonical_sha256"] != canon.sha256 or address_map["variant_sha256"] != variant.sha256:
        raise ValueError("the address map was made for other executables")
    anchors = [(int(v), int(c)) for v, c in address_map["data"]]
    functions = [(int(f[0]), int(f[1])) for f in address_map["functions"]]

    blocks: list[tuple[int, int, int]] = []
    for cs in canon.data_sections():
        vs = next((s for s in variant.data_sections() if s.name == cs.name), None)
        if vs is not None:
            blocks += _section_blocks(cs, vs, canon, variant)
    blocks = _merge(blocks)
    blocks += _moved_blocks(canon, variant, blocks)

    # breakpoints: function entries, data anchors, block starts, then the pointer pairs the alignment saw (most specific last)
    votes: dict[int, dict[int, int]] = {}

    def vote(v: int, delta: int, weight: int) -> None:
        votes.setdefault(v, {})
        votes[v][delta] = votes[v].get(delta, 0) + weight

    for v, c in functions:
        vote(v, c - v, 1)
    for v, c in anchors:
        vote(v, c - v, 1)
    for c, v, n in blocks:
        vote(v, c - v, 1)
    canon_bytes = {s.va: s.data for s in canon.data_sections()}
    variant_bytes = {s.va: s.data for s in variant.data_sections()}

    def word(sections: dict[int, bytes], va: int) -> int:
        for start, b in sections.items():
            if start <= va and va + 4 <= start + len(b):
                return struct.unpack_from("<I", b, va - start)[0]
        raise KeyError(va)

    for c, v, n in blocks:
        for o in range((-v) % 4, n - 3, 4):
            x = word(variant_bytes, v + o)
            if variant.in_image(x):
                y = word(canon_bytes, c + o)
                if canon.in_image(y):
                    vote(x, y - x, 4)
    breakpoints = []
    for v in sorted(votes):
        delta = max(votes[v].items(), key=lambda kv: (kv[1], -abs(kv[0])))[0]
        if not breakpoints or breakpoints[-1][1] != delta:
            breakpoints.append((v, delta))

    # drop breakpoints the pointers do not need: a breakpoint is kept only if removing it would change a translated pointer
    used = set()
    starts = [v for v, _ in breakpoints]
    for c, v, n in blocks:
        for o in range((-v) % 4, n - 3, 4):
            x = word(variant_bytes, v + o)
            if variant.in_image(x):
                used.add(bisect.bisect_right(starts, x) - 1)
    kept = [breakpoints[k] for k in sorted(used) if k >= 0]
    merged = []
    for v, d in kept:
        if not merged or merged[-1][1] != d:
            merged.append((v, d))

    recipe = {
        "variant": variant_name,
        "variant_sha256": variant.sha256,
        "canonical_sha256": canon.sha256,
        "canonical_timestamp": canon.timestamp,
        "canonical_size_of_image": canon.size_of_image,
        "sections": [[s.va, len(s.data)] for s in canon.data_sections()],
        "import_area": [list(a) for a in import_area(canon)],
        "masked": masked,
        "copy": [[c, v, n] for c, v, n in _merge(blocks)],
        "breakpoints": [list(p) for p in merged],
        "zero": [],
        "bytes": [],
    }
    # what the copies and the pointer rule give, against the canonical bytes: the rest is zero or literal
    probe = dict(recipe, zero=[], bytes=[], image_sha256="")
    built = _build_unchecked(variant, probe)
    mask = [(int(m["address"]), int(m["size"])) for m in masked]
    zero, literal = [], []
    for s in canon.data_sections():
        got = built[s.va]
        want = bytearray(s.data)
        for a, n in mask:
            lo, hi = max(a, s.va), min(a + n, s.va + len(want))
            if lo < hi:
                want[lo - s.va:hi - s.va] = bytes(hi - lo)
        i = 0
        while i < len(want):
            if got[i] == want[i]:
                i += 1
                continue
            j = i
            while j < len(want) and got[j] != want[j]:
                j += 1
            # a run of differences; gaps of up to 3 equal bytes are absorbed so that a run is one entry
            while True:
                k = j
                while k < len(want) and k - j < 4 and got[k] == want[k]:
                    k += 1
                if k < len(want) and k - j < 4 and got[k] != want[k]:
                    j = k
                    while j < len(want) and got[j] != want[j]:
                        j += 1
                else:
                    break
            run = bytes(want[i:j])
            if not any(run):
                zero.append([s.va + i, j - i])
            else:
                literal.append([s.va + i, run.hex()])
            i = j
    recipe["zero"] = zero
    recipe["bytes"] = literal
    image = otsd(canon.timestamp, canon.size_of_image, [(s.va, bytes(_masked(s, mask))) for s in canon.data_sections()])
    recipe["image_sha256"] = hashlib.sha256(image).hexdigest()
    return recipe


def _masked(s: Section, mask: list[tuple[int, int]]) -> bytearray:
    b = bytearray(s.data)
    for a, n in mask:
        lo, hi = max(a, s.va), min(a + n, s.va + len(b))
        if lo < hi:
            b[lo - s.va:hi - s.va] = bytes(hi - lo)
    return b


def _merge(blocks: list[tuple[int, int, int]]) -> list[tuple[int, int, int]]:
    out: list[tuple[int, int, int]] = []
    for c, v, n in sorted(blocks):
        if out and out[-1][0] + out[-1][2] == c and out[-1][1] + out[-1][2] == v:
            out[-1] = (out[-1][0], out[-1][1], out[-1][2] + n)
        elif out and c < out[-1][0] + out[-1][2]:
            continue  # overlapping alignment output: keep the first
        else:
            out.append((c, v, n))
    return out


def _build_unchecked(variant: Executable, recipe: dict) -> dict[int, bytearray]:
    sources = variant.data_sections()
    out = {va: bytearray(size) for va, size in recipe["sections"]}
    starts = sorted(out)
    translate = Translator([(int(v), int(d)) for v, d in recipe["breakpoints"]])
    for c, v, n in recipe["copy"]:
        s = next(s for s in sources if s.va <= v and v + n <= s.va + len(s.data))
        chunk = bytearray(s.data[v - s.va:v - s.va + n])
        for o in range((-v) % 4, n - 3, 4):
            x = struct.unpack_from("<I", chunk, o)[0]
            if variant.in_image(x):
                struct.pack_into("<I", chunk, o, translate(x))
        k = bisect.bisect_right(starts, c) - 1
        out[starts[k]][c - starts[k]:c - starts[k] + n] = chunk
    return out


# ------------------------------------------------------------------------------------------------------------------- files

VARIANT = "original_2000"
INCLUDE = "src/launcher/original_2000_static.inc"


def variant_spec(name: str = VARIANT) -> dict:
    return load_yaml("re/xref/config.yml")["variants"][name]


def recipe_path(name: str = VARIANT) -> Path:
    return resolve(variant_spec(name)["static_recipe"])


def load_recipe(name: str = VARIANT) -> dict:
    return load_yaml(recipe_path(name))


def derive_from_config(name: str = VARIANT) -> dict:
    spec = variant_spec(name)
    canonical = resolve(load_yaml("re/config/binaries.yml")["executables"]["thps2_pc"]["path"])
    variant = resolve(spec["path"])
    for path in (canonical, variant):
        if not path.is_file():
            raise SystemExit(f"{path}: not found (`tony xref static-recipe` needs both executables)")
    address_map = load_yaml(f"re/xref/{name}.yml")
    masked = [{"address": int(m["address"]), "size": int(m["size"]), "why": " ".join(str(m["why"]).split())} for m in spec.get("static_masked", [])]
    return derive(canonical, variant, address_map, name, masked)


def classify(recipe: dict) -> dict[str, int]:
    """literal bytes by kind: the import tables (the linker's, Win32 names and RVAs), addresses, and the rest (content)"""
    imports = recipe.get("import_area", [])
    out = {"imports": 0, "content": 0}
    for c, text in recipe["bytes"]:
        n = len(text) // 2
        if any(a <= c < a + size for a, size in imports):
            out["imports"] += n
        else:
            out["content"] += n
    return out


def render_recipe(recipe: dict) -> str:
    lit = classify(recipe)
    lines = [
        "version: 1",
        f"generated_by: tony xref static-recipe --variant {recipe['variant']}",
        f"variant: {recipe['variant']}",
        f"variant_sha256: {recipe['variant_sha256']}",
        f"canonical_sha256: {recipe['canonical_sha256']}",
        f"canonical_timestamp: {recipe['canonical_timestamp']:#010x}",
        f"canonical_size_of_image: {recipe['canonical_size_of_image']:#010x}",
        "# sha256 of the image this recipe makes: the canonical static-data image with the masked ranges zeroed",
        f"image_sha256: {recipe['image_sha256']}",
        "summary:",
        f"  copied_bytes: {sum(n for *_, n in recipe['copy'])}",
        f"  zero_bytes: {sum(n for _, n in recipe['zero'])}",
        f"  literal_bytes: {sum(len(t) // 2 for _, t in recipe['bytes'])}",
        f"  literal_import_table_bytes: {lit['imports']}",
        f"  literal_other_bytes: {lit['content']}",
        "# the canonical image's sections: [va, size]",
        "sections:",
        *[f"  - [{va:#010x}, {size:#x}]" for va, size in recipe["sections"]],
        "# the canonical import directory, lookup tables, names and IAT (the linker's, laid out in another order in the variant)",
        "import_area:",
        *[f"  - [{a:#010x}, {n:#x}]" for a, n in recipe.get("import_area", [])],
        "masked:",
    ]
    for m in recipe["masked"]:
        lines += [f"  - address: {int(m['address']):#010x}", f"    size: {int(m['size'])}", f"    why: {json.dumps(m['why'])}"]
    lines += ["# copy: [canonical va, variant va, size]", "copy:"]
    lines += [f"  - [{c:#010x}, {v:#010x}, {n:#x}]" for c, v, n in recipe["copy"]]
    lines += ["# breakpoints: [variant address, delta]: an address word x of a copied range becomes x + delta of the last breakpoint <= x", "breakpoints:"]
    lines += [f"  - [{v:#010x}, {d}]" for v, d in recipe["breakpoints"]]
    lines += ["# zero: [canonical va, size]", "zero:" + ("" if recipe["zero"] else " []")]
    lines += [f"  - [{c:#010x}, {n:#x}]" for c, n in recipe["zero"]]
    lines += ["# bytes: [canonical va, hex], written last", "bytes:" + ("" if recipe["bytes"] else " []")]
    lines += [f"  - [{c:#010x}, \"{t}\"]" for c, t in recipe["bytes"]]
    return "\n".join(lines) + "\n"


def render_include(recipe: dict) -> str:
    def rows(items, per_line, fmt):
        out = []
        for k in range(0, len(items), per_line):
            out.append("    " + " ".join(fmt(x) for x in items[k:k + per_line]))
        return out

    blob = b"".join(bytes.fromhex(t) for _, t in recipe["bytes"])
    literals, at = [], 0
    for c, t in recipe["bytes"]:
        literals.append((c, at, len(t) // 2))
        at += len(t) // 2
    lines = [
        f"// Generated by `tony xref static-recipe` from {variant_spec(recipe['variant'])['static_recipe']}. Do not edit.",
        "// The canonical static-data image rebuilt from the original 2000 release's THawk2.exe (tony/static_variant.py, opentony_launcher.cpp).",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace opentony::original_2000 {",
        "",
        f'constexpr const char* kVariantSha256 = "{recipe["variant_sha256"]}";',
        f'constexpr const char* kImageSha256 = "{recipe["image_sha256"]}";',
        "",
        "struct Range { std::uint32_t va, size; };",
        "struct Copy { std::uint32_t canonical, variant, size; };",
        "struct Breakpoint { std::uint32_t variant; std::int32_t delta; };",
        "struct Literal { std::uint32_t canonical, offset, size; };",
        "",
        "constexpr Range kSections[] = {",
        *rows(recipe["sections"], 4, lambda s: f"{{{s[0]:#010x}u, {s[1]:#x}u}},"),
        "};",
        "constexpr Copy kCopies[] = {",
        *rows(recipe["copy"], 3, lambda s: f"{{{s[0]:#010x}u, {s[1]:#010x}u, {s[2]:#x}u}},"),
        "};",
        "constexpr Breakpoint kBreakpoints[] = {",
        *rows(recipe["breakpoints"], 4, lambda s: f"{{{s[0]:#010x}u, {s[1]}}},"),
        "};",
        "constexpr Range kZeros[] = {" + ("" if recipe["zero"] else "{0u, 0u}};  // (none)"),
    ]
    if recipe["zero"]:
        lines += [*rows(recipe["zero"], 4, lambda s: f"{{{s[0]:#010x}u, {s[1]:#x}u}},"), "};"]
    lines += [
        "constexpr Literal kLiterals[] = {",
        *rows(literals, 3, lambda s: f"{{{s[0]:#010x}u, {s[1]}u, {s[2]}u}},"),
        "};",
        "constexpr std::uint8_t kLiteralBytes[] = {",
        *rows(list(blob), 16, lambda b: f"{b:#04x},"),
        "};",
        "",
        "}  // namespace opentony::original_2000",
    ]
    return "\n".join(lines) + "\n"


def static_recipe(args) -> int:
    """`tony xref static-recipe [--check]`: write (or check) the recipe and the launcher's copy of it"""
    recipe = derive_from_config(args.variant)
    variant = read_executable(resolve(variant_spec(args.variant)["path"]))
    apply(variant, recipe)  # the result must be the promised image before anything is written
    targets = [(recipe_path(args.variant), render_recipe(recipe)), (resolve(INCLUDE), render_include(recipe))]
    stale = [path for path, text in targets if not path.is_file() or path.read_text() != text]
    lit = classify(recipe)
    summary = (f"{len(recipe['copy'])} copies ({sum(n for *_, n in recipe['copy'])} bytes), {len(recipe['breakpoints'])} breakpoints, "
               f"{sum(len(t) // 2 for _, t in recipe['bytes'])} literal bytes ({lit['imports']} import tables, {lit['content']} other)")
    if args.check:
        for path in stale:
            print(f"{path.relative_to(resolve('.'))}: STALE; run `tony xref static-recipe`")
        print(summary)
        return 1 if stale else 0
    for path, text in targets:
        path.write_text(text)
        print(f"wrote {path.relative_to(resolve('.'))}")
    print(summary)
    return 0
