"""Phase 4.10a frontend capture probe: park the game in the frontend, pulse keys, log drawn frames.

Run with build/p410a/run_front.py (movies skipped, no level launch).  Output is NDJSON in $FE_OUT.

Record types (the "seam" format every later frontend replay compares against):

  screen   the frontend screen id each time FrontEnd_Main's screen log (0x0045326b) sees a new one
  tick     one input-poll tick (0x004e41b0 caller boundary): tick number, the keys pressed this tick
  font     one Font::draw (0x0044a070): font image (0x4c bytes), x, y, the string, the 5th argument
  mess     one Mess_* call (set_text_justify / set_scale / set_rgb / draw_panel_text / display)
  elem     one screen-element or window call (ELEMENTS below): this, the two stack arguments and, for a
           text element, its string and font pointer
  frame    one Mess_Display (0x004737c0) call: the message pool before and after (the frame boundary)
  const    a read-only global the replay needs (the empty string text elements compare with)
  case     (FE_CASES=1) one call of a window-base function (CASES below): its arguments, the object's image on entry
           and on exit, and the ordered calls it made through virtual methods or the engine (CASE_SITES), each with
           the object it was made on.  scripts/make_frontend_trace_fixture.py turns these into the forced-call
           fixture format, so frontend_retail_calls_test replays natural frames with no new test code.

Environment:
  FE_OUT      output path
  FE_TICKS    stop after this many input ticks (default 600)
  FE_KEYS     JSON file [{"tick": n, "scan": 28, "press": 5}, ...]: hold scan code `scan` (DirectInput)
              for `press` ticks starting at tick n.  Scan codes: 0x1c enter, 0x01 esc, 0xc8 up, 0xd0 down,
              0xcb left, 0xcd right.
  FE_FRAMES   stop after this many Mess_Display frames (default unlimited)
  FE_CASES    1 = also record `case` records (default 0)

Recipe, one screen (the main menu): `bash build/p410a/run_front.sh` with build/p410a/env.sh setting FE_TICKS=600,
FE_CASES=1 and FE_OUT; the game skips its movies, reaches "Shell Screen: 0 - MAIN_MENU" at about tick 10 and the
probe logs every element draw from then on.  To walk to another screen add FE_KEYS (Enter, Down...).
"""

import json
import os

import gdb

from opentony.breakpoint import TonyBreakpoint
from opentony.memory import mem

OUT_PATH = os.environ.get("FE_OUT", (os.path.expanduser("~/THPSource2") + "/build/p410a/front.ndjson"))
TICKS = int(os.environ.get("FE_TICKS", "600"))
FRAMES = int(os.environ.get("FE_FRAMES", "0"))
KEYS_PATH = os.environ.get("FE_KEYS", "")
CASES = os.environ.get("FE_CASES", "0") == "1"

INPUT_STATE = 0x004E41B0
KEYBOARD_STATE = 0x006A43E4
SCREEN_LOG = 0x0045326B
FONT_DRAW = 0x0044A070
MESS_DISPLAY = 0x004737C0
MESS = {
    0x004734B0: "set_text_justify",
    0x004734F0: "set_scale",
    0x00473500: "get_scale",
    0x00473580: "set_rgb",
    0x004735B0: "draw_panel_text",
}
FONT_SIZE = 0x4C
# Window base (screentools.cpp): window draw, and the draw method of the two element classes.
ELEMENTS = {
    0x004AC770: "window_draw",
    0x004ACB50: "text_element_draw",
    0x004ACE20: "sprite_element_draw",
}

# Window-base functions recorded as replayable cases: address -> (name, stack argument count, object image size).
CASE_ENTRIES = {
    0x004AC4D0: ("ScreenElement_Update", 0, 0x30),
    0x004AC730: ("Window_Update", 0, 0xB0),
    0x004AC770: ("Window_Draw", 2, 0xB0),
    0x004AC7C0: ("Window_SnapX", 0, 0xB0),
    0x004AC800: ("Window_SnapY", 0, 0xB0),
    0x004ACB50: ("TextScreenElement_Draw", 2, 0x30),
    0x004ACE20: ("SpriteScreenElement_Draw", 2, 0x30),
}
# The call instructions inside them that reach virtual methods or the engine: address -> (service name, stack
# arguments, index of a string argument or None).  The object is ECX at the call.
CASE_SITES = {
    0x004AC748: ("element_update", 0, None),
    0x004AC7A2: ("element_draw", 2, None),
    0x004AC7DD: ("element_snap_x", 0, None),
    0x004AC81D: ("element_snap_y", 0, None),
    0x004AC4D6: ("element_hook", 0, None),
    0x004ACBBF: ("font_draw", 4, 2),
    0x004ACE4F: ("sprite_draw", 3, None),
}
# Window_Update calls ScreenElement_Update directly (not a virtual call): it is part of Window_Update's case.
WINDOW_UPDATE_RETURN = 0x004AC761
EMPTY_STRING = 0x0055FE10

out = open(OUT_PATH, "w")
keys = json.load(open(KEYS_PATH)) if KEYS_PATH else []
state = {"tick": 0, "frames": 0, "done": False, "held": {}, "screen": None}


def emit(record):
    out.write(json.dumps(record, sort_keys=True) + "\n")
    out.flush()


def cstr(address, limit=256):
    try:
        raw = mem.bytes(address, limit)
    except Exception:
        return None
    end = raw.find(b"\0")
    return raw[: end if end >= 0 else limit].decode("latin-1")


def finish():
    if state["done"]:
        return
    state["done"] = True
    emit({"t": "end", "tick": state["tick"], "frames": state["frames"]})
    out.close()
    for probe in probes:
        probe.enabled = False
    probes[0].should_stop = True


class Tick(TonyBreakpoint):
    """Input state helper: one call per polled input tick; the key script is applied here."""

    def on_hit(self, ctx):
        if state["done"]:
            return
        state["tick"] += 1
        tick = state["tick"]
        for key in keys:
            if key["tick"] == tick:
                state["held"][key["scan"]] = key["press"]
        pressed = []
        for scan, left in list(state["held"].items()):
            if left > 0:
                mem.write(KEYBOARD_STATE + scan, bytes((0x80,)))
                pressed.append(scan)
                state["held"][scan] = left - 1
            else:
                mem.write(KEYBOARD_STATE + scan, bytes((0,)))
                del state["held"][scan]
        emit({"t": "tick", "tick": tick, "keys": pressed})
        if tick >= TICKS:
            finish()


class Screen(TonyBreakpoint):
    def on_hit(self, ctx):
        screen = ctx.register("eax")
        if screen != state["screen"]:
            state["screen"] = screen
            emit({"t": "screen", "tick": state["tick"], "screen": screen})


class FontDraw(TonyBreakpoint):
    def on_hit(self, ctx):
        if state["done"]:
            return
        this = ctx.register("ecx")
        args = [mem.u32(ctx.esp + 4 + 4 * i) for i in range(4)]
        emit({
            "t": "font",
            "tick": state["tick"],
            "this": this,
            "image": mem.bytes(this, FONT_SIZE).hex(),
            "x": args[0] - (1 << 32) if args[0] >= 1 << 31 else args[0],
            "y": args[1] - (1 << 32) if args[1] >= 1 << 31 else args[1],
            "text": cstr(args[2]),
            "arg5": args[3],
        })


class MessCall(TonyBreakpoint):
    def __init__(self, address, name):
        super().__init__(address, internal=True)
        self.name = name

    def on_hit(self, ctx):
        if state["done"]:
            return
        emit({
            "t": "mess",
            "tick": state["tick"],
            "name": self.name,
            "ecx": ctx.register("ecx"),
            "args": [mem.u32(ctx.esp + 4 + 4 * i) for i in range(4)],
        })


class ElementCall(TonyBreakpoint):
    def __init__(self, address, name):
        super().__init__(address, internal=True)
        self.name = name

    def on_hit(self, ctx):
        if state["done"]:
            return
        this = ctx.register("ecx")
        record = {
            "t": "elem",
            "tick": state["tick"],
            "name": self.name,
            "this": this,
            "args": [mem.u32(ctx.esp + 4 + 4 * i) for i in range(2)],
        }
        if self.name == "text_element_draw":
            record["text"] = cstr(mem.u32(this + 0x28))
            record["font"] = mem.u32(this + 0x2C)
        emit(record)


class CaseEntry(TonyBreakpoint):
    """Entry of a window-base function: start a case; its exit image is taken at the return address."""

    def __init__(self, address, name, nargs, size):
        super().__init__(address, internal=True)
        self.name = name
        self.nargs = nargs
        self.size = size
        self.address = address

    def on_hit(self, ctx):
        if state["done"]:
            return
        ret = ctx.return_address()
        if self.address == 0x004AC4D0 and ret == WINDOW_UPDATE_RETURN:
            return
        this = ctx.register("ecx")
        record = {
            "t": "case", "tick": state["tick"], "fn": self.address, "name": self.name, "this": this,
            "args": [mem.u32(ctx.esp + 4 + 4 * i) for i in range(self.nargs)],
            "in": mem.bytes(this, self.size).hex(), "calls": [],
        }
        if self.name == "TextScreenElement_Draw":
            # the element's string (+0x28) is read by the draw: keep its bytes for the replay
            record["str_ptr"] = mem.u32(this + 0x28)
            record["str"] = cstr(record["str_ptr"], 0x7F)
        stack = case_stack
        stack.append(record)
        returns.setdefault(ret, []).append((record, this, self.size))
        return_probe(ret)


class CaseSite(TonyBreakpoint):
    """A call instruction inside a window-base function: log the call for the innermost running case."""

    def __init__(self, address, name, nargs, text_index):
        super().__init__(address, internal=True)
        self.name = name
        self.nargs = nargs
        self.text_index = text_index

    def on_hit(self, ctx):
        if state["done"] or not case_stack:
            return
        args = [mem.u32(ctx.esp + 4 * i) for i in range(self.nargs)]
        call = {"name": self.name, "this": ctx.register("ecx"), "args": args}
        if self.text_index is not None:
            call["text"] = cstr(args[self.text_index])
        case_stack[-1]["calls"].append(call)


class CaseReturn(TonyBreakpoint):
    """The return address of a recorded case: take the exit image and emit it."""

    def on_hit(self, ctx):
        pending = returns.get(self.location_address)
        if not pending:
            return
        record, this, size = pending.pop()
        if case_stack and case_stack[-1] is record:
            case_stack.pop()
        record["out"] = mem.bytes(this, size).hex()
        emit(record)


case_stack = []
returns = {}
return_probes = {}


def return_probe(address):
    if address not in return_probes:
        probe = CaseReturn(address, internal=True)
        probe.location_address = address
        return_probes[address] = probe
        probes.append(probe)


class MessDisplay(TonyBreakpoint):
    def on_hit(self, ctx):
        if state["done"]:
            return
        state["frames"] += 1
        emit({"t": "frame", "tick": state["tick"], "frame": state["frames"]})
        if FRAMES and state["frames"] >= FRAMES:
            finish()


probes = [
    Tick(INPUT_STATE, internal=True),
    Screen(SCREEN_LOG, internal=True),
    FontDraw(FONT_DRAW, internal=True),
    MessDisplay(MESS_DISPLAY, internal=True),
]
for address, name in MESS.items():
    probes.append(MessCall(address, name))
for address, name in ELEMENTS.items():
    probes.append(ElementCall(address, name))
if CASES:
    emit({"t": "const", "address": EMPTY_STRING, "bytes": mem.bytes(EMPTY_STRING, 0x10).hex()})
    for address, (name, nargs, size) in CASE_ENTRIES.items():
        probes.append(CaseEntry(address, name, nargs, size))
    for address, (name, nargs, text_index) in CASE_SITES.items():
        probes.append(CaseSite(address, name, nargs, text_index))
