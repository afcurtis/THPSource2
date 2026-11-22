# Autokick / Vibration controller options

Status: static-confirmed (2026-09-28); corroborated by the Cumulative Patch
names and by the constant runtime value in the Warehouse B010 replays.

Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`

## Result

The "local profile table" that gates the ground-motion producer
`Skater_UpdateAcceleration` (`0x0049b010`) is the per-player **Autokick**
controller option. Its sibling table is the per-player **Vibration** option.

| Address | Name | Meaning |
|---|---|---|
| `0x0056a3d8[2]` | `AutoKickSetting` | live per-player Autokick flag read by gameplay |
| `0x0056a3e0[2]` | `VibrationSetting` | live per-player Vibration flag read by the actuator path |
| `0x0055fc2c[2]` | `AutoKickState` | options-menu copy of the Autokick flag |
| `0x0055fc34[2]` | `VibrationState` | options-menu copy of the Vibration flag |

Both arrays are indexed by player `+0x2cc4`, XORed with `DAT_0056a854` when
`LevelEventGameModeSelector == 7`, exactly as B010 indexes them.

## Evidence

1. **Menu construction (`0x00487490`).** The controller-options object creates
   two `0x104`-byte option records with `0x004189c0(kind, owner, strings, 3, 1)`.
   The strings are loaded from the English text table
   (`0x0052e188`, 4-byte pointers): record `+0x244` from entry 403
   (`"Autokick:"`, pointer at `0x0052e7d4`) plus the `"On"`/`"Off"` pointers
   at `0x0052e52c`/`0x0052e530`; record `+0x248` from entry 402
   (`"Vibration:"`, `0x0052e7d0`) plus the same On/Off pair.
2. **Menu initial selection (`0x00487bbe..0x00487bf3`).**
   `record->+0x10 = AutoKickState[player] != 0 ? 1 : 2` (the `neg/sbb/add 2`
   idiom), and the same for Vibration. Selection `1` is the first choice after
   the label (`"On"`), `2` is `"Off"`.
3. **Menu write-back (`0x00487c30`, stores `0x00487d27`/`0x00487d45`).**
   `AutoKickState[player] = (record+0x244)->+0x10 == 1`, likewise Vibration.
   So a nonzero flag means **On**.
4. **Publication (`0x00413c10`, store `0x00413c49`).** Copies the menu state
   into `AutoKickSetting`/`VibrationSetting`; the reverse copy is at
   `0x00413db7..0x00413dcc`.
5. **Factory default (`Options_ResetControllerDefaults`, `0x00413f30`).**
   Writes `1` (On) to both `AutoKickState[player]` and `VibrationState[player]`.
6. **Consumers.** `AutoKickSetting` is read by B010 (`0x0049b028`/`0x0049b042`),
   by `Skater_DoGroundPhysics` (`0x004965fe`/`0x00496618`, saved to a local
   before the ordinary movement query) and by `0x0049d630`/`0x0049d646`, where
   only a zero (Off) value reaches an additional low-speed `v -= v >> 5`
   decay path; that path's owner is outside this note.
   `VibrationSetting` is read at `0x0048ffd7`, which calls the actuator helper
   `0x004896a0` only when the flag is nonzero — consistent with Vibration.
7. **Cross-reference.** The THPS2 Cumulative Patch (pinned, names only) calls
   `0x0056a3d8` `AutoKickSetting` and `0x0055fc2c` `AutoKickState`
   (`re/symbols/imported.yml`), matching the static result independently.
8. **Runtime.** The Warehouse B010 replay (`ground-movement.md`, "Fresh
   B010-to-commit replay") recorded `local_profile_lookup.value == 1` in every
   window — the factory default On.

## Effect on B010

With `AutoKickSetting != 0` B010 runs its automatic-push block (random
threshold re-arm with seed `+0xaa`, animation `1` reason `0x2570`, then the
animation-2/3, animation-`0x5e` or ordinary correction writes) and returns
before the manual block. With `AutoKickSetting == 0` the automatic block runs
only while the controller `+0x10` action slot is held, and the manual block
(seed `+0xdc`, reason `0x25e5`) re-arms a push when that slot is held. The
`+0x10` slot also selects scale 8 instead of 4 in the animation-2/3
correction. The slot's user-facing binding name is not settled by this note.
