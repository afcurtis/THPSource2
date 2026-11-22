"""scripts/texture_classes.py and texture_census.py's recipe and merge (docs/post_phase_5/textureupscale.md, T3): the class rule, applied in its order."""

import importlib.util
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def _load(name: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


classes = _load("texture_classes")
census = _load("texture_census")
pack = _load("texture_test_pack")

INDEX_HEADER = "# key\twidth\theight\tformat\talpha\tuse\twrap\tmod2x\tvolatile\ttag"


def k(n: int) -> str:
    return f"{n:016x}"


def _census(root: Path, keys: dict[str, tuple[str, list[tuple[str, str]]]]) -> None:
    """A merged census: keys -> (use, [(run, draw|walk)]); runs: every level 0..12 in pc, a skater run, a menu session."""
    root.mkdir()
    runs = ["# name\tkind\ttextures\tlevel\tskater\tlook\tfinished\tseconds\tkeys\tnew\tskater_line"]
    runs += [f"pc-L{level}\tlevel\tpc\t{level}\t\t\t1\t10\t0\t0\t" for level in range(13)]
    runs += ["look-pc-s1-k1\tskater\tpc\t12\t1\t1\t1\t10\t0\t0\t\tworld", "menu-pc-boot\tsession\tpc\t\t\t\t1\t10\t0\t0\t\t2d",
             "menu-park\tsession\tpc\t\t\t\t1\t10\t0\t0\t\tworld"]
    (root / "runs.tsv").write_text("\n".join(runs) + "\n")
    index, seen = [INDEX_HEADER], []
    for key, (use, where) in keys.items():
        index.append(f"{key}\t16\t16\targb1555\topaque\t{use}\twrap\tno\t0\t{where[0][0]}")
        seen += [f"{run}\t{key}\t{how}" for run, how in where]
    (root / "index.tsv").write_text("\n".join(index) + "\n")
    (root / "seen.tsv").write_text("\n".join(seen) + "\n")


EVERY_LEVEL = [(f"pc-L{level}", "walk") for level in range(13)]


def _table(tmp_path: Path, play: list[Path] = (), person: dict[str, str] | None = None) -> dict[str, tuple[str, str]]:
    evidence = classes.Evidence()
    evidence.add_census(tmp_path / "census")
    for log in play:
        evidence.add_play(log)
    return classes.classify(evidence, person or {})


def test_rule_order(tmp_path):
    _census(tmp_path / "census", {
        k(1): ("world", [("pc-L3", "draw")]),                          # drawn in the world
        k(2): ("2d", [("pc-L3", "draw")]),                             # drawn in 2D
        k(3): ("both", [("pc-L3", "draw")]),                           # drawn both ways: world, listed
        k(4): ("unknown", [("menu-pc-boot", "walk")]),                 # a menu's store only: frontend
        k(5): ("unknown", EVERY_LEVEL),                                 # every level's store, never drawn: global (a font glyph)
        k(6): ("unknown", [("pc-L4", "walk")]),                        # one level's store: level
        k(7): ("unknown", [("look-pc-s1-k1", "walk")]),                # a skater run's store: level
        k(8): ("unknown", [("menu-pc-boot", "walk"), ("pc-L5", "walk")]),   # a menu and a level (a session that enters one): level, not frontend
        k(9): ("2d", EVERY_LEVEL),                                      # drawn wins over global
        k(10): ("unknown", EVERY_LEVEL[:12]),                           # 12 of 13 levels: not global
        k(11): ("unknown", [("menu-pc-boot", "walk")] + EVERY_LEVEL),   # menus and every level: global, 2d
        k(12): ("unknown", [("menu-park", "walk")]),                    # a session marked undrawn: world (the park editor): level
        k(13): ("unknown", [("menu-park", "walk"), ("menu-pc-boot", "walk")]),   # and in a menu as well: still level
    })
    table = _table(tmp_path)
    assert table == {
        k(1): ("world", "drawn"), k(2): ("2d", "drawn"), k(3): ("world", "drawn-both"), k(4): ("2d", "rule:frontend"), k(5): ("2d", "rule:global"),
        k(6): ("world", "rule:level"), k(7): ("world", "rule:level"), k(8): ("world", "rule:level"), k(9): ("2d", "drawn"), k(10): ("world", "rule:level"),
        k(11): ("2d", "rule:global"), k(12): ("world", "rule:level"), k(13): ("world", "rule:level"),
    }


def test_siblings_rule(tmp_path):
    font = [("menu-pc-boot", "walk"), ("pc-L5", "walk")]     # the menu font: every menu's store and a level's (the pause menu)
    level = [("pc-L6", "walk")]
    _census(tmp_path / "census", {
        k(1): ("2d", font), k(2): ("2d", font), k(3): ("2d", font),
        k(4): ("unknown", font),                                         # three drawn siblings, all 2D: 2d, before rule:level
        k(5): ("unknown", [("menu-pc-boot", "walk")] + font[1:] + [("pc-L7", "walk")]),   # another run set: no siblings
        k(6): ("world", level), k(7): ("world", level), k(8): ("2d", level),
        k(9): ("unknown", level),                                        # drawn siblings disagree: the rules below
        k(10): ("world", [("pc-L8", "draw")]), k(11): ("world", [("pc-L8", "draw")]),
        k(12): ("unknown", [("pc-L8", "walk")]),                         # two drawn siblings are not enough
    })
    table = _table(tmp_path)
    assert table[k(4)] == ("2d", "rule:siblings")
    assert table[k(5)] == ("world", "rule:level")
    assert table[k(9)] == ("world", "rule:level")
    assert table[k(12)] == ("world", "rule:level")


def test_play_evidence_and_person_rows(tmp_path):
    _census(tmp_path / "census", {k(1): ("unknown", EVERY_LEVEL), k(2): ("unknown", [("pc-L4", "walk")]), k(3): ("world", [("pc-L4", "draw")])})
    log = tmp_path / "run.txt"
    log.write_text("video: pack use: 0000000000000001 16x16 replaced world\n"      # a "global" key the person saw in the world
                   "video: pack use: 0000000000000002 16x16 missed 2d\n"
                   "video: pack use: 00000000000000ff 64x32 missed world\n"        # a key no census run has: a recipe gap
                   "video: pack use: 00000000000000fe 176x48 missed 2d\n")         # a frame of the level select's movie: not a texture set
    table = _table(tmp_path, [log], person={k(3): "2d"})
    assert table[k(1)] == ("world", "drawn")
    assert table[k(2)] == ("2d", "drawn")
    assert table[k(3)] == ("2d", "person")
    assert table[k(255)] == ("world", "drawn")
    assert k(254) not in table


def test_table_written_keeps_person_rows(tmp_path):
    _census(tmp_path / "census", {k(1): ("world", [("pc-L1", "draw")]), k(2): ("unknown", [("menu-pc-boot", "walk")])})
    table_path = tmp_path / "classes.tsv"
    evidence = classes.Evidence()
    evidence.add_census(tmp_path / "census")
    classes.write_table(table_path, evidence, classes.classify(evidence, {}))
    text = table_path.read_text()
    assert f"{k(1)}\t16\t16\tworld\tdrawn\t1" in text and f"{k(2)}\t16\t16\t2d\trule:frontend\t1" in text
    table_path.write_text(text.replace(f"{k(1)}\t16\t16\tworld\tdrawn", f"{k(1)}\t16\t16\t2d\tperson"))
    assert classes.read_person(table_path) == {k(1): "2d"}
    assert classes.classify(evidence, classes.read_person(table_path))[k(1)] == ("2d", "person")


def test_recipe_loader_checks_runs(tmp_path):
    recipe = tmp_path / "r.yml"
    recipe.write_text("version: 1\nframes: 300\nenv: {OT_UNLOCK: skaters}\nruns:\n  - {name: a, level: 1, textures: pc}\n"
                      "  - {name: b, level: 12, textures: psx, skater: 19, look: 3, frames: 100}\n  - {name: c, textures: pc, deliveries: 900, keys: [{poll: 5, key: enter, hold: 2}]}\n")
    defaults, runs = census.load_recipe(recipe)
    assert defaults["env"] == {"OT_UNLOCK": "skaters"}
    assert [r["frames"] for r in runs] == [300, 100, 300]
    assert [census.kind_of(r) for r in runs] == ["level", "skater", "session"]
    for bad in ("  - {name: d, level: 1, textures: gl}\n", "  - {name: a, level: 2, textures: pc}\n", "  - {name: e, textures: pc}\n",
                "  - {name: f, level: 1, textures: pc, colour: red}\n"):
        recipe.write_text(recipe.read_text() + bad)
        try:
            census.load_recipe(recipe)
        except SystemExit:
            pass
        else:
            raise AssertionError(f"accepted {bad!r}")
        recipe.write_text(recipe.read_text().replace(bad, ""))


def test_shipped_recipe_loads_and_matches_its_generator():
    _, runs = census.load_recipe(census.DEFAULT_RECIPE)
    assert {(r["textures"], r["level"]) for r in runs if census.kind_of(r) == "level"} == {(m, level) for m in ("pc", "psx") for level in range(13)}
    looks = {(r["skater"], r["look"]) for r in runs if census.kind_of(r) == "skater"}
    assert {s for s, _ in looks} == set(range(13)) | {17, 18, 19}   # every pro and hidden skater; rows 13-16 are created skaters
    assert _load("make_texture_census").recipe_text() == census.DEFAULT_RECIPE.read_text()


def test_merge_unions_runs(tmp_path):
    out = tmp_path / "out"
    for name, rows in {"r1": [(k(1), "world", "wrap"), (k(2), "unknown", "unknown")], "r2": [(k(1), "2d", "clamp"), (k(3), "2d", "clamp")]}.items():
        folder = out / "runs" / name
        folder.mkdir(parents=True)
        index = [INDEX_HEADER]
        for key, use, wrap in rows:
            pack.write_png(folder / f"{key}.png", 1, 1, b"\1\2\3\4")
            index.append(f"{key}\t1\t1\targb1555\topaque\t{use}\t{wrap}\tno\t0\t{name}")
        (folder / "index.tsv").write_text("\n".join(index) + "\n")
        (folder / "seen.tsv").write_text("".join(f"{name}\t{key}\tdraw\n" for key, _, _ in rows))
        (folder / "done.txt").write_text("10.0\n\n")
    census.merge([{"name": "r1", "level": 1, "textures": "pc"}, {"name": "r2", "level": 2, "textures": "pc"}], out)
    rows = {line.split("\t")[0]: line.split("\t") for line in (out / "index.tsv").read_text().splitlines() if not line.startswith("#")}
    assert rows[k(1)][5:7] == ["both", "both"] and rows[k(1)][9] == "r1"
    assert rows[k(3)][9] == "r2"
    assert sorted(p.name for p in out.glob("*.png")) == [f"{k(n)}.png" for n in (1, 2, 3)]
    runs = [line.split("\t") for line in (out / "runs.tsv").read_text().splitlines()[1:]]
    assert [(r[0], r[8], r[9], r[11]) for r in runs] == [("r1", "2", "2", "world"), ("r2", "2", "1", "world")]


def test_two_colour_pack(tmp_path):
    dump = tmp_path / "dump"
    dump.mkdir()
    index = [INDEX_HEADER]
    for key, (w, h) in {k(1): (16, 16), k(2): (8, 16), k(3): (8, 8), k(4): (16, 16)}.items():
        pack.write_png(dump / f"{key}.png", w, h, bytes([100, 100, 100, 255]) * (w * h))
        index.append(f"{key}\t{w}\t{h}\targb1555\topaque\tunknown\tunknown\tno\t0\tpc:L1")
    (dump / "index.tsv").write_text("\n".join(index) + "\n")
    (dump / "seen.tsv").write_text("")
    table = tmp_path / "classes.tsv"
    table.write_text(f"# header\n{k(1)}\t16\t16\tworld\tdrawn\t1\n{k(2)}\t8\t16\t2d\trule:global\t1\n{k(3)}\t8\t8\tworld\trule:level\t1\n{k(5)}\t16\t16\t2d\tdrawn\t0\n")
    assert pack.make_pack(dump, tmp_path / "out", [], 2, "tint", classes=table) == 2   # world 16 a side, 2d of any size; k(3) world under 16, k(4) not in the table, k(5) not dumped
    _, _, world = pack.read_png((tmp_path / "out" / f"{k(1)}.png").read_bytes())
    _, _, twod = pack.read_png((tmp_path / "out" / f"{k(2)}.png").read_bytes())
    assert world[:4] == bytes([65, (100 * 65 + 255 * 35) // 100, (100 * 65 + 255 * 35) // 100, 255])   # cyan
    assert twod[:4] == bytes([(100 * 65 + 255 * 35) // 100, 65, (100 * 65 + 255 * 35) // 100, 255])    # magenta
