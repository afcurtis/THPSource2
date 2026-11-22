# The skater object's methods (bruce.cpp)

Status: confirmed by retail forced calls (8,488 cases of the skater methods, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x0046c5f0`..`0x0046e470` (thirty-eight functions: the constructor `0x0046c720`, the destructors, the board bits, the registry of animation names `0x0046cdf0..0x0046cfe0`, the light colour
`0x0046d040`, the geometry helpers `0x0046d1d0`, `0x0046d220`, the shade `0x0046d2e0`, the ground probe `0x0046d7a0`, the flip `0x0046d970`, the blood splat `0x0046da70`, the water `0x0046e080`, the grunt
`0x0046e1d0` and the droplet object) (slice `world-skater-methods`)
Native: `src/runtime/world_kit.hpp`, `world_skater.cpp`, `world_skater_effects.cpp`; replay `src/runtime/world_retail_calls_test.cpp`; probes `re/gdb/probes/world_specs_skater.py` (group `wsk`),
`world_specs_skater2.py` (groups `wsb`, `wsg`); fixtures `re/evidence/fixtures/world-calls-ws*.txt`

## What is here

The skater (CBruce, vtable `0x00518dd8`, about 0x3200 bytes) is an animated object with the panel (`+0x168`), the trick state (`+0x2c00..+0x3100`), the orientation rows (`+0x2e58`), the light state
(`+0x3174..+0x3190`) and the registry of animation names (`+0x3214`: a count and 100 entries of `{hash, users}`).

- **Construction** (`0x0046c720`): the animated object, the panel, a long list of counters cleared, the character's overrides (the starting score from the game mode: `0xe2e` in the goal game with its flag,
  `0x276` in mode 7, `0x1c3e` otherwise, the table value in modes 5, 8 and 9), the name registry emptied, the orientation set to the identity, the place in the list of players (asserting there are at most
  two), the pad profile of its number (`0x0056aff8` or `0x0056b164`), the light tint of its character, and, in a one-player game without a second view, the two small buffers of the light fade (else the sprite is
  wanted and made). The destructor stops the two voices, frees the buffers, leaves the player list and destroys the panel's six text members (`0x0046cc00` is their unwinding clean-up).
- **The registry.** An animation name is hashed (rotate left by one, xor the byte, from the seed's low bit); `0x0046ce70` counts one more user (adding the name when it is new, asserting the registry of 100
  is not full), `0x0046cf20` one less, `0x0046cfe0` answers the count.
- **Light.** `0x0046d040` fades the light colour towards its target at the step in `+0x317a` and builds the light word; `0x0046d7a0` throws a line down from the skater and, if the surface it hits is flat
  enough for the skater's state, keeps the hit; `0x0046d2e0` takes the face the skater stands on: the average of its colour, or the shade interpolated at the skater's place from the corners (the shadow's
  line-intersection helpers `0x0046d1d0`, `0x0046d220`), smooths it by 0x28 a frame and scales the three channels. Its virtual call (slot 5) is `Skater_ApplyShadeLighting` `0x0046d160` (no Ghidra
  function; 10g): the GTE lighting at `+0x31ac` (13 dwords: light matrix, colour matrix `+0x14`, back colour `+0x28`) copied forward from the record at `+0x31e0`, then colour-matrix element i times channel
  i mod 3 (`+0x3184`) and the back colour times `+0x3190`, each `>> 8`. Replayed against 300 forced calls (group `gsl`, fixture `world-calls-gslA.txt`, sources that overlap the copy included).
- **Effects.** `CBruce__BloodSplat` throws up to 250 bits of blood from the hook, each at its own speed, falling to the ground under the skater; `CBruce__HandleWater` plays the splash once and throws a spray of
  drops from a circle; `CBruce__FlipToVelocity` negates the front and side vectors when the skater now moves against them; `SFX_Grunt` plays a character-specific grunt and one of three others.

- **The frame** (`Skater_Update` `0x0046dd60`, vtable slot 3, `mech.cpp`; 10g): nothing while the world is frozen; the model scale from the region's flag; in the competition intro only the clock stamp
  `+0x2cc8`; else the skater is the current one (`0x0056a958`) for the rest: the trick-repeat stamps `+0x2e30` / `+0x2e34` expire after ten frames, the replay stream of its slot (`0x0056e5d8` for player
  one, `0x0056e450` otherwise; asserting `mech.cpp(1912)` that the stream has a block) plays the frame back (mode 2, nothing more) or records it first (mode 1), then the input, the physics frame (or the
  network player's), the water, the air counter `+0x31ec`, two line-to-sphere tests sharing one local vector (baddies, then power-ups whose `+0xf0` is `0xffff`), the ground probe unless a standing
  skater moved less than 0x41 in y, and the once-only sounds of the animations 0x5d and 0x5e past frame 15. Replayed against 300 forced calls (group `gsu`, `re/gdb/probes/gpu_specs.py`, fixture
  `world-calls-gsuA.txt`); every callee is a service.

## Method

A fake skater (0x3600 bytes, all captured), the sprite objects, a profile record and the strings sit in the slab; the collision zone query answers three scripted fields (the body, the slope and the height)
for the blood splat and fills the hit record (0x50 scripted bytes) for the ground probe; the effect objects, the hooks, the panel's text members and the sound are stubs. The shade uses a face, an object, a
model and its colour table in the slab.

## Lessons

(1) The ground probe copies its hit from fields the query service wrote (`+0x6c..+0x7c` of the record), so the probe scripts the whole record. (2) The shade's smoothing moves by 0x28 in either direction and
snaps to the target past it; the `0x51eb851f` multiplications are the compiler's `/ 100`. (3) `Skater_Shade` makes a virtual call through slot 5 at its end, so the fake skater needs the retail vtable.
