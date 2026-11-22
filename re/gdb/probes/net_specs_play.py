"""Phase 4.13b2 probe, part 3: the handshake and the per-frame update (group netp). See net_specs.py for the worlds, the slab layout and the stubs."""

from __future__ import annotations

from frontend_specs_menu import Image
from net_specs import BODY, LEVELMSG, LOCAL, MESSAGE, REMOTE, net_world, netp, put_string, string_at, u


def world(env, **kw):
    img, rng = Image(), env.rng
    net_world(img, env, rng, **kw)
    return img, rng


SCENES = {2: [0, 1, 2, 3], 7: [5, 6, 7, 8], 9: [8, 9, 10], 14: [13, 14, 15], 15: [14, 15, 16], 16: [14, 15, 16, 17], 17: [15, 16, 17, 18], 18: [17, 18, 19, 20], 20: [19, 20, 21], 21: [20, 21]}


def update_world(img, env, rng, force=False):
    """A step of the handshake and the other machine's step next to it (so the transitions are reached), with the clocks of the two skaters ahead of the level tick."""
    if not force and rng.random() < 0.3:
        return
    slab = env.slab
    local = rng.choice(list(SCENES))
    remote = rng.choice(SCENES[local])
    img.u8(0x006A3B21, 1)
    img.u32(0x006A3A90, local)
    img.u32(0x006A18B0, remote)
    img.u32(0x006A3A98, local if rng.random() < 0.6 else rng.randrange(0, 22))
    img.u32(0x006A18B8, rng.choice([remote, 7, rng.randrange(0, 22)]))
    tick = rng.choice([0, 0x40, 0x78])
    img.u32(0x005685F4, tick)
    for base in (slab + LOCAL, slab + REMOTE):
        img.u32(base + 0x2CC8, tick + rng.choice([1, 5, 0x1E, 0x100]))
    img.u32(0x006A3A88, slab + LOCAL)
    img.u32(0x006A18A8, slab + REMOTE)
    img.u32(0x00533F38, rng.choice([9, 5, 8, 0, 9]))
    img.u32(0x00561BFC, rng.choice([0, 0, 0, 0x33]))
    img.u32(0x006A3D1C, rng.choice([0, 1, 4, 0x1F, 0x3C, u(-3)]))
    img.u32(0x006A3DF0, rng.choice([0, 3, 6, 20, 40, 60, 1, 7]))
    img.u8(0x006A3D25, rng.choice([0, 1, 1]))
    img.u32(0x006A18BC, rng.choice([0, 0x8000, 0x10000, 0x4000, 0x8000 | 0x10000]))
    img.u32(0x006A3A9C, rng.choice([0, 0x2000, 0x80000, 0x2000]))
    img.u32(0x006A3D3C, rng.choice([0, 0, 1, 5000]))


def waiting_world(img, env, rng):
    """The waiting loops end when the other side reaches a step, a quit key is pressed or the link is lost: a scripted key answer ends them in a few passes."""
    update_world(img, env, rng)
    img.u32(0x006A18B0, rng.choice([0xB, 0xC, 0x14, 0x15, 5, 7, 0x12, rng.randrange(0, 22)]))
    img.u8(0x006A3B21, 1 if rng.random() > 0.08 else 0)
    img.u8(0x006A3B30, rng.choice([0, 0, 0, 0, 1]))
    img.u8(0x006A1417, rng.choice([0, 0, 0, 1]))


@netp(0x004DEB00, "NetGame_BeginMatch")
def gen_begin_match(env, n):
    img, rng = world(env)
    return [], img.pokes()


@netp(0x004DEB50, "NetGame_EndMatch")
def gen_end_match(env, n):
    img, rng = world(env)
    img.u32(0x006A3D4C, rng.getrandbits(32))
    img.u8(0x006A3D60, rng.choice([0, 1]))
    return [], img.pokes()


@netp(0x004DECE0, "NetGame_PauseAgreement")
def gen_pause_agreement(env, n):
    img, rng = world(env)
    img.u32(0x00561C74, rng.choice([5, 8, 9, 9, 0]))
    img.u8(0x006A3B30, rng.choice([0, 0, 0, 1]))
    img.u32(0x006A3D3C, rng.choice([0, u(0), 1, 5000, rng.getrandbits(32)]))
    return [], img.pokes()


@netp(0x004DEE50, "NetGame_GameOver")
def gen_game_over(env, n):
    img, rng = world(env)
    waiting_world(img, env, rng)
    return [], img.pokes()


@netp(0x004DEFC0, "NetGame_EndRun")
def gen_end_run(env, n):
    img, rng = world(env)
    return [], img.pokes()


@netp(0x004DF370, "NetGame_SendString")
def gen_send_string(env, n):
    img, rng = world(env)
    text = rng.choice(["", "hi", "chat message", "x" * 30, "a b c", "NETPARK"])
    where = rng.choice([put_string(img, env, 7, text), 0x006A3C93])
    if where == 0x006A3C93:
        img.put(0x006A3C93, text.encode("latin-1") + b"\0")
    return [where], img.pokes()


@netp(0x004DF6D0, "NetGame_SendState")
def gen_send_state(env, n):
    img, rng = world(env)
    img.u32(0x00533F38, rng.choice([8, 8, 5, 9, 1]))
    img.u32(0x006A3A90, rng.choice([0, 5, 0xE, 0xF, 0x10, 0x12, 0x13, 0x14, 0x15, 0x16]))
    return [], img.pokes()


@netp(0x004DFE50, "NetGame_WaitForOptions")
def gen_wait_options(env, n):
    img, rng = world(env)
    waiting_world(img, env, rng)
    img.u32(0x006A3A90, rng.choice([0, 3, 5, 6, 7]))
    return [], img.pokes()


@netp(0x004DFFB0, "NetGame_WaitForLoading")
def gen_wait_loading(env, n):
    img, rng = world(env)
    waiting_world(img, env, rng)
    return [], img.pokes()


@netp(0x004E02D0, "NetGame_Update")
def gen_update(env, n):
    img, rng = world(env)
    update_world(img, env, rng)
    img.u8(0x006A3B30, rng.choice([0, 0, 0, 0, 1]))
    return [], img.pokes()


def message(img, env, rng, body_at=BODY):
    img.u32(env.slab + MESSAGE + 0x10, env.slab + body_at)
    return env.slab + MESSAGE


@netp(0x004E1230, "NetGame_OnEventMessage")
def gen_on_event(env, n):
    img, rng = world(env)
    msg = message(img, env, rng)
    img.u16(env.slab + BODY + 8, rng.getrandbits(16))
    img.u16(env.slab + BODY + 0xA, rng.getrandbits(16))
    return [msg], img.pokes()


@netp(0x004E1250, "NetGame_OnCustomLevelMessage")
def gen_on_custom_level(env, n):
    img, rng = world(env)
    msg = message(img, env, rng, LEVELMSG)
    body = env.slab + LEVELMSG
    img.put(body, bytes(rng.getrandbits(8) for _ in range(0x100)))
    img.u32(body + 4, rng.choice([0x64, 0, 1, 3, 7]))
    img.put(body + 8, rng.choice([b"Park", b"A" * 20, b""]) + b"\0")
    if rng.random() < 0.4:
        img.u32(0x005614CC, 0)
    return [msg], img.pokes()


@netp(0x004E1370, "NetGame_OnSystemMessage")
def gen_on_system(env, n):
    img, rng = world(env)
    msg = message(img, env, rng)
    body = env.slab + BODY
    img.u32(body, rng.choice([3, 3, 5, 5, 4, 7, 0x21, 0x22, 0x31, 0x31, 0x102, 0x103, 0x104, 0x104, 0x105, 0, 100, 0x12345, u(-1)]))
    img.u32(body + 0xC, rng.getrandbits(32))
    img.u32(body + 0x20, rng.choice([0, put_string(img, env, 8, rng.choice(["Joe", "a long player name", "x"]))]))
    img.u32(body + 0x24, rng.choice([0, put_string(img, env, 9, "Guest")]))
    img.u32(body + 0x30, rng.getrandbits(32))
    return [0x006A05C8, msg], img.pokes()
