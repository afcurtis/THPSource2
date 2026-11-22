# Contributing to THPSource2

Thanks for helping. This is a decompilation project, so a few rules keep it checkable and legal.

## Setup

Follow [docs/BUILDING.md](docs/BUILDING.md): WSL2 with Ubuntu 22.04 or later, `python3 -m venv .tools/venv`, then `.tools/venv/bin/pip install -e ".[dev]"`.
The map of the code and the reverse-engineering workspace is [docs/DEVELOPING.md](docs/DEVELOPING.md); day-to-day commands and known traps
are in [docs/WORKING.md](docs/WORKING.md).

## Before you open a pull request

- `./tony.sh verify --all` and `pytest -q` pass. Matching work must stay byte-identical.
- `ruff check tony tests` is clean (ruff is pinned in `pyproject.toml`).
- Match the line endings of the file you edit; do not re-save whole files with different endings.
- Matching work follows [match/POLICY.md](match/POLICY.md).

## Never commit game data

No game files, disc images, `ALL.PKR`, extracted assets, memory dumps, or the retail fixtures under `re/evidence/fixtures/` (they hold the
game's memory and are ignored by git on purpose). Bring your own copy of the game to test; the repository ships none of it.

## Bugs

Use the bug report template: launcher version, game release, Windows version, what happened, and the logs (after a crash, the launcher's red panel has a
**Report on GitHub** button that zips them and opens the form).
