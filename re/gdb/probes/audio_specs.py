"""Phase 4.13b probe: the game's audio and movie interface (groups crowd, sfx, vab, redbook, movie).

crowd.cpp (the crowd's reactions and per-frame update), the sound effect front of sfx.cpp (SFX_PlayX / PlayLR, the positional sounds, the reverb), the VAB opening and closing, the
redbook / XA music sequencer, Audio_StopAll, the game FMV list and the movie interface of pc\\pcmovie.cpp. Same shape as platform_specs.py: the functions work on the game's own globals
(windows of the real address space poked per case); the HAL's voices, the sound bank, the SPU reverb calls, Bink and the Win32 imports are stubs. The imports are stubbed on their call
sites in the functions under test (fcall `stub_site`), not on the DLL functions: Wine's own threads call the same ones.

All five groups share one set of windows and one seed: the nested natives (the sequencer calls the movie interface, the pause calls both) are the real retail functions.
"""

from __future__ import annotations

import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import ensure
from frontend_specs_menu import Image
from platform_specs import mark_under_test

KSTACK = 0x7FF00700            # what an argument that points at a stack local is recorded as
SLAB = 0x3000

# the slab's layout
RECORDS = 0x100                # four bank records of 0x40 bytes (+4 the pitch scale as a float, +0x2c the loaded byte)
STRINGS = 0x200                # the names the VAB openers and the movie list point at (0x20 bytes each)
SCRATCH = 0x400                # words the case generators use for pointers (a value to move, a rect)
RECT = 0x480                   # the movie window's rectangle (0x30 bytes)
OT = 0x500                     # the ordering table object (+0x84 the pointer to the bucket cell)
HEAD = 0x600                   # the bucket cell
POOL = 0x700                   # the primitive pool: room for three 0xc0-byte records
DISPLAY = 0x980                # the display surface object, its vtable at +0x40 (slot 0x23 is the fake method)
MOVIE_TABLE = 0xB00            # the movie list: 36-byte rows, +0 the name pointer, +4 a second pointer
PLAYER = 0x40                  # the second player's object: only +0x2cc0 is read
FAKE_SURFACE_METHOD = 0x004ADC40       # a `ret` in the sound code nothing else uses

WINDOWS = [
    (0x0052BE40, 0x10),        # the effects volume (0x52be44) and the music volume (0x52be48)
    (0x005376F0, 0xA0),        # the sector offsets of the groups (0x5376fc), the lengths of the 32 tracks (0x53770c), the music fade (0x53778c)
    (0x00537920, 0x10),        # the open VAB ids (0x537928, 0x53792c)
    (0x00533F30, 0x10),        # the game type words (0x533f38, 0x533f3c)
    (0x0053A510, 0x200),       # the players' sound set flags (0x170 bytes apart)
    (0x0053C240, 0x1700),      # the level records (0x1ac bytes each)
    (0x0056A880, 0x30),        # the pause flag (0x56a880) and the current level (0x56a898)
    (0x0056A950, 0x10),        # the split screen player (0x56a958)
    (0x00560FA0, 0x40),        # the crowd (0x560fa8..0x560fc0), the primitive pool head and the ordering table (0x560fd0, 0x560fd4)
    (0x0056BED8, 0x8),         # the pitch Audio_PlaySound reads
    (0x0056C058, 0x8),         # the reverb state
    (0x0056C4E0, 0x10),        # the sound flag byte (0x56c4e8)
    (0x029D7D20, 0x640),       # the bank record pointers
    (0x029D6920, 0x1400),      # the voice slots
    (0x029D8360, 0x8),         # the audio device flag
    (0x0056B860, 0x44),        # the XA sequencer (0x56b864..0x56b898)
    (0x005616F0, 0x10),        # the saved music position
    (0x005615D0, 0x20),        # the music mute flags (0x5615d0, 0x5615dc)
    (0x0056D9D0, 0x60),        # the mute table (17 bytes per entry from 0x56d9d5)
    (0x0056DB30, 0x8),         # its index
    (0x00561CF0, 0xF0),        # the current game FMV (0x561cf8..0x561ddc), the movie-active flag (0x561dc8)
    (0x0052CF80, 0x560),       # the FMV rows (36 bytes each from 0x52cfa2), the pointer to the movie list (0x52d4a8), the current entry (0x52d4dc)
    (0x006A6CE0, 0x30),        # the movie state: playing, stopping, the Bink handle, the window (0x6a6d08)
    (0x0054B0A8, 0x8),         # the music thread's stop flag
    (0x006A6BD8, 0x8),         # the movie's texture words
    (0x006A6964, 0x4),         # the control mode
    (0x029DA3C0, 0x8),         # the display surface object
]
TABLES = [
    (0x00519720, 0x10),        # the pitch scale float
    (0x0052CA60, 0x60), (0x0052CE60, 0x10),         # the front end VAB names
    (0x005379E0, 0x80),        # the level VAB names
    (0x0054B230, 0x10), (0x0054B2F0, 0x20),         # ".DAT", "LTIX"
]

for _group in ("crowd", "sfx", "vab", "redbook", "movie"):
    SLAB_BYTES[_group] = SLAB
    EXTRA_REGIONS[_group] = lambda slab: [(slab, SLAB)] + WINDOWS
    READ_ONLY[_group] = TABLES


def _seed(env, rng):
    """The bank table points every sound at the first record; the display surface has the fake method."""
    slab = env.slab
    out = {}
    records = bytearray(0x100)
    for k in range(4):
        struct.pack_into("<f", records, 0x40 * k + 4, [1.0, 0.5, 2.0, 1.37][k])
        records[0x40 * k + 0x2C] = 1 if k % 2 == 0 else 0
    out[slab + RECORDS] = bytes(records)
    out[0x029D7D20] = struct.pack("<I", slab + RECORDS) * 0x18C
    out[slab + DISPLAY] = struct.pack("<I", slab + DISPLAY + 0x40)
    vtable = [0] * 0x30
    vtable[0x23] = FAKE_SURFACE_METHOD
    out[slab + DISPLAY + 0x40] = struct.pack("<48I", *vtable)
    return out


for _group in ("crowd", "sfx", "vab", "redbook", "movie"):
    SEEDS[_group] = _seed


def u(value):
    return value & 0xFFFFFFFF


def c_string(text):
    return text.encode("latin-1") + b"\0"


def audio(group):
    def deco(address, name):
        return spec(address, name, group)
    return deco


crowd = audio("crowd")
sfx = audio("sfx")
vab = audio("vab")
redbook = audio("redbook")
movie = audio("movie")

CROWD_SOUNDS = [0xDE, 0xDF, 0x30, 0x11D, 0x2E, 0x31, 0x2F, 0x32, 0x2C]


def settings(img, rng):
    """The volumes: the effects scale (0x4000 full) and the music volume (0: the level-picked music; 1..0xff the CD music; a value above 0xff trips an assertion)."""
    img.u32(0x0052BE44, rng.choice([0x4000, 0x4000, 0x2000, 0, 0x3FFF, 0x1234, 0x5000, rng.randrange(0x4001)]))
    img.u32(0x0052BE48, rng.choice([0, 0, 0xFF, 0x80, 1, 0x7F, 0x100, rng.randrange(0x100)]))


def bank(img, env, rng, sounds):
    """The bank entries of the sounds a case plays: a loaded record, an unloaded one or none; the records get a random pitch scale (rarely a huge one)."""
    for sound in sounds:
        if 0 <= sound < 0x18C:
            img.u32(0x029D7D20 + 4 * sound, rng.choice([env.slab + RECORDS + 0x40 * k for k in range(4)] + [0]) if rng.random() < 0.6 else env.slab + RECORDS)
    for k in range(4):
        pitch = rng.choice([1.0, 0.5, 2.0, 1.37, rng.uniform(0.1, 5.0), rng.uniform(0.0, 40000.0), 3.0e38, -1.5, 0.0])
        img.put(env.slab + RECORDS + 0x40 * k + 4, struct.pack("<f", pitch))


def level_world(img, rng, *, crowd_level=True):
    level = rng.choice(list(range(0, 14)) + [0, 1, 2, 5, 9, 10, 11])
    img.u32(0x0056A898, level)
    record = 0x0053C240 + 0x1AC * level
    img.u8(record, rng.choice([0, 1, 1]))
    img.u32(record + 0x3C, rng.choice([0, 1, 1, 1, 0x1234]) if crowd_level else 0)
    img.u32(0x0056A880, rng.choice([0, 0, 0, 1]))
    return level


def mode_world(img, rng):
    img.u32(0x00533F38, rng.choice([1, 1, 1, 0, 2, 7]))
    img.u32(0x00533F3C, rng.choice([0, 1, 2, 2, 0, 3]))


def crowd_world(img, env, rng):
    split_world(img, env, rng)
    level_world(img, rng)
    mode_world(img, rng)
    settings(img, rng)
    img.u32(0x00560FA8, rng.choice([0xFFFFFFFF, 0x10000, 5, 0x20003, 0]))
    for address in (0x00560FAC, 0x00560FB0, 0x00560FB4, 0x00560FB8):
        img.u32(address, rng.choice([0, 100, 0x1000, 0x2000, rng.randrange(0, 0x2000), u(-50)]))
    for level, target in ((0x00560FAC, 0x00560FB4), (0x00560FB0, 0x00560FB8)):
        if rng.random() < 0.4:
            img.u32(level, img.b[target] | img.b[target + 1] << 8 | img.b[target + 2] << 16 | img.b[target + 3] << 24)      # (already there: the update draws new targets)
    img.u32(0x00560FC0, rng.choice([0, 0, 1, 2, 5, 100, u(-1)]))
    bank(img, env, rng, CROWD_SOUNDS)


def split_world(img, env, rng):
    """The second player's object and the players' sound set flags (the split screen substitutes)."""
    player = env.slab + PLAYER
    img.u32(0x0056A958, rng.choice([0, player, player]))
    img.u32(player + 0x2CC0, rng.choice([0, 1]))
    for k in range(2):
        img.u8(0x0053A514 + 0x170 * k, rng.choice([0, 1, 1]))


# ---- crowd.cpp ---------------------------------------------------------------------------------------------------------------------------------------------------------------

@crowd(0x0042F3E0, "Crowd_MoveTowards")
def gen_move_towards(env, n):
    img, rng = Image(), env.rng
    where = env.slab + SCRATCH
    current = rng.choice([0, 100, 0x1000, 0x2000, rng.randrange(0, 0x2000), u(-77), rng.getrandbits(32)])
    img.u32(where, current)
    target = rng.choice([current, current + 50, current - 50, current + 100, current - 100, current + 99, current - 101, 0, 0x2000, rng.randrange(0, 0x2000)])
    return [where, u(target), rng.choice([100, 100, 1, 50, 150, 0, 0x2000])], img.pokes()


@crowd(0x0042F430, "Crowd_Update")
def gen_crowd_update(env, n):
    img = Image()
    crowd_world(img, env, env.rng)
    return [], img.pokes()


def score_value(rng):
    return rng.choice([0, 1, 50, 99, 100, 200, 0x190, 0x18F, 1000, 0x1F40, 0x1F3F, 5000, 0x3A98, 0x3A99, 20000, 60000, 100000, 1000000, u(-5), u(-20000), rng.randrange(0, 70000)])


@crowd(0x0042F540, "Crowd_ReactToScore")
def gen_react_score(env, n):
    img = Image()
    crowd_world(img, env, env.rng)
    return [score_value(env.rng)], img.pokes()


@crowd(0x0042F680, "Crowd_ReactToBail")
def gen_react_bail(env, n):
    img = Image()
    crowd_world(img, env, env.rng)
    return [score_value(env.rng)], img.pokes()


@crowd(0x0042F700, "Crowd_ReactToTrick")
def gen_react_trick(env, n):
    img = Image()
    crowd_world(img, env, env.rng)
    return [score_value(env.rng)], img.pokes()


@crowd(0x0042F800, "Crowd_Boo")
def gen_boo(env, n):
    img = Image()
    crowd_world(img, env, env.rng)
    return [env.rng.choice([0, 1, 0x2000, 0x4000, 8192 * 3, 8192 * 100, u(-8192), env.rng.randrange(0, 0x100000)])], img.pokes()


@crowd(0x0042F2E0, "Crowd_ClearSound")
def gen_crowd_clear_sound(env, n):
    img = Image()
    img.u32(0x00560FA8, env.rng.getrandbits(32))
    return [], img.pokes()


@crowd(0x0042F2F0, "Crowd_Nothing")
def gen_crowd_nothing(env, n):
    return [], {}


@crowd(0x0042F300, "Crowd_Start")
def gen_crowd_start(env, n):
    img = Image()
    crowd_world(img, env, env.rng)
    return [], img.pokes()


@crowd(0x0042F370, "Crowd_Stop")
def gen_crowd_stop(env, n):
    img = Image()
    crowd_world(img, env, env.rng)
    return [], img.pokes()


# ---- sfx.cpp ----------------------------------------------------------------------------------------------------------------------------------------------------------------

PLAY_SOUNDS = [0xFFFFFFFF, 0x1C, 0x1C, 0x1C, 0x1D, 0x1E, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0x180, 0x180, 0x180, 0x10, 0x30, 0x189, 0x18A, 0x18B, 0x181, 0x2E, 5, 0x11D]


def volume_value(rng):
    return rng.choice([0, 0x4000, 0x2000, 0x7FFF, 0x8000, 0xFFFF, u(-1), 1, 0x1234, 0x12345678, u(-0x4000), rng.getrandbits(32)])


def play_world(img, env, rng, sound):
    split_world(img, env, rng)
    mode_world(img, rng)
    settings(img, rng)
    mapped = {0x1C: 0x189, 0xB5: 0x18A, 0xB6: 0x18A, 0xB7: 0x18A, 0xB8: 0x18A, 0xB9: 0x18B, 0xBA: 0x18B, 0xBB: 0x18B, 0x180: 0x181}
    bank(img, env, rng, [sound if 0 <= sound < 0x18C else 0, mapped.get(sound, 0), 0x189, 0x18A, 0x18B, 0x181])


@sfx(0x004AD620, "SFX_PlayX")
def gen_play_x(env, n):
    img, rng = Image(), env.rng
    sound = rng.choice(PLAY_SOUNDS)
    play_world(img, env, rng, -1 if sound == 0xFFFFFFFF else sound)
    return [sound, volume_value(rng), rng.choice([0, 1, 5, 0x100, rng.getrandbits(32)])], img.pokes()


@sfx(0x004AD750, "SFX_PlayLR")
def gen_play_lr(env, n):
    img, rng = Image(), env.rng
    sound = rng.choice(PLAY_SOUNDS)
    play_world(img, env, rng, -1 if sound == 0xFFFFFFFF else sound)
    return [sound, volume_value(rng), volume_value(rng), rng.choice([0, 1, 5, 0x100, rng.getrandbits(32)])], img.pokes()


@sfx(0x004AD7D0, "Audio_PlayPositionalSound")
def gen_play_positional(env, n):
    img, rng = Image(), env.rng
    sound = rng.choice(PLAY_SOUNDS)
    play_world(img, env, rng, -1 if sound == 0xFFFFFFFF else sound)
    return [sound, rng.getrandbits(32) & 0xFFFFFFF0, rng.choice([0, 1, 5, 0x100, rng.getrandbits(32)])], img.pokes()


def voice_value(rng):
    return rng.choice([0xFFFFFFFF, 0, 1, 5, 0x10003, 0x20007, rng.getrandbits(32)])


@sfx(0x004AD870, "SFX_GetVoiceFrequency")
def gen_voice_frequency(env, n):
    return [voice_value(env.rng)], {}


@sfx(0x004AD890, "SFX_SetVoiceFrequency")
def gen_set_voice_frequency(env, n):
    rng = env.rng
    return [voice_value(rng), rng.choice([0, 0x5622, 0xFFFF, 0x10000, 0x12345, rng.getrandbits(32)])], {}


@sfx(0x004AD8B0, "SFX_ModifyPos")
def gen_modify_pos(env, n):
    img, rng = Image(), env.rng
    settings(img, rng)
    return [voice_value(rng), rng.getrandbits(32) & 0xFFFFFFF0], img.pokes()


@sfx(0x004AD950, "Audio_UpdatePositionalSound")
def gen_update_positional(env, n):
    img, rng = Image(), env.rng
    settings(img, rng)
    value = lambda: rng.choice([0, 1, 0x1000, 0x3FFF, 0x4000, 0x4001, 0x7FFF, u(-1), u(-0x4001), rng.randrange(0, 0x6000)])
    return [voice_value(rng), value(), value()], img.pokes()


@sfx(0x004AD9B0, "SFX_ClearFlag")
def gen_clear_flag(env, n):
    img = Image()
    img.u8(0x0056C4E8, env.rng.choice([0, 1, 0xFF]))
    return [], img.pokes()


def movie_world(img, env, rng):
    """The movie state (see movie_gen): used by every function that pauses, stops or plays music."""
    handle = rng.choice([0, 0x1234, 0x00AB0000])
    img.u32(0x006A6CE4, handle)
    img.u8(0x006A6CE0, rng.choice([0, 1, 1]))
    img.u8(0x006A6CE2, rng.choice([0, 0, 1]))
    img.u8(0x0054B0A8, rng.choice([0, 1]))
    img.u32(0x006A6D08, rng.choice([0, rng.getrandbits(32)]))
    img.u32(0x029DA3C0, env.slab + DISPLAY)          # (the display surface: a fake object whose slot 0x23 is the fake method)


@sfx(0x004AD9C0, "SFX_Pause")
def gen_sfx_pause(env, n):
    img, rng = Image(), env.rng
    movie_world(img, env, rng)
    img.u32(0x0056B880, rng.choice([0, 0, 1, 7]))
    return [], img.pokes()


@sfx(0x004AD9D0, "SFX_Resume")
def gen_sfx_resume(env, n):
    img, rng = Image(), env.rng
    movie_world(img, env, rng)
    img.u32(0x0056B880, rng.choice([0, 1, 1, 7]))
    return [], img.pokes()


@sfx(0x004AD9F0, "SFX_StopVoice")
def gen_stop_voice(env, n):
    return [voice_value(env.rng)], {}


def voices_world(img, rng):
    img.u32(0x029D8360, rng.choice([0, 1, 1, 1]))
    for slot in range(128):
        img.u32(0x029D6920 + 0x28 * slot, rng.choice([0, 0, 0, 0x01230000 + slot]) if rng.random() < 0.5 else 0)


def vab_world(img, rng):
    img.u32(0x00537928, rng.choice([0xFFFFFFFF, 0, 1, 2, 3]))
    img.u32(0x0053792C, rng.choice([0xFFFFFFFF, 4, 5, 9, 17, 0]))


@sfx(0x004ADA10, "SFX_StopAllAndCloseVab")
def gen_stop_all_and_close(env, n):
    img, rng = Image(), env.rng
    voices_world(img, rng)
    vab_world(img, rng)
    return [], img.pokes()


@sfx(0x004F38D0, "Audio_StopAll")
def gen_stop_all(env, n):
    img = Image()
    voices_world(img, env.rng)
    return [], img.pokes()


@sfx(0x004ADA20, "SFX_SetReverb")
def gen_set_reverb(env, n):
    img, rng = Image(), env.rng
    img.u32(0x0056C058, rng.getrandbits(32))
    return [rng.choice([0, 1, 2, 5, 9, 10, 11, 0x100, 0x103, 0xFF, rng.getrandbits(32)])], img.pokes()


@sfx(0x004ADB90, "SFX_ReverbOff")
def gen_reverb_off(env, n):
    img = Image()
    img.u32(0x0056C058, env.rng.getrandbits(32))
    return [], img.pokes()


@sfx(0x004ADBA0, "SFX_SetReverbDepth")
def gen_set_reverb_depth(env, n):
    rng = env.rng
    depth = lambda: rng.choice([0, 1, 0x40, 0x7F, 0x80, 0xFF, 0x100, 0x1FF, rng.getrandbits(32)])
    return [depth(), depth()], {}


for _address, _name in ((0x004ADC30, "SFX_Nothing1"), (0x004ADC50, "SFX_Nothing2"), (0x004ADC60, "SFX_Nothing3"), (0x004ADC70, "SFX_Nothing4")):
    @sfx(_address, _name)
    def gen_nothing(env, n):
        return [], {}


# ---- the VAB banks ----------------------------------------------------------------------------------------------------------------------------------------------------------

FRONTEND_NAMES = ["shell.vab", "skate2.vab", "skate2g.vab", "skate2p.vab", "skate2.VAB", "", "shell", "skate2.vabx"]
LEVEL_NAMES = ["hangar", "school2", "mar", "venice", "philly", "bullring", "heaven", "sked", "ware", "comp", "JAM", "Hangar", "unknown", "", "marx", "ma", "ny", "sf", "la"]


def vab_name_world(img, env, rng, names):
    name = env.slab + STRINGS
    chosen = rng.choice(names)
    img.put(name, c_string(chosen))
    return name


@vab(0x004ACFF0, "Audio_CloseFrontendVab")
def gen_close_frontend_vab(env, n):
    img = Image()
    vab_world(img, env.rng)
    return [], img.pokes()


@vab(0x004AD5B0, "Audio_CloseLevelVab")
def gen_close_level_vab(env, n):
    img = Image()
    vab_world(img, env.rng)
    return [], img.pokes()


@vab(0x004AD5E0, "Audio_Nothing")
def gen_vab_nothing(env, n):
    return [], {}


@vab(0x004AD020, "Audio_OpenFrontendVab")
def gen_open_frontend_vab(env, n):
    img, rng = Image(), env.rng
    vab_world(img, rng)
    return [vab_name_world(img, env, rng, FRONTEND_NAMES)], img.pokes()


@vab(0x004AD180, "Audio_OpenLevelVab")
def gen_open_level_vab(env, n):
    img, rng = Image(), env.rng
    vab_world(img, rng)
    # (the level names the retail table knows are read from the executable's own strings: pick from the table's names too)
    from opentony.memory import mem

    table = [mem.cstring(a, 32) for a in (0x537A38, 0x537A30, 0x537A2C, 0x537A28, 0x537A20, 0x537A1C, 0x537A14, 0x537A08, 0x537A04, 0x5379FC, 0x5379F4, 0x5379EC, 0x5379E4, 0x5379E0)]
    return [vab_name_world(img, env, rng, LEVEL_NAMES + table + table)], img.pokes()


# ---- the redbook sequencer -------------------------------------------------------------------------------------------------------------------------------------------------

def redbook_world(img, env, rng):
    lengths = []
    for index in range(32):
        # (the tracks the level-picked music and the CD music use: some empty ones; the sequencer needs a valid track at or above 16 to move on)
        lengths.append(0xFFFFFFFF if rng.random() < (0.3 if index < 16 else 0.2) else rng.randrange(1, 6000))
    lengths[16] = rng.randrange(1, 6000)
    lengths[24] = rng.randrange(1, 6000)
    for index, value in enumerate(lengths):
        img.u32(0x0053770C + 4 * index, value)
    for group in range(4):
        img.u32(0x005376FC + 4 * group, rng.randrange(0, 100000))
    img.u32(0x0053778C, rng.choice([0xFF, 0, 1, 2, 3, 0x80, rng.randrange(0, 0x100)]))
    img.u32(0x0056B898, rng.choice([0, 0, 1]))
    img.u32(0x0056B874, rng.getrandbits(32))
    img.u32(0x0056B864, rng.getrandbits(32))
    img.u32(0x0056B868, rng.getrandbits(32))
    img.u32(0x0056B888, rng.randrange(0, 4))
    img.u32(0x0056B88C, rng.randrange(0, 8))
    img.u32(0x0056B878, rng.choice([0, 0, 1, 5, 0x1E]))
    img.u32(0x0056B880, rng.choice([0, 0, 0, 1]))
    img.u8(0x0056B884, rng.choice([0, 1, 1, 1]))
    img.u8(0x0056B87C, rng.choice([0, 1]))
    img.u8(0x0056B87D, rng.choice([0, 1]))
    img.u32(0x0056B890, rng.choice([0, 1]))
    img.u32(0x0056B894, rng.choice([0, 1]))
    img.u32(0x005615D0, rng.choice([0, 0, 0, 1]))
    img.u8(0x005615DC, rng.choice([0, 0, 0, 1]))
    img.u32(0x00561DC8, rng.choice([0, 0, 0, 1]))
    img.u32(0x0056DB30, rng.randrange(0, 4))
    for k in range(4):
        img.u8(0x0056D9D5 + 17 * k, rng.choice([0, 0, 0, 1]))
    settings(img, rng)
    img.u32(0x0056A898, rng.choice(list(range(0, 14)) + [3, 9, 10]))
    img.u32(0x0056A880, rng.choice([0, 0, 0, 1]))
    movie_world(img, env, rng)
    return lengths


@redbook(0x004AB9C0, "Redbook_ResetVolume")
def gen_rb_reset_volume(env, n):
    img = Image()
    redbook_world(img, env, env.rng)
    return [], img.pokes()


@redbook(0x004AB9E0, "Redbook_UpdateVolume")
def gen_rb_update_volume(env, n):
    img = Image()
    redbook_world(img, env, env.rng)
    return [], img.pokes()


@redbook(0x004ABA90, "Redbook_Reset")
def gen_rb_reset(env, n):
    img = Image()
    redbook_world(img, env, env.rng)
    return [], img.pokes()


@redbook(0x004ABB00, "Redbook_Play")
def gen_rb_play(env, n):
    img, rng = Image(), env.rng
    redbook_world(img, env, rng)
    group = rng.choice([0, 1, 2, 3, 0, 1, 2, 3, 4])          # (group 4's only readable track is 0: index 32 is the music fade word)
    return [group, 0 if group == 4 else rng.randrange(0, 8)], img.pokes()


@redbook(0x004ABC80, "Redbook_Stop")
def gen_rb_stop(env, n):
    img = Image()
    redbook_world(img, env, env.rng)
    return [], img.pokes()


def next_track_ends(lengths, group, track, step):
    """The sequencer's walk (a valid track at or above 16 ends it): False when it would go round for ever."""
    for _ in range(200):
        track += step
        if track >= 8:
            group, track = group + 1, 0
        elif track < 0:
            group, track = group - 1, 7
        group = 0 if group >= 4 else 3 if group < 0 else group
        if lengths[track + group * 8] != 0xFFFFFFFF and track + group * 8 >= 16:
            return True
    return False


@redbook(0x004ABCB0, "Redbook_Next")
def gen_rb_next(env, n):
    img, rng = Image(), env.rng
    lengths = redbook_world(img, env, rng)
    step = rng.choice([1, 1, 1, -1, -1, 2, -2, 3, 5, 7, 8, 9, 17, -7, -9, 16])
    group, track = rng.randrange(0, 4), rng.randrange(0, 8)
    img.u32(0x0056B888, group)
    img.u32(0x0056B88C, track)
    if not next_track_ends(lengths, group, track, step):
        for index in range(32):
            img.u32(0x0053770C + 4 * index, rng.randrange(1, 6000))
    return [u(step)], img.pokes()


@redbook(0x004ABE20, "Redbook_SetPaused")
def gen_rb_set_paused(env, n):
    img, rng = Image(), env.rng
    redbook_world(img, env, rng)
    return [rng.choice([0, 1, 1, 0, 2, 0x100, 0x101])], img.pokes()


@redbook(0x004ABE50, "Redbook_Update")
def gen_rb_update(env, n):
    img = Image()
    redbook_world(img, env, env.rng)
    return [], img.pokes()


@redbook(0x004ABED0, "Redbook_CanContinue")
def gen_rb_can_continue(env, n):
    img = Image()
    redbook_world(img, env, env.rng)
    return [], img.pokes()


def position_world(img, env, rng):
    where = env.slab + SCRATCH
    img.put(where, bytes(rng.randrange(256) for _ in range(12)))
    return where


@redbook(0x004ABF10, "Redbook_SavePosition")
def gen_rb_save_position(env, n):
    img, rng = Image(), env.rng
    redbook_world(img, env, rng)
    return [position_world(img, env, rng)], img.pokes()


@redbook(0x004ABF80, "Redbook_RestorePosition")
def gen_rb_restore_position(env, n):
    img, rng = Image(), env.rng
    redbook_world(img, env, rng)
    where = env.slab + SCRATCH
    img.u32(where, rng.randrange(0, 4))
    img.u32(where + 4, rng.randrange(0, 8))
    img.u32(where + 8, rng.getrandbits(32))
    if rng.random() < 0.4:
        img.u32(where, 0)
        img.u32(where + 4, 0)
        img.u32(0x0056B888, 0)
        img.u32(0x0056B88C, 0)
    return [where], img.pokes()


@redbook(0x004AC010, "Redbook_TrackNumber")
def gen_rb_track_number(env, n):
    img = Image()
    redbook_world(img, env, env.rng)
    return [], img.pokes()


# ---- gamefmv.cpp and pc\pcmovie.cpp ---------------------------------------------------------------------------------------------------------------------------------------------------------

MOVIE_FILES = ["intro.bik", "activision.bik", "neversoft.bik", "tony.bik", "", "a", "ltix1.dat", "levels\\x.bik"]


def movie_list_world(img, env, rng):
    """The movie list (rows of 36 bytes: the name pointer, a second pointer) in the slab and the current game FMV index."""
    table = env.slab + MOVIE_TABLE
    img.u32(0x0052D4A8, table)
    for k in range(8):
        name = env.slab + STRINGS + 0x20 * k
        img.put(name, c_string(rng.choice(MOVIE_FILES)))
        img.u32(table + 36 * k, name)
        img.u32(table + 36 * k + 4, env.slab + STRINGS + 0x20 * rng.randrange(8))
    img.u8(0x00561D36, rng.randrange(0, 8))


def rect_world(img, env, rng):
    rect = env.slab + RECT
    img.put(rect, bytes(rng.randrange(256) for _ in range(0x30)))
    return rect


def fmv_rows_world(img, rng, count):
    img.u32(0x00561DDC, count)
    for k in range(count + 1):
        row = 0x0052CFA2 + 36 * k
        img.put(row, bytes(rng.randrange(256) for _ in range(34)))
        img.u16(row, rng.choice([0x60, 0x60, 0x20, 0x40, 0x61, 0, 0x1060]))


@movie(0x00454D30, "GameFmv_ChangeTrack")
def gen_change_track(env, n):
    img, rng = Image(), env.rng
    count = rng.choice([3, 4, 6, 8, 10])
    fmv_rows_world(img, rng, count)
    img.u32(0x00561DC8, rng.choice([0, 0, 0, 1]))
    img.u32(0x0052D4DC, rng.choice([0, 1, 2, count - 2, count - 1, count, rng.randrange(0, count + 1)]))
    delta = rng.choice([u(-1 - k) for k in range(count)] + [0, 1, 2, 5])
    return [delta], img.pokes()


@movie(0x00454E50, "GameFmv_Start")
def gen_gamefmv_start(env, n):
    img, rng = Image(), env.rng
    movie_list_world(img, env, rng)
    return [rng.getrandbits(32)], img.pokes()


@movie(0x00454E70, "GameFmv_Update")
def gen_gamefmv_update(env, n):
    img, rng = Image(), env.rng
    movie_world(img, env, rng)
    return [rng.getrandbits(32)], img.pokes()


@movie(0x004E7070, "PCMOVIE_GetMovieName")
def gen_movie_name(env, n):
    img, rng = Image(), env.rng
    movie_list_world(img, env, rng)
    return [rng.randrange(0, 8)], img.pokes()


@movie(0x004E7090, "PCMovie_PlayGameFMV")
def gen_play_game_fmv(env, n):
    img, rng = Image(), env.rng
    movie_list_world(img, env, rng)
    movie_world(img, env, rng)
    startup_state(rng)
    img.u32(0x006A6964, rng.choice([0, 0, 1]))
    return [], img.pokes()


@movie(0x004E70E0, "PCMOVIE_StartFMV")
def gen_start_fmv(env, n):
    img, rng = Image(), env.rng
    movie_list_world(img, env, rng)
    return [], img.pokes()


@movie(0x004E7140, "PCMOVIE_StopFMV")
def gen_stop_fmv(env, n):
    img, rng = Image(), env.rng
    movie_world(img, env, rng)
    return [], img.pokes()


@movie(0x004E71C0, "PCMOVIE_Update")
def gen_movie_update(env, n):
    img, rng = Image(), env.rng
    movie_world(img, env, rng)
    return [], img.pokes()


@movie(0x004E6E70, "PCMOVIE_Pause")
def gen_movie_pause(env, n):
    img, rng = Image(), env.rng
    movie_world(img, env, rng)
    return [rng.choice([0, 1, 1, 0, 2, 0x100, 0x101, 0x7F])], img.pokes()


@movie(0x004E7260, "PCMOVIE_StartMusic")
def gen_start_music(env, n):
    img, rng = Image(), env.rng
    movie_world(img, env, rng)
    name = env.slab + STRINGS
    img.put(name, c_string(rng.choice(["LTIX05.DAT", "Music\\ltix16.dat", "", "x"])))
    return [name], img.pokes()


@movie(0x004E72D0, "PCMOVIE_SetXAVolume")
def gen_set_xa_volume(env, n):
    img, rng = Image(), env.rng
    movie_world(img, env, rng)
    value = lambda: rng.choice([0, 1, 0x7F, 0xFF, 0x100, 0x1FFF, u(-1), u(-300), rng.randrange(0, 0x10000), 0x7FFFFFFF])
    return [value(), value()], img.pokes()


@movie(0x004E7310, "PCMOVIE_XAPlay")
def gen_xa_play(env, n):
    img, rng = Image(), env.rng
    movie_world(img, env, rng)
    return [rng.choice([0, 1, 2, 3, 12, u(-1), 15, 99, 100]), rng.choice([0, 1, 2, 7, 8, u(-3), 12])], img.pokes()


@movie(0x004E7370, "PCMOVIE_XAStop")
def gen_xa_stop(env, n):
    img, rng = Image(), env.rng
    movie_world(img, env, rng)
    return [], img.pokes()


@movie(0x004E73E0, "PCMOVIE_XADone")
def gen_xa_done(env, n):
    img, rng = Image(), env.rng
    movie_world(img, env, rng)
    return [], img.pokes()


@movie(0x004E6F00, "PCMOVIE_DrawWindow")
def gen_draw_window(env, n):
    img, rng = Image(), env.rng
    movie_world(img, env, rng)
    slab = env.slab
    img.u32(0x00560FD0, slab + POOL)
    img.put(slab + POOL, bytes(rng.randrange(256) for _ in range(0x180)))
    img.u32(0x00560FD4, slab + OT)
    img.u32(slab + OT + 0x84, slab + HEAD)
    img.u32(slab + HEAD, rng.choice([0, slab + POOL + 0x180, rng.getrandbits(32)]))
    img.u32(0x006A6BD8, rng.choice([0x3F800000, rng.getrandbits(32)]))
    img.u32(0x006A6BDC, rng.choice([0x3F800000, rng.getrandbits(32)]))
    return [rng.choice([rect_world(img, env, rng)] * 6 + [0])], img.pokes()


# the startup media loop: what the message pump, Bink and the pad answer on each pass (a pass counter forces an exit after a few)
STARTUP = {"passes": 0, "peek": 0}


def startup_state(rng):
    STARTUP["passes"] = 0
    STARTUP["peek"] = 0
    STARTUP["limit"] = rng.choice([1, 2, 3, 5, 8])


@movie(0x004E5EC0, "PCMovie_PlayStartupMedia")
def gen_play_startup_media(env, n):
    img, rng = Image(), env.rng
    movie_world(img, env, rng)
    startup_state(rng)
    img.u32(0x006A6964, rng.choice([0, 0, 1]))
    img.u32(0x006A6CE4, rng.choice([0x1234, 0x1234, 0]))
    name = env.slab + STRINGS
    img.put(name, c_string(rng.choice(MOVIE_FILES)))
    return [name], img.pokes()


# ---- stubs --------------------------------------------------------------------------------------------------------------------------------------------------------------------------

def reverb_blank():
    """The reverb command record is {command, value, packed}: SetReverb fills the first two dwords (the third is stack garbage), SetReverbDepth the first and third."""
    from opentony.memory import mem

    import gdb

    esp = int(gdb.parse_and_eval("$esp")) & 0xFFFFFFFF
    command = mem.u32(mem.u32(esp + 4))
    return [(8, 4)] if command == 1 else [(4, 4)]


def install_audio_stubs(fc, env):
    """The HAL's audio calls, the sound bank, the SPU, Bink and the engine calls the movie code reaches; the Win32 imports on their call sites."""
    if getattr(fc, "_audio_installed", False):
        return
    fc._audio_installed = True
    mark_under_test()
    import gdb
    from opentony.memory import mem

    # the launch helper tony-skip-movies keeps breakpoints on StartBinkMovie and PlayStartupMedia (they return at once): they would intercept the forced calls
    for point in gdb.breakpoints() or []:
        try:
            where = str(point.location or "")
        except gdb.error:
            continue
        if any(f"0x{a:x}" in where.lower() for a in (0x004E5EC0, 0x004E6590)):
            point.enabled = False

    rng = env.rng
    # the crowd's one-in-30 and one-in-50 draws (new targets, a boo) and the coin flips: zero more often than chance gives
    biased = {0x1E: 0.3, 0x32: 0.3, 2: 0.5}
    ensure(fc, 0x004C9340, 1, "rnd", result=lambda a: 0 if rng.random() < biased.get(a[0], 0) else env.rnd(a))
    voice = lambda a: rng.choice([0xFFFFFFFF, 0, 5, 0x10003, rng.getrandbits(32)])
    ensure(fc, 0x004F2E20, 4, "engine_004f2e20", result=voice)
    ensure(fc, 0x004F34B0, 3, "engine_004f34b0")
    ensure(fc, 0x004F3690, 2, "engine_004f3690")
    ensure(fc, 0x004F3720, 1, "engine_004f3720", result=lambda a: rng.choice([0, 0x5622, 0xAC44, rng.getrandbits(32)]))
    ensure(fc, 0x004F37C0, 1, "engine_004f37c0")
    ensure(fc, 0x004F3900, 0, "engine_004f3900")
    ensure(fc, 0x004F3940, 0, "engine_004f3940")
    ensure(fc, 0x004F2960, 1, "engine_004f2960")
    ensure(fc, 0x004F2AD0, 1, "engine_004f2ad0")
    ensure(fc, 0x004CB3F0, 3, "engine_004cb3f0")
    ensure(fc, 0x004CA690, 3, "engine_004ca690", result=lambda a: rng.getrandbits(32))
    ensure(fc, 0x004E8C30, 1, "engine_004e8c30", blob=(0, 12), blank=reverb_blank, stack_to=KSTACK, result=lambda a: rng.choice([0xFFFFFFFF, 0, 1, 0, 0x55]))
    ensure(fc, 0x004E8C20, 2, "engine_004e8c20")
    ensure(fc, 0x004E8C10, 1, "engine_004e8c10", result=lambda a: rng.choice([0, 1, 1, 0xFF00]))
    # Bink and the movie window (owned by the HAL's movie backend), the thread start, the engine's set-up call, the pad
    ensure(fc, 0x004E6590, 3, "engine_004e6590", text=True, stack_to=KSTACK, result=lambda a: rng.choice([0, 1, 1, 1, 0x100, 0x101]))
    ensure(fc, 0x004E6020, 0, "engine_004e6020", result=lambda a: rng.choice([0, 1, 1, 0x1234]))
    ensure(fc, 0x004E6DA0, 0, "engine_004e6da0")
    ensure(fc, 0x004E6E50, 0, "engine_004e6e50")
    ensure(fc, 0x004E6890, 0, "engine_004e6890", result=lambda a: 0 if STARTUP["passes"] >= STARTUP.get("limit", 3) else rng.choice([0, 1, 1, 1, 7]))
    ensure(fc, 0x004E5720, 0, "engine_004e5720")
    ensure(fc, 0x004E5A80, 0, "engine_004e5a80")
    ensure(fc, 0x004F7CE0, 0, "engine_004f7ce0")
    ensure(fc, 0x004D0EC0, 0, "engine_004d0ec0")
    ensure(fc, 0x00501E57, 3, "engine_00501e57", result=lambda a: rng.getrandbits(32))
    ensure(fc, 0x00501439, 1, "engine_00501439")
    ensure(fc, 0x004CFB60, 3, "engine_004cfb60")
    ensure(fc, 0x004D0F80, 1, "engine_004d0f80")
    ensure(fc, 0x00489A10, 0, "engine_00489a10")
    ensure(fc, 0x004E41D0, 1, "engine_004e41d0", result=lambda a: rng.choice([0, 0, 3, 0x1F]))
    ensure(fc, 0x004E4260, 0, "engine_004e4260", result=lambda a: rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 0, 3]))
    ensure(fc, 0x004E4290, 0, "engine_004e4290")
    ensure(fc, FAKE_SURFACE_METHOD, 3, f"engine_{FAKE_SURFACE_METHOD:08x}", convention="stdcall", result=lambda a: 0)          # (an error ends in exit(), which never returns: its argument is never cleaned, so a stub that returns would unbalance the stack; that branch is not forced-called)
    # the imports, on the call sites
    sleep_value = lambda a: rng.choice([0, 0, 1])
    fc.stub_site(0x004E6E88, 6, 2, "win32_BinkPause", convention="stdcall")
    for address in (0x004E6EBB, 0x004E7164, 0x004E7394):
        fc.stub_site(address, 2, 1, "win32_Sleep", convention="stdcall", writes=[(0x006A6CE0, 1, sleep_value)])
    fc.stub_site(0x004E72F5, 6, 2, "win32_BinkSetVolume", convention="stdcall")
    fc.stub_site(0x004E7306, 6, 2, "win32_BinkSetPan", convention="stdcall")

    def peek_message(values):
        STARTUP["passes"] += 1
        finish = STARTUP["passes"] > STARTUP.get("limit", 3)
        STARTUP["peek"] = 1 if finish or rng.random() < 0.35 else 0
        message = rng.choice([0x12, 0x100]) if finish else rng.choice([0x113, 0x12, 0x100, 0x200, 0x0F, 5])
        return struct.pack("<II", rng.getrandbits(32), message)

    fc.stub_site(0x004E5F30, 2, 5, "win32_PeekMessageA", convention="stdcall", stack_to=KSTACK, pokes=[(0, None, peek_message)], result=lambda a: STARTUP["peek"])
    fc.stub_site(0x004E5F4D, 2, 1, "win32_TranslateMessage", convention="stdcall", stack_to=KSTACK)
    fc.stub_site(0x004E5F54, 2, 1, "win32_DispatchMessageA", convention="stdcall", stack_to=KSTACK)
    fc.stub_site(0x004E5F5F, 6, 1, "win32_BinkService", convention="stdcall")
    fc.stub_site(0x004E5F6C, 6, 1, "win32_BinkWait", convention="stdcall", result=lambda a: 0 if STARTUP["passes"] >= STARTUP.get("limit", 3) else rng.choice([0, 1, 1, 0xFF]))


GROUP_STUBS = {group: install_audio_stubs for group in ("crowd", "sfx", "vab", "redbook", "movie")}
