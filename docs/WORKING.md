# Working on THPSource2

How a session works here. The person prompts in plain English ("do milestone W2 of weather.md") and plays the builds; the AI reads, builds, tests,
installs and commits. Plans and the project log live outside this repository, in the person's plans folder; `AGENTS.local.md` (untracked, per
machine) names it, the test install and any local rules. Read this file whole; read only the section of `docs/working/` the task needs.

## 1. Start of a session

1. The plan the prompt names (a feature plan in `post_phase_5\NAME.md` of the plans folder; play-mode fixes: `phases\PHASE_6_SUMMARY.md`).
   Read its status, the one milestone asked for, and its Constraints and Standing facts. Do not read the milestone log or `[complete]` plans unless
   the milestone points there.
2. The section for the kind of work:

| Work | Read |
|---|---|
| A feature or a fix a player sees: launcher, graphics, sound, HUD, options, mods, packaging | `docs/working/FEATURES.md`, then the area's doc (`docs/LAUNCHER.md`, `docs/HAL.md`, `docs/HOOKS.md`) |
| Porting or changing a retail function's port (`src/runtime/`), a scenario that diverges | `docs/working/RECONSTRUCTION.md`, `docs/SLICE_WORKFLOW.md` |
| `match/`, `re/` | `docs/SLICE_WORKFLOW.md`, `match/POLICY.md` |

3. Helper scripts are in `scripts/play/` (index: `scripts/play/README.md`). A path `build/<dir>/<file>.py|.sh` in an old note is
   `scripts/play/<dir>/<file>` now.

## 2. Running commands (Windows tools driving WSL)

The tools run on Windows; the repository is in WSL (`\\wsl.localhost\Ubuntu\home\<user>\THPSource2` = `~/THPSource2`).

- **Anything with `$`, quotes, a loop, a pipe or a heredoc goes in a script file**: write it with the Write tool (scratchpad or `build/<dir>/`), run it with
  the PowerShell tool as `wsl.exe -d Ubuntu -- bash /path/to/x.sh` (or `-- python3 /path/x.py`). Passed inline, `wsl.exe` strips `$VAR`, `$?`
  and `awk '{print $1}'`, halves backslashes (`'\\n'` arrives as a newline) and mangles quotes. A one-word command (`wsl.exe -d Ubuntu -- ls ~/x`) is fine inline.
  A `| tail` after `wsl.exe ...` runs on the Windows side, where there is no `tail`.
- **Search and read with the Grep, Glob and Read tools** on the `\\wsl.localhost` path. A shell `du`, `find` or `grep -r` over that path from Windows
  takes minutes; run those inside WSL from a script.
- **Long runs** (> 1 minute: builds, gates, fuzzing, pytest): one `run_in_background` call per script. Never `&`, `nohup` or `(cmd &)` inside
  `wsl.exe`: the children die when `wsl.exe` returns.
- **Python and tony:** `.tools/venv/bin/python`, `.tools/venv/bin/tony` (or `bash tony.sh ...`: it is committed executable, but a Windows-side edit can drop the bit, so never commit a mode change
  to it). Same for `run.sh`. There is no `python`/`pytest` on WSL's PATH.
- **Line endings:** the index is LF. Check `git diff --ignore-space-at-eol` and `git ls-files --eol` before a commit; `tony hooks generate` may rewrite
  `kit_abi.h` with CRLF (`git checkout` it). `scripts/play/m10e/eol.sh` lists files that differ only by line endings.
- **Killing:** by PID only. `pkill -f NAME` inside `bash -c` kills its own shell and `while pgrep -f NAME` matches itself: wait on a done-file.
  A script named `dis.py` / `bisect.py` shadows a module: pick another name.
- **Never edit a generated file** (`*_generated.*`, `standalone_natives.yml`): change its source and regenerate. A new ctest or a deleted
  fixture needs `cmake -S . -B build/native` first (CMake globs).

## 3. The machine

i5-12600 (12 threads), 15 GB in WSL. pytest runs `-n auto`; builds and ctest take `-j"$(nproc)"`.

- Gates use `min(cores/2, RAM/1.5 GB)` = 6 Wine workers (`.tools/wineprefix-head-w00..w05`, `build/gw/00..05`) and 3 CPU jobs.
- **Never at once:** two `gates full`; a retail run, a capture or gates while fuzz jobs run (they share `.tools/wineprefix-headless`, and a
  retail run's `wineserver -k` kills the others); an edit of `tony/` or a source while a gate or a mutation run is going.
- Every Wine run fails: a leftover `THawk2.nocd.exe` or Wine processes from a crashed run (`scripts/play/m10/killall.sh`,
  `scripts/play/m10e/killgates.sh`, `scripts/play/p46/kill.sh`).

## 4. Where files go

- **`build/`** is generated output: the native and hook builds, gate state (`build/gw`, `build/gates`), session runs and images the tests read
  (`build/sessions`, `build/standalone-*`), the extracted disc and assets, and one-off probes in `build/<dir>/`. Delete only what you made this
  session; never clear `build/` or `.tools/` wholesale.
- **A helper you will use again** goes in `scripts/play/<area>/` with a line in its README. A recurring check becomes a test or a `tony` command.
- **Knowledge** that outlives the session goes in the plan's Standing facts, in this file's sections (a trap that generalises), or in the
  area's doc in `docs/`. Not in chat only.

## 5. Finishing a milestone

1. Narrow checks while working: the unit test of what changed, `tony hooks smoke` (~35 s after a hook build), `launcher_core` ctest for launcher work.
2. **Gate depth** (`AGENTS.md`): `tony gates quick` (~2 min) for the GL backend, the launcher or docs behind an off-by-default option; `tony gates full`
   (~11 min) when a ported function in `src/runtime/`, `match/`, `re/` or the draw stream changed. Start it with `run_in_background` and watch
   `build/gates/status.txt` for `exit [1-9]`, `aborted`, `DONE` (the `fault-*` controls print `exit 1` by design; the parent step counts).
   - Known flakes: `could not translate path for Wine`, `timed out waiting for Xvfb display metadata`, `no session.done` over a run of `session/`
     steps, a `fault-*` control: rerun with `--only PREFIX`.
   - `gates full` in one go has stopped at `smoke/session` since 11-17 (open item in `PHASE_6_SUMMARY.md`): run the two lanes apart,
     `gates full --only smoke session standalone parity` and `gates full --only native-build ctest verify-all pytest selftest replay32 hooks-build-64`.
   - Orphaned Wine `services.exe` from an earlier run can fail `smoke/session` repeatedly: kill them by PID, rerun.
3. Before committing matching work: `tony verify --all` and `pytest -q` (`AGENTS.md`).
   - `pytest -q` skips the Wine tests. With `OPENTONY_WINE_TESTS=1` they run under `-n auto` on one shared headless prefix at once, and the
     `boot-*` cold-boot comparisons fail with `no session.done` or diverge from delivery 2: run them `-n 0`, or a file at a time.
     `test_standalone_spike.py::test_a_process_without_the_retail_image_answers_like_retail_on_a_career_image` fails at HEAD (`0 refused`, 11-25).
4. **Ship to the person's test install** when the player can see the change: `bash scripts/play/m12/package.sh` (or `tony hooks build --arch 64`
   then `tony release package --arch 64`: a package alone ships the old DLL), then
   `bash tony.sh release install --to <test install>` (the path is in `AGENTS.local.md`). The launcher must be closed (it holds `Build\SDL3.dll`; an install
   with it open stops half-way). Install replaces `Build\`, `Licenses\`, the launcher and `README.md` and never touches `Config`, `Data`, `Saves`,
   `Mods`, `Screenshots`, `Logs`; an emptied folder is made whole with `--game-files <folder with THawk2.exe, ALL.PKR, Music, med/Movies>`.
5. **Write it down:** the plan's status (replace, do not append), new Standing facts, one short line in the log (a feature plan's own
   Milestone log; play-mode fixes: `phases\PHASE_6_LOG.md` in the plans folder): what changed, the checks with their counts, what the person should look at in play.
   A trap that generalises goes in the `docs/working/` section it belongs to. Say "waiting for the person's play" when only they can confirm it.
6. **Commit unasked** at the end of every session and at any stopping point: this repository, and the plans folder when its docs changed. Stage
   files by name; `tony.sh` (mode only), `.claude/`, `AGENTS.local.md` and the fixtures stay out. No force-push, no skipped hooks.
