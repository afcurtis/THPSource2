"""input_log_to_script.py PLAY_LOG [--rate N] [-o FILE]

Turns a play log (a run's `input.txt`, written by the launcher through OPENTONY_INPUT_LOG) into an `OT_INPUT_LOG` script (standalone_platform.cpp) that replays the
same keys, pad buttons and sticks in a headless standalone run.

A line stamped `poll N` (logs since 2026-11-16) keeps that poll. An older line has only its time: the poll is round(seconds * RATE) (`--rate`, default 60: the
polls a second the run had, about the frame rate it ran at; replays of old logs drift by a few polls).

Keys are SDL scancodes in the log and DirectInput codes in a script: the ones the menus use are mapped, the rest are listed on stderr and left out. The log records a stick
or trigger only as + / - / rest, which replays as full deflection or none.
"""
import argparse
import re
import sys

LINE = re.compile(r"^\s*(?P<time>\d+\.\d+)\s+(?:poll\s+(?P<poll>\d+)\s+)?(?P<what>.*)$")
PAD_BUTTON = re.compile(r"^pad button\s+(?P<dir>down|up)\s.*\((?P<id>\d+)\)\s*$")
PAD_AXIS = re.compile(r"^pad axis (?P<axis>\d+) -> (?P<zone>[+-]|rest)\s*$")
KEY = re.compile(r"^key\s+(?P<dir>down|up)\s.*\(scancode (?P<code>\d+)\)\s*$")

# SDL scancode -> DirectInput scan code (hex), for the keys the menus and the default bindings use
SDL_TO_DIK = {
    40: 0x1C,   # Return
    41: 0x01,   # Escape
    42: 0x0E,   # Backspace
    43: 0x0F,   # Tab
    44: 0x39,   # Space
    79: 0xCD,   # Right
    80: 0xCB,   # Left
    81: 0xD0,   # Down
    82: 0xC8,   # Up
}
AXIS_FULL = 32767


def convert(lines, rate=60.0):
    """Returns (script lines, the unsupported key scancodes seen)."""
    out = []
    skipped = set()
    for text in lines:
        match = LINE.match(text.rstrip("\n"))
        if not match:
            continue
        poll = int(match["poll"]) if match["poll"] is not None else round(float(match["time"]) * rate)
        what = match["what"]
        if m := PAD_BUTTON.match(what):
            out.append((poll, f"{poll} pad {m['dir']} {m['id']}"))
        elif m := PAD_AXIS.match(what):
            axis = int(m["axis"])
            value = 0 if m["zone"] == "rest" else AXIS_FULL if m["zone"] == "+" else (0 if axis >= 4 else -32768)
            out.append((poll, f"{poll} pad axis {axis} {value}"))
        elif m := KEY.match(what):
            code = int(m["code"])
            if code in SDL_TO_DIK:
                out.append((poll, f"{poll} key {m['dir']} {SDL_TO_DIK[code]:02x}"))
            else:
                skipped.add(code)
    out.sort(key=lambda item: item[0])   # stable: events of one poll stay in log order
    return [line for _, line in out], sorted(skipped)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("log")
    parser.add_argument("--rate", type=float, default=60.0)
    parser.add_argument("-o", "--output")
    args = parser.parse_args(argv)
    with open(args.log, encoding="utf-8", errors="replace") as handle:
        lines, skipped = convert(handle, args.rate)
    text = "# replay of " + args.log + "\n" + "\n".join(lines) + "\n"
    if args.output:
        with open(args.output, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
    else:
        sys.stdout.write(text)
    if skipped:
        print("input_log_to_script: keys left out (SDL scancodes): " + ", ".join(map(str, skipped)), file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
