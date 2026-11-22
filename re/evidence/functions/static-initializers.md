# The C++ dynamic initializers with straight-line bodies

Status: modeled (transcribed from the disassembly by a script; checked at the session level)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: 27 slots of the initializer table `0x00524000..0x00524118`, each a `jmp` stub (`StaticInitThunk_<body>`) to a body (`StaticInit_<body>`); 54 functions
Native: `src/hooks/kits/standalone_static_init_generated.cpp` (written by `scripts/static_init_transcribe.py`; `tests/test_standalone_boot.py` regenerates it and compares), run by
`src/hooks/kits/standalone_boot.cpp`

## What is here

The C runtime's `_cinit` calls every non-null slot of the table `0x00524000..0x00524118` before `WinMain`. Of its 69 slots, 28 are functions that already had a native (the camera tuning
initialisers, the credits fonts and others), 14 are `call CONSTRUCTOR; jmp REGISTER` stubs that `standalone_boot.cpp` writes by hand (the class constructor on its global, the destructor to
`atexit`), and these 27 are a `jmp` to a body of stores with no branch and no call: a global set to a constant, to another global, or to a value derived from one. No symbol table named them,
because nothing but the table reaches them. Each row's description says which globals its stores reach.

## Method and what is not shown

`scripts/static_init_transcribe.py` reads each body from the canonical executable and writes one C++ statement per instruction, the registers as local variables, so a body is the instruction
sequence and nothing else; it refuses an instruction it does not know. The cold boot runs the 69 slots in table order. Checked at the session level: the `boot-menu` scenario, cold-booted in the
standalone from the static data alone, equals retail's recording in every block of `.data` from the first record (`python -m tony.standalone_boot`, milestone 9). There is no per-function oracle
and no matching source; the bodies' names say only where they are.
