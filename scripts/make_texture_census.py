#!/usr/bin/env python3
"""Writes re/config/texture_census.yml, the recipe of the full texture census (texture-upscaling plan, T3; run by scripts/texture_census.py, later by the launcher).

    python scripts/make_texture_census.py [--check]

The runs, and why each is there (measured 2026-11-17, docs/post_phase_5/textureupscale.md, Standing facts T3):
  * every level 0-12 in both texture modes, the default skater (Tony Hawk, look A), 600 frames idle: the level's store (the dump's walk) is the whole level;
  * every other skater, look A, in level 0 and in level 2 (PC textures): the skater's own textures and the three sponsor logos the levels with banners show
    for the chosen pro (64 x 32 in levels 0, 1, 5, 7; 32 x 16 in levels 2, 4: the same logos in every level of one size, and in both texture modes);
  * every look in Warehouse with PSX textures (a skater's own textures differ in one key between the modes), and every further look (B, Hawk's third,
    Spider-Man's C and D) with PC textures: a look's textures replace some of look A's;
  * a trick run in every level (PC textures; Warehouse also PSX): the HUD's trick names, score digits and gaps drawn in 2D, the competitions' own HUD (the class
    rule covers glyphs no run draws);
  * the menus and screens, keyed sessions from the boot (PC textures; the menus do not read the level textures): the scenarios of re/scenarios/sessions that reach
    a screen of their own, and sessions for a whole single session to its results, free skate's pause menu, two players, Create-a-Skater's pages (a created
    skater's own textures are not covered: every part and colour choice bakes new texels, a combinatorial set);
  * the career's Select Course row by row, each level's checklist or invite page (both texture modes: the person's play found them with PSX textures);
  * every Select Player row (photo, bio, 3D skater) and every deck of the row's Skate Shop set chosen in turn, with the decks unlocked: the deck pictures and
    the boards the 3D skater (and the levels) draw, both texture modes. The level select's Video box is a movie (a texture a frame): not in the census.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
RECIPE = ROOT / "re" / "config" / "texture_census.yml"
SESSIONS = ROOT / "re" / "scenarios" / "sessions"

SKATERS = {0: 3, 1: 2, 2: 2, 3: 2, 4: 2, 5: 2, 6: 2, 7: 2, 8: 2, 9: 2, 10: 2, 11: 2, 12: 2, 17: 2, 18: 2, 19: 4}   # select-screen row: looks (rows 13-16 are created skaters)
DEFAULT_SKATER = 0
LOGO_LEVELS = (0, 2)            # one level for each logo size
LOOK_LEVEL = 12                 # Warehouse: the smallest store
SESSION_SCENARIOS = ("boot-career-hangar", "boot-career-end-run", "boot-options", "boot-saved-data", "boot-park-editor")   # boot-menu: every session passes the main menu;
# boot-create-skater: replaced by menu-create-skater below (more pages)
UNDRAWN_WORLD = ("boot-park-editor",)   # a key only its store holds is a park piece: a level texture, 128 x 128 (PC) or 64 x 64 (PSX) like the levels'
BOTH_MODES = ("boot-park-editor", "boot-saved-data")   # park pieces are level textures (69 more keys with the PlayStation ones); Saved Data's 3D model has 5
CAREER_LEVELS = 10              # Select Course in a career: 10 levels (Hangar to Skate Heaven), then the empty park
MENU_BOTH_MODES = ("menu-career-levels",)
SHOP_ROWS = 16                  # the Select Player rows offered with the skaters unlocked (Right from the 16th wraps to Hawk: measured 2026-11-17)
DECKS_PER_SET = 8               # a pro's Skate Shop set (EquipSelect::deck_rack_build_sets, 0x00446d80); a created skater's own 20 are not reached (no created skater)
SHOP_UNLOCK = "levels,skaters,decks"
DECK_LOOP = 1600                # polls for one deck: Skate Shop, Right, Accept, back on the skater's menu

UP, DOWN, LEFT, RIGHT = 0x1000, 0x4000, 0x8000, 0x2000
CROSS, SQUARE, CIRCLE, TRIANGLE = 0x40, 0x80, 0x20, 0x10


def trick_actions(frames: int) -> list[str]:
    """Up held; every 45 frames an ollie, then a flip (square) or a grab (circle) with a direction; a grind try every third."""
    directions = [LEFT, RIGHT, UP, DOWN, LEFT | UP, RIGHT | DOWN, 0]
    out = [f"{UP:#x} 20 {frames - 40}"]
    for k, start in enumerate(range(60, frames - 60, 45)):
        d = directions[k % len(directions)]
        out.append(f"{CROSS:#x} {start} 8")
        out.append(f"{SQUARE | d:#x} {start + 12} 6" if k % 2 == 0 else f"{CIRCLE | d:#x} {start + 12} 14")
        if k % 3 == 2:
            out.append(f"{TRIANGLE:#x} {start + 30} 10")
    return out


def presses(start: int, key: str, count: int, every: int, hold: int = 5) -> list[tuple[int, str, int]]:
    return [(start + i * every, key, hold) for i in range(count)]


def menu_sessions() -> dict[str, tuple[int, list[tuple[int, str, int]]]]:
    """name -> (deliveries, keys). The main menu is a carousel: Career, Single Session, Free Skate, 2 Player, Create-a-Skater, Park Editor, Options. A poll is about
    a frame and a delivery about half a poll; every screen is held 250 polls or more, so the dump's walk (120 still presents) sees its store."""
    runs = {}
    # Career: New Career, Hawk, Continue to Select Course; then every level row (career has 10, then the empty park): Accept opens the level's checklist, a
    # competition's invite or Chopper Drop's page (256 x 256 screens and the hand-written goal font), Back returns, Right moves on; the last row, the empty
    # park, is only shown (its box: "can't play custom park in career mode"; Accept does nothing there, so a Back would leave the screen)
    ev = [(500, "enter", 5), (800, "enter", 5), (1100, "enter", 5), (1400, "enter", 5)]
    for row in range(CAREER_LEVELS):
        ev += [(1700 + 1000 * row, "enter", 5), (2200 + 1000 * row, "esc", 5), (2500 + 1000 * row, "right", 5)]
    runs["menu-career-levels"] = (2 * (1700 + 1000 * CAREER_LEVELS + 500), ev)
    ev = [(530, "right", 5), (700, "enter", 5), (1000, "enter", 5), (1300, "enter", 5), (1600, "enter", 5), (1900, "enter", 5)] + presses(6400, "enter", 12, 150)
    runs["menu-single-session"] = (16800, ev)
    ev = [(530, "right", 5), (560, "right", 5), (700, "enter", 5), (1000, "enter", 5), (1300, "enter", 5), (1600, "enter", 5), (3000, "esc", 5)]
    ev += presses(3300, "down", 6, 120) + [(4200, "esc", 5)]
    runs["menu-free-skate"] = (9600, ev)
    runs["menu-two-player"] = (10000, presses(530, "right", 3, 30) + presses(800, "enter", 8, 250))
    # No run per level through the menus: free skate's level select moved Right N times loads level N, but the loading is the one rules screen ("Loading...
    # Free Skate") for every level, and the 13 runs added no key over the rest (measured 2026-11-17)
    # Create-a-Skater: the roster, an empty slot opens the editor; page one (name ... weight): Down x6 to Continue, Enter; the appearance page: every row stepped
    # through with Right (10 rows x 8 choices), Down to the next row
    ev = presses(530, "right", 4, 30) + [(710, "enter", 5), (900, "enter", 5)] + presses(1300, "down", 6, 40) + [(1600, "enter", 5)]
    for row in range(10):
        ev += presses(1900 + row * 400, "right", 8, 40) + [(1900 + row * 400 + 350, "down", 5)]
    runs["menu-create-skater"] = (12800, ev + presses(5950, "esc", 3, 120))
    return runs


def shop_sessions() -> dict[str, tuple[int, list[tuple[int, str, int]]]]:
    """name -> (deliveries, keys), one run per Select Player row (16 with the skaters unlocked; the empty created-skater rows are not offered): Single Session,
    Right N times to the row and a wait (the photo, the bio, the 3D skater), Accept; then, once for each of the character's 8 decks, Down x3 to Skate Shop,
    Accept, Right, Accept: the next deck is the character's board, drawn on the 3D skater (its own texture, 32 x 64, the board the levels draw), and the
    rack's pictures (64 x 256) of every deck of the sponsor's sets are spooled. Run with the decks unlocked (OT_UNLOCK decks: an unowned deck's picture is held
    back, a placeholder shown). Both texture modes: the skater and its board are drawn with the mode's textures."""
    runs = {}
    for row in range(SHOP_ROWS):
        ev = [(530, "right", 5), (700, "enter", 5)] + presses(850, "right", row, 40)
        t0 = 850 + 40 * row + 400
        ev.append((t0, "enter", 5))
        for deck in range(DECKS_PER_SET):   # the screens change slowly: a press during a change is lost
            at = t0 + 400 + DECK_LOOP * deck
            ev += presses(at, "down", 3, 80) + [(at + 400, "enter", 5), (at + 900, "right", 5), (at + 1200, "enter", 5)]
        runs[f"r{row}"] = (2 * (t0 + 400 + DECK_LOOP * DECKS_PER_SET + 400), ev)
    return runs


def flow(mapping: dict) -> str:
    def value(v):
        if isinstance(v, list):
            return "[" + ", ".join(value(x) for x in v) + "]"
        if isinstance(v, dict):
            return "{" + ", ".join(f"{k}: {value(x)}" for k, x in v.items()) + "}"
        return f'"{v}"' if isinstance(v, str) and (" " in v or "," in v) else str(v)
    return value(mapping)


def recipe_text() -> str:
    runs: list[dict] = []
    for mode in ("pc", "psx"):
        runs += [{"name": f"{mode}-L{level}", "level": level, "textures": mode} for level in range(13)]
    for mode in ("pc", "psx"):
        for skater, looks in SKATERS.items():
            if skater != DEFAULT_SKATER and mode == "pc":   # the logos are the same textures in both modes
                runs += [{"name": f"skater-{mode}-s{skater}-L{level}", "level": level, "textures": mode, "skater": skater, "look": 0} for level in LOGO_LEVELS]
            first = 0 if mode == "psx" and skater != DEFAULT_SKATER else 1   # a skater's own textures differ in one key between the modes
            runs += [{"name": f"look-{mode}-s{skater}-k{look}", "level": LOOK_LEVEL, "textures": mode, "skater": skater, "look": look}
                     for look in range(first, looks)]
    for mode, levels in (("pc", range(13)), ("psx", (12,))):   # every level: the competitions have a HUD of their own
        runs += [{"name": f"tricks-{mode}-L{level}", "level": level, "textures": mode, "frames": 2400, "actions": trick_actions(2400)} for level in levels]
    for stem in SESSION_SCENARIOS:
        document = yaml.safe_load((SESSIONS / f"{stem}.yml").read_text())
        keys = [{"poll": k["poll"], "key": k["key"], "hold": k["hold"]} for k in document["keys"] if k["key"] != "f9"]
        for mode in ("pc", "psx") if stem in BOTH_MODES else ("pc",):
            suffix = "" if mode == "pc" else "-psx"
            run = {"name": f"menu-{stem.removeprefix('boot-')}{suffix}", "textures": mode, "deliveries": document["deliveries"], "keys": keys}
            runs.append({**run, "undrawn": "world"} if stem in UNDRAWN_WORLD else run)
    for name, (deliveries, events) in menu_sessions().items():
        for mode in ("pc", "psx") if name in MENU_BOTH_MODES else ("pc",):
            runs.append({"name": name + ("" if mode == "pc" else "-psx"), "textures": mode, "deliveries": deliveries,
                         "keys": [{"poll": p, "key": k, "hold": h} for p, k, h in sorted(events)]})
    for mode in ("pc", "psx"):
        for name, (deliveries, events) in shop_sessions().items():
            runs.append({"name": f"menu-shop-{mode}-{name}", "textures": mode, "unlock": SHOP_UNLOCK, "deliveries": deliveries,
                         "keys":[{"poll": p, "key": k, "hold": h} for p, k, h in sorted(events)]})
    head = ["# The full texture census (docs/post_phase_5/textureupscale.md, T3): the runs that dump every texture the game can show, headless.",
            "# Written by scripts/make_texture_census.py (its docstring says why each run is here); run by scripts/texture_census.py. Keys and levels only, no pictures.",
            "version: 1",
            "frames: 600",
            '# levels and skaters unlocked; not the cheats (the census was taken without them; the trick menu runs with them since 11-18)',
            'env: {OT_UNLOCK: "levels,skaters"}',
            "runs:"]
    return "\n".join(head + [f"  - {flow(run)}" for run in runs]) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--check", action="store_true", help="fail if the recipe on disk is not what this script writes")
    args = parser.parse_args()
    text = recipe_text()
    if args.check:
        return 0 if RECIPE.read_text() == text else 1
    RECIPE.write_text(text)
    print(f"{text.count(chr(10) + '  - ')} runs -> {RECIPE}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
