"""Cross-reference recovered types against PSX demo and Cumulative Patch struct names.

Nothing here is applied to Ghidra. The output is a candidate table for type work:

* PSX demo headers carry compile-time checks ``sizeof(X) == N`` for real Neversoft
  struct names (sizes are for the MIPS build; PC-only fields can change them).
* The Cumulative Patch declares partial PC structs (names/locations only are recorded).
* A candidate pairs an OpenTony type with an external struct by (normalised) name. Size-only
  pairings were tried and rejected: they produced coincidences, not correspondences.
"""

from __future__ import annotations

import re
from glob import glob

from ..common import ROOT, load_yaml
from .common import write_rows
from .community import require_reference

_SIZE_CHECK = re.compile(r"\(\s*sizeof\s*\(\s*(?:struct\s+|class\s+)?(?P<name>\w+)\s*\)\s*==\s*(?P<size>0x[0-9a-fA-F]+|\d+)\s*\)")
_CP_STRUCT = re.compile(r"^\s*(?:typedef\s+)?(?:struct|class)\s+(?P<name>[A-Za-z_]\w*)\s*(?::[^{]*)?\{?\s*$")


def _key(name: str) -> str:
    return re.sub(r"^(runtime|retail)", "", name.lower())


def psx_sizes() -> dict[str, tuple[int, str]]:
    root = require_reference("thps2_demo_decomp") / "src"
    sizes = {}
    for path in sorted(root.glob("*.h")) + sorted(root.glob("*.cpp")):
        text = path.read_text(errors="ignore")
        for match in _SIZE_CHECK.finditer(text):
            line = text.count("\n", 0, match.start()) + 1
            sizes.setdefault(match.group("name"), (int(match.group("size"), 0), f"src/{path.name}:{line}"))
    return sizes


def cp_structs() -> dict[str, str]:
    root = require_reference("thps2_cumulative_patch") / "th2patch/hawk2/thawk2"
    found = {}
    for path in sorted(root.rglob("*.h")):
        for number, line in enumerate(path.read_text(errors="ignore").splitlines(), 1):
            if (match := _CP_STRUCT.match(line)) and not line.rstrip().endswith(";"):
                found.setdefault(match.group("name"), f"{path.relative_to(root.parents[1])}:{number}")
    return found


def opentony_types() -> dict[str, tuple[int | None, str]]:
    types = {}
    for path in sorted(glob(str(ROOT / "re/types/*.yml"))):
        for item in load_yaml(path).get("types") or []:
            size = item.get("size")
            types[item["name"]] = (int(size) if isinstance(size, int) else None, f"re/types/{path.rsplit('/', 1)[-1]}")
    return types


def write_types() -> dict:
    psx, cp, ours = psx_sizes(), cp_structs(), opentony_types()
    psx_by_key = {_key(n): n for n in psx}
    cp_by_key = {_key(n): n for n in cp}
    rows = []
    for name, (size, where) in sorted(ours.items()):
        psx_name, basis = psx_by_key.get(_key(name)), None
        if psx_name:
            basis = "name" if psx[psx_name][0] == size else "name (size differs)"
        cp_name = cp_by_key.get(_key(name)) or (cp_by_key.get(_key(psx_name)) if psx_name else None)
        if psx_name or cp_name:
            rows.append([name, size, where, psx_name, psx[psx_name][0] if psx_name else None,
                         psx[psx_name][1] if psx_name else None, basis, cp_name, cp.get(cp_name)])
    header = {
        "version": 1,
        "generated_by": "tony xref types",
        "summary": {"opentony_types": len(ours), "psx_size_checked_structs": len(psx),
                    "cumulative_patch_structs": len(cp), "candidates": len(rows)},
    }
    write_rows("re/xref/types.yml", header, {
        "candidates": (("[opentony_type, size, defined_in, psx_struct, psx_size, psx_source, basis, "
                        "cumulative_patch_struct, cumulative_patch_source]"), rows),
        "psx_structs": ("[psx_struct, psx_size, psx_source]",
                        [[n, s, w] for n, (s, w) in sorted(psx.items())]),
        "cumulative_patch_structs": ("[struct, source]", [[n, w] for n, w in sorted(cp.items())]),
    })
    return header["summary"]
