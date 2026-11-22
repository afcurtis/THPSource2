# The memory-card layer (mmu.cpp) and its backend

Status: confirmed by retail forced calls (11,494 full-capture cases for the card layer and backend, 1,131 for the screen; 0 mismatches)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x00473d20..0x0047ba0f` (82 functions: the card manager, the save screen, the set-up functions, the replay card helpers, the string helpers) and
`0x004e8d40..0x004e9132` (the ten backend functions). The earlier [save-runtime.md](save-runtime.md) describes the file family from the callers' side; this page
is the layer itself.
Native: `src/runtime/mmu_kit.hpp`, `mmu_manager.cpp`, `mmu_setup.cpp` (the card manager, set-up, replay helpers, backend; replay `mmu_retail_calls_test.cpp`),
`mmu_screens.hpp`, `mmu_screens.cpp` (the save screen; replay `mmu_screens_retail_calls_test.cpp`). Probes: `re/gdb/probes/mmu_specs.py`, `mmu_specs_mgr.py`,
`mmu_specs_misc.py` (group `mmu`), `mmu_specs_ui.py` (group `mmui`). Fixtures `re/evidence/fixtures/mmu-calls-*.txt`, `mmu-screens-calls-*.txt`.

## One object, two classes

The global `MmuManager` (`0x0056ad00`) is the **save screen**, an object of 0xdd0 bytes (vtable `0x00519308`: 0 the deleting destructor `0x00477910`, 1 the state machine
`Front_SaveCardStateMachine` `0x004780d0`, 2 the draw `0x00479f60`, then the window base). The **card manager** is a sub-object of it at `+0x1f8` (0xb4c bytes, vtable
`0x005192e4`): the callers add `0x1f8` to the screen to call a card method (`Mmu_RegisterSaveBuffer` does it itself). The decompiler hides this, which made the card
fields look like fields of the screen.

```text
card manager (this = screen + 0x1f8)
  +0x004 + 0x94 * i   file record i (the file name, +0x84 flags, +0x88 the size the directory scan marks as 0x2000)
  +0x8b0 + 4 * i      type of file i (1 career, 2 replay, 3 park, 4 other)
  +0x928 + 0x21 * i   display name of file i
  +0xb18 file count   +0xb1c card state   +0xb20 free 8 KB blocks   +0xb28 registered buffers   +0xb2c / +0xb3c four buffer pointers / sizes
screen (this = MmuManager)
  +0x0ac a Font object (s2tricks.fnt)   +0xd44 banner text, +0xd48 / +0xd4c the "blocks needed" / "blocks free" texts, +0xd50 delete hint (loads)
  +0xd54 the file list (a ScrollList)   +0xd58 / +0xd5c two styles   +0xd60 + 4 * i the 16 rows   +0xda0 busy flag   +0xda4 display state 0..0xf
  +0xda8 file type   +0xdac confirm box   +0xdb0 message text box   +0xdb4 counter   +0xdb8 name entry   +0xdbc 1 saving / 0 loading
  +0xdc0 timer   +0xdc4 result (1 done, 2 back)   +0xdc8 prompt group (saves)   +0xdcc button flags
```

## The files

`THPS2 CAREER- <name>.SAV`, `THPS2 REPLAY- <name>.SAV` and `THPS2 PARK- <name>.SAV` (prefixes at `0x00532c34`, `0x00532c24`, `0x00532c44`; the directory scan keeps
a file whose name starts with a prefix, case-insensitively; type labels `career`, `replay`, `park`, `other` at `0x00532b28..`). `Mmu_BuildSaveFilename` makes a different
kind of name, `THPS2_<first><random><random><last><G|V|P><two-character counter>.SAV`, from the newest file's counter (base 32, read from the character at index 11 twice,
the digits counted as digits and as non-letters, which is the compiled code's behaviour). The directory comes from the PC build's libcard emulation
(`Card_ScanDirectory`: the card directory string `0x006a71bc`, a separator, a pattern; each file found is one 0x94-byte record whose size field is set to 0x2000, so every
file is one block). A save writes the 0x200-byte `SC` header (`Mmu_BuildSaveHeader`: type + 0x10, the block count, the description line `<label><name>` at +4, the user name at +0x60,
three 128-byte blocks at +0x80 / +0x100 / +0x180), then each registered buffer, all 128-byte aligned; a load reads the buffers from offset 0x200.

## Functions

- Manager: `Mmu_ValidateSaveActionType` (answers 1 for types 1..3 with the replay and horse-mode assertions), records and names (`Mmu_FileRecord`, `Mmu_DisplayName`,
  `Mmu_FindFile`, `Mmu_FileExists`, `Mmu_NameTaken`, `Mmu_NameTakenByOther`, `Mmu_CountOfType`, `Mmu_FileType`, `Mmu_DescribeFile`), `Mmu_NewestFile` (by `stat` time),
  `Mmu_ScanSaveFiles`, `Mmu_MapCardResult` (card library result 0..4 to the manager's state word 0, 1, 3, 4, 2), `Mmu_SaveFile`, `Mmu_LoadFile`, `Mmu_DeleteSaveFile`.
- Set-up (`Mmu_SetupCareerSaveBuffer`, `Mmu_SetupReplaySaveBuffers`, `Mmu_SetupParkSaveBuffer` and the three load variants): each builds a screen (`operator new(0xdd0)`
  and the constructor), registers the buffers (the career image 0x1e00 bytes, one or two replay streams of 0x7fe00, the park buffer rounded to 128), and sets the screens to
  return to. `Mmu_FinalizeIngameScreen` deletes the screen, copies a loaded career image into place (and applies it when the flag says so), reads a loaded replay's header
  fields and frees the load buffers.
- The save screen: `MmuScreen_Construct` (and the prompt group `MmuPrompts_Construct`), the destructors, `MmuScreen_RebuildList`, `Front_SaveCardStateMachine` (display state 0 browse,
  2 card warning, 3 overwrite, 4 delete, 5 name entry, 6 formatting, 7 deleting, 8 saving, 9 loading, 0xb..0xe messages, 0xf name exists), `MmuScreen_Browse`, `MmuScreen_Display`.
- Backend (`Card_*`): the PC file operations behind the PS1 card API: scan, create, remove, open, read / write at an offset (the size a multiple of 128, the card flag 0 and
  the card state 5 / 6 afterwards, then 3 / 4 once the whole file call ends).

## Replay

Everything outside the group is a stub that answers from the case's generator (the recorded answer is what the native is scripted with): the C runtime's `stat` and directory
calls, the PC file primitives (`PCcreate`, `PCremove`, `FileIO_BackendOpen`, seek, read, write, close), the card library's state call (4.14a), `String_FindNoCase`,
`ParkFile_Exists`, the screen's constructor and `operator new` in the set-up cases, and the career, replay and window engine. A transfer is recorded for its first 0x400
bytes (the replay streams are 0x7fe00 bytes). The functions of the group call each other for real. The screen's group uses the 4.10 harness (`frontend_specs_screens.py`):
the toolkit and window base run natively, the card manager's methods are reported services (`engine_<address>`, the card first).

Things the replay pinned down (all retail behaviour, kept): `Mmu_RegisterSaveBuffer` is a method of the screen (it adds 0x1f8); the type label of an unknown type leaves the
caller's local buffer untouched (stack garbage), so the probe never asks for one on a new file's name; `Card_WriteOpenFile` with a zero size is a valid call for retail but the probe
cannot record zero bytes, so the cases use at least 128; `Mmu_FileCode` reads the same character twice.

Counts: the full captures hold 4,720 cases for the card records and names, 2,100 for the operations, 2,000 for the set-up functions, 1,200 for the replay helpers, 1,474 for the backend
and 1,131 for the screen (the prompt group, constructor / destructors, list rebuild, state machine 119, browse 120, banner, display 117 and draw 115); the committed fixtures keep the first 30 cases of
each function. Eight display / draw cases crash retail itself (`eip 0x0049e680`) and are skipped. The fake NameEntry grid slot (`0x0046f480`) is a `grid_v1` service in both the probe and the native.
