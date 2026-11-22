# The skater's dust and the dynamic logos (dust.cpp)

Status: confirmed by retail forced calls (2,117 cases of the dust and the logo loader, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x00431260`, `0x00431320`, `0x00431650`, `0x004317c0`, `0x004318a0`, `0x00431b80`, `0x00431e80`, `0x00432180`, `0x004321d0`, `0x00432250`, `0x00432330` (11 functions) (slice `dust-and-logos`)
Native: `src/runtime/bit_kit.hpp`, `bit_dust.cpp`; replay `src/runtime/bit_retail_calls_test.cpp`; probe `re/gdb/probes/bit_specs3.py` (group `bdst`); fixtures `re/evidence/fixtures/bit-calls-bdst*.txt`

## What is here

- **The dust object.** It keeps up to twelve flat smoke bits (the clouds, pointers at `+4 + 4 * k`, their count in `+0x34`), its type (`+0x38`: 0 landing, 1 bailing, 2 braking; `Dust_Reset` asserts the
  type) and the number of clouds still to make (`+0x3c`). `Dust_Reset(terrain, type)` asks the terrain table of the type for the scale, tint, fade, scale decay, number of clouds and offset, and keeps the number of clouds.
- **Update.** `Dust_Update(skater)` (asserts a skater): every cloud grows by its scale decay while its scale is below `0x200`, fades, drifts with the skater's velocity (`+0x4c`, `+0x54`; halved unless the
  type is 1) and rises by up to two units (`rnd(3) << 12` subtracted from the height); while clouds are left to make a new one is made at the skater (`insert_dust`); when none are left and all have faded
  the clouds are deleted through their virtual destructors.
- **InsertDust.** Puts a cloud into the next free place (asserting the place is empty), or, when twelve are used, over the cloud whose three colour bytes sum lowest (the old cloud is deleted through its
  virtual destructor first).
- **The terrain tables.** `Dust_GetTerrainParameters` picks the table of the dust's kind; the three tables (`Land`, `Bail`, `Brake`) map the terrain id to the cloud's scale, tint, fade, scale decay, count
  and offset (landing on grass-like terrain 3 and 0xe makes a little, on 6 a lot).
- **ApplyRandomOffset.** Adds a random offset about zero (a shift of the random range) to the x and z of a vector.
- **The dynamic logos.** `DynamicLogo_NameMatches` tests that a bitmap name (before its `.`) ends with the digit that follows the size index; `DynamicLogo_FindNext` walks the ring of names (a list up to a
  null, then back to the first) for the next one carrying a size's digit; `DynamicLogo_LoadAll` replaces the level's placeholder logos with bitmaps picked from the skaters' lists of names, for each of the
  four widths (32, 64, 96, 128) a random number of steps into the ring, finding the bitmap in the `logos` folder.

## Method

A fake dust object with up to twelve fake clouds and a fake skater in the slab; the clouds' virtual destructors, the random numbers and the skater manager are stubs. For the logos the probe lays arrays of
name pointers (names like `logo3.bmp`, `ab1.bmp`) and scene-material records out, with the sub-directory query, the scene material lookup and the bitmap loader as stubs; the order of the service calls is compared.

## Lessons

(1) `Dust_Update` with no skater asserts and then dereferences it: retail crashes, so those cases (about 3 %) end in an error and are skipped. (2) A faulting branch hides coverage: the first captures never
reached the replacement of the dimmest cloud until the generator gave the dust object all twelve clouds. (3) The three terrain tables are plain switch statements; the generator draws the terrain id from the
table's keys and a few outsiders.
