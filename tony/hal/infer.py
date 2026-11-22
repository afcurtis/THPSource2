"""Interface-pointer type propagation over exported Ghidra facts.

Seeds are the factory imports (DirectDrawCreateEx, DirectInputCreateA,
DirectSoundCreate, DirectPlayLobbyCreateA, CoCreateInstance) and reviewed
root declarations from re/config/hal_boundary.yml. Types then flow to a
fixpoint through:

- COM producer out-parameters (CreateSurface, CreateDevice, QueryInterface
  by IID, ...);
- stores into globals, tables, stack slots and object fields;
- direct-call arguments (caller value -> callee parameter);
- object fields reached through parameters, in both directions across call
  sites (callee `arg0->0x4` <-> caller `(E + 0x4)`);
- return values.

Every indirect call shaped `(*obj)->vtable[k]` whose object gets a type is a
COM call site; the remaining vtable-shaped calls are reported for review.
"""

from __future__ import annotations

import collections
import dataclasses
from typing import Any

from . import com
from .expr import Expr, Location, location, normalize, parse, render, render_location, value_location

MAX_PASSES = 40


@dataclasses.dataclass
class Call:
    site: int
    function: int
    kind: str  # "d" direct, "i" indirect
    target: Expr | int
    args: list[Expr]


@dataclasses.dataclass
class ComSite:
    site: int
    function: int
    interface: str
    offset: int
    this: Expr

    @property
    def method(self) -> str:
        return com.method(self.interface, self.offset)


@dataclasses.dataclass
class ImportSite:
    site: int
    function: int
    api: str
    args: list[Expr]


@dataclasses.dataclass
class Result:
    com_sites: list[ComSite]
    import_sites: list[ImportSite]
    untyped_vtable: list[tuple[int, int, Expr, int, str]]  # site, function, object, offset, call shape
    other_indirect: list[tuple[int, int, Expr]]
    typed: dict[Location, dict[str, str]]  # location -> interface -> first evidence
    conflicts: list[str]
    leaks: list[str]
    guid_args: dict[int, str]
    callbacks: list[tuple[int, int, str, int]]  # site, function, registering API, callback entry
    not_com: list[tuple[int, int, str]]  # reviewed vtable-shaped calls that are not COM


def vtable_shape(target: Expr) -> tuple[Expr, int] | None:
    """(object, vtable offset) for `*(*(obj) + off)` / `*(*(obj))`."""

    if target[0] != "load":
        return None
    slot = target[1]
    if slot[0] == "load":
        return slot[1], 0
    if slot[0] == "add" and slot[1][0] == "load" and slot[2][0] == "const":
        return slot[1][1], slot[2][1]
    return None


class Engine:
    def __init__(self, facts: dict[str, Any], image, roots: list[dict[str, Any]] | None = None):
        self.image = image
        self.functions = {record["a"]: record for record in facts["functions"]}
        self.iat = {int(k, 16): v for k, v in facts["iat"].items()}
        self.thunks = {int(k, 16): v for k, v in facts["thunks"].items()}
        self.calls: dict[int, list[Call]] = collections.defaultdict(list)
        self.callers: dict[int, list[Call]] = collections.defaultdict(list)
        self.stores: dict[int, list[tuple[int, Expr, Expr]]] = collections.defaultdict(list)
        self.returns: dict[int, list[Expr]] = collections.defaultdict(list)
        self.typed: dict[Location, dict[str, str]] = collections.defaultdict(dict)
        # (location, interface) typings a callee only inherited from its
        # callers; they never flow back out to other callers.
        self.inherited: set[tuple[Location, str]] = set()
        self.conflicts: list[str] = []
        self.guid_args: dict[int, str] = {}
        self.callbacks: dict[tuple[int, int], str] = {}  # (site, callback) -> registering API
        self.tables: list[tuple[int, int]] = [
            (int(root["table"]), int(root["size"])) for root in roots or [] if "table" in root
        ]
        # Reviewed per-site facts for calls whose object the decompiler cannot name.
        self.site_types: dict[int, str] = {
            int(root["site"]): root["interface"] for root in roots or [] if "site" in root and "interface" in root
        }
        self.not_com: dict[int, str] = {
            int(root["site"]): root["not_com"] for root in roots or [] if "site" in root and "not_com" in root
        }
        self.param_slots: dict[int, dict[int, int]] = {
            address: {int(storage[1:]): index for index, storage in enumerate(record.get("params", []))
                      if storage.startswith("S")}
            for address, record in self.functions.items()
        }
        self.vtable_evidence: dict[int, list[str]] = {
            int(site): value for record in self.functions.values()
            for site, value in record.get("vtable", {}).items()
        }
        for address, record in self.functions.items():
            for row in record["calls"]:
                site, kind, target, args = row[:4]
                arguments = [self.localize(parse(arg), address) for arg in args]
                if not arguments and len(row) > 4:  # instruction-level fallback, nearest push first
                    arguments = [("const", int(item, 16)) if item.startswith("0x") else ("unk", item)
                                 for item in row[4]]
                call = Call(site, address, kind,
                            int(target, 16) if kind == "d" else self.localize(parse(target), address), arguments)
                self.calls[address].append(call)
                if kind == "d":
                    self.callers[call.target].append(call)
            for site, dest, value in record["stores"]:
                self.stores[address].append((site, self.localize(parse(dest), address),
                                             self.localize(parse(value), address)))
            for value in record["ret"]:
                self.returns[address].append(self.localize(parse(value), address))
        for root in roots or []:
            self.declare(root)

    def localize(self, value: Expr, function: int) -> Expr:
        """Name a parameter's own stack slot as the parameter (a callee may
        reuse an argument slot as a local, e.g. as an out-parameter)."""

        slots = self.param_slots.get(function)
        if not slots:
            return value
        kind = value[0]
        if kind == "s" and value[1] in slots:
            return ("arg", slots[value[1]])
        if kind == "sa" and value[1] in slots:
            return ("pa", slots[value[1]])
        if kind in ("load",):
            return normalize(("load", self.localize(value[1], function)))
        if kind in ("add", "mul"):
            return normalize((kind, self.localize(value[1], function), self.localize(value[2], function)))
        if kind == "phi":
            return normalize(("phi", tuple(self.localize(part, function) for part in value[1])))
        if kind == "op":
            return ("op", value[1], tuple(self.localize(part, function) for part in value[2]))
        return value

    # ------------------------------------------------------------------ typing

    def declare(self, root: dict[str, Any]) -> None:
        if "site" in root:
            return  # handled by site_types / not_com
        where = self.declared_location(root)
        self.add(where, root["interface"], f"declared: {root.get('evidence', 'reviewed root')}")

    def declared_location(self, root: dict[str, Any]) -> Location:
        """Declarations name a global, a table, or a function-scoped object expression."""

        if "global" in root:
            return ("G", int(root["global"]))
        if "table" in root:
            return ("T", int(root["table"]))
        function = int(root["function"])
        value = parse(root["value"])
        where = value_location(value, function)
        if where is None:
            raise ValueError(f"root {root} does not name a storage location")
        return where

    def add(self, where: Location | None, interface: str, evidence: str, *, inherited: bool = False) -> bool:
        where = self.canon(where)
        if where is None:
            return False
        current = self.typed[where]
        if interface in current:
            if not inherited and (where, interface) in self.inherited:
                self.inherited.discard((where, interface))  # now established by the callee itself
                current[interface] = evidence
                return True
            return False
        if inherited:
            self.inherited.add((where, interface))
        if current and not self._compatible(set(current), interface):
            self.conflicts.append(f"{render_location(where)}: {sorted(current)} vs {interface} ({evidence})")
        current[interface] = evidence
        return True

    @staticmethod
    def _compatible(existing: set[str], interface: str) -> bool:
        return all(com.FAMILY.get(item) == com.FAMILY.get(interface) for item in existing)

    def type_of(self, value: Expr, function: int, depth: int = 0) -> set[str]:
        if depth > 6:
            return set()
        kind = value[0]
        if kind == "phi":
            result: set[str] = set()
            for part in value[1]:
                result |= self.type_of(part, function, depth + 1)
            return result
        if kind == "ret":
            return set(self.typed.get(("R", value[1]), {}))
        if kind == "load" and value[1][0] == "phi":
            # loop-carried table cursors: *(phi{base + i*k | cursor + 4})
            result = set()
            for part in value[1][1]:
                result |= self.type_of(("load", part), function, depth + 1)
            return result
        where = self.canon(value_location(value, function))
        return set(self.typed.get(where, {})) if where is not None else set()

    def object_types(self, call: Call, obj: Expr) -> set[str]:
        declared = self.site_types.get(call.site)
        return {declared} if declared else self.type_of(obj, call.function)

    def canon(self, where: Location | None) -> Location | None:
        """Map an interior table address onto its declared table."""

        if where is None or where[0] not in ("T", "G"):
            return where
        for base, size in self.tables:
            if base <= where[1] < base + size:
                return ("T", base)
        return where

    # --------------------------------------------------------------- analysis

    def guid_at(self, value: Expr) -> str | None:
        if value[0] != "const":
            return None
        try:
            data = self.image.read(value[1], 16)
        except Exception:  # noqa: BLE001 - not file-backed
            return None
        return com.guid_name(data)

    def import_name(self, call: Call) -> str | None:
        if call.kind == "d":  # thunk entry, or the IAT slot of `call [IAT]`
            return self.thunks.get(call.target) or self.iat.get(call.target)
        if call.target[0] == "g":
            return self.iat.get(call.target[1])
        return None

    def _produce(self, call: Call, out_index: int, result: object, evidence: str) -> bool:
        if out_index >= len(call.args):
            return False
        interface = result
        if isinstance(result, int):  # IID argument index
            if result >= len(call.args):
                return False
            name = self.guid_at(call.args[result])
            if name is not None:
                self.guid_args[call.site] = name
            interface = com.IID_INTERFACE.get(name or "")
            if interface is None:
                return False
        return self.add(location(call.args[out_index], call.function), str(interface), evidence)

    def step(self) -> bool:
        changed = False
        for function, calls in self.calls.items():
            for call in calls:
                if call.kind == "cb":
                    continue
                api = self.import_name(call)
                if api is not None:
                    factory = com.FACTORIES.get(api)
                    if factory is not None:
                        changed |= self._produce(call, factory[0], factory[1], f"{api} at {call.site:#x}")
                    changed |= self._register_callback(call, api, call.args, api)
                    continue
                if call.kind == "i":
                    shape = vtable_shape(call.target)
                    if shape is None:
                        continue
                    obj, offset = shape
                    for interface in self.object_types(call, obj):
                        name = com.method(interface, offset)
                        evidence = f"{interface}::{name} at {call.site:#x}"
                        changed |= self._register_callback(call, (interface, name), call.args[1:],
                                                           f"{interface}::{name}")
                        if name == "QueryInterface":
                            changed |= self._produce(Call(call.site, function, "i", call.target, call.args[1:]),
                                                     1, 0, evidence)
                            continue
                        producer = com.PRODUCERS.get((interface, name))
                        if producer is not None:
                            changed |= self._produce(Call(call.site, function, "i", call.target, call.args[1:]),
                                                     producer[0], producer[1], evidence)
                    continue
        # every function, including leaves without calls (getters, setters)
        for function in self.functions:
            for site, dest, value in self.stores.get(function, []):
                for interface in self.type_of(value, function):
                    changed |= self.add(location(dest, function), interface, f"store at {site:#x}")
            changed |= self._type_return(function)
        changed |= self._parameters_by_agreement()
        changed |= self._fields_across_calls()
        return changed

    def _agreement(self, callee: int, source) -> set[str]:
        """Interfaces the typed callers of `callee` agree on.

        `source(call)` returns the caller-side value's interfaces (empty when
        unknown, which is neutral). A caller typed with a different interface
        removes that interface. The result is inherited by the callee and never
        flows back to other callers (see `add(..., inherited=True)`), which is
        what keeps a generic helper called with an interface pointer from
        typing the unrelated objects of its other callers.
        """

        common: set[str] | None = None
        for call in self.callers.get(callee, []):
            interfaces = source(call)
            if not interfaces:
                continue
            common = set(interfaces) if common is None else common & interfaces
            if not common:
                return set()
        return common or set()

    def _parameters_by_agreement(self) -> bool:
        """Direct calls and callbacks: argument value -> callee parameter."""

        changed = False
        for callee, calls in self.callers.items():
            if callee not in self.functions:
                continue
            for index in range(max(len(call.args) for call in calls)):
                def source(call: Call, index: int = index) -> set[str]:
                    if index >= len(call.args):
                        return set()
                    return self.type_of(call.args[index], call.function)

                for interface in self._agreement(callee, source):
                    changed |= self.add(("P", callee, index), interface, f"argument {index} from the typed callers",
                                        inherited=True)
        return changed

    def _register_callback(self, call: Call, key: object, args: list[Expr], label: str) -> bool:
        """Model a platform -> game callback(context) as a pseudo call."""

        spec = com.CALLBACKS.get(key)
        if spec is None or spec[0] >= len(args) or args[spec[0]][0] != "const":
            return False
        target = args[spec[0]][1]
        if target not in self.functions or (call.site, target) in self.callbacks:
            return False
        self.callbacks[(call.site, target)] = label
        pseudo_args: list[Expr] = []
        if spec[1] is not None and spec[2] is not None and spec[1] < len(args):
            pseudo_args = [("unk", "platform")] * spec[2] + [args[spec[1]]]
        pseudo = Call(call.site, call.function, "cb", target, pseudo_args)
        self.calls[call.function].append(pseudo)
        self.callers[target].append(pseudo)
        self._offset_cache = None
        return True

    def _type_return(self, function: int) -> bool:
        """A return is typed only when every returned value agrees (or is NULL).

        Functions such as Audio_PlaySound return a voice handle on most paths
        and a buffer pointer on one; typing their result would leak an
        interface type into unrelated game objects.
        """

        values = self.returns.get(function, [])
        if not values:
            return False
        common: set[str] | None = None
        for value in values:
            parts = value[1] if value[0] == "phi" else (value,)
            for part in parts:
                if part == ("const", 0):
                    continue
                interfaces = self.type_of(part, function)
                if not interfaces:
                    return False
                common = interfaces if common is None else common & interfaces
        changed = False
        for interface in common or ():
            changed |= self.add(("R", function), interface, f"returned by {function:#x}")
        return changed

    def _fields_across_calls(self) -> bool:
        """Callee field `argN->off` <-> caller location `(argsN + off)`."""

        changed = False
        for where, interfaces in list(self.typed.items()):
            if where[0] != "F" or where[1] is None or where[2][0] != "arg":
                continue
            callee, index, offset = where[1], where[2][1], where[3]
            for call in self.callers.get(callee, []):
                if index >= len(call.args):
                    continue
                target = location(normalize(("add", call.args[index], ("const", offset))), call.function)
                for interface in list(interfaces):
                    if (where, interface) not in self.inherited:
                        changed |= self.add(target, interface, f"field via call {call.site:#x}")
        # caller -> callee, for every field the callee dereferences, when every
        # caller's object has that field typed alike
        for callee, calls in self.callers.items():
            if callee not in self.functions:
                continue
            for index in range(max(len(call.args) for call in calls)):
                for offset in self._field_offsets(callee, index):
                    def source(call: Call, index: int = index, offset: int = offset) -> set[str]:
                        if index >= len(call.args):
                            return set()
                        where = self.canon(location(normalize(("add", call.args[index], ("const", offset))),
                                                    call.function))
                        return set(self.typed.get(where, {})) if where is not None else set()

                    for interface in self._agreement(callee, source):
                        changed |= self.add(("F", callee, ("arg", index), offset), interface,
                                            "field from the typed callers", inherited=True)
        return changed

    _offset_cache: dict[tuple[int, int], set[int]] | None = None

    def _field_offsets(self, function: int, index: int) -> set[int]:
        """Offsets at which `function` dereferences its parameter `index`."""

        if self._offset_cache is None:
            cache: dict[tuple[int, int], set[int]] = collections.defaultdict(set)

            def visit(value: Expr, owner: int) -> None:
                kind = value[0]
                if kind == "load":
                    where = location(value[1], owner)
                    if where and where[0] == "F" and where[2][0] == "arg":
                        cache[(owner, where[2][1])].add(where[3])
                    visit(value[1], owner)
                elif kind in ("add", "mul"):
                    visit(value[1], owner)
                    visit(value[2], owner)
                elif kind == "phi":
                    for part in value[1]:
                        visit(part, owner)
                elif kind == "op":
                    for part in value[2]:
                        visit(part, owner)

            for owner, calls in self.calls.items():
                for call in calls:
                    if call.kind == "i":
                        visit(call.target, owner)
                    for argument in call.args:
                        visit(argument, owner)
                        where = location(argument, owner)  # out-parameter addresses
                        if where and where[0] == "F" and where[2][0] == "arg":
                            cache[(owner, where[2][1])].add(where[3])
            for owner, stores in self.stores.items():
                for _site, dest, value in stores:
                    visit(("load", dest), owner)
                    visit(value, owner)
            self._offset_cache = cache
        return self._offset_cache.get((function, index), set())

    def run(self) -> Result:
        for _ in range(MAX_PASSES):
            if not self.step():
                break
        com_sites: list[ComSite] = []
        import_sites: list[ImportSite] = []
        untyped: list[tuple[int, int, Expr, int, str]] = []
        not_com: list[tuple[int, int, str]] = []
        other: list[tuple[int, int, Expr]] = []
        for function, calls in self.calls.items():
            for call in calls:
                if call.kind == "cb":
                    continue
                api = self.import_name(call)
                if api is not None:
                    import_sites.append(ImportSite(call.site, function, api, call.args))
                    continue
                if call.kind != "i":
                    continue
                shape = vtable_shape(call.target)
                if shape is None:
                    other.append((call.site, function, call.target))
                    continue
                if call.site in self.not_com:
                    not_com.append((call.site, function, self.not_com[call.site]))
                    continue
                interfaces = self.object_types(call, shape[0])
                if not interfaces:
                    untyped.append((call.site, function, shape[0], shape[1],
                                    call_shape(call.args, shape[0], self.vtable_evidence.get(call.site))))
                    continue
                for interface in sorted(interfaces):
                    com_sites.append(ComSite(call.site, function, interface, shape[1], shape[0]))
        leaks = self._leaks()
        return Result(
            sorted(com_sites, key=lambda item: (item.site, item.interface)),
            sorted(import_sites, key=lambda item: item.site),
            sorted(untyped, key=lambda row: row[:2]), sorted(other, key=lambda row: row[:2]), dict(self.typed),
            self.conflicts, leaks, self.guid_args,
            sorted((site, next(c.function for c in self.callers[target] if c.site == site and c.kind == "cb"),
                    label, target) for (site, target), label in self.callbacks.items()),
            sorted(not_com),
        )

    def _leaks(self) -> list[str]:
        """Typed values stored where no location can be named."""

        leaks = []
        for function, stores in self.stores.items():
            for site, dest, value in stores:
                if self.type_of(value, function) and location(dest, function) is None:
                    leaks.append(f"{site:#x}: {render(value)} stored through {render(dest)}")
        return leaks


def call_shape(call_args: list[Expr], obj: Expr, evidence: list[str] | None = None) -> str:
    """'com' when `this` is the first stack argument (stdcall COM), 'thiscall'
    when it is not (a C++ virtual with `this` in ECX) or when nothing at all is
    pushed, 'unknown' when neither the instructions nor the decompiler decide.

    Instruction evidence wins: the vtable loaded from ECX without pushing ECX
    is a C++ virtual; the vtable loaded from the register pushed last is COM.
    """

    if evidence is not None:
        register, pushed = evidence
        if register == "ecx" and pushed != "ecx":
            return "thiscall"
        if register not in ("?", "") and register == pushed:
            return "com"
    if not call_args:
        return "thiscall"  # no stack argument at all, so `this` cannot be on the stack
    if call_args[0][0] == "unk":
        return "unknown"
    return "com" if call_args[0] == obj else "thiscall"


def overrides_for(result: Result) -> dict[int, tuple[int, int]]:
    """{site: (function, stack arguments incl. this)} for every typed COM call."""

    wanted: dict[int, tuple[int, int]] = {}
    for site in result.com_sites:
        count = com.arity(site.interface, site.offset)
        if count is not None:
            wanted[site.site] = (site.function, count + 1)
    return wanted


def describe_location(where: Location) -> str:
    return render_location(where)
