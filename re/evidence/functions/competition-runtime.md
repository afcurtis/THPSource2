# Competition judging and ranking (competition.cpp) and the tag handicap (tag.cpp)

Status: native port of the judging, ranking and scheduling rules of the competition mode and of
the tag game's stat handicap, replayed against retail forced calls (Phase 4.8); the mode's
windows, marquee, medal objects and videos are catalogued, not ported.
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Native: `src/runtime/competition.{hpp,cpp}` (shares `retail_memory.hpp`, `retail_services.hpp`,
`career_tables.*` with [career-runtime.md](career-runtime.md))

## The object

The competition (`CCompetition`) is one object at `0x0055fe28`:

```text
participant e (0..7) at +0x04 + e * 0x14
  +0x04 u8 is human (0 computer rider)   +0x05 human number or 0xff   +0x06 character   +0x07 costume
  +0x08 u8 heat bracket (0 = best ... computer riders only; 0xff = unassigned)
  +0x09 slot id      +0x0a/+0x0c/+0x0e u16 heat scores (three heats)   +0x10 u16 total
judge j (0..4) at +0xb8 + j * 8
  +0x04 participant it favours, +0x05 its bonus, +0x06 participant it dislikes, +0x07 its penalty
  score for the current heat: s16 at +0xe0 + j * 2      +0xea / +0xeb the judges dropped
+0xa4 humans   +0xec + i participant in ranking place i   +0xf4 heat   +0xf5 participant up   +0xf6 previous
+0x108 / +0x10c / +0x110 medal objects   +0x114 marquee text   +0x164 marquee scroll   +0x168..+0x16a flags
globals: 0x00560080 multi-player mode, 0x00560081 score-changed flag, 0x0056007c judging table row,
0x005185d8 judging rows (0x20 bytes: three score thresholds), 0x00525774 five u16 score anchors
```

## Functions ported

| Address | Name | Rule |
|---|---|---|
| `0x00419460` | `Competition_AllMedalsWon` | exactly three contest levels carry a medal for the player's character (asserts past three) |
| `0x00419500` | `Competition_AllGoldsWon` | exactly three golds |
| `0x00419880` | `Competition_AssignHeatOrder` | the computer riders are dealt brackets in a random order (`FUN_0040b500` picks the n-th free seat) |
| `0x00419910` | `Competition_JudgeScore` | five judges each map the player's score to 0..999 on a piecewise-linear ladder between the level's thresholds and the five anchors, subtract a random deduction scaled by the skater's difficulty word (`+0x174`), add or subtract a judge's bias for the participant, clamp; the lowest and highest judge are dropped and the other three averaged |
| `0x00419cc0` | `Competition_AiScore` | a computer rider's score: its bracket's anchor (bracket 3+: a random value between two anchors), minus a random 0..14, plus and minus the judges' biases |
| `0x00419f90` | `Competition_MedalDisplayDone` | the medal object's show counter (`+0xfc`) has reached `0x28` (or there is none) |
| `0x00419fb0` | `Competition_NextHuman` | hand over to the next human seat; false (and back to seat 0) when none is left |
| `0x0041aee0` | `Competition_SortRanking` | selection sort of the ranking order by total, highest first |
| `0x004b8410` | `Tag_StatModifier` | tag game: every skater but the one who is "it" loses a handicap equal to `it`'s score (0..12000) scaled to 0..120; stat 3 loses it twice |

The arithmetic in `Competition_JudgeScore` is unsigned 32-bit throughout (the ladder terms such as
`(score + threshold * 0xffffe) * 0x1000` only make sense modulo 2^32); the port reproduces that
and raises a fault where retail would divide by zero. In `Competition_AiScore` bracket 1 reads the
whole dword at `0x00525778`; only the low 16 bits are stored, so the high half never matters.

## Not ported (catalogued)

`Competition_Init` (`0x00419600`: player and character allocation, the heat table, the judges'
leanings), the win/lose and end-of-game logic and displays (`0x0041a040`, `0x00419da0`,
`0x0041a610`, `0x0041b590`, `0x0041a270`, `0x0041a780`), the marquee (`0x0041af40`, `0x0041b540`,
`0x0041b8b0`, `0x0041ba50`) and the object plumbing (`0x0041ad40` .. `0x0041aec0`): they drive the
frontend menu system, `Mess_*` drawing, medal objects and the ending video, which belong to
milestone 4.10 and the engine object layer. They are the planned slice `competition-runtime`.

## Validation

As for the career: forced calls in the running game on generated competition states (human and
computer participants, random judges, scores around and across the level thresholds, both modes of
`0x00560080`), with the two random-number services scripted and their calls compared. See the
slice completion note for the counts.
