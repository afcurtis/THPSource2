# The traffic cars' collision with the skaters (traffic.cpp)

Status: confirmed by retail forced calls (500 cases of the traffic collision, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x00412ef0`, `0x00413000` (slice `world-traffic-collision`)
Native: `src/runtime/world_kit.hpp`, `world_traffic.cpp`; replay `src/runtime/world_retail_calls_test.cpp`; probe `re/gdb/probes/world_specs_traffic.py` (group `wtc`); fixtures
`re/evidence/fixtures/world-calls-wtc*.txt`

## What is here

`0x00412ef0` runs once per car per frame. Unless a movie is playing it scans the models of the car's region (the table of pointers at `0x0056d43c + 68 * region`, the count a word at `0x0056d460 + 68 *
region`) for the box with the largest floor area (`|w[+0xc] - w[+0xe]| * |w[+0x14] - w[+0x16]|`), transposes the car's rotation (`+0x118`) into a local with `Render_TransposeMatrixQ12` and runs the
collision for the players: the one the net flag `0x006a3b21` names, or both skaters (`0x0056a858`, `0x0056a85c`).

`0x00413000` (car, skater, matrix, box) does nothing if the skater already hit this car (`+0x29b8`), if it is in some other state, or if it is more than 1000 away (`Utils_CrapDist`). Otherwise the skater's
position and its position of the last frame (`+0xbc`), relative to the car, are shifted right 12 and rotated through the GTE by the transposed matrix (the GTE's rotation registers are loaded from the
matrix, the vector from `IR`); the swept span in the car's frame, widened by 0x14000 on every axis, is tested against the box's six shorts (each shifted left 12; the box stores high before low). On a hit:

- **from above** (the skater is in state 1 and the car is more than 0x64000 above it for the types `0xd7`/`0xd8`, or merely above it for the others): the skater's velocity is halved, thrown up, a
  grunt, the vibration and a gap bookkeeping call whose arguments depend on the car's type, then a burst of twenty glass bits and the car is remembered in `+0x29b8`;
- **from the front or the back**: the car's velocity is turned into the skater's (types `0xd7`: a third larger, randomly mirrored; `0xdb`: a quarter reversed; the others: 1.5 times on the larger axis
  with a random kick), the skater bails, then in every case the blood splat is requested, the pad vibrates, the grunt plays and a sound by the car's type (`0xd5..0xdc`).

## Method

A car (0x180 bytes), a fake skater (0x3600 bytes), the box of a model and the region tables are in the slab; the GTE register window is captured, so the matrix-vector products run as the port's own
math. The transposed matrix is scripted (the transpose service writes it into a local), and the vibration, the bail, the glass bits, the gap bookkeeping and the sound are stubs.

## Lessons

(1) The stomp threshold is `0x64000` for the car types `0xd7` and `0xd8` and zero for the rest; the comparison is signed. (2) The span's box test uses the minimum and maximum of the two rotated
positions, so only their order is irrelevant: the two GTE products must still use the previous and the current positions in turn.
