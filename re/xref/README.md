# Cross-reference knowledge (`tony xref`)

Everything in `re/symbols`, `re/types` and `re/native` is keyed to the canonical
executable `executables.thps2_pc` (SHA-256 `f2c7ca7c…`, the Activision Value
re-release on `game/THPS2.img`). This directory records **external evidence** that
has been translated onto that build, and the inputs needed to regenerate it.

| File | Produced by | Contents |
|---|---|---|
| `config.yml` | hand-written | Variant executables (identity + section ranges) and pinned reference projects |
| `subsystems.yml` | hand-written | Source file → subsystem grouping for coverage reports |
| `original_2000.yml` | `tony xref match-builds` | Function and data address map original-2000 → canonical |
| `original_2000_static.yml` | `tony xref static-recipe` | Recipe that rebuilds the canonical static-data image from the original-2000 executable (copies, address breakpoints, a few literal bytes); `src/launcher/original_2000_static.inc` is the launcher's copy |
| `source-units.yml` | `tony xref units` | Canonical function → Neversoft source file (retail assert paths) |
| `community.yml` | `tony xref community` | Names/addresses declared by PARTYMOD and the Cumulative Patch |
| `psx-demo.yml` | `tony xref psx` | PSX demo symbols matched onto canonical functions |
| `types.yml` | `tony xref types` | Recovered types vs PSX/Cumulative Patch struct names |
| `library-matches.yml` | `scripts/match_library_signatures.py --write` | Executable functions whose code equals a VC6 `LIBC.LIB`/`LIBCMT.LIB` object, relocations masked (the C runtime) |
| `attribution.yml` | `scripts/neighbour_scan.py --write-attribution` (`generated:`), hand-written (`reviewed:`) | Source unit of functions with no assert path: interior of one file, or a split between two by callers and data statics |
| `milestones.yml` | hand-written | Phase 4 sub-milestone that owns each source unit (or address range); sizes the remaining work in `tony xref coverage` |

`re/symbols/scope.yml` (hand-written, next to the other curated knowledge) tags the functions Phase 4 replaces
instead of reconstructing; `tony xref coverage` reports native / in-scope from it.

`tony xref apply` merges these into `re/symbols/imported.yml`, which `tony ghidra
rebuild`/`sync` apply as `IMPORTED` symbols **only where no curated name exists**.
Imported names are `inferred` or `provisional`; promote one into
`re/symbols/functions.yml` (with its own evidence record) once it has been reviewed.

## Regenerating

```bash
tony xref fetch-references           # pinned clones under build/xref/references/
tony xref all                        # features -> match-builds -> units -> community -> psx -> types -> apply -> coverage
tony ghidra sync                     # apply re/symbols/imported.yml to the Ghidra project
```

`tony xref features --build original_2000` needs the original 2000 executable at the
path recorded in `config.yml` (`game/variants/original-2000/THawk2.exe`, never committed).
`tony xref psx --holdout` reports the PSX matcher's precision on hidden anchors.
`tony xref static-recipe` (`--check` to only compare) needs both executables; rerun it after `match-builds` changes
`original_2000.yml` or `config.yml`'s `static_masked` changes. It refuses to write a recipe whose result is not the
promised image.

Cartography (4.12), not part of `tony xref all` because it needs the VC6 toolchain and a feature export:

```bash
python scripts/match_library_signatures.py --write   # re/xref/library-matches.yml (names: `tony xref apply`)
python scripts/neighbour_scan.py                     # runs of unattributed functions: build/xref/neighbour-scan.{md,json}
python scripts/neighbour_scan.py --write-attribution # refresh the `generated:` rows of attribution.yml
tony xref coverage                                   # native / in-scope and the per-sub-milestone sizes
```

## Evidence records

- `re/evidence/xref-cross-build.md` — original-2000 ↔ canonical address map
- `re/evidence/xref-static-original-2000.md` — the original release's static data rebuilt at canonical addresses (players of that release)
- `re/evidence/xref-source-units.md` — assert-path source-file attribution
- `re/evidence/xref-community.md` — PARTYMOD / Cumulative Patch names
- `re/evidence/xref-psx-demo.md` — PSX demo symbol matching and its validation
- `re/evidence/xref-libraries.md` — C runtime and zlib identification, the scope map's evidence, the netgame surface

## Licensing

Only facts (names, addresses, unit membership, call edges, struct names/sizes) are
recorded. No code from the reference projects is copied: thps2-demo-decomp has no
blanket license, and the Cumulative Patch is GPL-3.0. PARTYMOD is MIT.
