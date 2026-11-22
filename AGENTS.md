# THPSource2 agent instructions

## Start here

Read `docs/WORKING.md` at the start of every session: how a session runs, which doc a task needs (`docs/working/FEATURES.md` for
launcher, graphics, sound, options and mods; `docs/working/RECONSTRUCTION.md` for ported functions), gates, shipping to the test install,
and committing. Then read `AGENTS.local.md` if it exists (untracked, per machine): where this machine keeps the person's plans and project
log, the test install, and any local rules. A prompt like "do milestone W2 of weather.md" means `post_phase_5\weather.md` in that plans
folder, that milestone only. Reusable helper scripts are in `scripts/play/` (index in its README).

Commands from the Windows tools into WSL: put anything with `$`, quotes, loops, pipes or heredocs in a script file and run
`wsl.exe -d Ubuntu -- bash /path/x.sh` (inline, `wsl.exe` strips `$VAR` and mangles quotes and backslashes). Search with the Grep and Glob
tools, not a shell `grep -r`/`find`/`du` over `\\wsl.localhost` (minutes). Python is `.tools/venv/bin/python`; tony is `bash tony.sh` or
`.tools/venv/bin/tony`. Never clear `build/` or `.tools/` wholesale: gate state, session images the tests read and one-off probes live there.

## Reconstruction

Before changing `match/`, `re/`, or `src/`, read and follow the operational
quick start in `docs/SLICE_WORKFLOW.md`. Detailed policy lives in
`docs/RECONSTRUCTION_WORKFLOW.md`; exact matching status rules live in
`match/POLICY.md`. Do not redefine those rules elsewhere.

In particular, never mark a naked inline-assembly block as semantic `cpp`; use
the status required by `match/POLICY.md`.

Before committing matching work, run:

```bash
tony verify --all
pytest -q
```

`tony verify --all` must report a byte-identical rebuilt executable and reject
misclassified `vc6_asm`/`cpp` sources.

Gate depth follows the change: `tony gates quick` (about 2 minutes) is enough when
only the GL backend (`src/hal/`), the launcher or docs changed behind an option that is
off by default, because the gates run with no display options and cannot reach that code.
Run `tony gates full` (about 11 minutes) when a ported function in `src/runtime/`, a
`match/` or `re/` file, or anything that changes the draw stream changed.

The retail fixtures (`re/evidence/fixtures/`) are not tracked: never `git add -f` one (`re/evidence/fixtures/README.md`).

Use the whole machine for verification: `pytest` runs on all cores by default
(`-n auto` in `pyproject.toml`; `-n 0` for a serial debug run), and native
builds and `ctest` take `-j"$(nproc)"`. Run long verification in the
background alongside other work.
