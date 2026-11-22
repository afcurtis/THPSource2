"""Phase 4.13a probe: pad.cpp and the small functions of pc\\pcinput.cpp (groups padc, pcin).

Unlike the screen groups these work on the game's own globals (the player blocks at 0x0056aff8, the PC layer's bindings and keyboard state), not on a slab: the slab the probe
allocates is only the scratch the shared machinery needs. Every region a function touches is captured as a window of the real addresses and poked per case; the outside
services (the PSX pad library, the DirectInput wrapper's chord test, GetCursorPos, the HAL's rumble, the net layer's tail targets) are stubs with scripted answers.
"""

from __future__ import annotations

import os
import struct

from create_specs import EXTRA_REGIONS, READ_ONLY, SEEDS, SLAB_BYTES, spec
from frontend_specs_front import UNDER_TEST, ensure
from frontend_specs_menu import Image

BLOCK = 0x0056AFF8
STRIDE = 0x16C
CHORDS = (0x0054AD94, 0x0054AD20, 0x0054AD5C)          # the three chord sets (mode 0, 1, 2)

# the windows of the real address space the functions touch (mutable), and the tables they only read
WINDOWS = [
    (0x0056AF60, 0x4F0),      # the player blocks, the pad registers, the thresholds, the idle count and the initialised flag
    (0x0056A880, 0x40),       # the player port (0x0056a888), the player count (0x0056a88c), the motor limit (0x0056a89c)
    (0x00533F30, 0x20),       # the mode selector and the game mode
    (0x0054ACE0, 0x1C0),      # joystick enable, pad types, default chords, the three chord sets, the binding pointers, the joystick buttons, the config table
    (0x006A3D20, 0x10),       # the port select (0x006a3d28)
    (0x006A3F10, 0x20),       # the pending masks (0x006a3f1c)
    (0x006A42E0, 0x220),      # the joystick wrapper (0x006a42e8), the keyboard wrapper (0x006a43e0, key bytes from 0x006a43e4) and the mouse position and button (0x006a44f0, 0x006a44f8)
    (0x006A6960, 0x40),       # the control mode, the pending word and count, the pause latch
    (0x006A1400, 0x10),       # the mouse flag
    (0x00561C00, 0x10),       # the touched flag
]
TABLES = [(0x00535F20, 0x30), (0x0052EFF0, 0x500)]       # the pad thresholds; the key name pointers and the key character table

for _group in ("padc", "pcin"):
    SLAB_BYTES[_group] = 0x1000
    EXTRA_REGIONS[_group] = lambda slab: [(slab, 0x1000)] + WINDOWS
    READ_ONLY[_group] = TABLES
    SEEDS[_group] = lambda env, rng: {}


def padc(address, name):
    return spec(address, name, "padc")


def pcin(address, name):
    return spec(address, name, "pcin")


def u(rng):
    return rng.getrandbits(32)


def counter(rng):
    return rng.choice([0, 1, 2, rng.randrange(0, 200), rng.randrange(0, 70000), 0xFFFFFFFF, u(rng)])


def pad_world(img, rng, *, pointers=True):
    """Both players' action records and the globals the pad code reads: legal random states."""
    for player in range(2):
        base = BLOCK + STRIDE * player
        for index in range(20):
            record = base + 16 * index
            img.u8(record, rng.choice([0, 1, 1, 0, 0xFF]))
            img.u8(record + 1, rng.choice([0, 1]))
            img.u32(record + 4, counter(rng))
            img.u32(record + 8, counter(rng))
            img.u32(record + 12, counter(rng))
        if pointers:
            standard = [base + 0x40, base + 0x50, base + 0x60, base + 0x70]
            if rng.random() < 0.65:
                table = standard
            else:
                table = [base + 16 * rng.randrange(0, 20) for _ in range(4)]
            for k, pointer in enumerate(table):
                img.u32(base + 0x15C + 4 * k, pointer)
    img.u32(0x0056B2E4, rng.choice([0, 1, 100, u(rng)]))
    img.u32(0x0056B2E8, rng.choice([0, 1]))
    img.u32(0x0056A88C, rng.choice([0, 0, 1, 2]))
    img.u32(0x0056A89C, rng.choice([0, 0, 0, 1]))
    img.u32(0x00533F3C, rng.choice([0, 1, 1, 2]))
    img.u32(0x00533F38, rng.choice([0, 7, 7, 3]))
    img.u32(0x0056A888, rng.choice([0, 1]))


def player_arg(rng):
    value = rng.choice([0, 1, 0, 1, 0, 1])
    return value | (rng.getrandbits(24) << 8 if rng.random() < 0.15 else 0)


def record_address(rng):
    return BLOCK + STRIDE * rng.randrange(0, 2) + 16 * rng.randrange(0, 20)


# ---- pad.cpp -------------------------------------------------------------------------------------------------------------------------------------------------------------

@padc(0x004894A0, "Pad_InitAtStart")
def gen_init(env, n):
    img, rng = Image(), env.rng
    pad_world(img, rng)
    return [], img.pokes()


@padc(0x004895C0, "Pad_ClearAllOne")
def gen_clear_one(env, n):
    img, rng = Image(), env.rng
    pad_world(img, rng)
    return [rng.randrange(0, 2)], img.pokes()


@padc(0x00489610, "Pad_ClearAll")
def gen_clear_all(env, n):
    img, rng = Image(), env.rng
    pad_world(img, rng)
    return [], img.pokes()


@padc(0x00489630, "Pad_Clear")
def gen_clear(env, n):
    img, rng = Image(), env.rng
    pad_world(img, rng)
    return [BLOCK + STRIDE * rng.randrange(0, 2)], img.pokes()


@padc(0x00489670, "Pad_ClearTriggers")
def gen_clear_triggers(env, n):
    img, rng = Image(), env.rng
    pad_world(img, rng)
    return [BLOCK + STRIDE * rng.randrange(0, 2)], img.pokes()


@padc(0x00489690, "Pad_Nothing")
def gen_nothing(env, n):
    return [], {}


@padc(0x004896A0, "Pad_ActuatorOn")
def gen_actuator_on(env, n):
    img, rng = Image(), env.rng
    pad_world(img, rng)
    for player in range(2):
        for k in range(0x16C):
            if rng.random() < 0.02:
                img.u8(0x0056B14C + STRIDE * player + k, rng.getrandbits(8))
    strength = rng.choice([0, 1, 255, 256, 0x1234, rng.getrandbits(16), u(rng)])
    duration = rng.choice([0, 1, 10, 255, rng.getrandbits(8), u(rng)])
    return [player_arg(rng), strength, rng.choice([0, 1]) | (rng.getrandbits(24) << 8 if rng.random() < 0.1 else 0), duration], img.pokes()


@padc(0x00489850, "Pad_ActuatorOff")
def gen_actuator_off(env, n):
    img, rng = Image(), env.rng
    pad_world(img, rng)
    for player in range(2):
        for k in range(0x16C):
            if rng.random() < 0.02:
                img.u8(0x0056B14C + STRIDE * player + k, rng.getrandbits(8))
    return [player_arg(rng), rng.choice([0, 1]) | (rng.getrandbits(24) << 8 if rng.random() < 0.1 else 0)], img.pokes()


@padc(0x00489930, "Input_AdvanceActionState")
def gen_advance(env, n):
    img, rng = Image(), env.rng
    pad_world(img, rng)
    pressed = rng.choice([0, 0, 1, 0x10, 0x80, 0x8000, u(rng), rng.randrange(0, 0x100)])
    return [record_address(rng), pressed], img.pokes()


@padc(0x00489990, "Pad_Analog")
def gen_analog(env, n):
    rng = env.rng
    img = Image()
    pad_world(img, rng)
    while True:
        low = rng.choice([0, 0, 5, rng.randrange(0, 100), rng.randrange(-20, 60), 0x20])
        high = rng.choice([255, 255, 250, rng.randrange(180, 300)])
        mid = rng.choice([128, 128, 127, rng.randrange(40, 220)])
        value = rng.choice([rng.randrange(-20, 300), rng.randrange(0, 256), 0, 255, 128, low - 1, low, high, high + 1, mid, mid - 0x20, mid + 0x20, mid - 0x21, mid + 0x21,
                            rng.randrange(-100000, 100000)])
        if value < low or value > high:
            break
        if value < mid - 0x20 and mid - low - 0x20 == 0:
            continue
        if value > mid + 0x20 and high - mid - 0x20 == 0:
            continue
        break
    return [value & 0xFFFFFFFF, low & 0xFFFFFFFF, high & 0xFFFFFFFF, mid & 0xFFFFFFFF], img.pokes()


def analog_world(img, rng):
    """The sample bytes of both pads, the lower thresholds and the pad types the PC layer reports."""
    for player in range(2):
        img.u8(0x0056AFB9 + 0x20 * player, rng.choice([ord("A"), ord("s"), ord("s"), ord("S"), 0, rng.getrandbits(8)]))
        for k in range(4):
            img.u8(0x0056AFBC + 0x20 * player + k, rng.choice([128, 128, 0, 255, rng.getrandbits(8), rng.randrange(90, 170), rng.randrange(0, 40), rng.randrange(215, 256)]))
    for address in (0x0056B2D0, 0x0056B2D4, 0x0056B2D8, 0x0056B2DC):
        img.u32(address, rng.choice([0, 0, 5, rng.randrange(0, 0x5F), 0x5E]))
    types = [rng.choice([0x73, 0x73, 0x53, 0x41, 0, rng.getrandbits(8)]) for _ in range(2)]
    img.u32(0x0054ACE4, types[0] | (types[1] << 8) | (rng.getrandbits(16) << 16 if rng.random() < 0.3 else 0))
    img.u8(0x0056B140, rng.getrandbits(8))
    img.u8(0x0056B141, rng.getrandbits(8))
    img.u16(0x006A3F1C, rng.choice([0, rng.getrandbits(16), 0x8000, 0x2000, 0x1000, 0x4000, 0x00FF]))
    img.u16(0x006A3F1E, rng.choice([0, rng.getrandbits(16), 0x8000, 0x2000, 0x1000, 0x4000, 0x00FF]))


@padc(0x00489A10, "Input_UpdateActionStates")
def gen_update(env, n):
    img, rng = Image(), env.rng
    pad_world(img, rng)
    analog_world(img, rng)
    return [], img.pokes()


@padc(0x00489CD0, "Pad_Restore")
def gen_restore(env, n):
    img, rng = Image(), env.rng
    pad_world(img, rng)
    return [], img.pokes()


@padc(0x00489E70, "Pad_Remap")
def gen_remap(env, n):
    img, rng = Image(), env.rng
    pad_world(img, rng)
    return [], img.pokes()


# ---- pc\pcinput.cpp ------------------------------------------------------------------------------------------------------------------------------------------------------

def binding_world(img, rng):
    """The default chords, the three chord sets, the binding pointers and the joystick buttons."""
    for k in range(14):
        img.u32(0x0054ACE8 + 4 * k, rng.choice([0, rng.getrandbits(16), rng.getrandbits(32)]))
        img.u32(0x0054AD94 + 4 * k, rng.getrandbits(32))
    for k in range(14):
        sets = rng.choice(CHORDS)
        img.u32(0x0054ADCC + 4 * k, sets + 4 * rng.randrange(0, 14) if rng.random() < 0.8 else rng.getrandbits(32))
    for address in (0x0054AE04, 0x0054AE08, 0x0054AE0C, 0x006A6984, 0x0054AE10, 0x0054AE14, 0x0054AE18, 0x0054AE1C, 0x0054AE20, 0x0054AE24):
        img.u32(address, rng.choice([0xFFFFFFFF, 0, 1, 2, 7, 31, rng.randrange(0, 32)]))
    img.u32(0x006A6964, rng.choice([0, 0, 1, 2, 3]))


def keyboard_world(img, rng, density=0.1):
    for k in range(256):
        img.u8(0x006A43E4 + k, 0x80 if rng.random() < density else rng.choice([0, 0, 0x01, 0x7F]))


@pcin(0x004E3E80, "PCInput_ResetBindings")
def gen_reset_bindings(env, n):
    img, rng = Image(), env.rng
    binding_world(img, rng)
    return [], img.pokes()


@pcin(0x004E3F80, "PCInput_SetControlMode")
def gen_set_mode(env, n):
    img, rng = Image(), env.rng
    binding_world(img, rng)
    return [rng.choice([0, 1, 2, 3, 0xFFFFFFFF, rng.getrandbits(32)])], img.pokes()


@pcin(0x004E4190, "PCInput_SetPendingButtons")
def gen_pending(env, n):
    img, rng = Image(), env.rng
    img.u32(0x006A6970, rng.choice([0, 0, 1, 5, 0xFFFFFFFF, rng.getrandbits(32)]))
    img.u16(0x006A696C, rng.getrandbits(16))
    return [rng.choice([0, 0x10, 0x8000, rng.getrandbits(16), u(rng)])], img.pokes()


@pcin(0x004E41B0, "PCInput_KeyDown")
def gen_key_down(env, n):
    img, rng = Image(), env.rng
    keyboard_world(img, rng, rng.choice([0.05, 0.5]))
    return [rng.choice([rng.randrange(0, 256), rng.randrange(0, 256), 0x1C, 0xFF, 0x100 + rng.randrange(0, 256)])], img.pokes()


@pcin(0x004E41D0, "PCInput_FirstKeyDown")
def gen_first_key(env, n):
    img, rng = Image(), env.rng
    keyboard_world(img, rng, rng.choice([0, 0.01, 0.05, 0.3]))
    return [rng.choice([0, 0, 1, rng.randrange(0, 0xEE), 0xED, 0xEE, 0xEF, 0xFF, 0xFFFFFFFF, 0x100, rng.randrange(0, 300)])], img.pokes()


@pcin(0x004E4230, "PCInput_KeyChar")
def gen_key_char(env, n):
    return [env.rng.randrange(0, 0xEE)], {}


@pcin(0x004E4250, "PCInput_KeyName")
def gen_key_name(env, n):
    return [env.rng.randrange(0, 0x100)], {}


@pcin(0x004E4260, "PCInput_FirstJoystickButton")
def gen_first_joystick(env, n):
    img, rng = Image(), env.rng
    density = rng.choice([0, 0.02, 0.1, 0.5])
    for k in range(32):
        img.u32(0x006A4354 + 4 * k, 1 if rng.random() < density else 0)
    return [], img.pokes()


@pcin(0x004E4290, "Input_ClearPadTriggers")
def gen_clear_pad_triggers(env, n):
    img, rng = Image(), env.rng
    pad_world(img, rng)
    return [], img.pokes()


@pcin(0x004E42B0, "PCInput_EnableJoystickButtons")
def gen_enable_joystick(env, n):
    return [env.rng.choice([0, 1, 2, u(env.rng)])], {}


@pcin(0x004E4650, "PCInput_GetActionMask")
def gen_get_mask(env, n):
    img, rng = Image(), env.rng
    analog_world(img, rng)
    return [], img.pokes()


@pcin(0x004E4BE0, "PCInput_Nothing")
def gen_nothing_a(env, n):
    return [], {}


@pcin(0x004E4CE0, "PCInput_NothingMotor")
def gen_nothing_b(env, n):
    return [env.rng.choice([0, 1]), env.rng.choice([0, 1])], {}


@pcin(0x004E4BF0, "PCInput_MouseButtonMessage")
def gen_mouse_message(env, n):
    img, rng = Image(), env.rng
    img.u8(0x006A44F8, rng.choice([0, 1, 0x7F]))
    return [rng.choice([0x201, 0x202, 0x203, 0x200, 0, rng.getrandbits(32)])], img.pokes()


@pcin(0x004E42C0, "PCInput_BuildActionMask")
def gen_build_mask(env, n):
    img, rng = Image(), env.rng
    binding_world(img, rng)
    for k in range(32):
        img.u32(0x006A4354 + 4 * k, rng.choice([0, 0, 1, 0x80]))
    img.u8(0x006A43D4, rng.choice([0, 1, 1]))
    for address in (0x006A42EC, 0x006A42F0):
        img.u32(address, rng.choice([0, 0, 500, 501, -500 & 0xFFFFFFFF, -501 & 0xFFFFFFFF, 1000, -1000 & 0xFFFFFFFF, rng.randrange(-1000, 1001) & 0xFFFFFFFF]))
    img.u32(0x0054ACE0, rng.choice([0, 1, 1]))
    keyboard_world(img, rng, 0.1)
    img.u8(0x006A1400, rng.choice([0, 1, 1]))
    img.u8(0x006A44F8, rng.choice([0, 1, 2]))
    img.u32(0x006A6970, rng.choice([0, 0, 1, 3, 0xFFFFFFFF]))
    img.u32(0x006A696C, rng.choice([0, 0, rng.getrandbits(16), u(rng)]))
    img.u8(0x006A6988, rng.choice([0, 1]))
    img.u32(0x00561C04, rng.choice([0, 1, 5]))
    img.u32(0x006A3D28, rng.choice([0, 1]))
    img.u32(0x0056A888, rng.choice([0, 1]))
    img.u16(0x006A3F1C, rng.getrandbits(16))
    img.u16(0x006A3F1E, rng.getrandbits(16))
    return [], img.pokes()


# ---- the options file: LoadBindings, SaveBindings ----------------------------------------------------------------------------------------------------------------------------

SECTION_JOYPAD = 0x0054B08C
PAIRS = 0x0054AE28
SETTINGS: list = []          # the answers the options file stub gives in the case being run, in order (the joystick entries first, then one per keyboard entry)


class BindingTables:
    """The read-only tables plus the strings the key name table names (found when the capture starts, in the game)."""

    def __iter__(self):
        from opentony.memory import mem

        out = list(TABLES) + [(0x0054B070, 0x40), (0x00525700, 0x80)]
        for code in range(256):
            pointer = mem.u32(0x0052EFF0 + 4 * code)
            out.append((pointer, 0x40))
        return iter(out)


def bind(address, name):
    return spec(address, name, "pcbind")


SLAB_BYTES["pcbind"] = 0x1000
EXTRA_REGIONS["pcbind"] = lambda slab: [(slab, 0x1000)] + WINDOWS
READ_ONLY["pcbind"] = BindingTables()
SEEDS["pcbind"] = lambda env, rng: {}


def key_names():
    from opentony.memory import mem

    return [mem.cstring(mem.u32(0x0052EFF0 + 4 * code), 0x40) for code in range(256)]


def named_codes():
    """The key codes whose name finds themselves (the loader maps a name to the first code that has it), apart from code 0, 'none'."""
    names = key_names()
    return [code for code in range(1, 256) if names[code] and names.index(names[code]) == code and "," not in names[code] and names[code] != names[0]]


def chord_world(img, rng, codes):
    """Fourteen chords with distinct codes (bytes 0..2 a key or none, byte 3 zero), written where the entries of the keyboard table point; returns the chord addresses."""
    from opentony.memory import mem

    chords = [mem.u32(PAIRS + 8 * k + 4) for k in range(14)]
    pool = rng.sample(codes, 14 * 3)
    for k, address in enumerate(chords):
        count = rng.choice([1, 1, 2, 3])
        keys = pool[3 * k:3 * k + count] + [0] * (3 - count)
        img.put(address, bytes(keys) + b"\0")
    return chords


def joystick_numbers(rng, scenario):
    numbers = rng.sample(range(0, 32), 10)
    for k in range(10):
        if rng.random() < 0.2:
            numbers[k] = -1
    if scenario == "dup":
        a, b = rng.sample(range(10), 2)
        numbers[b] = numbers[a] if numbers[a] != -1 else 5
        numbers[a] = numbers[b]
    elif scenario == "range":
        numbers[rng.randrange(10)] = rng.choice([33, 0x21, 100, -2, -100, 0x7FFFFFFF])
    return numbers


def number_text(rng, value):
    return rng.choice([str(value), str(value), " %d" % value, "%+d" % value if value >= 0 else str(value), str(value) + "x", "%d" % value])


def keyboard_text(rng, names, codes, scenario, taken):
    count = rng.choice([1, 1, 2, 3, 3])
    picks = [rng.choice(codes) for _ in range(count)]
    texts = [names[c] for c in picks]
    if scenario == "case":
        texts = [t.upper() if rng.random() < 0.5 else t.lower() for t in texts]
    elif scenario == "unknown":
        texts[rng.randrange(len(texts))] = rng.choice(["Bogus", "Q9Z", "x" * 12])
    elif scenario == "conflict" and taken:
        texts[rng.randrange(len(texts))] = names[rng.choice(taken)]
    elif scenario == "none":
        texts[rng.randrange(len(texts))] = names[0]
    return ",".join(texts)


@bind(0x004E4D10, "PCInput_LoadBindings")
def gen_load_bindings(env, n):
    img, rng = Image(), env.rng
    names = key_names()
    codes = named_codes()
    half = len(codes) // 2
    current, fresh = codes[:half], codes[half:]         # the chords in memory come from one half, the file's from the other (so a clean file never meets its own keys)
    chords = chord_world(img, rng, current)
    img.u32(0x006A6964, rng.choice([0, 1, 2]))
    for k in range(14):
        img.u32(0x0054ACE8 + 4 * k, rng.getrandbits(32) & 0x00FFFFFF)         # (the defaults a reset copies; byte 3 stays zero)
    joy_scenario = rng.choice(["ok", "ok", "ok", "ok", "dup", "range"])
    numbers = joystick_numbers(rng, joy_scenario)
    variables = (0x0054AE04, 0x0054AE08, 0x0054AE0C, 0x006A6984, 0x0054AE10, 0x0054AE18, 0x0054AE14, 0x0054AE1C, 0x0054AE20, 0x0054AE24)
    for variable in variables:
        img.u32(variable, rng.choice([0xFFFFFFFF, 1, 2, 3, rng.randrange(0, 32)]))
    SETTINGS.clear()
    SETTINGS.extend(number_text(rng, v) for v in numbers)
    taken = [img.b[chords[k] + j] for k in range(14) for j in range(3) if img.b.get(chords[k] + j, 0)]
    scenario = rng.choice(["clean", "clean", "clean", "case", "conflict", "unknown", "none"])
    for k in range(14):
        if scenario == "clean" and rng.random() < 0.08:
            SETTINGS.append(keyboard_text(rng, names, fresh, "none", []))
        else:
            SETTINGS.append(keyboard_text(rng, names, fresh, scenario if rng.random() < 0.5 else "clean", taken))
    return [], img.pokes()


@bind(0x004E5460, "PCInput_SaveBindings")
def gen_save_bindings(env, n):
    img, rng = Image(), env.rng
    codes = named_codes()
    chord_world(img, rng, codes)
    for variable in (0x0054AE04, 0x0054AE08, 0x0054AE0C, 0x006A6984, 0x0054AE10, 0x0054AE18, 0x0054AE14, 0x0054AE1C, 0x0054AE20, 0x0054AE24):
        img.u32(variable, rng.choice([0xFFFFFFFF, 0, 1, 31, rng.randrange(0, 32), rng.randrange(-5, 40) & 0xFFFFFFFF, rng.getrandbits(32)]))
    return [], img.pokes()


def setting_answer(a):
    """The options file's answer: the next scripted text, as many bytes as the caller's buffer holds (the joystick entries read into 16-byte locals, the others into 256)."""
    size = 16 if a[0] == SECTION_JOYPAD else 256
    text = SETTINGS.pop(0) if SETTINGS else ""
    return text.encode("latin-1")[:size - 1].ljust(size, b"\0")


# ---- stubs ---------------------------------------------------------------------------------------------------------------------------------------------------------------

def stop_clock(fc):
    """The simulation clock is moved by a timer thread: while a case runs its callback does nothing (the words it writes are not part of any function under test)."""
    ensure(fc, 0x004DACE0, 5, "timer_callback", convention="stdcall", when=lambda a: False)


def mark_under_test():
    """The functions of the selected groups: a stub on one of them would intercept the forced call itself."""
    from create_specs import SPECS

    selected = [g for g in os.environ.get("P410_GROUPS", "").split(",") if g]
    UNDER_TEST.clear()
    UNDER_TEST.update(s.address for s in SPECS if s.group in selected)


def install_pad_stubs(fc, env):
    """The services of pad.cpp: the demo timer, the PSX pad library (state, info, request), the HAL's rumble and the DirectInput poll."""
    if getattr(fc, "_platform_installed", False):
        return
    fc._platform_installed = True
    mark_under_test()
    stop_clock(fc)
    rng = env.rng
    ensure(fc, 0x00466B60, 0, "engine_00466b60")
    ensure(fc, 0x004E8AB0, 2, "engine_004e8ab0")
    ensure(fc, 0x004E8AD0, 0, "engine_004e8ad0")
    ensure(fc, 0x004E4C40, 4, "engine_004e4c40")
    ensure(fc, 0x004E8B20, 1, "engine_004e8b20", result=lambda a: rng.choice([0, 1, 2, 2, 6, 6, 3, 4]))
    ensure(fc, 0x004E8B10, 3, "engine_004e8b10")
    ensure(fc, 0x004E8B00, 2, "engine_004e8b00", result=lambda a: rng.choice([0, 1]))
    ensure(fc, 0x004E4CE0, 2, "engine_004e4ce0")
    ensure(fc, 0x00489CC0, 0, "engine_00489cc0")
    ensure(fc, 0x004E4CF0, 0, "engine_004e4cf0")
    ensure(fc, 0x004DBD90, 0, "engine_004dbd90")
    ensure(fc, 0x004DBE60, 0, "engine_004dbe60")


def install_pcin_stubs(fc, env):
    """The DirectInput wrapper's chord test, GetCursorPos and the mouse tracker, on top of the pad services."""
    install_pad_stubs(fc, env)
    if getattr(fc, "_pcin_installed", False):
        return
    fc._pcin_installed = True
    from opentony.memory import mem

    import gdb

    # the launch helpers (tony-key-loop, tony-input-sample) keep breakpoints of their own on the poll code: the one on BuildActionMask rewrites the keyboard state (it clears the Enter
    # key) when the forced call enters the function, which made 3 cases of 16 write a byte the native port never would
    for point in gdb.breakpoints() or []:
        try:
            where = str(point.location or "")
        except gdb.error:
            continue
        if any(f"0x{a:x}" in where.lower() for a in (0x004E42C0, 0x004E4A90, 0x004E41B0)):
            point.enabled = False

    rng = env.rng

    ensure(fc, 0x004EC6B0, 1, "engine_004ec6b0", convention="thiscall", this=True, result=lambda a: rng.choice([0, 0, 0, 1]))
    ensure(fc, 0x004DBEE0, 3, "engine_004dbee0", masks={2: 0xFF})
    cursor = mem.u32(0x0051826C)           # the import slot of user32!GetCursorPos: the stub goes on the function itself
    fc.stub(cursor, 1, "win32_GetCursorPos", convention="stdcall", keep=slice(0, 0),
            fill=(0, lambda a: struct.pack("<ii", rng.choice([0, 320, 639, rng.randrange(0, 1024), -5]), rng.choice([0, 240, 479, rng.randrange(0, 768), -5]))))


def install_bind_stubs(fc, env):
    """The options file: a load answers with the case's next scripted text, a save reports what it was given (the formatted locals are recorded at fixed scratch addresses)."""
    if getattr(fc, "_bind_installed", False):
        return
    fc._bind_installed = True
    mark_under_test()
    stop_clock(fc)
    ensure(fc, 0x004F6CA0, 4, "engine_004f6ca0", text=2, stack_to={2: 0x7FF00800, 3: 0x7FF00900}, fill=(3, setting_answer))
    ensure(fc, 0x004F6CD0, 3, "engine_004f6cd0", text=2, stack_to={2: 0x7FF00800})


GROUP_STUBS = {"padc": install_pad_stubs, "pcin": install_pcin_stubs, "pcbind": install_bind_stubs}
