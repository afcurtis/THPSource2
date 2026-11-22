# Source-file attribution from retail assert paths

Status: observed (`assert`), inferred (`bracketed`)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `re/xref/source-units.yml`

## Observation

The retail executable keeps its assert machinery. 120 strings of the form
`H:\TonyHawk\Pc2\<file>.cpp` (including `pc\winmain.cpp`, `pc\d3dtex.cpp`, …) are pushed
by asserting functions together with a line number and message.

727 canonical functions reference exactly one such path (`assert`); 4 reference several
(`assert-multi`, inlined header code such as `baddy.h`). Per-file spans are contiguous and
almost never interleave (4 overlapping span starts across 111 files), consistent with MSVC
emitting each object's functions contiguously. Object order is close to alphabetical by
file name, with the `pc\` platform objects last and the C runtime after them.

## Interpretation

A function lying between two functions that assert in the same file is attributed to
that file (`bracketed`, 1,025 functions). Total: 1,756 of 4,760 Ghidra functions.

## Open questions / falsifiers

- Functions between two *different* files are left unattributed; the boundary could
  belong to either neighbour or to an assert-free file between them.
- A bracketed attribution is falsified by any function in the run that references a
  different file's data exclusively (static tables, file-local strings).
