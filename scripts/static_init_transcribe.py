"""Transcribe the retail executable's C++ dynamic initializers into C++ for the standalone (milestone 9).

usage: python scripts/static_init_transcribe.py > src/hooks/kits/standalone_static_init_generated.cpp

The C runtime's _cinit (0x0050140c) runs the initializer table 0x00524000..0x00524118 before WinMain. A slot is a native (it has an
adapter), or a compiler stub: `jmp BODY`, or `call CONSTRUCTOR; jmp REGISTER` where REGISTER is `push DESTRUCTOR; call atexit`. The
bodies are straight-line register code over absolute addresses (a global set to a constant or to another global, a 16-bit field
derived from a screen constant). This script turns each body that uses only the instructions below into one C++ function, one
statement per instruction, so src/hooks/kits/standalone_static_init.cpp can be checked against it line by line. Anything else
(a call, a branch) stops the script with the instruction: those few are written by hand.

It reads the executable the build already reads for the static data (the canonical build, `tony.hooks._canonical_exe`); nothing it
prints contains game data beyond the addresses and immediates of the instructions.
"""

from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tony import hooks  # noqa: E402

REG32 = {"eax", "ecx", "edx", "ebx", "esi", "edi", "ebp"}
REG16 = {"ax": "eax", "cx": "ecx", "dx": "edx", "bx": "ebx", "si": "esi", "di": "edi"}
REG8 = {"al": "eax", "cl": "ecx", "dl": "edx", "bl": "ebx"}
WIDTH = {"l": 32, "w": 16, "b": 8}


class Unsupported(Exception):
    pass


def operand(text: str):
    text = text.strip()
    if text.startswith("$"):
        return ("imm", int(text[1:], 0) & 0xFFFFFFFF)
    if text.startswith("%"):
        name = text[1:]
        if name in REG32:
            return ("reg", name, 32)
        if name in REG16:
            return ("reg", REG16[name], 16)
        if name in REG8:
            return ("reg", REG8[name], 8)
        raise Unsupported(text)
    m = re.fullmatch(r"(-?0x[0-9a-f]+|-?\d+)?\(%(\w+)\)", text)
    if m:
        return ("mem", m.group(2), int(m.group(1) or "0", 0))
    m = re.fullmatch(r"0x[0-9a-f]+", text)
    if m:
        return ("abs", int(text, 16))
    raise Unsupported(text)


def address(op) -> str:
    if op[0] == "abs":
        return f"0x{op[1]:08x}"
    return op[1] if op[2] == 0 else f"{op[1]} + 0x{op[2]:x}"


def read(op, width: int) -> str:
    if op[0] == "imm":
        return f"0x{op[1] & ((1 << width) - 1):x}u"
    if op[0] == "reg":
        return op[1] if width == 32 else f"static_cast<std::uint{width}_t>({op[1]})"
    return f"r{width}({address(op)})"


def transcribe(start: int) -> list[str]:
    exe = hooks._canonical_exe()
    lines: list[str] = []
    for insn, mnem, ops in hooks._disassemble(exe, start, start + 0x400):
        ops = ops.split("#")[0].strip()
        parts = [p for p in re.split(r",\s*(?![^()]*\))", ops) if p] if ops else []
        if mnem.startswith("ret"):
            return lines
        if mnem in ("pushl", "popl") and parts and parts[0].startswith("%"):
            continue  # a callee-saved register around the body: the C++ variables need no saving
        if mnem == "cltd":
            lines.append("edx = static_cast<std::int32_t>(eax) < 0 ? 0xffffffffu : 0u;")
            continue
        if mnem == "rep" and ops.startswith("stosl"):
            lines.append("for (; ecx != 0; --ecx, edi += 4) w32(edi, eax);")
            continue
        m = re.fullmatch(r"(mov|xor|sub|add|sar|shl|lea|neg|and|or)([lwb])", mnem)
        if not m:
            raise Unsupported(f"{insn:08x} {mnem} {ops}")
        kind, width = m.group(1), WIDTH[m.group(2)]
        if kind == "neg":
            dst = operand(parts[0])
            lines.append(f"{dst[1]} = 0u - {dst[1]};")
            continue
        if kind == "sar" and len(parts) == 1:
            parts = ["$1", parts[0]]
        src, dst = operand(parts[0]), operand(parts[1])
        if kind == "lea":
            lines.append(f"{dst[1]} = {address(src)};")
            continue
        if kind == "mov":
            if dst[0] == "reg":
                value = read(src, width)
                lines.append(f"{dst[1]} = {value};" if width == 32 else f"{dst[1]} = ({dst[1]} & ~0x{(1 << width) - 1:x}u) | {value};")
            else:
                lines.append(f"w{width}({address(dst)}, {read(src, width)});")
            continue
        if dst[0] != "reg" or width != 32:
            raise Unsupported(f"{insn:08x} {mnem} {ops}")
        if kind == "xor" and src == dst:
            lines.append(f"{dst[1]} = 0;")
        elif kind == "sar":
            lines.append(f"{dst[1]} = static_cast<std::uint32_t>(static_cast<std::int32_t>({dst[1]}) >> {read(src, 32).rstrip('u')});")
        else:
            op = {"xor": "^", "sub": "-", "add": "+", "shl": "<<", "and": "&", "or": "|"}[kind]
            lines.append(f"{dst[1]} = {dst[1]} {op} {read(src, 32)};")
    raise Unsupported(f"no ret within 0x400 bytes of {start:08x}")


def stub_target(exe: bytes, slot_function: int) -> int | None:
    """`jmp BODY` -> BODY"""
    for insn, mnem, ops in hooks._disassemble(exe, slot_function, slot_function + 5):
        if mnem == "jmp":
            return int(ops.split()[0], 16)
    return None


def main() -> int:
    exe = hooks._canonical_exe()
    raw = Path(exe).read_bytes()
    lfanew = struct.unpack_from("<I", raw, 0x3C)[0]
    nsec = struct.unpack_from("<H", raw, lfanew + 6)[0]
    optsize = struct.unpack_from("<H", raw, lfanew + 20)[0]

    def word(va: int) -> int:
        for i in range(nsec):
            o = lfanew + 24 + optsize + 40 * i
            vsize, rva, rawsize, rawptr = struct.unpack_from("<4I", raw, o + 8)
            if 0x400000 + rva <= va < 0x400000 + rva + min(vsize, rawsize):
                return struct.unpack_from("<I", raw, rawptr + va - 0x400000 - rva)[0]
        raise KeyError(hex(va))

    from tony.common import load_yaml

    # a native is a function the resolver has an adapter for (the standalone rows and the hook rows), not any row of re/native/functions.yml: that one also
    # lists the bodies transcribed here
    natives = {int(str(row["address"]), 0)
               for path, key in (("re/config/standalone_natives.yml", "natives"), ("re/config/standalone_natives_manual.yml", "natives"),
                                 ("re/config/native_hooks.yml", "hooks"))
               for row in (load_yaml(path).get(key) or []) if isinstance(row, dict) and row.get("address") is not None}
    print(HEADER)
    rows = []
    for slot in range(0x00524004, 0x00524118, 4):
        function = word(slot)
        body = stub_target(exe, function)
        if body is None or function in natives:
            continue  # a `call; jmp` stub (written by hand) or a native (the resolver runs it)
        lines = transcribe(body)  # Unsupported stops the script: such a body is written by hand
        registers = sorted({r for line in lines for r in REG32 if re.search(rf"\b{r}\b", line)})
        print(f"// slot 0x{slot:08x}: 0x{function:08x} jmp 0x{body:08x}")
        print(f"void init_{body:08x}() {{")
        if registers:
            print(f"    std::uint32_t {', '.join(f'{r} = 0' for r in registers)};")
        for line in lines:
            print(f"    {line}")
        print("}\n")
        rows.append((slot, function, body))
    print("}  // namespace\n")
    print("// slot, the slot's function (a `jmp` stub), the transcription of the body it jumps to; sorted by slot")
    print("const StaticInitializer kTranscribedInitializers[] = {")
    for slot, function, body in rows:
        print(f"    {{0x{slot:08x}, 0x{function:08x}, &init_{body:08x}}},")
    print("};")
    print(f"const std::size_t kTranscribedInitializerCount = {len(rows)};\n")
    print("}  // namespace opentony::kitdll")
    return 0


HEADER = """// GENERATED by scripts/static_init_transcribe.py from the canonical executable's disassembly; do not edit by hand (milestone 9).
//
// The C++ dynamic initializers of the retail executable whose slot in the initializer table (0x00524000..0x00524118, run by the C runtime's _cinit
// before WinMain) is a `jmp` stub to a body of straight-line stores: a global set to a constant, to another global, or to a value derived from one.
// One statement per instruction, in the instruction's order; the registers are local variables. standalone_boot.cpp runs them in table order with the
// slots whose function is a native (through the resolver) and the constructor stubs written by hand there.
#include "standalone_static_init.hpp"
#include "guest_arena.hpp"

#include <cstddef>
#include <cstdint>

namespace opentony::kitdll {
namespace {

[[maybe_unused]] inline std::uint32_t r32(std::uint32_t address) { return arena::at<std::uint32_t>(address); }
[[maybe_unused]] inline std::uint16_t r16(std::uint32_t address) { return arena::at<std::uint16_t>(address); }
[[maybe_unused]] inline void w32(std::uint32_t address, std::uint32_t value) { arena::at<std::uint32_t>(address) = value; }
[[maybe_unused]] inline void w16(std::uint32_t address, std::uint16_t value) { arena::at<std::uint16_t>(address) = value; }
[[maybe_unused]] inline void w8(std::uint32_t address, std::uint8_t value) { arena::at<std::uint8_t>(address) = value; }
"""


if __name__ == "__main__":
    sys.exit(main())
