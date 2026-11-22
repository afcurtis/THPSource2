"""Phase 4.15b probe: the bit stream, the sound and event queues, the highlights and the small accessors of the replay recorder (video.cpp; group vbt).

The playback states (ReplayPlaybackState0 / 1, 0x0056e5d8 / 0x0056e450) and the globals around them (0x0056e330..0x0056e790: the sound queue, the splat and trick-object slots, the highlight
records, the car positions) are captured whole. A stream lives in the slab (the state's cursor points into it); the skaters are two fake objects (only their y at +0xc is read) that the Player
array (0x0056a858) names. Nothing but the allocator's Mem_Delete is stubbed.

Slab layout (offsets): a short scalar 0x000, vectors 0x010 / 0x020, a count 0x030, the sound outputs 0x040 / 0x044, the fake skaters 0x100 / 0x200 (0x100 bytes each), the stream 0x400 (0x200 bytes).
"""

from __future__ import annotations

from create_specs import EXTRA_REGIONS, READ_ONLY, SLAB_BYTES, spec
from frontend_specs_front import merged
from frontend_specs_menu import Image

GROUPS = ("vbt",)

SLAB = 0x800
SCALAR = 0x000
VEC_A = 0x010
VEC_OUT = 0x020
COUNT = 0x030
SOUND_OUT = 0x040
SKATER_A = 0x100
SKATER_B = 0x200
STREAM = 0x400
STREAM_WORDS = 0x80

PLAYER = 0x0056A858
MUTED = 0x00561C04
WORLD = 0x0056E330
WORLD_BYTES = 0x460
STATE0 = 0x0056E5D8
STATE1 = 0x0056E450
QUEUE_READ = 0x0056E338
QUEUE_WRITE = 0x0056E438
QUEUE = 0x0056E340
SPLAT_OTHER = 0x0056E43C
SPLAT_PLAYER = 0x0056E440
TRICK_PLAYER = 0x0056E430
TRICK_OTHER = 0x0056E434
END_FRAME = 0x0056E444
START_FRAME = 0x0056E448
START_Y = 0x0056E330
END_Y = 0x0056E334
OPEN = 0x0056E778
CARS = 0x0056E69C
CAR_ANGLES = 0x0056E71C
NUM_FRAMES = 0x0056E5F8
RANGES = 0x0056E62C


def vbt_regions(slab):
    return merged([(slab, SLAB), (PLAYER, 8), (MUTED, 4), (WORLD, WORLD_BYTES)], gap=0)


for _group in GROUPS:
    SLAB_BYTES[_group] = SLAB + 0x100
    EXTRA_REGIONS[_group] = vbt_regions
    READ_ONLY[_group] = []


def vbt(address, name):
    return spec(address, name, "vbt")


def install_stubs(fc, env):
    pass      # (the shared stubs cover the assertion, printf and Mem_Delete)


GROUP_STUBS = {group: install_stubs for group in GROUPS}


def word(rng):
    return rng.choice([0, 1, 2, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x1000, 0xFFFFF000, rng.getrandbits(32), rng.getrandbits(32), rng.randrange(0, 0x10000), rng.randrange(0, 0x100)])


def short(rng):
    return rng.choice([0, 1, -1, 0x1000, -0x1000, 0x7FFF, -0x8000, rng.randrange(-0x1000, 0x1001), rng.randrange(-0x8000, 0x8000), rng.randrange(-0x8000, 0x8000)])


def players(img, env, rng, first=0.5):
    """Player[0] names fake skater A; the second slot is B or empty. Returns (skater of player one, the other)."""
    img.u32(PLAYER, env.slab + SKATER_A)
    img.u32(PLAYER + 4, env.slab + SKATER_B if rng.random() < 0.7 else 0)
    return env.slab + SKATER_A, env.slab + SKATER_B


def pick_skater(env, rng):
    return rng.choice([env.slab + SKATER_A, env.slab + SKATER_B])


def put_stream(img, env, rng):
    for k in range(STREAM_WORDS):
        img.u32(env.slab + STREAM + 4 * k, rng.getrandbits(32))


def stream_state(img, env, rng, which, writing):
    """A state block whose cursor sits somewhere inside the stream and whose word is half used."""
    block = which
    cursor = env.slab + STREAM + 4 * rng.randrange(0, STREAM_WORDS - 8)
    img.u32(block + 0x14, cursor)
    img.u32(block + 0x18, rng.getrandbits(32))
    img.u32(block + 0x1C, rng.choice([0x20, 0x20, 1, 2, 5, 8, 16, 31, 32, rng.randrange(1, 33)]) if writing else rng.choice([0, 1, 2, 5, 8, 16, 31, 32, rng.randrange(0, 33)]))
    return block


# ---- vectors of shorts ------------------------------------------------------------------------------------------------------------------------------------------------------

@vbt(0x004CB130, "Replay_ScaleShorts")
def gen_scale_shorts(env, n):
    img, rng = Image(), env.rng
    img.u16(env.slab + SCALAR, short(rng))
    for k in range(3):
        img.u16(env.slab + VEC_A + 2 * k, short(rng))
    for k in range(3):
        img.u16(env.slab + VEC_OUT + 2 * k, short(rng))
    out = env.slab + (VEC_A if rng.random() < 0.15 else VEC_OUT)
    return [out, env.slab + SCALAR, env.slab + VEC_A], img.pokes()


@vbt(0x004CB1D0, "Replay_ShiftShorts")
def gen_shift_shorts(env, n):
    img, rng = Image(), env.rng
    img.u16(env.slab + COUNT, rng.choice([0, 1, 4, 12, 31, 32, 33, rng.randrange(0, 256), rng.getrandbits(16)]))
    for k in range(3):
        img.u16(env.slab + VEC_A + 2 * k, short(rng))
    for k in range(3):
        img.u16(env.slab + VEC_OUT + 2 * k, short(rng))
    out = env.slab + (VEC_A if rng.random() < 0.15 else VEC_OUT)
    return [out, env.slab + VEC_A, env.slab + COUNT], img.pokes()


# ---- the queues ----------------------------------------------------------------------------------------------------------------------------------------------------------------

@vbt(0x004CB320, "Replay_ResetSoundQueue")
def gen_reset_queue(env, n):
    img, rng = Image(), env.rng
    img.u32(QUEUE_READ, rng.randrange(0, 30))
    img.u32(QUEUE_WRITE, rng.randrange(0, 30))
    return [], img.pokes()


@vbt(0x004CB330, "Replay_PushSound")
def gen_push_sound(env, n):
    img, rng = Image(), env.rng
    img.u32(QUEUE_WRITE, rng.choice([0, 28, 29, rng.randrange(0, 30), rng.randrange(0, 30)]))
    return [word(rng), word(rng)], img.pokes()


@vbt(0x004CB360, "Replay_SoundPending")
def gen_sound_pending(env, n):
    img, rng = Image(), env.rng
    a = rng.randrange(0, 30)
    img.u32(QUEUE_READ, a)
    img.u32(QUEUE_WRITE, a if rng.random() < 0.4 else rng.randrange(0, 30))
    return [], img.pokes()


@vbt(0x004CB380, "Replay_PopSound")
def gen_pop_sound(env, n):
    img, rng = Image(), env.rng
    img.u32(QUEUE_READ, rng.choice([0, 28, 29, rng.randrange(0, 30)]))
    for k in range(30):
        img.u32(QUEUE + 8 * k, word(rng))
        img.u32(QUEUE + 8 * k + 4, word(rng))
    return [env.slab + SOUND_OUT, env.slab + SOUND_OUT + 4], img.pokes()


@vbt(0x004CB3F0, "Replay_RecordSoundStart")
def gen_record_sound(env, n):
    img, rng = Image(), env.rng
    img.u32(STATE0, rng.choice([0, 1, 1, 1, 2]))
    img.u32(MUTED, rng.choice([0, 0, 0, 1, 0x100]))
    img.u32(QUEUE_WRITE, rng.choice([0, 29, rng.randrange(0, 30)]))
    return [word(rng)], img.pokes()


@vbt(0x004CB420, "Replay_ResetSplats")
def gen_reset_splats(env, n):
    img, rng = Image(), env.rng
    img.u32(SPLAT_OTHER, word(rng))
    img.u32(SPLAT_PLAYER, word(rng))
    return [], img.pokes()


@vbt(0x004CB430, "Replay_RecordSplat")
def gen_record_splat(env, n):
    img, rng = Image(), env.rng
    players(img, env, rng)
    img.u32(STATE0, rng.choice([0, 1, 1, 2]))
    skater = rng.choice([env.slab + SKATER_A, env.slab + SKATER_B, 0])
    return [skater, rng.choice([0, 1, 2, 3, 4, 0xFFFFFFFF, rng.getrandbits(32)])], img.pokes()


@vbt(0x004CB4B0, "Replay_ResetTrickObjects")
def gen_reset_trick_objects(env, n):
    img, rng = Image(), env.rng
    img.u32(TRICK_PLAYER, word(rng))
    img.u32(TRICK_OTHER, word(rng))
    return [], img.pokes()


@vbt(0x004CB4C0, "Replay_RecordTrickObject")
def gen_record_trick_object(env, n):
    img, rng = Image(), env.rng
    img.u32(STATE0, rng.choice([0, 1, 1, 2]))
    return [rng.choice([1, 2, 1, 2, 0, 3, 0xFFFFFFFF]), rng.choice([0, 1, 255, 511, 512, 0x7FFFFFFF, 0xFFFFFFFF, rng.randrange(0, 600)])], img.pokes()


# ---- the highlights ---------------------------------------------------------------------------------------------------------------------------------------------------------

def highlight_world(img, env, rng):
    players(img, env, rng)
    for block in (STATE0, STATE1):
        img.u32(block + 0x20, rng.choice([0, 1, 100, 500, rng.randrange(0, 4000)]))
        for slot in range(5):
            record = block + 0x54 + 8 * slot
            img.u16(record, rng.randrange(0, 600))
            img.u16(record + 2, rng.randrange(0, 600))
            img.u32(record + 4, rng.choice([0, 0, 1, 10, 50, 100, 1000, rng.randrange(0, 2000), rng.getrandbits(32)]))
    img.u32(START_FRAME, rng.choice([0, 5, 14, 15, 16, 100, rng.randrange(0, 4000)]))
    img.u32(END_FRAME, rng.choice([0, 100, 0xE00, 0xE07, 0xE08, 0xE09, 0xE0F, 0xE10, 0xE21, 0xE2F, rng.randrange(0, 4000)]))
    img.u32(START_Y, word(rng))
    img.u32(END_Y, word(rng))
    img.u32(OPEN, rng.choice([0, 0, 1]))
    for base in (SKATER_A, SKATER_B):
        img.u32(env.slab + base + 0x0C, rng.choice([0, 0x1000, 0x100000, 0x7FFFFFF, -0x100000 & 0xFFFFFFFF, word(rng)]))


@vbt(0x004CB580, "Replay_RecordHighlightStart")
def gen_highlight_start(env, n):
    img, rng = Image(), env.rng
    highlight_world(img, env, rng)
    return [pick_skater(env, rng)], img.pokes()


@vbt(0x004CB5C0, "Replay_RecordHighlightEnd")
def gen_highlight_end(env, n):
    img, rng = Image(), env.rng
    highlight_world(img, env, rng)
    score = rng.choice([0, 0, 1, 10, 100, 500, 1000, -1, -100, -1000, rng.randrange(-3000, 3000), rng.getrandbits(32) - (1 << 31)])
    return [pick_skater(env, rng), score & 0xFFFFFFFF], img.pokes()


@vbt(0x004CB6E0, "Replay_PrepareHighlights")
def gen_prepare_highlights(env, n):
    img, rng = Image(), env.rng
    frames = rng.choice([1, 50, 100, 200, 400, 0xFFFF, 0x10001, rng.randrange(1, 500)])
    img.u32(NUM_FRAMES, frames)
    limit = min(frames + 40, 500)
    near = rng.random() < 0.5
    cursor = 0
    for slot in range(5):
        record = RANGES + 8 * slot
        if near:       # ranges that sit next to each other so the merge fires
            start = cursor + rng.randrange(0, 20)
            end = start + rng.randrange(0, 30)
            cursor = end + rng.randrange(0, 25)
        else:
            start = rng.randrange(0, limit)
            end = rng.randrange(start, limit + 1) if rng.random() < 0.8 else rng.randrange(0, limit)
        if rng.random() < 0.1:
            start = rng.randrange(-30, 0)
        img.u16(record, start & 0xFFFF)
        img.u16(record + 2, end & 0xFFFF)
        img.u32(record + 4, rng.choice([0, 1, 5, 20, rng.randrange(0, 100), rng.randrange(0, 100)]))
    return [], img.pokes()


# ---- the bit stream ---------------------------------------------------------------------------------------------------------------------------------------------------------

@vbt(0x004CCC40, "Replay_BitsNeeded")
def gen_bits_needed(env, n):
    rng = env.rng
    # (retail loops for ever from 0x40000000 up and below -0x40000000: the shifted limit overflows to the wrong sign)
    v = rng.choice([0, 1, 2, 3, 4, 7, 8, -1, -2, -3, -4, -5, -8, -9, 0x3FFFFFFF, -0x40000000, 0x20000000, -0x20000001, rng.randrange(-100000, 100000), rng.randrange(-(1 << 30), 1 << 30),
                    rng.randrange(-256, 256)])
    return [v & 0xFFFFFFFF], Image().pokes()


@vbt(0x004CCC70, "Replay_WriteBits")
def gen_write_bits(env, n):
    img, rng = Image(), env.rng
    put_stream(img, env, rng)
    block = stream_state(img, env, rng, rng.choice([STATE0, STATE1]), writing=True)
    bits = rng.choice([0, 1, 2, 3, 4, 8, 9, 16, 20, 31, 32, rng.randrange(0, 33), rng.randrange(0, 33), -1 & 0xFFFFFFFF, 33])
    return [block, word(rng), bits], img.pokes()


@vbt(0x004CD5A0, "Replay_ReadBits")
def gen_read_bits(env, n):
    img, rng = Image(), env.rng
    put_stream(img, env, rng)
    block = stream_state(img, env, rng, rng.choice([STATE0, STATE1]), writing=False)
    bits = rng.choice([1, 2, 3, 4, 8, 9, 16, 20, 31, 32, rng.randrange(1, 33), rng.randrange(1, 33), 0, 33])
    return [block, bits], img.pokes()


@vbt(0x004CD690, "Replay_ReadSignedBits")
def gen_read_signed(env, n):
    img, rng = Image(), env.rng
    put_stream(img, env, rng)
    block = stream_state(img, env, rng, rng.choice([STATE0, STATE1]), writing=False)
    bits = rng.choice([1, 2, 3, 4, 8, 9, 16, 20, 31, 32, rng.randrange(1, 33), rng.randrange(1, 33), 0, 33])
    return [block, bits], img.pokes()


@vbt(0x004CD710, "Replay_ReadFlag")
def gen_read_flag(env, n):
    img, rng = Image(), env.rng
    put_stream(img, env, rng)
    block = stream_state(img, env, rng, rng.choice([STATE0, STATE1]), writing=False)
    return [block], img.pokes()


@vbt(0x004CBF70, "Replay_FlushInputBlock")
def gen_flush(env, n):
    img, rng = Image(), env.rng
    put_stream(img, env, rng)
    block = stream_state(img, env, rng, rng.choice([STATE0, STATE1]), writing=True)
    return [block], img.pokes()


@vbt(0x004CBF30, "Replay_ResetSkaterInput")
def gen_reset_skater(env, n):
    img, rng = Image(), env.rng
    put_stream(img, env, rng)
    players(img, env, rng)
    skater = rng.choice([env.slab + SKATER_A, env.slab + SKATER_B, 0])
    for block in (STATE0, STATE1):
        stream_state(img, env, rng, block, writing=True)
        img.u32(block, rng.choice([0, 1, 1, 2]))
    img.u32(0x0056E774, word(rng))
    return [skater], img.pokes()


@vbt(0x004CBC90, "Replay_ClearPlaybackState")
def gen_clear_state(env, n):
    img, rng = Image(), env.rng
    img.u32(STATE0, word(rng))
    img.u32(STATE1, word(rng))
    return [], img.pokes()


@vbt(0x004CBCA0, "Replay_FreeStreamBuffers")
def gen_free_buffers(env, n):
    img, rng = Image(), env.rng
    img.u32(STATE0 + 4, rng.choice([0, env.slab + STREAM, rng.getrandbits(32)]))
    img.u32(STATE1 + 4, rng.choice([0, env.slab + STREAM + 0x100, rng.getrandbits(32)]))
    return [], img.pokes()


@vbt(0x004CDE70, "Video_Playing")
def gen_playing(env, n):
    img, rng = Image(), env.rng
    img.u32(STATE0, rng.choice([0, 1, 2, 2, 3, word(rng)]))
    return [], img.pokes()


@vbt(0x004CDE80, "Replay_StoreCarPosition")
def gen_store_car(env, n):
    img, rng = Image(), env.rng
    slot = rng.choice([0, 1, 7, 8, 9, 0x7FFFFFFF, 3, 5, -1 & 0xFFFFFFFF])
    for k in range(8):
        for j in range(3):
            img.u32(CARS + 0x10 * k + 4 * j, word(rng))
        img.u16(CAR_ANGLES + 8 * k, short(rng) & 0xFFFF)
        img.u16(CAR_ANGLES + 8 * k + 2, short(rng) & 0xFFFF)
        img.u16(CAR_ANGLES + 8 * k + 4, short(rng) & 0xFFFF)
    return [word(rng), slot, word(rng), word(rng), word(rng), word(rng), rng.getrandbits(32)], img.pokes()


@vbt(0x004CDEF0, "Replay_LoadCarPosition")
def gen_load_car(env, n):
    img, rng = Image(), env.rng
    slot = rng.choice([0, 1, 7, 8, 9, 0x7FFFFFFF, 3, 5, -1 & 0xFFFFFFFF])
    for k in range(8):
        for j in range(3):
            img.u32(CARS + 0x10 * k + 4 * j, word(rng))
        for j in range(3):
            img.u16(CAR_ANGLES + 8 * k + 2 * j, short(rng) & 0xFFFF)
    return [word(rng), slot, env.slab + VEC_A, env.slab + VEC_OUT], img.pokes()
