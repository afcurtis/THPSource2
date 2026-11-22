#!/usr/bin/env python3
"""The texture class table (texture-upscaling plan, T3): every known texture key gets a class, `world` (the 3D method) or `2d` (the HUD, fonts and menus method).

    python scripts/texture_classes.py CENSUS_DIR [--play LOG ...] [--table re/config/texture_classes.tsv]

CENSUS_DIR is the merged output of scripts/texture_census.py (index.tsv, seen.tsv, runs.tsv). A --play LOG is a run.txt of a played session with OT_TEXTURE_PACK_LOG=1
(`video: pack use: <key> <w>x<h> replaced|missed 2d|world` lines) or with OT_TEXTURE_DUMP (then give its folder: its index.tsv's use column counts as drawn evidence).
The rule, first match wins (`evidence` column):
  person         the row was set by hand in the table (kept when the table is written again)
  drawn          drawn in a census run or in play: `world` if it was ever drawn in the world, else `2d`; a key drawn both ways is `world` (evidence `drawn-both`, listed
                 for a look)
  rule:siblings  never drawn; the keys seen in exactly the same census runs (a font, a screen: it loads as a whole) include 3 or more drawn ones, all of one
                 class: that class (the menu font is in every menu's store and in the levels' too, for the pause menu: no rule below gets it right)
  rule:frontend  never drawn; in the store during a session run (menus, screens) and never in a level or skater run's store: `2d` (a session the recipe
                 marks `undrawn: world`, the park editor, counts as a level run here)
  rule:global    never drawn; in the store of every level (0-12) in one texture mode: fonts, HUD digits: `2d`
  rule:level     never drawn; in a level or skater run's store: `world`
The table holds keys and sizes only, never pictures: key, width, height, class, evidence, census (1: the recipe dumps it; 0: only play has seen it, a recipe gap).
Play's frames of the level select's movie (176 x 48, a key a frame) are not evidence: a movie stays original.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_TABLE = ROOT / "re" / "config" / "texture_classes.tsv"
LEVELS = range(13)
SIBLINGS_DRAWN = 3   # rule:siblings needs this many drawn keys with the same run set, all of one class
HEADER = ("# The texture class table (docs/post_phase_5 texture-upscaling plan, T3), written by scripts/texture_classes.py from the census of\n"
          "# re/config/texture_census.yml and the person's play. Keys only (a hash of the texels, src/hal/texture_key.hpp), never pictures.\n"
          "# Edit a row's class by hand and set its evidence to `person`: the generator keeps it.\n"
          "# key\twidth\theight\tclass\tevidence\tcensus")
# The level select's Video box plays a movie into a 176 x 48 texture, a new key every frame (the person's play: 145 in one visit). A movie is not a texture
# set: play evidence of that size is dropped unless the census has the key (it has none), so its frames stay original and never become recipe gaps.
MOVIE_FRAME_SIZES = {(176, 48)}
PLAY_LINE = re.compile(r"video: pack use: ([0-9a-f]{16}) (\d+)x(\d+) (?:replaced|missed) (2d|world)")


def read_tsv(path: Path) -> list[list[str]]:
    return [line.split("\t") for line in path.read_text().splitlines() if line and not line.startswith("#")]


class Evidence:
    def __init__(self) -> None:
        self.size: dict[str, tuple[int, int]] = {}
        self.census: set[str] = set()
        self.world: set[str] = set()
        self.twod: set[str] = set()
        self.store_session: set[str] = set()     # in a session run (menus), drawn or walked
        self.store_level: set[str] = set()       # in a level or skater run
        self.per_level: dict[tuple[str, int], set[str]] = {}   # (texture mode, level) -> keys of that level's plain run
        self.runs: dict[str, set[str]] = {}       # key -> every census run that saw it (drawn or walked)

    def add_census(self, folder: Path) -> None:
        for cols in read_tsv(folder / "index.tsv"):
            key = cols[0]
            self.size[key] = (int(cols[1]), int(cols[2]))
            self.census.add(key)
            if cols[5] in ("world", "both"):
                self.world.add(key)
            if cols[5] in ("2d", "both"):
                self.twod.add(key)
        runs = {cols[0]: cols for cols in read_tsv(folder / "runs.tsv")}
        for tag, key, _how in read_tsv(folder / "seen.tsv"):
            run = runs.get(tag)
            if run is None:
                continue
            self.runs.setdefault(key, set()).add(tag)
            kind, mode, level = run[1], run[2], run[3]
            undrawn = run[11] if len(run) > 11 and run[11] else ("2d" if kind == "session" else "world")
            if undrawn == "2d":
                self.store_session.add(key)
            else:
                self.store_level.add(key)
                if kind == "level":
                    self.per_level.setdefault((mode, int(level)), set()).add(key)

    def add_play(self, path: Path) -> None:
        if path.is_dir():
            for cols in read_tsv(path / "index.tsv"):
                self._play(cols[0], int(cols[1]), int(cols[2]), cols[5])
            return
        for match in PLAY_LINE.finditer(path.read_text(errors="replace")):
            key, width, height, use = match.groups()
            self._play(key, int(width), int(height), use)

    def _play(self, key: str, width: int, height: int, use: str) -> None:
        if key not in self.census and (width, height) in MOVIE_FRAME_SIZES:
            return
        self.size.setdefault(key, (width, height))
        if use in ("world", "both"):
            self.world.add(key)
        if use in ("2d", "both"):
            self.twod.add(key)

    def global_keys(self) -> set[str]:
        """Keys in the store of every level, in one texture mode at least."""
        result: set[str] = set()
        for mode in {mode for mode, _ in self.per_level}:
            sets = [self.per_level.get((mode, level)) for level in LEVELS]
            if all(s is not None for s in sets):
                result |= set.intersection(*sets)
        return result


def sibling_classes(evidence: Evidence) -> dict[frozenset[str], str]:
    """run set -> the class every drawn key with exactly that run set has, when there are SIBLINGS_DRAWN of them or more and they agree (a font or a screen
    loads as a whole: its glyphs are in the same stores, and the ones a run drew say what the rest are)."""
    drawn: dict[frozenset[str], list[str]] = {}
    for key, runs in evidence.runs.items():
        if key in evidence.world or key in evidence.twod:   # drawn both ways counts as world, as in classify
            drawn.setdefault(frozenset(runs), []).append("world" if key in evidence.world else "2d")
    return {runs: uses[0] for runs, uses in drawn.items() if len(uses) >= SIBLINGS_DRAWN and len(set(uses)) == 1}


def classify(evidence: Evidence, person: dict[str, str]) -> dict[str, tuple[str, str]]:
    """key -> (class, evidence), by the rule in the module's docstring, in that order."""
    everywhere = evidence.global_keys()
    siblings = sibling_classes(evidence)
    table: dict[str, tuple[str, str]] = {}
    for key in evidence.size:
        if key in person:
            table[key] = (person[key], "person")
        elif key in evidence.world and key in evidence.twod:
            table[key] = ("world", "drawn-both")
        elif key in evidence.world:
            table[key] = ("world", "drawn")
        elif key in evidence.twod:
            table[key] = ("2d", "drawn")
        elif frozenset(evidence.runs.get(key, ())) in siblings:
            table[key] = (siblings[frozenset(evidence.runs[key])], "rule:siblings")
        elif key in evidence.store_session and key not in evidence.store_level:
            table[key] = ("2d", "rule:frontend")
        elif key in everywhere:
            table[key] = ("2d", "rule:global")
        elif key in evidence.store_level:
            table[key] = ("world", "rule:level")
        else:
            table[key] = ("world", "rule:none")   # seen nowhere a rule looks: cannot happen for a census key; a play key is always drawn
    return table


def read_person(table: Path) -> dict[str, str]:
    if not table.exists():
        return {}
    return {cols[0]: cols[3] for cols in read_tsv(table) if len(cols) > 4 and cols[4] == "person"}


def write_table(path: Path, evidence: Evidence, table: dict[str, tuple[str, str]]) -> None:
    lines = [HEADER]
    for key in sorted(table):
        width, height = evidence.size[key]
        cls, why = table[key]
        lines.append(f"{key}\t{width}\t{height}\t{cls}\t{why}\t{int(key in evidence.census)}")
    path.write_text("\n".join(lines) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("census", type=Path)
    parser.add_argument("--play", type=Path, action="append", default=[])
    parser.add_argument("--table", type=Path, default=DEFAULT_TABLE)
    args = parser.parse_args()
    evidence = Evidence()
    evidence.add_census(args.census)
    for play in args.play:
        evidence.add_play(play)
    person = read_person(args.table)
    table = classify(evidence, person)
    write_table(args.table, evidence, table)
    counts: dict[tuple[str, str], int] = {}
    for cls, why in table.values():
        counts[(cls, why)] = counts.get((cls, why), 0) + 1
    print(f"{len(table)} keys -> {args.table}; not in the census: {sum(1 for k in table if k not in evidence.census)}")
    for (cls, why), n in sorted(counts.items()):
        print(f"  {cls:5} {why:14} {n}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
