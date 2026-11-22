#!/usr/bin/env python3
"""Phase 5.4a: write re/config/standalone_natives.yml, the natives that have no hook row.

    python scripts/standalone_rows.py            # rewrite the file
    python scripts/standalone_rows.py --check    # compare, exit 1 when it differs

A standalone program has no retail code to call, so every native a table or a Kit's `engine(...)` can name needs an entry
point. The Kit hooks already have one (their generated adapter). The other natives are called in the replay tests through
an adapter table (`src/runtime/*_retail_calls_test.cpp`: `{0x00414030, {"Career_Got", 0xff, false, A(c.career.got(S(a[0])))}}`),
which is the same call expression a hook row holds, spelled with the test's context `c`. This tool reads those tables,
maps the context member to the Kit that owns it (KITS below) and writes the rows of the natives that have no hook row.
It validates its own reading on the rows that exist: the call it derives for a hooked native must equal that row's `call`.

The screens' tables map onto ScreensKit (src/runtime/screens_kit.hpp), which holds every screen the tests construct.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

import yaml  # noqa: E402

from tony import hooks, kit_hooks  # noqa: E402

OUT = ROOT / "re/config/standalone_natives.yml"

# test file -> {context member: (Kit id, keep the member in the call)}.
# A Kit that is the object itself (MathKit) has its functions called directly: `c.math.sqrt(x)` is `sqrt(x)` in the row.
# A Kit that has the member (CareerKit::career) keeps it: `c.career.got(x)` is `career.got(x)`.
KITS: dict[str, dict[str, tuple]] = {
    "audio": {"audio": ("audio", False)},
    "bit": {"bits": ("bit", False)},
    "camera": {"camera": ("camera", False)},
    "career": {"career": ("career", True), "sk": ("career", True), "comp": ("career", True)},
    "core": {"core": ("core", False)},
    "create": {name: ("create", True) for name in ("color", "textures", "customs", "parts", "spool", "palette", "stitch")},
    "createobj": {"kit": ("createobj", False)},
    "frontend": {name: ("frontend", True) for name in ("fonts", "mess", "window", "effects")},
    "gen_objs": {"gen": ("genobjs", False)},
    "heap": {"heap": ("heap", False)},
    "hud": {"panel": ("hud", True)},
    "m3d": {"kit": ("m3d", False)},
    "math": {"math": ("math", False)},
    "mmu": {"kit": ("mmu", False)},
    "net": {"net": ("net", False)},
    # the window-graph screens: ScreensKit members (src/runtime/screens_kit.hpp); a third element renames the member
    "options": {"options": ("screens", True)},
    "front": {"front": ("screens", True), "helpers": ("screens", True, "shell_helpers"), "net": ("screens", True, "net_menu")},
    "menu": {"menu": ("screens", True), "main": ("screens", True, "main_menu"), "credits": ("screens", True)},
    "equip_select": {"screen": ("screens", True, "equip")},
    "player_select": {"screen": ("screens", True, "player_select")},
    "goals": {"goals": ("screens", True)},
    "select_screens": {"career": ("screens", True, "career_select"), "handicap": ("screens", True), "multi": ("screens", True, "multi_select"),
                       "level": ("screens", True, "level_select")},
    "skater_windows": {"helpers": ("screens", True, "skater_helpers"), "windows": ("screens", True, "skater_windows")},
    "create_screens": {"screens": ("screens", True, "create_screens")},
    "trick_screens": {"screens": ("screens", True, "trick_screens")},
    "mmu_screens": {"screens": ("screens", True, "mmu_screens")},
    "level_editor": {"ed": ("leveleditor", False)},
    "level_gen": {"gen": ("levelgen", True), "lg": ("levelgen", True)},
    "platform": {"pad": ("platform", True), "sys": ("platform", True)},
    "player_spool": {name: ("pspool", True) for name in ("spooler", "skater", "pre", "files")},
    "psx_spool": {"spool": ("psx", False)},
    "rail": {"rail": ("rail", False)},
    "render": {"render": ("render", False)},
    "skater": {"skater": ("skater", False)},
    "video": {"video": ("video", False)},
    "world": {"world": ("world", False)},
}

# the namespace of the constants a test's call expressions name unqualified
CONSTANTS = {"createobj": "::opentony::runtime::createobj::"}

_HEAD = re.compile(r'\{\s*(0x[0-9a-fA-F]{8})\s*,\s*\{\s*"([A-Za-z0-9_]+)"\s*,\s*[^,]+,\s*(?:true|false)\s*,\s*(AS|[ABV])\(')
_DEFINE = re.compile(r"#define\s+([A-Z_][A-Z_0-9]*)\s+(c\.[a-z_0-9.]+)\s*$", re.M)


def balanced(text: str, start: int) -> str | None:
    """The text inside the parenthesis that opens just before `start`."""

    depth = 1
    for index in range(start, len(text)):
        if text[index] == "(":
            depth += 1
        elif text[index] == ")":
            depth -= 1
            if depth == 0:
                return text[start:index]
    return None


def read_tables() -> dict[int, tuple[str, str, str, str]]:
    """address -> (table name, test, macro, call expression with the context spelled `c.`)."""

    found: dict[int, tuple[str, str, str, str]] = {}
    for path in sorted((ROOT / "src/runtime").glob("*_retail_calls_test.cpp")):
        test = path.name.removesuffix("_retail_calls_test.cpp")
        if test not in KITS:
            continue
        text = path.read_text(errors="replace")
        defines = dict(_DEFINE.findall(text))
        for match in _HEAD.finditer(text):
            expression = balanced(text, match.end())
            if expression is None:
                continue
            for macro, value in defines.items():
                expression = re.sub(rf"\b{macro}\b", value, expression)
            found.setdefault(int(match.group(1), 16), (match.group(2), test, match.group(3), expression))
    return found


def to_expr(test: str, expression: str) -> tuple[str, str] | None:
    """(Kit id, expression with `kit.` explicit) for a table expression that is not a plain call (`!c.options.f(self)`,
    `(c.front.g(), 0u)`, `static_cast<std::uint32_t>(c.career.h(S(a[0])))`) but uses the members of one Kit only."""

    kits: set[str] = set()

    def member(found: re.Match) -> str:
        head = found.group(1)
        if head not in KITS[test]:
            kits.add("?")
            return found.group(0)
        kit, keep, *rename = KITS[test][head]
        kits.add(kit)
        return f"kit.{rename[0] if rename else head}." if keep else "kit."

    text = re.sub(r"\bc\.([a-z_0-9]+)\.", member, expression)
    if len(kits) != 1 or "?" in kits or re.search(r"\bc\b", text) or "[&" in text or "->" in text:
        return None
    text = re.sub(r"(?<![:\w])((?!std\b)[a-z][a-z_0-9]*)::", r"::opentony::runtime::\1::", text)
    return next(iter(kits)), text


def to_call(test: str, expression: str) -> tuple[str, str] | None:
    """(Kit id, call) for a table expression, or None when it is not a plain call on a mapped member."""

    if expression.strip() in ("0", "(void)0"):
        return "none", expression.strip()  # the retail function returns 0 or does nothing: no Kit object
    if expression.strip() == "":
        return "none", "(void)0"
    if re.fullmatch(r"0u|0xffffffffu", expression.strip()):
        return "none", expression.strip()
    match = re.match(r"c\.([a-z_0-9]+)\.(.*)$", expression, re.S)
    if match is None or match.group(1) not in KITS[test]:
        return None
    kit, keep, *rename = KITS[test][match.group(1)]
    head = rename[0] if rename else match.group(1)
    rest = match.group(2)
    # the test's own helpers (`reset_source(c)`) and its context are not in a Kit
    if re.search(r"\bc\b", rest) or "[&" in rest or "->" in rest:
        return None
    if test in CONSTANTS:
        rest = re.sub(r"\b(k[A-Z]\w*)\b", lambda found: CONSTANTS[test] + found.group(1), rest)
    # the tests say `using namespace opentony::runtime`: a name qualified by a namespace of it (competition::kObject) is qualified in full
    rest = re.sub(r"(?<![:\w])((?!std\b)[a-z][a-z_0-9]*)::", r"::opentony::runtime::\1::", rest)
    return kit, (f"{head}.{rest}" if keep else rest)


# Functions whose `ret` cannot be read from the code (a tail jump out of the function): address -> (convention, stack_args, why).
# The row says `unchecked: <why>` and `tony hooks verify` does not compare its convention with the code.
OVERRIDES: dict[int, tuple[str, int, str]] = {
    0x0042ffc0: ("cdecl", 0, "tail-calls its target after pushing constants; takes no arguments"),
    0x00430510: ("cdecl", 0, "tail-calls its target after pushing constants; takes no arguments"),
    0x004a8250: ("thiscall", 0, "sets a flag and jumps through the object's vtable slot 1; takes no arguments"),
    0x004dc180: ("thiscall", 0, "sets the vtable and jumps to the base destructor; takes no arguments"),
    0x00426300: ("thiscall", 0, "loads the list object at +0x418 and jumps through its vtable (slot 4, the update); takes no arguments"),
}


# The retail-table functions of the PC file backend that the standalone replaces with its host backend (src/hooks/kits/standalone_files.cpp: one table of real file streams
# for the resources, the memory card and the custom parks): the Kits reach them by address, and a native here would use the retail table the host's open, seek, read and
# close do not keep.
HOST_BACKEND = {0x004e7500, 0x004e7900, 0x004e7b60}


def build() -> tuple[list[dict], dict[str, int]]:
    manifest = hooks.load_manifest()
    existing = {hook["builds"]["canonical"]: hook for hook in manifest["hooks"] if "kit" in hook}
    native = {int(item["address"]): item.get("status") for item in yaml.safe_load((ROOT / "re/native/functions.yml").read_text())["functions"]}
    symbols = hooks._symbols()
    stats = {"tables": 0, "agree": 0, "disagree": 0, "unmapped": 0, "rows": 0, "problems": 0}
    rows: list[dict] = []
    for address, (name, test, macro, expression) in sorted(read_tables().items()):
        stats["tables"] += 1
        if address in HOST_BACKEND:
            continue
        converted = to_call(test, expression)
        as_expr = False
        if converted is None and address not in existing:
            converted = to_expr(test, expression)
            as_expr = converted is not None
        if address in existing:
            if converted is not None and converted == (existing[address]["kit"], existing[address]["call"]):
                stats["agree"] += 1
            elif converted is not None:
                stats["disagree"] += 1
                print(f"differs {name}: table {converted} row {existing[address]['kit']} {existing[address]['call']}", file=sys.stderr)
            continue
        if converted is None or not native.get(address):
            stats["unmapped"] += 1
            continue
        kit, call = converted
        unchecked = None
        if address in OVERRIDES:
            convention, stack_args, unchecked = OVERRIDES[address]
            problems = []
        else:
            convention, stack_args, problems = kit_hooks.derive_convention(name, address, call)
        if problems:
            stats["problems"] += 1
            print("skipped " + "; ".join(problems), file=sys.stderr)
            continue
        rows.append({"name": symbols.get(address, name), "address": address, "kit": kit, ("expr" if as_expr else "call"): call, "returns": macro != "V",
                     "convention": convention, "stack_args": stack_args, "unchecked": unchecked})
        stats["rows"] += 1
    return rows, stats


def render(rows: list[dict]) -> str:
    header = (
        "# Generated by scripts/standalone_rows.py from the adapter tables of src/runtime/*_retail_calls_test.cpp. Do not edit.\n"
        "#\n"
        "# The natives that have no hook row (re/config/native_hooks.yml), as `natives:`. A standalone program calls them by retail address:\n"
        "# tony/kit_hooks.py turns each row into an adapter next to the Kit hooks' and lists it in the address table the resolver searches.\n"
        "# A row here is never patched into the retail executable.\n"
        "version: 1\n\nnatives:\n"
    )
    lines = []
    for row in rows:
        field = "expr" if "expr" in row else "call"
        call = row[field].replace('"', '\\"')
        lines.append(f'  - {{name: {row["name"]}, address: 0x{row["address"]:08x}, kit: {row["kit"]}, {field}: "{call}", '
                     f'returns: {"true" if row["returns"] else "false"}, convention: {row["convention"]}, stack_args: {row["stack_args"]}'
                     + (f', unchecked: "{row["unchecked"]}"' if row.get("unchecked") else "") + "}")
    return header + "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    rows, stats = build()
    print(stats)
    text = render(rows)
    if args.check:
        if not OUT.is_file() or OUT.read_text() != text:
            print(f"{OUT.relative_to(ROOT)} is stale; run scripts/standalone_rows.py", file=sys.stderr)
            return 1
        return 0
    OUT.write_text(text)
    print(f"wrote {OUT.relative_to(ROOT)} ({len(rows)} rows)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
