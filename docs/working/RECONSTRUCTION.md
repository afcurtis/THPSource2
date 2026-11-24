# Reconstruction: porting a retail function, a scenario that diverges

For changes to `src/runtime/` ports and their fixtures, a standalone `FATAL` that names an unported function, and a recorded scenario that no
longer matches retail. Read `docs/WORKING.md` and `docs/SLICE_WORKFLOW.md` first (`match/`, `re/`, `src/` rules). Script paths below are
relative to `scripts/play/` unless they start with `build/` (outputs) or name a tracked file. Phase 4's long record of reverse-engineering gotchas is
`phases\PHASE_4_SUMMARY.md` in the plans folder (`AGENTS.local.md`) "Lessons and gotchas": open it only for a probe problem not covered here.

## Playbook A: a scenario diverges

`tony standalone warehouse NAME [--keep-retail] [--dump A[,B]]`; result in `build/standalone-warehouse/NAME/result.json`. Sessions:
`tony standalone parity NAME... --keep-retail` (or `m10/parity.sh`); a failed run's reason is in `build/sessions/runs/NAME-standalone/run.txt`
(the FATAL line, or `caller 0x1000xxxx` stack words: `m10/lk.sh NAME`). A FATAL in `vcall_<addr>` is a function only a vtable names: dump its bytes
before deciding. `m10g/whd22.sh N` is a fresh idle run with a dump at delivery N (written for idle: copy it for another scenario).

1. Read the first differing record: its delivery and poll. **A different poll count at that record is a different code path, not different data.**
2. Dump both sides at that delivery and diff: `standalone-warehouse/realdiff.py A B` (`.data`, masks applied), `m10g/zd22.py A.heap B.heap [ADDR]`
   (zone memory; the capture host's zones start at 0x091e0000). Dumps are `build/sessions/runs/NAME-standalone[-retail]/session_N.{img,heap}`.
   A whole load in the first differing delivery: diff retail against standalone *and each side against its own previous dump*
   (`s9/ddiff.py A B --zones auto`), then read what the load fills in readable form (the PSX region table at 0x0056d428, 0x44 bytes each:
   names). The first visible effect is often a missing release; the cause a skipped call earlier. `m10g/s25c.py`, `s25m.py` are small dump readers.
3. Name the callers. Retail: `m10g/whw22.sh ADDR:BYTES,... [N]` logs every call of those *functions* (return address, four stack arguments,
   delivery) into `build/hooks/runtime/opentony.log`; BYTES is the length of whole instructions at the entry covering at least 5 bytes, nothing
   relative (count them from the disassembly: a cut instruction crashes the run). Standalone: `m10g/w22.sh DELIVERIES ADDRS` or
   `m10g/sp22.py NAME DELIVERIES PREFIXES VAR=..` with `OT_RESOLVER_WATCH`. Natives that call each other inside a Kit are not visible to the resolver.
4. Compare the two call sequences argument by argument (first mismatch; `m10g/s25cmp.py` is a template), then read the retail disassembly
   against the port there. **Check order and re-entrancy first:** who runs inside whose loop, which record and generation are shared.
5. Other tools: `OT_DELIVERY_LOG` / `OPENTONY_DELIVERY_LOG` (each clock delivery's site), `OT_FILES_LOG`, `OT_RESOLVER_TRACE`, `OT_WATCH_DUMP`,
   `m10g/rd20.sh` / `vx22.sh` / `ar22.sh` (career dumps). "Does retail do this too" past the recordings: the retail *session* with the
   standalone's keys and `OPENTONY_SESSION_WATCH` (`m10g/rw.sh`), not a gdb launch.
6. DLLs after a source change: `tony hooks build` (`s9a/build.sh`; `m10g/rail23b.sh` also runs the rail scenario). `m10g/next.sh [SESSION]
   [SECONDS]` = rows + `hooks generate` + DLL build + a standalone spin (~12 min), printing the next FATAL.

## Playbook B: port a function (the forced-call method)

The standalone's `FATAL` names it. A flat function needs no probe over a level.

1. Save the disassembly (`m10e/dis2.sh ADDR LEN | tail -n +8 > build/m10g/NAME.asm`; it crosses the 64 KB module files), read it whole, decode
   every branch before writing. Argument lists come from the `push`/`add esp` pairs, not from Ghidra or stubs.
2. Port it in the Kit its siblings live in (`SkaterKit` for physics; a new file goes in `cmake/opentony_sources.cmake` and the layer test's source
   list, `tests/test_physics_layer.py` etc.). Pure math callees (vectors, GTE, dot, root) run as retail code in the capture and natively in the
   port; every other callee is a stub. Locals retail keeps on its stack go in the scratch window (below); a native that can run inside another
   query gets its own record.
3. Spec in `re/gdb/probes/` by kind: skater `gpu_specs.py` (`m10g/cap24.sh NAME CASES SEED [ENV=..]`), world `world_specs_*.py` (through
   `m10g/chainG.sh` and `scripts/make_career_calls_fixture.py`), bit class `bit_specs*.py` (`m10g/cap24c.sh NAME CASES SEED`;
   `python scripts/play/m10g/vtscan17.py 5183d8 5185c0` lists vtable methods without a native), HUD panel `frontend_specs_panel.py` (`m10g/cap23.sh`;
   a hard `fc.stub` of the function under test in `frontend_specs_panel_draw.py` must be skipped). A spec = a window list, `ensure` stubs for every
   callee not ported with it, a generator of random *legal* state; a callee that is itself a spec goes in its own group (or `mark_under_test`
   switches its stub off). A new empty function (a lone `ret`): group `bem`, adapter `V(G.empty_hook())` in `world_retail_calls_test.cpp`, a
   symbol, a `functions.yml` entry, the slice's scope. **Before a capture** check the group and fixture names are free
   (`grep -n '"gXX"' re/gdb/probes/gpu_specs.py`, `ls re/evidence/fixtures | grep NAME`): a later definition wins and the capture overwrites the fixture.
4. Capture (500-800 cases: 1-25 minutes; fixtures of 1-23 MB are normal), then replay: `build/native/opentony_skater_retail_calls_test FIXTURE`
   (skater), `m10g/rep_one.sh NAME` / `repall.sh` (world), `m10e/nb.sh opentony_bit_retail_calls_test` + the binary (bit; `m10g/rep_bit.sh`).
   Add the adapter to the Kit's `*_retail_calls_test.cpp` table (`scripts/standalone_rows.py` derives the row; a native with a hook row needs none).
5. Mutants: `m10g/mutate.py SRC FIXTURES MUTANTS.py` (copy `m10g/mut24.py` / `mut24b.py` / `mut23.py`). ~20 s each when nothing builds. A survivor on
   a narrow path means a second fixture whose generator builds that path; an *equivalent* survivor (the change cannot show in memory) is named in
   the commit. The pattern hits the **first** occurrence in the file: include a neighbouring line of its own function. A mutant that makes a loop
   endless needs its replay killed.
6. `re/native/functions.yml` -> `trace-validated`, new names in `re/symbols/functions.yml`, `tony ghidra sync` **last**, `m10/gen.sh` (rows,
   `hooks generate`, hook build), `s10d/arity2.py` (FAULT lines are live `ServiceFault`s), then `m10g/next.sh`. A stub's `writes` are not applied by
   the replay: a port that reads a global a callee writes uses `s_.value(name)` when `!s_.live()` and memory when live. A function Phase 4 left
   `tested` / `modeled` / `integrated` has no memory form yet (`TrickTips_Update` is a model over a byte image: give it one like
   `CreateObjKit::tips_reset`). Count what the standalone cannot call with `m10g/rowless.py`.
7. Probe templates for loops and re-entrant traced functions: `m10e/ob_ai_trace.py` (+ `run_all_ob_ai.sh`, `gen_ob_ai.sh`, `rt_ob_ai.sh`),
   `anim_start_trace.py` (a leaf), `powerup_trace.py` (two functions in one probe), `construct_trace.py`, `trick_table_trace.py`, `enviro_trace.py`,
   `script_size_trace.py`; replay modes of `src/runtime/world_trig_nodes_trace_test.cpp` (`--lists --tricks --enviro --construct --powerup --obai --animstart`).

## Standing facts

- **Adding a standalone native:** a row in `re/config/standalone_natives_manual.yml` (hand-written; `standalone_natives.yml` is generated by
  `scripts/standalone_rows.py` from the replay tests' adapter tables), then `m10/gen.sh`; `tony hooks verify` checks each row's convention against the
  function's `ret`. `returns:false` rows return 0: a port whose caller reads the result needs the adapter `A(...)` and regenerated rows.
- **Masks** (`tony/standalone_boot.py`): `DEFAULT_MASKS` (session scenarios), `PROCESS_IDENTITY_BLOCKS` {24, 587}, `STANDALONE_MASKS` by class
  (c-runtime, directx-objects, c-runtime-heap, screenshot-number, stack-address-clut). A new mask needs a class, a comment, and a check that the root
  cause is really outside the game.
- **Scratch window** `0x7ff00000..0x7ff10000` (aliased onto a buffer live; Wine will not map it; replay fixtures cover only `kScratchBytes` 0x1000 from
  `kScratchBase` 0x7ff00800). Taken: `0x7ff08800` (psx load name), `0x7ff0a000..0x7ff0d000` (9b manifests), `0x7ff0e000..0x7ff0e908` (10a palette
  buffer, ring order, manifests), `0x7ff0ea00..0x7ff0ea50` (10c interpreter locals), `0x7ff0f800` (HUD text), `kScratchLine + 0xc0`,
  `kScratchBase + 0x6000` (TextureKit); the loading screen's fonts at 0x800/0x950/0xaa0 (`goals_loading.cpp`), `level_gen.hpp` 0x800..0x8ff. Inside
  the Kits (offsets from `kScratchBase`; `kScratch` of the physics, world and bit Kits is `kScratchBase + 0xc00`): `+0x300` second floor-query record,
  `+0x500` the shared query record `kInfoScratch` (0xa4 bytes; skater, camera-effect and trigger queries), `+0xb00..0xbb0` turn phase,
  `+0xd00..0xdc0` collision response, `+0xd40` camera record (**overlaps collision response; never concurrent as far as known**), `+0xe00..0xe50`
  sway, `+0xe10..0xeb0` platform (`kScratch + 0x210..0x2b0`, record at `kScratch + 0x340`), `+0xf50` floor-query record, world scripts
  `kScratch + 0x2c0..0x364`; BitKit: shatter `kScratch + 0x40..0x460`, its own glass query record `kScratch + 0x480..0x524` (live only), dust
  `+0x180..0x1a0`, `+0x300`, `+0x600`. Other Kits' areas were never audited for overlap.
- **Seams:** 102 named (`seam::<category>::<name>`, `src/runtime/seam_addresses_generated.hpp`); `tests/test_oos_seams.py` forbids raw out-of-scope
  addresses in native code; table-only functions in `re/config/table_seams.yml`.
- **Hooked build** (`opentony.dll` patching retail, `opentony_native.dll` with the Kits): the differential reference of the gates, 1,253 hook rows.

## Lessons

**Live versus replay (the main source of standalone failures)**
- A named service that is really a virtual call (`<object>_v<slot>`, a probe stub on a fake vtable entry) must be listed in `kit_dll.cpp`
  `kVirtualServices`: otherwise the resolver takes the stub's address from `kServices` and calls whatever lives there. `dialog_v1` called
  Audio_OpenLevelVab, so the stat page's yes / no box never drew and the page looked stuck (6 f10; `tests/test_kit_hooks.py` guards `*_v<N>`).
- A Kit that reads `s_.value(...)` after a real call is a live bug: live, the callee wrote memory itself and EAX is something else. Guard with
  `if (s_.live())` and read memory. `OT_SERVICE_AUDIT=1` prints every `RetailServices::value()` read live: a bug until proven otherwise.
- Calls in the probe's form (a pointer-to-local passed as a value or not at all, the name as text, missing arguments) must be made in full live: put
  locals in the scratch window and pass the address; write the local's bytes before the call (retail did).
- **Re-entrancy:** retail keeps a query record in a stack local, the port in shared scratch. A native retail runs *inside another query* overwrites
  the outer query's record: give it its own when live (`bit_shatter.cpp`). Two Kits that run one inside the other need their own scratch areas.
- Never keep a value you need across a call in the scratch window: take it from the return value.
- A native that reads a pointer the fixture had loaded dies at boot when retail has not loaded it yet. Fixtures do not cover every virtual target;
  live virtual calls go through `vcall_<vtable[slot]>`.
- A command word pushed from `bx` carries the caller's upper half (`masks={0: 0xFFFF}` on the stub); a stub whose result the port stores must script one
  (`result=`). A stub that took only the pushed arguments hides `ecx` (a thiscall); a stack-pointer placeholder as a destination leaves the real
  buffer stale. A callee that changes what the port reads next records it as a named value (`writes=[...]`); a buffer read through memory needs a
  callable `blob` source.
- A port that passes `ctest` can fail `replay32`: signed overflow is UB there; do wrapping arithmetic in `Word`, run `tony hooks replay32 --only NAME`.
- Layer tests compile their own source lists: a new dependency of a shared Kit file must be added there.

**Reading retail code**
- Argument lists and conventions come from the disassembly, not the decompile (Ghidra had several lists wrong, gets GTE writes wrong, drops stores).
- A `[esp+N]` read before the `add esp` that cleans a cdecl call is shifted by the pending bytes. The return address of `call [edx+4]` is call site + 3.
  A 5-byte `jmp` is a thunk. A replay table's `V` is not evidence EAX is dead (bool returns leave stale bytes above AL).
- Retail stamps the assertion line/file words (0x00568638, 0x0056881c) on every check: trace-validated Kits must write them. Retail reads
  uninitialised stack into game state: behaviour that depends on stack layout is not a native bug.
- A draw handler can write back into its packet and order its device state after an early return: read its stores and their order. A retail hook
  that counts calls also counts the ones a Kit makes inside itself.

**Method**
- **Trace first, port, replay word for word.** Whole-session state blocks are too coarse an oracle for a large port.
- A replay can pass because the oracle does not see the difference: mutate the port and read which mutants survive. `trace-validated` does not mean
  the standalone can call it.
- Find a divergence by its first differing record and the caller of the writes, not by bisecting hooks. Look for the *cheapest differing structure*
  (a cache count, a generation) before reading code. Bisect by behaviour: flip a row and rerun the one scenario (~3 min with a DLL rebuild).
- The harness is part of the oracle (the capture DLL's movie skipping cost two frame ticks). A retail run from the capture host has its zones elsewhere
  (0x091e0020; session runs 0x04f00020): give the standalone the bounds (`OT_ZONE_BLOCK`). Session images hold `.data` and the zones, not the C heap.
- **GDB trace probes:** start runs as a script through `wsl.exe` with `run_in_background`, never two chains at once, kill leaked Wine with a kill
  script; disable any breakpoint at an address under test; a store that does not change a byte is invisible in a write diff (fill state with garbage).
- 64-bit: a clean x86-64 compile proves nothing about pointers (`static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p))` truncates
  silently): route every guest/host conversion through `arena::`; a formatter over game words must never fault on a guest string (`arena::readable_text`).

## Traps (symptom -> cause -> do)

| Symptom | Cause | Do |
|---|---|---|
| A screen misbehaves only after another ran; a pointer kept in the scratch window is gone | two Kits' scratch addresses overlap | take it from the return value; grep the Kits for `0x7ff00...` constants |
| A second level in one process stops (`PClseek failed`, a name with an invisible byte) | a scratch address passed as a stack local, nothing written there | write the local's bytes before the call |
| `tony hooks build`: `native_hooks_generated.h is stale` after a hand edit | the check compares the generated Kit sources too | change the row, `tony hooks generate`; never edit a generated file |
| `OPENTONY_SESSION_WATCH` empty after a few thousand deliveries, or "starts with a relative jump" | the watch is incomplete; hooked functions cannot be watched | do not trust an empty watch: diff dumps (Playbook A step 2) |
| A session-watch run crashes or never starts | BYTES cut an instruction, or a data address was given | read the entry disassembly, count bytes; function addresses only |
| A capture gives 0 cases, or a fixture is overwritten | the group or fixture name exists already | check names before capturing; fixtures are untracked (`re/evidence/fixtures/README.md`): keep a copy of the folder and restore an overwritten one from it |
| A fixture named `trick-*` breaks `tests/test_trick_script.py` | it globs that prefix | rename |
| A mutant "survives" on fields the fixture fills with garbage | the pattern hit another function | include a neighbouring line of its own function |
| `verify-all`: Ghidra stale | any edit under `re/symbols/`, `re/types/`, `re/native/` (comments too) | `tony ghidra sync` last |
| `verify-all`: HAL inventory stale | new virtual calls | `tony hal derive --write` |
| `verify-all` / `hooks generate` break on YAML | a note with `: ` outside quotes; `default: "off"` unquoted | quote it |
| `.data` blocks differ by heap pointers after any change to a Wine run | the environment block length and run-directory name move the Wine heap | keep `pad_environment` (360) and `session_label` (24); warm a fresh worker prefix with two runs |
| `native-default` (or another variant) of some sessions diverges in block 3 and/or 23 from the first records, the rest match; which sessions changes with the repository's path or the shell the gates start from | an unmasked cell that holds a Wine-heap address (the heap's base follows the environment's length) | find it by moving the heap on purpose: the same retail run started by its `Z:\` path instead of `D:\`, dumped (`OPENTONY_SESSION_DUMP`), compared with masks and zone normalisation applied; add the cell to `DEFAULT_MASKS` |
| A hook with `OPENTONY_HOOKS` over 511 chars does nothing | silently ignored | `opentony.ini` |
| `tony hooks verify` takes 14 minutes | `which` called in a loop | never do that |
