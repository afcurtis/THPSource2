# scripts/play: the helper scripts of the working loop

These are the scripts that `docs/WORKING.md` and its sections name: headless runs, fuzzing, pictures, packaging, gate housekeeping, and the probes
and replays used to port a retail function. Until 2026-11-22 they lived in `build/<dir>/` (git-ignored) and were nearly lost in a cleanup;
they are tracked now.

- **Old references resolve by one rule:** `build/<dir>/<file>.py|.sh` in a plan, a phase log or a source comment is now
  `scripts/play/<dir>/<file>`. The directory names are kept (`m12`, `m10g`, `s9`, ...) so that rule always holds.
- **Outputs stay in `build/<dir>/`** (logs, runs, pictures, dumps); each script makes that directory when it runs. Run from the repository
  root: `bash scripts/play/m12/package.sh`, `.tools/venv/bin/python scripts/play/m12/replay_play.py ...`.
- **A new helper you will use again goes here**, in the directory of its area, with a line below. A one-off probe stays in `build/<dir>/`.
- Not recovered (rewrite when needed): `m12/menu_shots.py` (a menu's picture at any aspect: boot with SDL video under xvfb, Enter at polls 500
  and 600, frames as PNG and `.otvh`), `c1/ride.py`, `c1/capdiff.py` (camera rides), `m10e/kill.sh` (`p46/kill.sh` does the same),
  `p410b/hud_build.sh`, and Phase 4's `p410*`/`p411*` probes.

## Play mode: reproduce, fuzz, package

| Script | Does |
|---|---|
| `m12/package.sh` | `tony hooks build --arch 64` + `tony release package --arch 64`; log `build/m12/package.log` (then `tony release install`, `docs/WORKING.md`) |
| `m12/replay_play.py SCRIPT [POLLS] [NAME]` | replays a player's run (an `OT_INPUT_LOG` script from `scripts/input_log_to_script.py`) headless with the player's options |
| `m12/levels_spin.py [LEVEL ...]` | the standalone alone on levels: frames recorded and the stop, one line a level |
| `m12/fuzz_hangar.py`, `fuzz_career.py`, `fuzz_levels.sh`, `wide_fz.sh` | random inputs: Hangar riding, keys through the career menus, over many levels |
| `m12/rerun.sh`, `rerun_seed.py`, `wide_redo.sh` | rerun one seed, or every run a wineserver kill ended early |
| `m12/fuzz_compare.sh`, `fuzz_to_yml.py` | a fuzz seed against retail (untracked scenario files, removed after) |
| `m12/sym.py [--dll PATH] ADDR...` | names the addresses of a `CRASH c0000005` report |
| `m10e/killgates.sh`, `m10/killall.sh`, `p46/kill.sh` | stop a stuck gates run / trace chain / probe and its leaked Xvfb and Wine (by PID) |
| `nb/bisect_gates.sh` | the smoke beside groups of the CPU lane (the open "gates full in one go" item) |

## Feature checks and pictures (templates: copy and adapt)

| Script | Does |
|---|---|
| `g9/ride.py`, `g9/replay_shots.py` | ride forward or replay a player's script and keep chosen frames as PNG |
| `g1/shot.py`, `g1/crop.py` | a level rendered headless (SDL GL under Wine + Xvfb), chosen frames as PNG; crops of them |
| `qs/run.py NAME LEVEL MODE DELIVERIES [poll:key:hold ...]` | a headless quick start with keys by poll |
| `qp/run.py SCRIPT TAG MODE` | a headless quick start replaying a script, frames 300..2400 |
| `fr/sweep.py`, `fr/trick.py`, `fr/trf.py` | frame-rate checks: parallel sweeps (4 workers, staggered), tricks at 30/60, traffic and platforms per delivery |
| `g4/census.py`, `sweep.py`, `table.py`; `t1/census.py` | level-of-detail census over levels and its table; texture census (texture dump on) |
| `nb/umedia.py`, `nb/badmusic.sh` | a non-ASCII media folder; unplayable songs in the custom soundtrack |

## Reconstruction (porting a retail function: `docs/working/RECONSTRUCTION.md`)

| Directory | Scripts |
|---|---|
| `m10/` | `gen.sh` (rows, `hooks generate`, DLL build), `cycle.sh`, `parity.sh`, `lk.sh SESSION` (symbolise a run's caller words), `bytes.sh ADDR N`, `dis.sh`, `decomp.sh`, `cov10c.py`, TRG trace runs |
| `m10e/` | `dis2.sh ADDR LEN` (disassembly across module files), `nb.sh` (native build), probe templates `*_trace.py` (loops, leaves, two functions in one probe), Ob_AI trace/replay chain, `mutate.sh`, `eol.sh`, `jt.sh`, `respin.sh`, `spin.py` |
| `m10c/` | `nb.sh` |
| `m10g/` | `mutate.py` + `mut23.py`/`mut24.py`/`mut24b.py` (mutant lists), captures `cap23.sh`/`cap24.sh`/`cap24c.sh`/`chainG.sh`, replays `rep_one.sh`/`repall.sh`/`rep_bit.sh`, `next.sh` (rows + build + spin, prints the next FATAL), watches `w22.sh`/`sp22.py`/`rw.sh`/`whw22.sh`/`retail_watch2.py`, dumps `whd22.sh`/`zd22.py`/`s25*.py`/`rd20.sh`/`vx22.*`/`ar22.sh`, `rowless.py`, `vtscan17.py`, `rail23*.sh` |
| `m10h/` | `reach.py`, `reach_report.py` (resolver traces) |
| `s9/`, `s9a/` | dump diffs `ddiff.py`/`zdiff.py`/`zones.py`/`arena.py`, `retail_watch.py`, `boot_run.py`, `cycle.sh`; `s9a/build.sh`, `sw.py`, `lk.py` |
| `s10d/` | `arity2.py` (live `ServiceFault` sites), `dis.sh` |
| `standalone-warehouse/` | `realdiff.py A B` (masked `.data` diff), `run.sh` |
| `p51t/` | `crashbis.py SCENARIO` (which rows crash a session) |
