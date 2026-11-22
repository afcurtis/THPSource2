# The pose side of the animation (decomp.cpp, the model list, m3d.cpp)

Status: confirmed by retail forced calls (659 cases of the pose functions, no mismatch)
Build: `f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c`
Addresses: `0x004305e0`, `0x00430920`, `0x00431190`, `0x004311b0`, `0x004311d0`, `0x00464b90`, `0x00464bb0`, `0x00464c00`, `0x00464d10`, `0x00464e90`, `0x00465060`, `0x00465110`, `0x00465300`, `0x00465830` (14 functions) (slice `render-animation-pose`)
Native: `src/runtime/render_kit.hpp`, `render_anim.cpp`, `render_m3d_anim.cpp`; replay `src/runtime/render_retail_calls_test.cpp`; probe `re/gdb/probes/render_specs3.py` (group `ranim`); fixtures `re/evidence/fixtures/render-calls-ranim*.txt`

## What is here

`Animation_GetDecompressedPose(model)` answers the pose buffer of a model's current animation and frame. Animations of kind `0x2c` store a stream of channel codes (a literal, 2..14-bit deltas, a repeat,
zeros) that `Animation_DecompressStream` (an earlier slice, replayed natively here) turns into one channel of angles or positions; the pose is a matrix and a translation per bone built from the decoded
channels and the region's bone hierarchy (parents before children), cached in the model (`+0xe8` the order the bones are posed in, `+0xec` the decoded data, `+0xf2`/`+0xf3` the animation and frame in the
cache). Animations of kind `0x2a` are stored uncompressed: the pose is a pointer into the animation, or the blend of key frames (`M3D_InterpolateAnimationPose`, `M3D_TweenAnimationBuffer`: the six-byte
vectors from the first bone on are the blend of the segment ends around the model's frame). Any other kind asserts.

The same file holds the readers of a model's hook and link tables (`M3D_ReadHooksPacket`, `M3D_ReadLinksPacket`: the link records are read in place and each one finds its parent's record),
`M3D_ClearPoseStuff` (frees a model's link pose and link table), the world position of a part's hook or of a hook record the caller holds (`M3D_GetPartPosition`, `M3D_GetHookPosition`: the bone's pose
entry through the model's matrix, with the lighting matrix's first column negated around the call for a mirrored model), `M3D_BuildPose` (poses a model whose bones follow links: a five-deep matrix stack
of 0x1c-byte entries, the parent's record number at +0xa of the link) and `M3D_SegmentDistance` (the closest points of two Q12 segments through the GTE: cross product, square of a length, the
interpolation by a reciprocal). `ModelList_Construct`, `ModelList_ScalarDelete` and `ModelList_Destruct` are the small container that follows the decoder in the executable (a vtable, twelve cleared
pointers and the count; the destructor deletes every object through its vtable), and `Animation_EmptyHook` (`0x004305e0`) is a bare `ret`.

## Method

The probe builds a model with a region record (bone count, the model table, the raw region, the animation table, the hook table), an animation table whose entries point into a stream of random bytes, and
the model's animation, frame and cached state. The pose generator makes half of its cases "already prepared" (the order and the decoded data exist, as on a second call). Positions and matrices are
random; the world-position functions use a small matrix and positions within +-0x40000 so that the products stay in the GTE's range. The GTE registers live in memory at `0x006a3e00`, so the
captured window covers them and the replay compares the register file as ordinary bytes.

## Lessons

(1) `M3D_GetPartPosition` and `M3D_GetHookPosition` take the output buffer *first* (out, model, part or hook), the opposite of the order the kit's own helpers use; the adapters reorder. (2) A bone index
beyond the bone count, or a frame beyond the animation, makes the retail function read memory outside any window; the generators keep both inside. (3) The pose cache compares the animation byte
`+0xf2` with the model's animation `+0xf6`: the cases give the cached byte the model's animation, its neighbour (`anim ^ 1`) or `0xff`, so that both the reuse and the decode are reached.
