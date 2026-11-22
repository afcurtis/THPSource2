# Original-2000 ↔ canonical address map

Status: confirmed (per-pair consistency recorded; `order` pairs individually inferred)
Build: canonical `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`;
variant `003152194111a3e564c1a458da2a02cc1187a4f47c147a4126f5d37913cfee8a`
Addresses: `re/xref/original_2000.yml`

## Observation

Two PC builds of THawk2.exe are in circulation:

| | canonical (`thps2_pc`) | variant (`original_2000`) |
|---|---|---|
| Release | Activision Value re-release | original 2000 retail (crack applied) |
| PE timestamp | 2001-01-04 (978633206) | 2000-10-15 (971636783) |
| Size / entry | 1,450,035 B / `0x00502f74` | 1,460,224 B / `0x004ffe6c` |
| Sections | 4 | 6 (vestigial SafeDisc `stxt774`, `stxt371`) |

PARTYMOD and the THPS2 Cumulative Patch hard-code original-2000 addresses: 89/96 of
PARTYMOD's and 339/350 of the Cumulative Patch's code-pointer declarations fall on
padded function entries in original-2000, versus 7/96 and 76/350 in the canonical build.
PARTYMOD also rejects the Activision Value release.

`tony xref features` exports normalized fingerprints (in-image absolute addresses masked)
for both Ghidra analyses; `tony xref match-builds` pairs them in stages (unique normalized
hash → unique string set → unique mnemonic sequence → call-graph alignment → link-order
gap fill between monotone anchors).

## Result

4,543 of 4,771 variant functions map onto 4,760 canonical functions (2,508 hash,
218 strings, 59 mnemonic, 348 call-graph, 1,410 order). 4,184 pairs have identical
normalized code. 5,409 data addresses map with one conflicting vote. Only 13 pairs break
link order, all identical-code C runtime functions.

Independent check: for each pair, the fraction of its callees and absolute references
that translate through the final map onto the partner's references ("consistency").
3,787 of 4,054 scorable pairs reach ≥ 0.8; mean consistency by method is hash 0.98,
order 0.995, call-graph 0.93, strings 0.75, mnemonic 0.79 (low scores mostly reflect
unmatched neighbours rather than wrong pairs).

520/561 curated functions, 123/135 curated globals and 113/116 native mappings now have
original-2000 addresses, which is what a PARTYMOD-style hook DLL on the original release needs.

## Open questions / falsifiers

- `strings` and `mnemonic` pairs with consistency < 0.5 (listed in the map) deserve a
  manual look before being relied upon for hooking.
- 228 variant / 217 canonical functions are unmatched; they concentrate in code that
  differs between releases (SafeDisc/CD-check paths, patched routines).
- A runtime check (same hook at mapped addresses in both builds) would upgrade `order` pairs.
