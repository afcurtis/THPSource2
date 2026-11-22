# Original-2000 static data rebuilt at canonical addresses

Status: confirmed (rebuild checked by SHA-256; session parity equal outside the masked key)
Build: canonical `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`;
variant `003152194111a3e564c1a458da2a02cc1187a4f47c147a4126f5d37913cfee8a`
Recipe: `re/xref/original_2000_static.yml` (`tony xref static-recipe`, tony/static_variant.py);
launcher copy `src/launcher/original_2000_static.inc`

## Observation

The standalone needs only the static data of `THawk2.exe` (its non-code sections, at canonical addresses:
tony/static_data.py); everything else it reads comes from `ALL.PKR`, `Music\` and `med\Movies\`.

Between the two discs (`game/THPS2.img` and the original 2000 release, extracted to `build/variants/original-2000-disc/`):

| Files | Same bytes |
|---|---|
| `ALL.PKR` | yes (SHA-256 `7e2ae173…`) |
| `Music\*.dat` (30) | all |
| `med\Movies\*.dat` (36) | 35; `Rowley.dat` differs in 4,083 scattered bytes inside one 168 KB stretch (0x55 in the 2000 copy), same size |
| `THawk2.exe` | no |
| readme / RTF / `cs.txt` | no (text only) |

The original disc's own `THawk2.exe` (2,193,512 bytes, `bca39d17…`) is SafeDisc 2: against the unprotected build
(`00315219…`, same PE timestamp `0x39ea002f` and section layout) only 0.57% of `.text` and 0.44% of `.data` are equal,
`.rdata` 89.6% (imports). Its data is encrypted, so it is not a source; the launchers refuse it by name
(`re/xref/config.yml` `protected_disc_executable`).

The unprotected 2000 build keeps the same tables and strings as the canonical one about `0x3000` lower
(`.rdata` `0x00515000` vs `0x00518000`, `.data` `0x00521000` vs `0x00524000`) with small insertions, and its
address words point at its own layout.

## Method

tony/static_variant.py, per canonical data section:

1. Address words (4-byte aligned values inside the image) are replaced by one placeholder in both builds, so that
   relocated pointers compare equal; the sections are aligned in order (patience on unique grams, exact matching on
   small spans), then a second pass finds uncovered stretches of 8+ bytes anywhere in the variant's data (tables the
   linker ordered differently).
2. Breakpoints `variant address -> delta` come from the function map and data anchors of `re/xref/original_2000.yml`,
   the aligned blocks, and (weighted most) the address-word pairs the alignment put side by side; only those a copied
   address word falls under are kept.
3. What the copies and the address rule leave different from the canonical bytes becomes `zero` or literal `bytes`.

`masked` ranges are zeroed instead of reproduced. One: `0x00525500`+400, `Front_KeyTable`'s key, which
`CD_ReadLocalFile` uses to decode `texture.dat` for `CD_CheckAudioTOC` (re/evidence/cd-check.md). It exists only in the
Value re-release (`0x00418140`, `0x004bae70`, `0x004bb0b0`, `0x004bb240` are all in `unmatched_canonical`), and the
standalone replaces the CD check (`standalone_host.cpp`, CD_Recheck), so nothing it runs reads it.

## Result

- 500 copies (293,567 bytes), 295 breakpoints, 3,035 literal bytes: 2,496 in the import area (the import directory,
  lookup tables, hint/name entries and the IAT: Win32 names and RVAs in another order), the rest address words the rule
  does not reach (an unaligned table of code addresses at `0x0055e60e`, the debug directory's timestamp) and about 240
  bytes of text the 2000 build does not have: the CD check's strings, a PKR directory dump's format, the build date
  (`__DATE__`/`__TIME__`), "point%s" pluralisation and "player one   %d" spacing.
- `.rsrc` is reproduced entirely from copies.
- The rebuilt image is the canonical image with the masked range zeroed (`image_sha256` in the recipe), from
  tony/static_data.py and from `opentony.exe --prepare` alike (tests/test_static_data.py, tests/test_launcher.py).
- THPSource2_Launcher's key tables read from the rebuilt image as from the canonical executable
  (`OPENTONY_THAWK2_EXE=<rebuilt thawk2_static.img> opentony_launcher_core_test`).
- Session parity (2026-11-19; `tony standalone parity --keep-retail`, all seven `re/scenarios/sessions`, the standalone
  started from the rebuilt image against the Value re-release's retail traces): in every session the deliveries and
  polls are the same in every record (1,200 to 6,000 records; boot, options, saved data, create-a-skater, park editor,
  a Hangar career run and its end), and of the 588 64 KB blocks only block 0 (`0x00524000`, which holds the masked key)
  differs, from record 0 on and nowhere else. The zeroed key is the whole difference; nothing the sessions run reads it.

## Open questions / falsifiers

- The standalone runs the Value re-release's code for both releases. 359 matched functions differ in code between the
  builds (`identical_code: false` in `re/xref/original_2000.yml`; mostly `mmu.cpp`, `physics.cpp`, `career.cpp`), many
  possibly only through shifted addresses; 228 variant functions are unmatched. A player of the 2000 release gets the
  re-release's behaviour wherever those differ. Not measured.
- Saves: the memory-card layer (`Mmu_*`) is among the unmatched canonical functions. Whether a `.SAV` written by the
  2000 executable loads is untested (needs such a save).
