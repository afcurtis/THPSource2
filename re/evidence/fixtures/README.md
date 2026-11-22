# Retail fixtures (not in the repository)

The reconstruction's parity tests replay **retail fixtures**: records of the retail `THawk2.exe` running under Wine and gdb, one per group of
forced calls or traced frames. A record holds the game's memory around each call (`C ... pokes=ADDR=BYTES;...`), reads of game memory
(`R ADDR BYTES`, up to hundreds of kilobytes for a level's tables), and the calls and writes the function made.

Because a fixture is a copy of the retail game's memory (its tables, text and level data), **none are published**. This folder is ignored
by Git except for this README and the four `*.json` notes, which are derived observations (collision policies, timer samples, a player
frame's handlers and positions), not memory; `re/types/physics.yml` cites one as evidence. You make the rest from your own copy of the game.

## Without fixtures

- `pytest`: every test module that reads `re/evidence/fixtures/` is skipped ("retail fixtures not present"); the rest run.
- `ctest`: a case whose command names a fixture that is missing is added disabled (`ot_add_test` in `CMakeLists.txt`) and listed as not run.
- `tony verify --all` (byte matching) and the recorded scenarios (`tony gates full`) do not read them.

## Making them

You need the game set up for reverse engineering (docs/DEVELOPING.md "Reverse-engineering setup": `game/THPS2.img`, `tony wine init`, the
recorder). Each fixture is made by a probe spec in `re/gdb/probes/` and a capture script; `docs/working/RECONSTRUCTION.md`
"Playbook B" step 3 and 4 is the method:

| Fixtures | Spec | Capture |
|---|---|---|
| `skater-calls-*` | `re/gdb/probes/gpu_specs.py` | `scripts/play/m10g/cap24.sh NAME CASES SEED` |
| world, career, `trg-*` | `re/gdb/probes/world_specs_*.py` | `scripts/play/m10g/chainG.sh`, `scripts/make_career_calls_fixture.py` |
| `bit-calls-*` | `re/gdb/probes/bit_specs*.py` | `scripts/play/m10g/cap24c.sh NAME CASES SEED` |
| HUD panels | `re/gdb/probes/frontend_specs_panel.py` | `scripts/play/m10g/cap23.sh` |

A capture of 500-800 cases takes 1-25 minutes; fixtures of 1-23 MB are normal, about 1.1 GB for the full set. The cases are random legal
states drawn from the SEED argument. The test that replays a fixture names it (the
`FIXTURE`/`FIXTURES` paths in `tests/` and the `ot_add_test` lines in `CMakeLists.txt`).
