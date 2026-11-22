"""Phase 4.14b probe, the card manager (group mmu): the case generators for the record queries, the file names, the directory scan, the card state, the header and buffer registration, and the
save and load of a file. See mmu_specs.py for the world, the slab layout and the stubs."""

from __future__ import annotations

from create_specs import Skip
from frontend_specs_menu import Image
from mmu_specs import (BUF, BUFCOUNT, BUFS, BUFSIZES, COUNT, FREE, G_PARK_BYTES, MGR, NAME_STRIDE, NAMES, PREFIX, REC, REC_STRIDE, SCRIPT, STATE, TEXT, TYPES, VT, fresh, manager_world, mmu, pick_index,
                       put_text, rec_at, u, user_name)


def mgr_arg(env):
    return env.slab + MGR


# ---- the manager's small functions ------------------------------------------------------------------------------------------------------------------------------------------------------------

@mmu(0x00473D20, "Mmu_ValidateSaveActionType")
def gen_validate(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    return [u(rng.choice([1, 2, 3, 1, 2, 3, 0, 4, -1, 5])), u(rng.choice([0, 1, 1, 2, -1]))], img.pokes()


@mmu(0x00473E50, "Mmu_ConstructCardState", this=MGR)
def gen_construct(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    SCRIPT["state"] = [(rng.choice([0, 1, 2, 3]), rng.choice([0, 1, 2, 3, 4]), 1)]
    return [], img.pokes()


@mmu(0x00473EE0, "Mmu_ScalarDelete", this=MGR)
def gen_scalar_delete(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    return [u(rng.choice([0, 1, 2, 3, rng.getrandbits(32)]))], img.pokes()


@mmu(0x00473F10, "Mmu_ResetVtable", this=MGR)
def gen_reset_vtable(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    return [], img.pokes()


@mmu(0x00473F30, "Mmu_NewestFile", this=MGR)
def gen_newest(env, n):
    img, rng = Image(), env.rng
    mgr = manager_world(img, env, rng)
    count = rng.choice([0, 1, 2, 3, 6, 15])
    img.u32(mgr + COUNT, count)
    SCRIPT["stat"] = [(rng.choice([0, 1, -1, rng.randrange(0, 2 ** 31), rng.randrange(0, 1000), 0x7FFFFFFF]), rng.choice([0, 0, 0, 0, -1, 1])) for _ in range(count)]
    return [], img.pokes()


@mmu(0x00474010, "Mmu_FileCode", this=MGR)
def gen_file_code(env, n):
    img, rng = Image(), env.rng
    mgr = manager_world(img, env, rng, count=rng.choice([1, 2, 5, 15]))
    index = rng.randrange(0, rng.choice([1, 2, 5, 15]))
    # a counter character: digits, A-V, and others
    name = (rng.choice(list(PREFIX.values())) + user_name(rng) + ".SAV").ljust(20, "x")
    chars = list(name)
    chars[11] = rng.choice(list("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefgvw _-~")) if rng.random() < 0.9 else name[11]
    img.put(mgr + REC + REC_STRIDE * index, ("".join(chars) + "\0").encode("latin-1"))
    return [index], img.pokes()


@mmu(0x00474100, "Mmu_EncodeCode", this=MGR)
def gen_encode(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    buffer = put_text(img, env, 0, "????????")
    return [buffer, u(rng.choice([0, 1, 9, 10, 31, 32, 0x3FF, 0x400, 0x7FFFFFFF, -1, -32, rng.getrandbits(32), rng.randrange(0, 0x400)]))], img.pokes()


@mmu(0x004741D0, "Mmu_FileCount", this=MGR)
def gen_file_count(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    return [], img.pokes()


@mmu(0x004741F0, "Mmu_CountOfType", this=MGR)
def gen_count_of_type(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    return [u(rng.choice([1, 2, 3, 4, 0, 5, -1]))], img.pokes()


@mmu(0x00474250, "Mmu_FileRecord", this=MGR)
def gen_file_record(env, n):
    img, rng = Image(), env.rng
    mgr = manager_world(img, env, rng)
    return [u(pick_index(rng, rng.choice([0, 3, 15])))], img.pokes()


@mmu(0x004742E0, "Mmu_DisplayName", this=MGR)
def gen_display_name(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    return [u(pick_index(rng, rng.choice([0, 3, 15])))], img.pokes()


def build_name_arg(env, img, rng, mgr):
    """The argument list [name]: a display name of the manager, a variation, or a fresh name."""
    kind = rng.choice(["same", "same", "case", "longer", "new", "empty"])
    # the display names were written by manager_world: re-derive them from the image bytes
    names = []
    for i in range(15):
        base = mgr + NAMES + NAME_STRIDE * i
        data = bytes(img.b.get(base + k, 0) for k in range(NAME_STRIDE))
        if data[0]:
            names.append(data.split(b"\0")[0].decode("latin-1"))
    name = names[rng.randrange(len(names))] if names else user_name(rng)
    if kind == "case":
        name = name.swapcase()
    elif kind == "longer":
        name += "x"
    elif kind == "new":
        name = user_name(rng) + "~"
    elif kind == "empty":
        name = ""
    return [put_text(img, env, 0, name, 0x40)]


@mmu(0x00474380, "Mmu_NameTaken", this=MGR)
def gen_name_taken(env, n):
    img, rng = Image(), env.rng
    mgr = manager_world(img, env, rng, count=rng.choice([0, 1, 3, 6, 15]))
    SCRIPT["park"] = [rng.choice([0, 0, 1, 0x100])]
    return build_name_arg(env, img, rng, mgr), img.pokes()


@mmu(0x004744A0, "Mmu_NameTakenByOther", this=MGR)
def gen_name_taken_other(env, n):
    img, rng = Image(), env.rng
    mgr = manager_world(img, env, rng, count=rng.choice([0, 1, 3, 6, 15]))
    SCRIPT["park"] = [rng.choice([0, 0, 1, 0x100])]
    args = build_name_arg(env, img, rng, mgr)
    return [*args, u(rng.choice([-1, 0, 1, 2, 5, 14, 15]))], img.pokes()


def file_name_arg(env, img, rng, mgr):
    names = []
    for i in range(15):
        base = mgr + REC + REC_STRIDE * i
        data = bytes(img.b.get(base + k, 0) for k in range(0x80))
        if data[0]:
            names.append(data.split(b"\0")[0].decode("latin-1"))
    name = names[rng.randrange(len(names))] if names and rng.random() < 0.65 else "THPS2 CAREER- NEW.SAV"
    if rng.random() < 0.15:
        name = name.upper()
    return [put_text(img, env, 0x80, name, 0x80)]


@mmu(0x004745D0, "Mmu_FileExists", this=MGR)
def gen_file_exists(env, n):
    img, rng = Image(), env.rng
    mgr = manager_world(img, env, rng, count=rng.choice([0, 1, 3, 6, 15]))
    return file_name_arg(env, img, rng, mgr), img.pokes()


@mmu(0x004746D0, "Mmu_FindFile", this=MGR)
def gen_find_file(env, n):
    img, rng = Image(), env.rng
    mgr = manager_world(img, env, rng, count=rng.choice([0, 1, 3, 6, 15]))
    return file_name_arg(env, img, rng, mgr), img.pokes()


@mmu(0x00474950, "Mmu_MakeFileToken", this=MGR)
def gen_make_token(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    name = put_text(img, env, 0, rng.choice(["A", "AB", "ALPHA", "Hawk", "x" * 30, "TONY"]), 0x40)
    out = put_text(img, env, 0x80, "????????", 0x10)
    return [name, out], img.pokes()


@mmu(0x004747D0, "Mmu_BuildSaveFilename", this=MGR)
def gen_build_filename(env, n):
    img, rng = Image(), env.rng
    mgr = manager_world(img, env, rng, count=rng.choice([0, 1, 3, 6, 12, 15]))
    SCRIPT["stat"] = [(rng.randrange(0, 1000), 0) for _ in range(15)]
    name = put_text(img, env, 0, rng.choice(["ALPHA", "Hawk", "TONY", "x", "AB", "NO NAME"]), 0x40)
    out = put_text(img, env, 0x80, "?" * 0x30, 0x40)
    return [out, u(rng.choice([1, 2, 3, 1, 2, 3, 0, 4])), name], img.pokes()


@mmu(0x00474A50, "Mmu_FileType", this=MGR)
def gen_file_type(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    return [u(pick_index(rng, rng.choice([0, 3, 15])))], img.pokes()


@mmu(0x00474AE0, "Mmu_DescribeFile", this=MGR)
def gen_describe(env, n):
    img, rng = Image(), env.rng
    mgr = manager_world(img, env, rng, count=rng.choice([1, 3, 6, 15]), aligned=False)
    description = put_text(img, env, 0, "?" * 0x20, 0x40)
    blocks = put_text(img, env, 0x80, "", 0x10)
    index = rng.randrange(0, SCRIPT["count"]) if rng.random() < 0.85 else pick_index(rng, SCRIPT["count"])
    if rng.random() < 0.15:
        img.u32(mgr + REC + REC_STRIDE * index + 0x84, rng.choice([0x2000, 0x4000, 0x6000, 0x7FFFE000, 0x80000000, 0xFFFFE000, 0x2001, 0x1FFF, 0xFFFFFFFF]))
    return [index, description, blocks], img.pokes()


@mmu(0x00474D50, "Mmu_FreeBlocks", this=MGR)
def gen_free_blocks(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    return [], img.pokes()


@mmu(0x00475620, "Mmu_GetCardState", this=MGR)
def gen_card_state(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    return [], img.pokes()


# ---- the directory scan -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------

def header_bytes(kind, name):
    description = PREFIX.get(kind, "THPS2 OTHER- ") + name
    return (b"SC" + bytes([0x12, 3]) + description.encode("latin-1") + b"\0").ljust(0x200, b"\0")


@mmu(0x00474D70, "Mmu_ScanSaveFiles", this=MGR)
def gen_scan(env, n):
    img, rng = Image(), env.rng
    mgr = manager_world(img, env, rng, count=rng.choice([0, 3, 15]))
    img.u32(mgr + STATE, 0 if rng.random() < 0.9 else rng.choice([1, 2, 3]))
    count = rng.choice([0, 1, 2, 3, 5, 8, 14, 15, 16, 17])
    kinds = [rng.choice([1, 2, 3, 4, 1, 2, 3, 5]) for _ in range(count)]
    users = [user_name(rng) for _ in range(count)]
    names = []
    for kind, user in zip(kinds, users):
        if kind == 5:
            names.append(rng.choice(["random.SAV", "THPS2.SAV", "thps2 career-.SAV", "THPS2 CAREER", "the THPS2 CAREER- inside.SAV"]))
        elif rng.random() < 0.3:
            names.append((PREFIX[kind] + user + ".SAV").lower() if rng.random() < 0.5 else (PREFIX[kind] + user + ".SAV").upper())
        else:
            names.append(PREFIX[kind] + user + ".SAV")
    SCRIPT["names"] = names
    filt = rng.choice([0, 0, 0, 1, 2, 3, 4])
    # the files that are kept, in order (the first 15 found), with the header each is read from
    kept = []
    for name, kind, user in list(zip(names, kinds, users))[:15]:
        for want, prefix in ((1, PREFIX[1]), (2, PREFIX[2]), (3, PREFIX[3])):
            if filt in (0, want) and name.lower().startswith(prefix.lower()):
                kept.append((want, name[len(prefix):-4] if name.upper().endswith(".SAV") else user))
                break
    SCRIPT["reads"] = [header_bytes(want, user) for want, user in kept]
    SCRIPT["open"] = [7] * (len(kept) + 2)
    SCRIPT["seek"] = [None] * (len(kept) + 2)
    SCRIPT["read"] = [None] * (len(kept) + 2)
    if kept and rng.random() < 0.2:
        at = rng.randrange(0, len(kept))
        SCRIPT["read"][at] = 0
    if kept and rng.random() < 0.1:
        SCRIPT["open"][rng.randrange(0, len(kept))] = 0xFFFFFFFF
    if rng.random() < 0.1:
        SCRIPT["handle"] = 0x4321
    return [u(filt)], img.pokes()


# ---- the card state, the header, the registration ---------------------------------------------------------------------------------------------------------------------------------------

@mmu(0x00475480, "Mmu_MapCardResult", this=MGR)
def gen_map_card_result(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    SCRIPT["state"] = [(rng.choice([0, 1, 2, 3, 3, 4]), rng.choice([0, 1, 2, 3, 4, 5, 9]), rng.choice([1, 1, 1, 0, 0xFFFFFFFF, 2]))]
    out = put_text(img, env, 0, "????", 0x10)
    return [out], img.pokes()


@mmu(0x00475640, "Mmu_CheckCardReady", this=MGR)
def gen_check_ready(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    SCRIPT["state"] = [(rng.choice([0, 3]), rng.choice([0, 1]), 1)]
    return [], img.pokes()


@mmu(0x00475660, "Mmu_DeleteSaveFile", this=MGR)
def gen_delete(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng, count=rng.choice([1, 3, 6, 15]))
    SCRIPT["remove"] = [rng.choice([0, 0, 1, 0xFFFFFFFF])]
    return [rng.randrange(0, SCRIPT["count"]) if rng.random() < 0.9 else pick_index(rng, SCRIPT["count"], wild=False)], img.pokes()


@mmu(0x00475700, "Mmu_BuildSaveHeader")
def gen_build_header(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    out = put_text(img, env, 0, "", 0x200)
    img.put(out, bytes(rng.getrandbits(8) for _ in range(0x200)))
    # three 128-byte sources and a 32-byte name in the text area / static tables
    a, b, c, name = (env.slab + BUF[0], env.slab + BUF[1], env.slab + BUF[2], env.slab + BUF[3])
    for base, size in ((a, 0x80), (b, 0x80), (c, 0x80), (name, 0x20)):
        img.put(base, bytes(rng.getrandbits(8) for _ in range(size)))
    kind = rng.choice([0, 1, 2, 3, 4, -1])
    # the third block comes from `c` when the type is 3 or more (retail dereferences it)
    return [out, u(rng.choice([1, 2, 3, 6, 0xFF, 0x1234])), u(kind), a, b, c if (kind >= 3 or rng.random() < 0.7) else 0, name], img.pokes()


@mmu(0x00475840, "Mmu_AddSaveBuffer", this=MGR)
def gen_add_buffer(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng, buffers=rng.choice([0, 1, 2, 3, 4]))
    return [u(rng.choice([env.slab + BUF[0], 0, rng.getrandbits(32)])), u(rng.choice([0x80, 0x1E00, 0x7FE00, 0, rng.getrandbits(32)]))], img.pokes()


# ---- save and load ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

def card_plan(rng, steps=10):
    """The backend answers for a save or a load: the success answers in every queue, and (a third of the time) one of them broken."""
    S = SCRIPT
    S["open"] = [7] * steps
    S["seek"] = [None] * steps
    S["read"] = [None] * steps
    S["write"] = [None] * steps
    S["create"] = [9] * steps
    S["remove"] = [0] * steps
    S["state"] = [(3, 0, 1)] * (2 * steps)
    if rng.random() < 0.35:
        kind = rng.choice(["open", "seek", "read", "write", "remove", "state", "state", "flag"])
        at = rng.randrange(0, 5)
        if kind == "open":
            S["open"][at] = 0xFFFFFFFF
        elif kind == "seek":
            S["seek"][at] = rng.choice([0, 1, 0x7FFFFFFF])
        elif kind == "read":
            S["read"][at] = 0
        elif kind == "write":
            S["write"][at] = rng.choice([0, 4])
        elif kind == "remove":
            S["remove"][at] = rng.choice([1, 0xFFFFFFFF])
        elif kind == "state":
            S["state"][at] = (rng.choice([0, 1, 2, 4]), 0, 1)
        else:
            S["state"][at] = (3, rng.choice([1, 2, 3]), 1)


def scatter_buffers(img, env, rng):
    for base in BUF:
        img.put(env.slab + base, bytes(rng.getrandbits(8) for _ in range(0x20)))
        img.put(env.slab + base + 0x3E0, bytes(rng.getrandbits(8) for _ in range(0x20)))


@mmu(0x004758E0, "Mmu_SaveFile", this=MGR)
def gen_save(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng, count=rng.choice([0, 1, 3, 6, 15]), buffers=rng.choice([1, 1, 2, 3, 4]))
    count = SCRIPT["count"]
    card_plan(rng)
    scatter_buffers(img, env, rng)
    kind = rng.choice([1, 1, 2, 2, 3, 3])       # (an unknown type leaves retail's local label and file name uninitialised: the unsupported-type branch is not replayed)
    index = rng.randrange(0, count) if (count and rng.random() < 0.4) else count
    name = put_text(img, env, 0, rng.choice(["ALPHA", "Hawk", "A", "TONY HAWK", "x" * 20, "PARK 1"]), 0x40)
    if kind == 3:
        img.u32(0x006A141C, env.slab + BUF[3])
        img.u32(0x006A1420, rng.choice([0, 0x100, 0x180]))
    return [name, index, kind], img.pokes()


@mmu(0x00476200, "Mmu_LoadFile", this=MGR)
def gen_load(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng, count=rng.choice([1, 3, 6, 15]), buffers=rng.choice([1, 1, 2, 3, 4]))
    count = SCRIPT["count"]
    card_plan(rng)
    SCRIPT["reads"] = [bytes(rng.getrandbits(8) for _ in range(0x20)) * 4 for _ in range(4)]
    kind = rng.choice([1, 1, 2, 3, 3, 0, 4])
    index = rng.randrange(0, count)
    return [index, kind], img.pokes()
