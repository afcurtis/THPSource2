# Developing THPSource2

The landing page (`README.md`) is for players. This page is the way in for anyone working on the code: building, the reconstruction, the
reverse-engineering workspace and the `tony` command. To build and run the game itself, start with `docs/BUILDING.md`; the rules for
contributing are in `CONTRIBUTING.md`.

THPSource2 runs the PC version of Tony Hawk's Pro Skater 2 on its own executable: the game's code, reconstructed function by function from
the retail `THawk2.exe`, on a portable platform layer (SDL3), started from the THPSource2 launcher. The build runs on Linux (WSL2 on
Windows); the game runs on Windows 10/11.

## Where to start

| Working on | Read |
|---|---|
| How a working session runs: commands, gates, shipping, committing | `docs/WORKING.md` (and `AGENTS.md`) |
| What a player sees, the install, the run logs | `docs/PLAYING.md` |
| Building, packaging, the test gates | `docs/BUILDING.md` |
| The launcher (`src/launcher/`): settings, presets, the install folder | `docs/LAUNCHER.md` |
| `opentony.exe`, the standalone build, hooks, mods (the loose-file overlay) | `docs/HOOKS.md` |
| The platform layer (`src/hal/`): video, the GL backend, input, audio | `docs/HAL.md` |
| The 64-bit build | `docs/PORT64_AUDIT.md` |
| Reconstruction (`match/`, `re/`, `src/runtime/`) | `AGENTS.md`, then `docs/SLICE_WORKFLOW.md` |

The programs, the Python package and the environment variables keep the names they had upstream (`opentony.exe`, the `opentony`
package and its `tony` command, `OT_*` and `OPENTONY_*`).

## Layout

- `src/` — the reconstruction: `runtime/` (ported game code), `hooks/` (`opentony.exe`, the standalone, mods), `hal/`, `launcher/`, `assets/`, `trg/`, `camera/`, `collision/`.
- `re/` — reverse-engineering knowledge. The retail trace fixtures the tests replay (`re/evidence/fixtures/`) are **not** in the
  repository: they hold the retail game's memory, so you capture them from your own copy (`re/evidence/fixtures/README.md`).
- `match/` — byte-matching sources checked by `tony verify --all`.
- `tony/` — the Python `tony` workflow CLI. `tests/` — pytest.
- `game/` — user-supplied disc image / installed game. Never committed.
- `build/` and `.tools/` — generated output and provisioned tools. Never committed.

## Reverse-engineering setup

This section sets up the reverse-engineering workspace (Ghidra, Wine, the retail game under Wine). To build and run the game
itself, follow `docs/BUILDING.md`.

After provisioning the pinned Ghidra installation and local game inputs, create
the shared PyGhidra environment and canonical analysis project once from the
primary `main` worktree:

```bash
./tony.sh prerequisites bootstrap
```

Use `./tony.sh prerequisites repair` only when the shared PyGhidra installation
or canonical project must be recreated. Additional Git worktrees should run
`./tony.sh worktree prepare`; they receive a private writable project copy while
sharing immutable tools. `./tony.sh worktree verify` reports dynamic tracing,
live PyGhidra, and binary matching as separate capabilities.

The bootstrap expects your own disc image here:

```text
game/THPS2.img
```

On supported Linux distributions (Ubuntu/Linux Mint and Arch-family in this starter):

```bash
./scripts/bootstrap-linux.sh
source .tools/venv/bin/activate
```

To provision only the media (or retry it), run:

```bash
./tony.sh setup media
```

For a lightweight per-command setup, use the repository wrapper instead:

```bash
./tony.sh doctor
```

It creates `.tools/venv` when needed, installs the `opentony` package in editable mode, and forwards all arguments to `tony`.

Then:

```bash
tony doctor
tony media identify --record
tony verify
```

Do not convert or overwrite the only copy of the image. All extraction/conversion work belongs in `build/`.

Once the game is installed under `game/installed/`, identify the actual executable rather than assuming its filename:

```bash
tony exe identify game/installed/<GAME.EXE> --record
tony verify
```

Then initialize Wine and build the Ghidra project:

```bash
tony wine init
tony wine mount-disc
tony run
tony ghidra rebuild
tony ghidra export-functions
tony ghidra decompile 0x0041c2d0 --output build/ghidra/decomp/game-loop.c
```

`tony media extract` keeps the original image untouched, converts raw Mode 2/Form 1 CD sectors when needed, writes a normalized ISO and extraction manifest under `build/disc/`, and restores files under `build/disc/files/`. The normalized ISO contains only the declared ISO-9660 volume; use `tony media tracks` to inspect any raw tail beyond it. Generated Ghidra output is under `build/ghidra/`.

Asset extraction is currently two-stage for the PC installer: extract `ALL.PKR` first, then inspect or extract individual `.PRE` resource containers from the generated tree. For example:

```bash
tony assets extract-pkr build/disc/files/SETUP/data/ALL.PKR --output build/assets/all-pkr
tony assets extract-pre build/assets/all-pkr/files/data/LEVEL.PRE --output build/assets/level-pre
```

Both commands validate the observed container structure and write manifests under `build/`; the original archives remain untouched.

The extracted PC data also includes a `CD.HED` hash table, a matching `CD.HET` filename table, a separate `CD.HEP` filename table, and a `CD.WAD` payload file. Inspect the metadata with `tony assets inspect-hed`; `tony assets extract-hed` only writes payloads when the WAD contains data and the table describes non-overlapping direct ranges.

`tony wine mount-disc` attaches the generated ISO read-only, maps it as Wine `D:` with CD-ROM semantics, adds the raw-device `D::` mapping, and verifies that Wine can read the volume. A Linux loop device is enough for filesystem access but does not implement optical CD-ROM TOC ioctls; `tony run` and `tony play` use the generated no-CD executable to bypass that retail check. If the command warns that the raw loop device is not readable, grant the current user read-only access using the exact command it prints. Keep it mounted while running the game so disc-hosted assets remain available; clean up afterward with `tony wine unmount-disc`.

`tony exe patch-nocd` creates `THawk2.nocd.exe` beside the recorded executable and leaves the canonical retail executable unchanged. The generated copy bypasses the retail CD audio-TOC check; `tony run` and `tony play` create and use it automatically. They also launch inside a `1024x768` Wine virtual desktop, so the game cannot change the host monitor resolution. The filesystem disc mount is still useful because the game loads data, music, and movies from the disc. Use `tony play` to mount and launch, or `tony run` when the disc is already mounted.

## Useful commands

```bash
tony doctor
tony setup ghidra
tony setup media

tony media identify [path] [--record]
tony media tracks [path]
tony media list [path]
tony media extract [path] [--output build/disc] [--force]

tony assets inspect-pkr <path>
tony assets extract-pkr <path> [--output build/assets/pkr] [--force]
tony assets inspect-pre <path>
tony assets extract-pre <path> [--output build/assets/pre] [--force]
tony assets inventory <path>
tony assets inspect-trg <path> [--nodes] [--scripts]
tony assets inspect-psx <path> [--models] [--textures] [--tags]
tony assets extract-psx <path> [--output build/assets/psx] [--force]
tony assets explore [generated-asset-directory] [--open]
tony assets inspect-hed <path> [--entries]
tony assets extract-hed <path> [--output build/assets/cd-wad] [--force]

tony exe identify <path> [--record]
tony exe patch-nocd
tony verify

tony split init
tony split extract
tony split build
tony split symbols
tony split module <start-va> <end-va>
tony split compare <module-id-or-va>
tony split coverage
tony split propose-modules
tony split accept-proposal <name-or-address> [--dry-run]
tony split accept-proposals --tracked-only [--range START:END] [--dry-run]
tony split rebuild
tony split verify

tony types verify

tony wine init
tony wine mount-disc
tony wine unmount-disc
tony run
tony play
tony run --headless --screenshot build/debug/frame.png
tony debug --record build/debug/session.mp4
tony record start --session <debug-session>
tony record stop --session <debug-session>
tony record validate <recording.otrec>
tony record export-json <recording.otrec> --output <recording.jsonl>
tony replay retail <recording.otrec> [--session <debug-session>]
tony replay native <recording.otrec> [--output build/parity/native.jsonl]
tony scenario list
tony scenario capture warehouse-idle
tony scenario retail warehouse-idle
tony scenario native warehouse-idle
tony scenario verify warehouse-idle
tony sessions list
tony sessions stop <session>
tony sessions clean <session>

tony ghidra rebuild
tony ghidra export-functions
tony ghidra decompile 0x0041c2d0 --output build/ghidra/decomp/game-loop.c

tony experiments list
tony compare <trace-a.jsonl> <trace-b.jsonl>
```

## Toolchain choices

- Static analysis: **Ghidra 12.1.3**
- Ghidra automation: **PyGhidra / CPython 3**, not Jython
- Java: **JDK 21**
- Runtime: **Wine stable 11+**, normal 64-bit prefix / new WoW64 model
- Matching compiler: **Visual C++ 6.0 SP3** (`CL 12.00.8168`, `LINK 6.00.8447`) in an isolated Wine prefix
- Dynamic debugger: **WineDbg GDB proxy + GDB Python API**
- Orchestration: **Python**

We intentionally do **not** use Git submodules initially. Ghidra and Wine are provisioned tools, not source dependencies. External THPS/Neversoft projects belong in `references/` as links until we have a concrete reason to pin one as a source dependency.

See `docs/WORKFLOW.md`, `docs/RECONSTRUCTION_WORKFLOW.md` and `docs/TOOLING.md`
before doing substantial RE or reconstruction work.

## Credits in full

- **OpenTony** by Joao Matos ([tritao](https://github.com/tritao/OpenTony)). THPSource2 is based on it and continues its
  reconstruction, tools and recorded knowledge.
- **PARTYMOD for THPS2** by PARTYMANX (MIT): the launcher's settings are ported from it (`THIRD_PARTY_NOTICES.md`).
- **OpenAladdin** and **Quicky**, both by tritao: the workflow ideas this workspace follows. From OpenAladdin: a single discoverable
  workflow frontend (`tony`), pinned and SHA-verified Ghidra provisioning, PyGhidra as a native CPython integration, generated Ghidra
  projects instead of a local `.gpr` as the source of truth, tracked experiment definitions with a versioned trace format, and the
  original-vs-reimplementation differential tests. From Quicky: exact binary provenance and hashes before giving addresses a meaning,
  a strict separation of observation, inference and confirmation, controlled experiments on copies (never on the canonical inputs),
  and reproducible command lines with small validation tools. No game-specific code or tools are copied from either project.
- **thps2-tools** by JayFoxRox: file-format research, and the independent extractor `tests/test_pkr_crosscheck.py` checks against.
- The **THPS2 Cumulative Patch** and **thps2-demo-decomp**: reference only. Names, addresses and struct facts were cross-checked
  against them; none of their code is included.
- **SDL3**, **Dear ImGui**, **FFmpeg** (decode-only, LGPL), **zlib** and the **llvm-mingw** runtime: the libraries the programs
  use, with their licences in `THIRD_PARTY_NOTICES.md` (and `Licenses\` in a release).
