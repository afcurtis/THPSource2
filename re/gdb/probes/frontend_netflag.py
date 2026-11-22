"""Phase 4.10f3 probe: the frontend trace of 4.10a plus a watch on the netgame flag 0x006a3b21 and the means to park the game on a chosen screen.

Run it like frontend_trace.py (build/p410a/run_front.py PROBE.py; movies skipped, no level launch).  It executes frontend_trace.py and wraps its input-tick
callback, so every tick also:

  F3_SEL=N      (ticks 1..3) pokes N into the main-menu wheel's remembered selection (0x00533e98): the wheel opens screen 21 (0), 1 (1, 2), 10 (3), 17 (4), 5 (5), 11 (6)
  F3_SCREEN=N   (from tick 8) pokes N into the front end's next-screen variable 0x00561c78 until the screen log shows it (9 = MULTI_SELECT, 19 = MULTI_HANDICAP ...)
  F3_MODE=N     pokes N into the game mode 0x00561c74 every tick
  F3_PADS=1     writes 1 into both pads' connected words (0x0056b144, 0x0056b2b0) every tick (the two-player entries want two pads)
  F3_WATCH=A,B  extra hex addresses whose byte is logged when it changes

and logs a `flag` record (the netgame flag and the wheel selection) whenever either changes.  Keys come from FE_KEYS as in frontend_trace.py.  Result of the 4.10f3 runs:
the flag stays 0 through MULTI_SELECT and the screens around it (re/evidence/xref-libraries.md, "Local two-player play").
"""

import os

exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "frontend_trace.py")).read(), "frontend_trace.py", "exec"))

FLAG = 0x006A3B21
WHEEL_SEL = 0x00533E98
SEL = os.environ.get("F3_SEL", "")
PADS = os.environ.get("F3_PADS", "0") == "1"
SCREEN = os.environ.get("F3_SCREEN", "")
MODE = os.environ.get("F3_MODE", "")
WATCH = [int(a, 16) for a in os.environ.get("F3_WATCH", "").split(",") if a]
seen = {"flag": None, "sel": None, "watch": {}}
_tick_on_hit = Tick.on_hit


def tick_hit(self, ctx):
    _tick_on_hit(self, ctx)
    if SEL and state["tick"] <= 3:
        mem.write(WHEEL_SEL, bytes((int(SEL),)))
    if SCREEN and state["screen"] != int(SCREEN) and state["tick"] >= 8:
        mem.write(0x00561C78, int(SCREEN).to_bytes(4, "little"))
    if MODE:
        mem.write(0x00561C74, int(MODE).to_bytes(4, "little"))
    if PADS:
        mem.write(0x0056B144, (1).to_bytes(4, "little"))
        mem.write(0x0056B2B0, (1).to_bytes(4, "little"))
    flag = mem.bytes(FLAG, 1)[0]
    sel = mem.bytes(WHEEL_SEL, 1)[0]
    if flag != seen["flag"] or sel != seen["sel"]:
        seen["flag"], seen["sel"] = flag, sel
        emit({"t": "flag", "tick": state["tick"], "flag": flag, "wheel": sel})
    for address in WATCH:
        value = mem.bytes(address, 1)[0]
        if seen["watch"].get(address) != value:
            seen["watch"][address] = value
            emit({"t": "watch", "tick": state["tick"], "address": address, "value": value})


Tick.on_hit = tick_hit
