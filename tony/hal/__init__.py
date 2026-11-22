"""Platform boundary audit and HAL tooling (`tony hal ...`).

`re/config/hal_boundary.yml` inventories every call site where retail
THPS2 crosses into the Windows platform (DirectDraw/Direct3D 7, DirectInput,
DirectSound, WinMM, Bink, DirectPlay, USER32/GDI32/ADVAPI32 and file I/O).
`tony hal check` re-derives those sites from Ghidra and fails when the
inventory is incomplete or stale; see docs/HAL.md and
re/notes/platform-boundary.md.
"""
