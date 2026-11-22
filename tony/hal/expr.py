"""Parse and normalize the symbolic expressions written by `tony.hal.facts`.

Expressions are tuples:

    ("const", n)          integer constant
    ("g", addr)           value of the global dword at addr
    ("s", off)            value of the stack slot at off (S4 = first stack argument)
    ("sa", off)           address of that stack slot
    ("pa", n)             address of parameter n's own stack slot
    ("arg", n)            n-th parameter of the enclosing function
    ("ret", addr)         return value of a direct call to addr
    ("reti", site)        return value of the indirect call at site
    ("load", e)           *(e)
    ("add", a, b)         a + b
    ("mul", a, b)         a * b
    ("phi", (e, ...))     control-flow merge
    ("op", name, (...))   any other p-code operation
    ("unk", text)         unresolved input
"""

from __future__ import annotations

import functools

Expr = tuple
Location = tuple
_MASK = 0xFFFFFFFF


class ParseError(ValueError):
    pass


@functools.lru_cache(maxsize=200_000)
def parse(text: str) -> Expr:
    parser = _Parser(text)
    value = parser.expr()
    if parser.pos != len(text):
        raise ParseError(f"trailing input at {parser.pos}: {text!r}")
    return normalize(value)


class _Parser:
    def __init__(self, text: str):
        self.text = text
        self.pos = 0

    def peek(self, token: str) -> bool:
        return self.text.startswith(token, self.pos)

    def take(self, token: str) -> None:
        if not self.peek(token):
            raise ParseError(f"expected {token!r} at {self.pos}: {self.text!r}")
        self.pos += len(token)

    def word(self) -> str:
        start = self.pos
        while self.pos < len(self.text) and (self.text[self.pos].isalnum() or self.text[self.pos] in "_-"):
            self.pos += 1
        return self.text[start:self.pos]

    def balanced(self) -> str:
        """Consume a parenthesized Ghidra varnode such as '(register, 0x0, 4)'."""

        start = self.pos
        depth = 0
        while self.pos < len(self.text):
            char = self.text[self.pos]
            self.pos += 1
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
                if depth == 0:
                    break
        return self.text[start:self.pos]

    def expr(self) -> Expr:
        if self.peek("*("):
            self.take("*(")
            inner = self.expr()
            self.take(")")
            return ("load", inner)
        if self.peek("phi{"):
            self.take("phi{")
            parts = [self.expr()]
            while self.peek("|"):
                self.take("|")
                parts.append(self.expr())
            self.take("}")
            return ("phi", tuple(parts))
        if self.peek("("):
            self.take("(")
            left = self.expr()
            if self.peek("*"):
                self.take("*")
                right = self.expr()
                self.take(")")
                return ("mul", left, right)
            self.take("+")
            middle = self.expr()
            if self.peek("*"):
                self.take("*")
                size = self.expr()
                self.take(")")
                return ("add", left, ("mul", middle, size))
            self.take(")")
            return ("add", left, middle)
        if self.peek("&S"):
            self.take("&S")
            return ("sa", int(self.word()))
        if self.peek("in:"):
            self.take("in:")
            return ("unk", "in:" + self.balanced())
        if self.peek("?") or self.peek("…"):
            char = self.text[self.pos]
            self.pos += 1
            rest = self.balanced() if self.peek("(") else ""
            return ("unk", char + rest)
        if self.peek("0x"):
            return ("const", int(self.word(), 16))
        if self.peek("-0x"):
            self.take("-")
            return ("const", -int(self.word(), 16) & _MASK)
        name = self.word()
        if not name:
            raise ParseError(f"unexpected {self.text[self.pos:self.pos + 10]!r} in {self.text!r}")
        if name.startswith("G0x"):
            return ("g", int(name[1:], 16))
        if name == "SP":
            return ("unk", "SP")
        if name.startswith("S") and name[1:].lstrip("-").isdigit():
            return ("s", int(name[1:]))
        if name.startswith("arg") and name[3:].isdigit():
            return ("arg", int(name[3:]))
        self.take("(")
        arguments = []
        if not self.peek(")"):
            arguments.append(self.expr())
            while self.peek(","):
                self.take(",")
                arguments.append(self.expr())
        self.take(")")
        if name == "ret":
            return ("ret", arguments[0][1])
        if name == "reti":
            return ("reti", arguments[0][1])
        return ("op", name, tuple(arguments))


def normalize(value: Expr) -> Expr:
    kind = value[0]
    if kind == "load":
        inner = normalize(value[1])
        if inner[0] == "const":
            return ("g", inner[1])
        if inner[0] == "sa":
            return ("s", inner[1])
        return ("load", inner)
    if kind in ("add", "mul"):
        left, right = normalize(value[1]), normalize(value[2])
        if left[0] == "const" and right[0] == "const":
            result = left[1] + right[1] if kind == "add" else left[1] * right[1]
            return ("const", result & _MASK)
        if kind == "add" and left[0] == "const":
            left, right = right, left
        if kind == "add" and right == ("const", 0):
            return left
        if kind == "add" and left[0] == "sa" and right[0] == "const":
            return ("sa", left[1] + _signed(right[1]))
        if kind == "add" and left[0] == "add" and left[2][0] == "const" and right[0] == "const":
            return normalize(("add", left[1], ("const", (left[2][1] + right[1]) & _MASK)))
        if kind == "mul" and right == ("const", 1):
            return left
        return (kind, left, right)
    if kind == "phi":
        parts = tuple(sorted({normalize(part) for part in value[1]}, key=repr))
        return parts[0] if len(parts) == 1 else ("phi", parts)
    if kind == "op":
        return ("op", value[1], tuple(normalize(item) for item in value[2]))
    return value


def _signed(value: int) -> int:
    return value - (1 << 32) if value & 0x80000000 else value


def linear(value: Expr) -> tuple[int, list[Expr]]:
    """Split an address expression into (constant, non-constant terms)."""

    if value[0] == "const":
        return value[1], []
    if value[0] == "add":
        left_constant, left_terms = linear(value[1])
        right_constant, right_terms = linear(value[2])
        return (left_constant + right_constant) & _MASK, left_terms + right_terms
    return 0, [value]


def substitute(value: Expr, arguments: dict[int, Expr]) -> Expr | None:
    """Replace ("arg", n) by the caller's argument expression; None if unknown."""

    kind = value[0]
    if kind == "arg":
        return arguments.get(value[1])
    if kind in ("load",):
        inner = substitute(value[1], arguments)
        return None if inner is None else normalize(("load", inner))
    if kind in ("add", "mul"):
        left = substitute(value[1], arguments)
        right = substitute(value[2], arguments)
        return None if left is None or right is None else normalize((kind, left, right))
    if kind in ("s", "sa", "pa"):
        return None  # a callee's own stack slots mean nothing to the caller
    if kind == "phi":
        parts = [substitute(part, arguments) for part in value[1]]
        return None if any(part is None for part in parts) else normalize(("phi", tuple(parts)))
    if kind == "op":
        parts = [substitute(part, arguments) for part in value[2]]
        return None if any(part is None for part in parts) else ("op", value[1], tuple(parts))
    return value


def is_local(value: Expr) -> bool:
    """True when the expression depends on function-local names."""

    kind = value[0]
    if kind in ("arg", "s", "sa", "pa", "reti", "unk"):
        return True
    if kind in ("const", "g", "ret"):
        return False
    if kind == "load":
        return is_local(value[1])
    if kind in ("add", "mul"):
        return is_local(value[1]) or is_local(value[2])
    if kind == "phi":
        return any(is_local(part) for part in value[1])
    if kind == "op":
        return any(is_local(part) for part in value[2])
    return True


def location(address: Expr, function: int) -> Location | None:
    """Storage location denoted by an address expression inside `function`.

    ("G", addr)               a global dword
    ("T", base)               an element of a global table (base + index*k)
    ("L", function, offset)   a stack slot (parameters included)
    ("F", scope, base, off)   field `off` of the object `base` points to;
                              scope is the function when `base` is local
    """

    if address[0] == "sa":
        return ("L", function, address[1])
    if address[0] == "pa":  # address of a parameter's own slot
        return ("P", function, address[1])
    constant, terms = linear(address)
    if not terms:
        return ("G", constant)
    if len(terms) == 1 and terms[0][0] != "mul":
        base = terms[0]
        return ("F", function if is_local(base) else None, base, constant)
    if 0x00400000 <= constant < 0x03000000:
        return ("T", constant)
    return None


def value_location(value: Expr, function: int) -> Location | None:
    """Storage location a value expression was read from, if any."""

    kind = value[0]
    if kind == "g":
        return ("G", value[1])
    if kind == "s":
        return ("L", function, value[1])
    if kind == "arg":
        return ("P", function, value[1])
    if kind == "load":
        return location(value[1], function)
    return None


def render(value: Expr | None) -> str:
    if value is None:
        return "?"
    kind = value[0]
    if kind == "const":
        return hex(value[1])
    if kind == "g":
        return f"[{value[1]:#x}]"
    if kind == "s":
        return f"stack[{value[1]}]"
    if kind == "sa":
        return f"&stack[{value[1]}]"
    if kind == "pa":
        return f"&arg{value[1]}"
    if kind == "arg":
        return f"arg{value[1]}"
    if kind == "ret":
        return f"ret({value[1]:#x})"
    if kind == "reti":
        return f"reti({value[1]:#x})"
    if kind == "load":
        return f"*({render(value[1])})"
    if kind == "add":
        return f"({render(value[1])}+{render(value[2])})"
    if kind == "mul":
        return f"({render(value[1])}*{render(value[2])})"
    if kind == "phi":
        return "phi{" + "|".join(render(part) for part in value[1]) + "}"
    if kind == "op":
        return f"{value[1]}(" + ",".join(render(part) for part in value[2]) + ")"
    return str(value[1])


def render_location(where: Location) -> str:
    kind = where[0]
    if kind == "G":
        return f"[{where[1]:#010x}]"
    if kind == "T":
        return f"[{where[1]:#010x} + i*k]"
    if kind == "L":
        return f"{where[1]:#010x}:stack[{where[2]}]"
    if kind == "P":
        return f"{where[1]:#010x}:arg{where[2]}"
    if kind == "R":
        return f"ret({where[1]:#010x})"
    if kind == "F":
        scope = f"{where[1]:#010x}:" if where[1] is not None else ""
        return f"{scope}{render(where[2])}->{where[3]:#x}"
    return repr(where)
