# Game inputs (not tracked)

Provision the recorded disc image with:

```bash
tony setup media
```

`.gitignore` excludes everything in this directory except this README.

Initial expected media path:

```text
game/THPS2.img
```

Installed files should go under:

```text
game/installed/
```

Do not assume the executable filename. After installation:

```bash
tony exe identify game/installed/<actual-exe> --record
```

Only record an executable whose hash matches `executables.thps2_pc` in
`re/config/binaries.yml` (the Activision Value build on `game/THPS2.img`); every tracked
address belongs to that build. `tony media extract` provides it at
`build/disc/files/SETUP/data/THawk2.exe`.

Other builds are *variants*, never recorded with `--record`. They live under
`game/variants/<name>/` and are declared in `re/xref/config.yml`, e.g. the original
2000 retail executable that PARTYMOD and the Cumulative Patch target:

```text
game/variants/original-2000/THawk2.exe
```

Players may use that build too: its static data is rebuilt at the canonical addresses
(`tony xref static-recipe`, `re/evidence/xref-static-original-2000.md`). The original disc's own
`THawk2.exe` is SafeDisc-protected (data encrypted) and is not a usable variant.

Never patch canonical files in this directory. Copy anything used for a mutation experiment into `build/experiments/` first.
