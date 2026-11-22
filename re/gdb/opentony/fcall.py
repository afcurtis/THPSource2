"""Forced calls of retail functions with snapshot/restore of the memory they touch.

Phase 4.8 validates game-logic functions (career, skater manager, competition) by calling the
*retail* function from a frame breakpoint on poked memory and recording what it returned, which
bytes it wrote and which outside services it reached.  The native port replays each case from the
same pre-state; every return value, written byte and service call must match.

Usage inside a probe::

    calls = ForcedCalls(regions=[(0x0055f900, 0x700), (0x00568a68, 0x1dc4)])
    base = calls.snapshot()
    calls.stub(0x004ad620, args=3, name="sfx")                      # skip a service, log its arguments
    calls.stub(0x004cde70, args=0, name="video_playing", result=lambda a: 0)   # scripted result
    record = calls.run(0x00414030, args=(3,), pokes={0x0055fd7c: b"\\x01\\0\\0\\0"}, base=base)

Only the listed regions are restored after every case; a function that writes outside them must
be stubbed.  A service stubbed with a `result` callable has its returned values recorded in the
case (`values`) so a replay can script them.

Phase 4.9 adds two more stub options.  `blob=(i, size)` records the bytes the service's pointer
argument `i` points at as the call's text (a palette store).  `fill=(i, fn)` writes `fn(args)` into
the buffer argument `i` before returning and records those bytes as scripted dwords under the
service's name (a palette read), so a replay can hand the same bytes back.

Phase 4.10a adds `this=True` (a thiscall stub records ECX as its first argument, so a virtual call on a fake
object says which object it was) and `pokes=[(i, size, fn), ...]` (a service that writes through pointer
arguments: before returning, `fn(args)` is written as a little-endian integer of `size` bytes at the address
argument `i` holds, and recorded as a scripted value under the service's name, in order, for the replay).
`text=N` records the string at argument N (rather than argument 0) as the call's text.

Phase 4.10b adds `masks={index: mask}`: an argument that is a 16-bit value pushed as a dword has stack garbage in its upper
half, so the recorded value (and the one `result` / `pokes` callables see) is ANDed with the mask.

Phase 4.10e adds `writes=[(address, size, fn), ...]` (a service that changes memory at a fixed address: `fn(args)` is written there as a
little-endian integer of `size` bytes and recorded as a scripted value under the service's name, after the `pokes` and before the
`result`; a stubbed Game_FrameTick advances the simulation clock this way, so a screen loop that waits for the clock can end) and a
list for `keep` (the indices of the arguments to record: a call that is handed pointers to stack locals records only the others).

Phase 4.10f2 adds `stack_to=ADDRESS`: an argument that points into the caller's frame (a string built in a local, whose address changes with every call) is
recorded as ADDRESS (the fixed scratch address the native code builds the same string at), while `text=` still reads the real pointer.

Phase 4.10h: `stack_to` also maps `this` (a thiscall on a local object: a stack Font, a vector), `blob`, `pokes` and `fill` read and write the real pointer
whatever `stack_to` records, `blob=(-1, size)` records the bytes `this` points at, and `ret_arg=i` returns the real pointer of argument `i` in eax (a function that
returns its output buffer).

Phase 4.11b: `P410_WATCHDOG=seconds` interrupts a forced call that runs longer (a retail function that loops for ever on a state the generator should not
have made): the call ends in a gdb error like a fault, is recorded with an `error` and skipped by the fixture maker, and the run goes on.

Phase 4.10i3: `blob` may be a list of (pointer argument, size) pairs (recorded one after the other: a rectangle then the palette it describes), and `blank=callable() ->
[(offset, length), ...]` zeroes ranges of the recorded blob that are stack leftovers in retail (the unwritten fields of a primitive built in a local).

Phase 4.13a: `run(..., edx=)` sets edx before the call (a fastcall's second register argument), the spec's `edx` names it.

Phase 4.13b: `stub_site(address, length, args, name, ...)` puts the stub on a `call` instruction of the function under test (an import called through
a pointer: Sleep, a Bink or a Win32 call) instead of on the function: the stub then fires only for the forced call (other threads, and Wine itself, run the
same imports), reads the arguments from the top of the stack (nothing is pushed yet), pops them for a stdcall and resumes after the `length`-byte instruction.

Phase 4.11e: `strings=(i, count)` (count an integer or callable(values)) records, as the call's text, the strings the first `count` pointers of the array argument `i` point at
(joined by 0x1f, then a 0x1e) followed by the bytes of `blob`: the texts of a menu built from a local array; and a poke whose pointer argument is null writes nothing and
records nothing.
"""

from __future__ import annotations

import os

import gdb

from .breakpoint import TonyBreakpoint
from .memory import mem


class _Stub(TonyBreakpoint):
    """Skip a service function: log its stack arguments, then return straight to the caller."""

    def __init__(self, owner, address, args, name, convention, result, when, keep, text, blob, fill, this=False,
                 pokes=None, deref=None, masks=None, writes=None, stack_to=None, ret_arg=None, blank=None, strings=None, site=None):
        super().__init__(address, internal=True)
        self.site = site
        self.strings = strings
        self.blank = blank
        self.ret_arg = ret_arg
        self.this = this
        self.stack_to = stack_to
        self.writes = writes or []
        self.masks = masks or {}
        self.pokes = pokes or []
        self.deref = deref or []
        self.owner = owner
        self.name = name
        self.args = args
        self.convention = convention
        self.result = result
        self.when = when
        self.keep = keep
        self.text = text
        self.blob = blob
        self.fill = fill

    def stop(self):
        if not self.owner.active:
            return False
        esp = int(gdb.parse_and_eval("$esp")) & 0xFFFFFFFF
        if self.site is not None:
            # a call site: the arguments are on top of the stack, no return address has been pushed
            ret = None
            values = [mem.u32(esp + 4 * i) for i in range(self.args)]
        else:
            ret = mem.u32(esp)
            values = [mem.u32(esp + 4 + 4 * i) for i in range(self.args)]
        for index, mask in self.masks.items():
            # an argument whose upper bits are stack garbage (a 16-bit value pushed as a dword): keep what the callee uses
            values[index] &= mask
        raw = list(values)
        real = list(values)       # (the real argument values, whatever is recorded)
        if isinstance(self.stack_to, dict):
            # Phase 4.11d: several locals reach one service (a camera's angles, matrix and position): each argument index names its own scratch address
            values = [self.stack_to[i] if i in self.stack_to and esp <= v < esp + 0x4000 else v for i, v in enumerate(values)]
        elif self.stack_to is not None:
            # Phase 4.10f2: an argument that points into the caller's frame (a string built in a local) is recorded as the fixed scratch address the native
            # code builds the same string at (the stack address is not repeatable); the text read below still comes from the real pointer
            values = [self.stack_to if esp <= v < esp + 0x4000 else v for v in values]
        ecx = int(gdb.parse_and_eval("$ecx")) & 0xFFFFFFFF
        if self.owner.trace is not None:
            where = ""
            if os.environ.get("P410_STUBTRACE") == "2":      # (the caller, `this` and its vtable: the first things to look at when a fake object is called)
                try:
                    where = f" eip={int(gdb.parse_and_eval('$eip')) & 0xFFFFFFFF:#x} esp={esp:#x} ret={ret or 0:#x} this={ecx:#x} vtable={mem.u32(ecx):#x}"
                except Exception:
                    where = f" ret={ret or 0:#x} this={ecx:#x}"
            self.owner.trace(f"    stub {self.name} {values}{where}")
        if self.when is None or self.when(values):
            if self.keep is None:
                kept = values
            elif isinstance(self.keep, (list, tuple)):
                kept = [values[i] for i in self.keep]
            else:
                kept = values[self.keep]
            if self.deref:
                # arguments that are pointers to a dword: record what they point at (a callback that gets a
                # position by reference), not the stack address
                kept = [mem.u32(raw[i]) if i in self.deref else v for i, v in enumerate(values)] if self.keep is None else kept
            if self.this:
                scratch = self.stack_to.get(-1) if isinstance(self.stack_to, dict) else self.stack_to
                kept = [scratch if scratch is not None and esp <= ecx < esp + 0x4000 else ecx, *kept]
            record = {"name": self.name, "args": kept}
            if self.text:
                try:
                    # `text=True` reads the string at argument 0; an integer names another argument
                    record["text"] = mem.cstring(raw[0 if self.text is True else int(self.text)], 256)
                except Exception:  # noqa: BLE001
                    record["text"] = ""
            prefix = ""
            if self.strings is not None:
                # Phase 4.11e: an array of string pointers (the texts of a menu): the strings they point at, one after the other, then the bytes of the blob (if any)
                index, count = self.strings
                count = count(values) if callable(count) else count
                prefix = "".join(mem.cstring(mem.u32(real[index] + 4 * k), 256) for k in range(count)) + ""
                record["text"] = prefix
            if self.blob is not None:
                # the bytes the routine was handed: (index of the pointer argument, size or callable(values) -> size); a list of such pairs is recorded one after the other
                data = b""
                for index, size in (self.blob if isinstance(self.blob, list) else [self.blob]):
                    if callable(index):
                        # Phase 5 10g s15: bytes the routine reaches by another way than an argument (`index(ecx, real, esp)` returns them: a script cursor the
                        # caller pointed at a stack buffer)
                        data += bytes(index(ecx, real, esp))
                        continue
                    size = size(values) if callable(size) else size
                    data += mem.bytes(ecx if index < 0 else real[index], size)
                if self.blank is not None:
                    # Phase 4.10i3: bytes of the record that are stack leftovers in retail (a packet built in a local with some fields never written): `blank()` lists
                    # (offset, length) ranges of the record that are recorded as zero; the native port leaves them zero
                    data = bytearray(data)
                    for offset, length in self.blank():
                        data[offset:offset + length] = bytes(length)
                    data = bytes(data)
                record["text"] = prefix + data.decode("latin-1")
            self.owner.log.append(record)
        for entry in self.pokes:
            index, size, produce = entry[:3]
            offset = entry[3] if len(entry) > 3 else 0      # Phase 4.14c: a fourth element is the offset of the field inside the record the pointer argument names
            if real[index] == 0:
                # Phase 4.11e: a service handed a null out pointer writes nothing (FindItem(0) asks only for the item)
                continue
            if size is None:
                # Phase 4.10d: a poke whose callable returns bytes (an array the service fills in): each dword is replayed as a scripted value
                data = bytes(produce(values))
                write_chunks(real[index] + offset, data)
                padded = data + b"\0" * (-len(data) % 4)
                self.owner.values.setdefault(self.name, []).extend(
                    int.from_bytes(padded[i:i + 4], "little") for i in range(0, len(padded), 4))
                continue
            value = int(produce(values)) & ((1 << (8 * size)) - 1)
            write_chunks(real[index] + offset, value.to_bytes(size, "little"))
            self.owner.values.setdefault(self.name, []).append(value)
        for entry in self.writes:
            address, size, produce = entry[:3]
            value = int(produce(values)) & ((1 << (8 * size)) - 1)
            write_chunks(address, value.to_bytes(size, "little"))
            # (Phase 5 10g s15: a fourth element names the scripted value, so a port that applies the write reads it under its own name)
            self.owner.values.setdefault(entry[3] if len(entry) > 3 else self.name, []).append(value)
        if self.fill is not None:
            # a routine that writes into a caller buffer: (index of the pointer argument, callable(values) -> bytes);
            # the bytes are replayed as scripted dwords of the same name
            index, produce = self.fill
            data = bytes(produce(values))
            write_chunks(real[index], data)
            padded = data + b"\0" * (-len(data) % 4)
            self.owner.values.setdefault(self.name, []).extend(
                int.from_bytes(padded[i:i + 4], "little") for i in range(0, len(padded), 4))
        if self.site is not None:
            eip = int(gdb.parse_and_eval("$eip")) & 0xFFFFFFFF
            gdb.execute(f"set $esp = {esp + (4 * self.args if self.convention == 'stdcall' else 0)}")
            gdb.execute(f"set $eip = {eip + self.site}")
            pop = None
        else:
            pop = 4 + (4 * self.args if self.convention in ("stdcall", "thiscall") else 0)
        if pop is None:
            pass
        elif 0x00400000 <= ret < 0x00700000:
            gdb.execute(f"set $esp = {esp + pop}")
            gdb.execute(f"set $eip = {ret}")
        else:
            # the stubbed function was entered straight from gdb's call (a tail jump from the forced function): the return address is the
            # dummy-call breakpoint, which gdb steps over when execution is simply resumed there.  Return through a real `ret` instead.
            write_chunks(esp + pop - 4, ret.to_bytes(4, "little"))
            gdb.execute(f"set $esp = {esp + pop - 4}")
            gdb.execute(f"set $eip = {RET_TRAMPOLINE}")
        if self.owner.trace is not None and os.environ.get("P410_STUBTRACE") == "2":
            twin = sum(1 for probe in self.owner._probes if getattr(probe, "address", None) == self.address and probe.enabled)
            now_eip, now_esp = (int(gdb.parse_and_eval(r)) & 0xFFFFFFFF for r in ("$eip", "$esp"))
            self.owner.trace(f"      -> eip={now_eip:#x} esp={now_esp:#x} stubs-here={twin}")      # (STUBTRACE=2: where the stub left the call)
        if self.result is not None:
            if callable(self.result):
                value = int(self.result(values)) & 0xFFFFFFFF
                self.owner.values.setdefault(self.name, []).append(value)
            else:
                value = int(self.result) & 0xFFFFFFFF
            gdb.execute(f"set $eax = {value}")
        if self.ret_arg is not None:
            gdb.execute(f"set $eax = {real[self.ret_arg]}")
        return False


RET_TRAMPOLINE = 0x0040101E   # a `ret` instruction in the executable
CHUNK = 8  # the Wine debug stub mangles or stalls on long memory writes: write a few bytes at a time


def write_chunks(address: int, data: bytes) -> None:
    for i in range(0, len(data), CHUNK):
        mem.write(address + i, data[i:i + CHUNK])


class ForcedCalls:
    def __init__(self, regions):
        self.regions = list(regions)
        self.trace = None  # optional callable(str): every stubbed service call, as it happens
        self.verify_pokes = True
        self.check_every = 25
        self.runs = 0
        self.active = False
        self.log: list[dict] = []
        self.values: dict[str, list[int]] = {}
        self._probes = []

    # ---- memory ---------------------------------------------------------------------------
    def snapshot(self) -> dict[int, bytes]:
        return {base: mem.bytes(base, size) for base, size in self.regions}

    def restore(self, snap: dict[int, bytes]) -> None:
        """Write back only the bytes that changed (large or read-only writes stall the Wine stub)."""
        for base, data in snap.items():
            current = mem.bytes(base, len(data))
            if current == data:
                continue
            i = 0
            n = len(data)
            while i < n:
                if current[i] != data[i]:
                    j = i
                    while j < n and current[j] != data[j]:
                        j += 1
                    write_chunks(base + i, data[i:j])
                    i = j
                else:
                    i += 1

    @staticmethod
    def diff(before: dict[int, bytes], after: dict[int, bytes]) -> list[tuple[int, str]]:
        """Changed bytes as (address, hex) runs of consecutive changed bytes."""
        runs = []
        for base, old in before.items():
            new = after[base]
            i = 0
            n = len(old)
            while i < n:
                if old[i] != new[i]:
                    j = i
                    while j < n and old[j] != new[j]:
                        j += 1
                    runs.append((base + i, new[i:j].hex()))
                    i = j
                else:
                    i += 1
        return runs

    # ---- services -------------------------------------------------------------------------
    def stub(self, address, args, name, convention="cdecl", result=None, when=None, keep=None, text=False, blob=None,
             fill=None, this=False, pokes=None, deref=None, masks=None, writes=None, stack_to=None, ret_arg=None, blank=None, strings=None, site=None):
        self._probes.append(_Stub(self, address, args, name, convention, result, when, keep, text, blob, fill, this,
                                  pokes, deref, masks, writes, stack_to, ret_arg, blank, strings, site))

    def stub_site(self, address, length, args, name, **kw):
        """Phase 4.13b: a stub on the `call` instruction at `address` (`length` bytes long); see the module notes."""
        self.stub(address, args, name, site=length, **kw)

    def stub_all_off(self) -> None:
        for probe in self._probes:
            probe.enabled = False

    # ---- calls ----------------------------------------------------------------------------
    @staticmethod
    def call(address: int, args=(), this: int | None = None, edx: int | None = None) -> int:
        sig = ", ".join("int" for _ in args) or "void"
        expr = f"((int (*)({sig}))0x{address:08x})({', '.join(str(int(a)) for a in args)})"
        if this is not None:
            gdb.execute(f"set $ecx = {this}")
        if edx is not None:
            gdb.execute(f"set $edx = {edx}")      # Phase 4.13a: a fastcall's second register argument
        return int(gdb.parse_and_eval(expr)) & 0xFFFFFFFF

    def _shadow_poke(self, shadow: dict[int, bytearray], address: int, data: bytes) -> None:
        for base, region in shadow.items():
            if base <= address and address + len(data) <= base + len(region):
                region[address - base:address - base + len(data)] = data
                return
        raise KeyError(f"poke at {address:#x} is outside every captured region")

    @staticmethod
    def _shadow_read(shadow: dict[int, bytearray], address: int, size: int) -> bytes:
        for base, region in shadow.items():
            if base <= address and address + size <= base + len(region):
                return bytes(region[address - base:address - base + size])
        raise KeyError(f"{address:#x}")

    def run(self, address: int, args=(), pokes=None, base=None, this: int | None = None, edx: int | None = None, via: int | None = None) -> dict:
        """Poke, call, record, restore.  `pokes` maps address -> bytes.

        Invariant: on entry memory equals `base`.  That lets the pre-state be computed in Python
        (base + pokes) and costs one memory read per case (the post-state); the restore writes
        only the bytes that differ from `base`.
        """
        base = base or self.snapshot()
        # a fixture lists pokes by address, so pokes that overlap would replay in another order
        spans = sorted((a, a + len(d)) for a, d in (pokes or {}).items())
        for (lo1, hi1), (lo2, _hi2) in zip(spans, spans[1:]):
            if lo2 < hi1:
                raise RuntimeError(f"pokes overlap at {lo2:#x} (a poke of {lo1:#x}..{hi1:#x} runs into it)")
        shadow = {b: bytearray(d) for b, d in base.items()}
        for addr, data in (pokes or {}).items():
            self._shadow_poke(shadow, addr, bytes(data))
            write_chunks(addr, bytes(data))
        before = {b: bytes(d) for b, d in shadow.items()}
        if self.verify_pokes:
            # (pokes may overlap, so compare with the final shadow state, not the poke's own bytes)
            for addr, data in (pokes or {}).items():
                expect = self._shadow_read(shadow, addr, len(data))
                if mem.bytes(addr, len(data)) != expect:
                    raise RuntimeError(f"poke at {addr:#x} did not stick")
        self.log = []
        self.values = {}
        self.active = True
        error = None
        value = None
        timer = None
        watchdog = float(os.environ.get("P410_WATCHDOG", "0") or 0)
        if watchdog > 0:
            import signal
            import threading
            timer = threading.Timer(watchdog, lambda: os.kill(os.getpid(), signal.SIGINT))
            timer.daemon = True
            timer.start()
        try:
            value = self.call(via or address, args, this=this, edx=edx)      # (Phase 4.14a: `via` is a trampoline that calls `address` under another x87 control word)
        except gdb.error as exc:
            error = str(exc)
            try:
                # Phase 4.10d: where the retail code faulted (the first thing to look at when a spec built a bad object)
                esp = int(gdb.parse_and_eval('$esp')) & 0xFFFFFFFF
                error += f" | eip={int(gdb.parse_and_eval('$eip')) & 0xFFFFFFFF:#x} esp={esp:#x}"
                # Phase 4.10e: the words on top of the stack (a jump to a bad address leaves the call's return address there)
                error += " stack=" + ",".join(f"{mem.u32(esp + 4 * i):#x}" for i in range(6 if not os.environ.get("P410_BT") else 40))
                # Phase 4.10e2: P410_BT=1 adds gdb's backtrace of the stopped call (with P410_NOUNWIND=1 the frames are still there)
                if os.environ.get("P410_BT"):
                    error += " bt=" + gdb.execute("bt 14", to_string=True).replace(chr(10), " ; ")
                    # (the registers: what the faulting instruction was reading through; P410_BT_OFFSET=0x4c0 also dumps twelve words of `this` from that offset)
                    if this is not None and os.environ.get("P410_BT_OFFSET"):
                        offset = int(os.environ["P410_BT_OFFSET"], 0)
                        error += f" this[{offset:#x}..]=" + ",".join(f"{mem.u32(this + offset + 4 * i):#x}" for i in range(12))
                    error += " regs=" + ",".join(f"{r}={int(gdb.parse_and_eval('$' + r)) & 0xFFFFFFFF:#x}" for r in ("eax", "ebx", "ecx", "edx", "esi", "edi", "ebp"))
            except gdb.error:
                pass
        finally:
            self.active = False
            if timer is not None:
                timer.cancel()
        after = self.snapshot()
        record = {
            "fn": address,
            "args": [int(a) & 0xFFFFFFFF for a in args],
            "this": this,
            "pokes": {f"{a:08x}": bytes(d).hex() for a, d in (pokes or {}).items()},
            "ret": value,
            "writes": [[f"{a:08x}", h] for a, h in self.diff(before, after)],
            "calls": self.log,
            "values": self.values,
        }
        if error is not None:
            record["error"] = error
        self.write_back(after, base)
        self.runs += 1
        if self.runs % self.check_every == 1:
            # a cheap guard for the invariant "memory equals base between cases"
            drift = self.diff(base, self.snapshot())
            if drift:
                raise RuntimeError(f"memory drifted from the base snapshot after the restore: {drift[:4]}")
        return record

    @staticmethod
    def write_back(current: dict[int, bytes], target: dict[int, bytes]) -> None:
        """Make memory equal `target`, knowing it currently equals `current` (writes only the differences)."""
        for base, data in target.items():
            now = current[base]
            if now == data:
                continue
            i = 0
            n = len(data)
            while i < n:
                if now[i] != data[i]:
                    j = i
                    while j < n and now[j] != data[j]:
                        j += 1
                    write_chunks(base + i, data[i:j])
                    i = j
                else:
                    i += 1
