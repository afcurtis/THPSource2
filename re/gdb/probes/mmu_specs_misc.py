"""Phase 4.14b probe, the rest of the card layer (group mmu): the screen's flags and entry points, the registration and set-up functions, the replay card helpers, the string helpers and the card
backend. See mmu_specs.py for the world, the slab layout and the stubs."""

from __future__ import annotations

from create_specs import Skip
from frontend_specs_menu import Image
from mmu_specs import (ARENA, BUF, BUFCOUNT, COUNT, G_ACTIONS, G_ACTIVE_PLAYER, G_CAREER_BUFFER, G_CAREER_LOAD_FLAG, G_HANDLE, G_LOADED_NAME, G_MANAGER, G_PARK_NAME, G_REPLAY0, G_REPLAY1, G_REPLAY_BUFFER,
                       G_RETURN_A, G_RETURN_B, G_SKIP_FLAG, MGR, REC, REC_STRIDE, SCR, SCRIPT, STATE, TEXT, cstr, fresh, manager_world, mmu, put_text, u, user_name)
from mmu_specs_mgr import card_plan, header_bytes, scatter_buffers


# ---- the screen's flags, the registration, the library calls ---------------------------------------------------------------------------------------------------------------------

@mmu(0x00479F20, "Mmu_ScreenFlag8", this=SCR)
def gen_flag8(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    return [], img.pokes()


@mmu(0x00479F40, "Mmu_ScreenFlag4", this=SCR)
def gen_flag4(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    return [], img.pokes()


@mmu(0x00479F90, "Mmu_ScreenResult", this=SCR)
def gen_screen_result(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    img.u32(G_ACTIVE_PLAYER, rng.choice([0, 1]))
    for address in (G_ACTIONS, G_ACTIONS + 0x16C):
        img.u8(address, rng.choice([0, 1, 2, rng.getrandbits(8)]))
    return [], img.pokes()


def with_manager(img, env, rng, count=None, buffers=None):
    manager_world(img, env, rng, count=count, buffers=buffers)
    img.u32(G_MANAGER, env.slab + SCR)


@mmu(0x0047A030, "Mmu_RegisterSaveBuffer", this=SCR)
def gen_register(env, n):
    img, rng = Image(), env.rng
    with_manager(img, env, rng, buffers=rng.choice([0, 1, 2, 3, 4]))
    return [u(rng.choice([env.slab + BUF[0], env.slab + BUF[2], 0, rng.getrandbits(32)])), u(rng.choice([0x80, 0x1E00, 0x7FE00, 0, rng.getrandbits(32)]))], img.pokes()


@mmu(0x0047A060, "MMU_Init")
def gen_init(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    return [], img.pokes()


@mmu(0x0047A080, "Mmu_Shutdown")
def gen_shutdown(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    return [], img.pokes()


@mmu(0x0047A090, "Mmu_RefreshCardState")
def gen_refresh(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    SCRIPT["state"] = [(rng.choice([0, 1, 2, 3, 4]), rng.choice([0, 1, 2, 3]), rng.choice([1, 0, 0xFFFFFFFF]))]
    return [], img.pokes()


# ---- the set-up functions -----------------------------------------------------------------------------------------------------------------------------------------------------------------

def setup_world(env, img, rng, leak=0.08):
    """No manager yet (a leak assertion fires 8 % of the time), the load buffers empty, the replay stream buffers pointing at slab buffers."""
    manager_world(img, env, rng)
    img.u32(G_MANAGER, 0 if rng.random() > leak else env.slab + SCR)
    img.u32(G_CAREER_BUFFER, 0 if rng.random() > leak else env.slab + BUF[0])
    img.u32(G_REPLAY_BUFFER, 0 if rng.random() > leak else env.slab + BUF[1])
    img.u32(G_REPLAY0, env.slab + BUF[0] if rng.random() > 0.08 else 0)
    img.u32(G_REPLAY1, env.slab + BUF[1] if rng.random() < 0.5 else 0)


@mmu(0x0047A0B0, "Mmu_SetupCareerSaveBuffer")
def gen_setup_career_save(env, n):
    img, rng = Image(), env.rng
    setup_world(env, img, rng)
    return [rng.getrandbits(8), rng.getrandbits(8), rng.getrandbits(8)], img.pokes()


@mmu(0x0047A220, "Mmu_SetupReplaySaveBuffers")
def gen_setup_replay_save(env, n):
    img, rng = Image(), env.rng
    setup_world(env, img, rng)
    img.u32(0x00533F38, rng.choice([0, 1, 2, 5, 7]))
    return [rng.getrandbits(8), rng.getrandbits(8), u(rng.choice([1, 2, 1, 2, 0, 3])), rng.getrandbits(8)], img.pokes()


@mmu(0x0047A480, "Mmu_SetupParkSaveBuffer")
def gen_setup_park_save(env, n):
    img, rng = Image(), env.rng
    setup_world(env, img, rng)
    buffer = env.slab + BUF[3] if rng.random() > 0.08 else 0
    return [buffer, u(rng.choice([0x80, 0x100, 0x180, 0x500, 0x1E00, 0x3F80, 0x4000, 0x81, 0x7F, 0x2000, 0x1FF80, 0])), rng.getrandbits(8), rng.choice([0, 1, 0xFF, rng.getrandbits(8)])], img.pokes()


@mmu(0x0047A6C0, "Mmu_SetupCareerLoadBuffer")
def gen_setup_career_load(env, n):
    img, rng = Image(), env.rng
    setup_world(env, img, rng)
    return [rng.getrandbits(8), rng.getrandbits(8), rng.getrandbits(8)], img.pokes()


@mmu(0x0047A8A0, "Mmu_SetupReplayLoadBuffer")
def gen_setup_replay_load(env, n):
    img, rng = Image(), env.rng
    setup_world(env, img, rng)
    return [rng.getrandbits(8)], img.pokes()


@mmu(0x0047AA50, "Mmu_SetupParkLoadBuffer")
def gen_setup_park_load(env, n):
    img, rng = Image(), env.rng
    setup_world(env, img, rng)
    buffer = env.slab + BUF[3] if rng.random() > 0.08 else 0
    return [buffer, u(rng.choice([0x80, 0x100, 0x180, 0x500, 0x1E00, 0x3F80, 0x4000, 0x81, 0x7F, 0x2000, 0x1FF80, 0]))], img.pokes()


# ---- the screen's entry points and the end of the screen -----------------------------------------------------------------------------------------------------------------------

@mmu(0x0047AC40, "Mmu_ScreenExists")
def gen_screen_exists(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    img.u32(G_MANAGER, rng.choice([0, env.slab + SCR]))
    return [], img.pokes()


for _address, _name in ((0x0047AC60, "Mmu_UpdateScreen"), (0x0047ACE0, "Mmu_DrawScreen"), (0x0047AD50, "Mmu_ScreenFlagA"), (0x0047ADB0, "Mmu_ScreenFlagB")):
    def _gen(env, n):
        img, rng = Image(), env.rng
        with_manager(img, env, rng)
        img.u32(G_ACTIVE_PLAYER, rng.choice([0, 1]))
        for address in (G_ACTIONS, G_ACTIONS + 0x16C):
            img.u8(address, rng.choice([0, 1, 2]))
        return [], img.pokes()

    mmu(_address, _name)(_gen)


@mmu(0x0047AE10, "Mmu_FinalizeIngameScreen")
def gen_finalize(env, n):
    img, rng = Image(), env.rng
    with_manager(img, env, rng)
    img.u32(G_CAREER_BUFFER, rng.choice([0, env.slab + BUF[0]]))
    img.u32(G_REPLAY_BUFFER, rng.choice([0, env.slab + BUF[1]]))
    img.u8(G_CAREER_LOAD_FLAG, rng.choice([0, 1, 2]))
    img.u32(G_RETURN_A, rng.getrandbits(32))
    img.u32(G_RETURN_B, rng.getrandbits(32))
    img.put(env.slab + BUF[1], bytes(rng.getrandbits(8) for _ in range(0x1A0)))
    img.put(env.slab + BUF[1] + 0x200, bytes(rng.getrandbits(8) for _ in range(0x40)))
    return [u(rng.choice([1, 1, 2, 2, 0, 3, -1]))], img.pokes()


@mmu(0x0047B190, "Mmu_Nothing")
def gen_nothing(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    return [rng.getrandbits(32)], img.pokes()


# ---- the replay card helpers ------------------------------------------------------------------------------------------------------------------------------------------------------------------------

@mmu(0x0047B1A0, "Replay_SaveCardBuffers")
def gen_replay_save(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    img.u32(G_REPLAY0, env.slab + BUF[0])
    img.u32(G_REPLAY1, env.slab + BUF[1] if rng.random() < 0.5 else 0)
    scatter_buffers(img, env, rng)
    SCRIPT["create"] = [rng.choice([9, 0, 0xFFFF, 0x1FFFF, 0xFFFFFFFF])]
    return [put_text(img, env, 0, "THPS2 REPLAY- " + user_name(rng) + ".SAV", 0x80)], img.pokes()


@mmu(0x0047B2D0, "Replay_LoadHeader")
def gen_replay_load_header(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    SCRIPT["resource"] = [rng.choice([0x800, 0x400, 0x100, 0x60, 1, 0])]
    img.put(env.slab + ARENA, bytes(rng.getrandbits(8) for _ in range(0x60)))
    return [put_text(img, env, 0, "THPS2 REPLAY- " + user_name(rng) + ".SAV", 0x80)], img.pokes()


@mmu(0x0047B400, "Replay_LoadValidatedStream")
def gen_replay_load_stream(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    card_plan(rng, 6)
    SCRIPT["reads"] = [bytes(rng.getrandbits(8) for _ in range(0x40)) for _ in range(3)]
    return [env.slab + BUF[0], env.slab + BUF[1] if rng.random() < 0.7 else 0, put_text(img, env, 0, "THPS2 REPLAY- " + user_name(rng) + ".SAV", 0x80)], img.pokes()


@mmu(0x0047B6E0, "Replay_LoadCardBuffers")
def gen_replay_load_buffers(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    SCRIPT["resource"] = [rng.choice([0x800, 0x400, 0x100, 0x60, 1, 0])]
    return [env.slab + BUF[0], env.slab + BUF[1] if rng.random() < 0.6 else 0, put_text(img, env, 0, "THPS2 REPLAY- " + user_name(rng) + ".SAV", 0x80)], img.pokes()


@mmu(0x0047B7D0, "Replay_BuildReturnBufferName")
def gen_return_name(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    return [put_text(img, env, 0, "?" * 0x40, 0x80)], img.pokes()


@mmu(0x0047B850, "Mmu_CopyParkName")
def gen_park_name(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    return [put_text(img, env, 0, "?" * 0x40, 0x80)], img.pokes()


@mmu(0x0047B8D0, "Mmu_ClearLoadedFlag")
def gen_clear_flag(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    return [], img.pokes()


@mmu(0x0047B8E0, "Mmu_TypeLabel")
def gen_type_label(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    return [put_text(img, env, 0, "?" * 0x20, 0x40), u(rng.choice([1, 2, 3, 1, 2, 3, 0, 4, -1]))], img.pokes()


for _address, _name in ((0x0047B9D0, "Mmu_CopyString"), (0x0047BA00, "Mmu_CopyStringAgain")):
    def _gen(env, n):
        img, rng = Image(), env.rng
        manager_world(img, env, rng)
        text = rng.choice(["", "a", "ALPHA", "THPS2 CAREER- X.SAV", "x" * 30])
        return [put_text(img, env, 0, text, 0x40), put_text(img, env, 0x80, "?" * 0x40, 0x80)], img.pokes()

    mmu(_address, _name)(_gen)


# ---- the card backend ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

@mmu(0x004E8D40, "Card_ScanDirectory")
def gen_scan_directory(env, n):
    img, rng = Image(), env.rng
    mgr = manager_world(img, env, rng)
    count = rng.choice([0, 1, 2, 3, 5, 8, 14, 15, 16, 17])
    SCRIPT["names"] = [rng.choice(["a.SAV", "THPS2 CAREER- X.SAV", "", "x" * 40, "THPS2 PARK- LONG NAME.SAV"]) for _ in range(count)]
    first = rng.choice([0, 0, 0, 2, 5])
    last = rng.choice([15, 15, 10, 5, 3, 2])
    if first >= last:
        first = 0
    if rng.random() < 0.1:
        SCRIPT["handle"] = 0x77
    return [0, put_text(img, env, 0, rng.choice(["*.SAV", "*.*", "x"]), 0x10), mgr + REC, mgr + COUNT, first, last], img.pokes()


@mmu(0x004E8F00, "Card_CreateFile")
def gen_create_file(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    SCRIPT["create"] = [rng.choice([9, 0, 0xFFFFFFFF, rng.getrandbits(32)])]
    return [0, put_text(img, env, 0, "THPS2 CAREER- " + user_name(rng) + ".SAV", 0x80), rng.choice([1, 3])], img.pokes()


@mmu(0x004E8F20, "Card_RemoveFile")
def gen_remove_file(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    SCRIPT["remove"] = [rng.choice([0, 0, 1, 0xFFFFFFFF, rng.getrandbits(32)])]
    return [0, put_text(img, env, 0, "THPS2 CAREER- " + user_name(rng) + ".SAV", 0x80)], img.pokes()


@mmu(0x004E8F40, "Card_OpenFile")
def gen_open_file(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    SCRIPT["open"] = [rng.choice([7, 0, 0xFFFFFFFF, 3, rng.getrandbits(32)])]
    return [0, put_text(img, env, 0, "THPS2 CAREER- " + user_name(rng) + ".SAV", 0x80), rng.choice([1, 2])], img.pokes()


@mmu(0x004E8F70, "Card_CloseFile")
def gen_close_file(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    return [], img.pokes()


def transfer_args(env, rng, allow_zero=True):
    size = rng.choice([0x80, 0x100, 0x200, 0x400, 0x500, 0x180, 0x7F, 0x81, 0, 0x4000, 0x80000])
    if size == 0 and not allow_zero:
        size = 0x80       # (the write stub records the bytes it was handed: a zero-length read of memory is not possible)
    return [env.slab + BUF[rng.randrange(0, 4)], u(rng.choice([0, 0x200, 0x80, 0x100, 0x1000, 0x7FFFFF80, -128])), size]


@mmu(0x004E8F90, "Card_ReadOpenFile")
def gen_read_open(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    args = transfer_args(env, rng)
    SCRIPT["seek"] = [rng.choice([None, None, None, 0, 1])]
    SCRIPT["read"] = [rng.choice([None, None, None, 0])]
    SCRIPT["reads"] = [bytes(rng.getrandbits(8) for _ in range(0x40))]
    return args, img.pokes()


@mmu(0x004E9000, "Card_WriteOpenFile")
def gen_write_open(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    args = transfer_args(env, rng, False)
    scatter_buffers(img, env, rng)
    SCRIPT["seek"] = [rng.choice([None, None, None, 0, 1])]
    SCRIPT["write"] = [rng.choice([None, None, None, 0, 4])]
    return args, img.pokes()


@mmu(0x004E9070, "Card_ReadFile")
def gen_read_file(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    args = transfer_args(env, rng)
    card_plan(rng, 3)
    SCRIPT["reads"] = [bytes(rng.getrandbits(8) for _ in range(0x40))]
    return [0, put_text(img, env, 0, "THPS2 CAREER- " + user_name(rng) + ".SAV", 0x80), *args], img.pokes()


@mmu(0x004E90D0, "Card_WriteFile")
def gen_write_file(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    args = transfer_args(env, rng, False)
    scatter_buffers(img, env, rng)
    card_plan(rng, 3)
    return [0, put_text(img, env, 0, "THPS2 CAREER- " + user_name(rng) + ".SAV", 0x80), *args], img.pokes()


@mmu(0x004E9130, "Card_Nothing")
def gen_card_nothing(env, n):
    img, rng = Image(), env.rng
    manager_world(img, env, rng)
    return [], img.pokes()
