# The cheat codes (cheat.cpp, the tail of shatter.cpp)

Status: confirmed by retail forced calls (1,400 cases of the cheat hash and the cheat table, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x004af660`, `0x004af6b0` (2 functions) (slice `cheat-codes`)
Native: `src/runtime/bit_kit.hpp`, `bit_cheat.cpp`; replay `src/runtime/bit_retail_calls_test.cpp`; probe `re/gdb/probes/bit_specs5.py` (group `bchz`); fixtures `re/evidence/fixtures/bit-calls-bchz*.txt`

## What is here

The routine sits at the end of `shatter.cpp` in the executable. While the pad's buttons are held (one of nine flag bytes at `0x0056b009..0x0056b099`), `Cheat_Code(flag)` picks the pair of seed words that
belongs to the button and mixes them into a rolling hash the way the typed code's letters were: `Cheat_Hash(first, second)` rotates the high hash (`0x0056c574`) left and multiplies it by `0x209`, then
`(high >> 8) ^ low ^ second` rotated left becomes the low hash (`0x0056c578`). The routine answers 0 when no button is held, in a net game, or in the game mode 7; otherwise it compares the high hash with the
hash of each of the 35 codes and applies the one that matches (several also compare the low hash and the flag first), answering 1.

What a code does is a call into the game: toggle a cheat (`Career_ToggleCheat` with an id), fatten both players, clear the current game slot, flip a flag, switch the mode word (`0x0056b7f4`, set to the code's
value unless it already is), add 5000 to a character's trick configuration, or open every selection record (clear bits `8` and `0x10`). All of these services are stubs of the replay, so what is compared is the
hash words, the flags the code writes itself and the ordered service calls with their arguments.

## Method

The hash words and the nine button bytes are poked. Every code's hash is a one-way function of the seed pair, so the probe *inverts* it: the generator builds the pair of words whose mix gives a code's high
hash (the pre-image generator), so that the table is reached, not just the fall-through. `Career_ToggleCheat` is called with a flag whose upper bytes are garbage, hence the `masks` on its argument.

## Lessons

(1) Random inputs never match a code: without the pre-image generator the table is unreachable. (2) The toggle service takes a byte flag; comparing the full register made every case differ, a mask of `0xFF`
on that argument is the faithful comparison. (3) Net game and mode 7 both forbid the codes before the buttons are even read.
