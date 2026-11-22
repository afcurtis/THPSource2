"""Match PSX demo symbols (thps2-demo-decomp) onto canonical PC functions.

The PC and PSX builds share most game code but not an instruction set, so matching
uses structure rather than bytes:

``anchor``          a community name (PARTYMOD / Cumulative Patch) equals the PSX symbol
``align+callgraph`` per-source-file link-order alignment, confirmed by >=1 call edge whose
                    callee is already matched on both sides
``align+strings``   alignment confirmed by a shared string literal
``align+sandwich``  alignment whose PSX and PC neighbours on both sides are matched pairs
``callgraph``       voted from ordered callee / unique-caller correspondences only

Order-only alignments are never emitted. ``tony xref psx --holdout`` re-runs the matcher
with a random subset of anchors hidden and reports how many it recovers correctly.
"""

from __future__ import annotations

import json
import math
import random
import re
import statistics
from collections import Counter, defaultdict
from itertools import pairwise
from pathlib import Path

from .common import CANONICAL, config, load_features, write_rows
from .community import load_community, require_reference
from .units import load_units

REFERENCE = "thps2_demo_decomp"
STRONG = {"anchor", "align+callgraph", "align+strings", "align+sandwich"}
_DEFINITION = re.compile(
    r"^(?!\s)(?!static\s+inline|inline|#|typedef|struct|class|enum|extern)[^;{}()=]*?"
    r"\b(?P<name>[A-Za-z_~][\w~]*(?:::[A-Za-z_~][\w~]*)?)\s*\((?P<args>[^;{}]*)\)\s*(?:const)?\s*\{?\s*$")
_CALL = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
_STRING = re.compile(r'"((?:[^"\\]|\\.)*)"')
_KEYWORDS = {"if", "while", "for", "switch", "return", "sizeof", "do", "else", "case"}


def load_psx_symbols(root: Path) -> list[dict]:
    report = json.loads((root / "build/report.json").read_text(encoding="utf-8"))
    rows = []
    for unit in report["units"]:
        for function in unit.get("functions", []):
            demangled = (function.get("metadata") or {}).get("demangled_name") or function["name"]
            rows.append({"unit": unit["name"], "name": function["name"], "demangled": demangled,
                         "base": demangled.split("(")[0].strip(), "addr": int(function["address"]),
                         "size": int(function["size"])})
    rows.sort(key=lambda r: r["addr"])
    for index, row in enumerate(rows):
        row["i"] = index
        row["short"] = row["base"].split("::")[-1]
    return rows


def parse_bodies(root: Path, symbols: list[dict]) -> None:
    """Attach ordered callee indices and string literals parsed from reconstructed units."""

    by_short = defaultdict(list)
    for row in symbols:
        by_short[row["short"]].append(row["i"])
    by_unit_base = {(r["unit"], r["base"]): r["i"] for r in symbols}
    bodies = {}
    for path in sorted((root / "src").glob("*.cpp")):
        unit = path.stem.upper()
        lines = path.read_text(errors="ignore").splitlines()
        i = 0
        while i < len(lines):
            match = _DEFINITION.match(lines[i])
            if not match:
                i += 1
                continue
            start = i
            if "{" not in lines[i]:
                start = i + 1
                if start >= len(lines) or lines[start].strip() != "{":
                    i += 1
                    continue
            depth, k, body = 0, start, []
            while k < len(lines):
                depth += lines[k].count("{") - lines[k].count("}")
                body.append(lines[k])
                if depth <= 0 and "}" in lines[k]:
                    break
                k += 1
            key = (unit, match.group("name"))
            if key in by_unit_base:
                text = "\n".join(body)
                bodies[by_unit_base[key]] = (
                    [c for c in _CALL.findall(text) if c not in _KEYWORDS],
                    {s for s in _STRING.findall(text) if len(s) >= 4},
                )
            i = k + 1
    for row in symbols:
        calls, strings = bodies.get(row["i"], ([], set()))
        sequence = [by_short[c][0] for c in calls if len(by_short.get(c, [])) == 1 and by_short[c][0] != row["i"]]
        row["callseq"] = list(dict.fromkeys(sequence))
        row["callees"] = sorted(set(sequence))
        row["strs"] = strings
        row["parsed"] = row["i"] in bodies


def community_anchors(symbols: list[dict]) -> dict[int, int]:
    lookup = defaultdict(set)
    for row in symbols:
        for key in (row["base"], row["base"].replace("::", "_"), row["base"].replace("::", "__")):
            lookup[key.lower()].add(row["i"])
    candidates: dict[int, set[int]] = defaultdict(set)
    for entry in load_community():
        if entry["kind"] != "function" or entry["canonical"] is None:
            continue
        matches = lookup.get(entry["name"].lower(), set())
        if len(matches) == 1:
            candidates[next(iter(matches))].add(entry["canonical"])
    return {p: next(iter(s)) for p, s in candidates.items() if len(s) == 1}


class PsxMatcher:
    def __init__(self, symbols: list[dict], functions: list[dict], units: dict[int, tuple[str, str]],
                 anchors: dict[int, int]):
        self.P = symbols
        self.F = {f["a"]: f for f in functions}
        self.match = dict(anchors)
        self.rmatch = {a: p for p, a in self.match.items()}
        self.method = {p: "anchor" for p in self.match}
        self.tentative: dict[int, int] = {}
        self.rejected: dict[int, int] = {}
        ratios = [math.log((self.F[a]["size"] + 8) / (self.P[p]["size"] + 8)) for p, a in anchors.items()
                  if a in self.F]
        self.K = statistics.median(ratios)
        self.spread = statistics.pstdev(ratios)
        self.pc_sorted = sorted(self.F)
        self.pc_index = {a: k for k, a in enumerate(self.pc_sorted)}
        self.psx_units = defaultdict(list)
        for row in symbols:
            self.psx_units[row["unit"]].append(row["i"])
        self.unit_of = {a: u for a, (u, basis) in units.items() if basis != "assert-multi"}
        self.pc_units = defaultdict(list)
        for a, unit in self.unit_of.items():
            self.pc_units[Path(unit).stem.upper()].append(a)
        self.psx_callers = defaultdict(set)
        for row in symbols:
            for q in row["callees"]:
                self.psx_callers[q].add(row["i"])
        self.pc_callers = defaultdict(set)
        for f in self.F.values():
            for x in f["callees"]:
                if x in self.F:
                    self.pc_callers[x].add(f["a"])

    def _set(self, p: int, a: int, method: str) -> bool:
        if method != "anchor" and self.unit_conflict(p, a):
            self.rejected[p] = a
            return False
        self.match[p], self.rmatch[a], self.method[p] = a, p, method
        return True

    @staticmethod
    def _link_key(unit_path: str) -> str:
        """Approximate MSVC link order: objects by file name, ``pc\\`` platform objects last."""

        return ("~" if unit_path.lower().startswith("pc\\") else "") + Path(unit_path.replace("\\", "/")).stem.upper()

    def _unit_near(self, index: int, step: int) -> str | None:
        """Link key of the nearest attributed PC function walking from ``index`` by ``step``."""

        k = index + step
        while 0 <= k < len(self.pc_sorted):
            a = self.pc_sorted[k]
            if a in self.unit_of:
                return self._link_key(self.unit_of[a])
            if a in self.rmatch and not self.P[self.rmatch[a]]["unit"].endswith("_H"):
                return self.P[self.rmatch[a]]["unit"]
            k += step
        return None

    def unit_conflict(self, p: int, a: int) -> bool:
        """True when PC source-file evidence contradicts the PSX translation unit.

        * an assert-attributed PC function must belong to the same file;
        * otherwise the PSX unit must fit between the nearest attributed neighbours in
          (approximately alphabetical) link order.

        Header units (``*_H``) hold inline code that any object may emit, so they are exempt.
        """

        unit = self.P[p]["unit"]
        if unit.endswith("_H"):
            return False
        if a in self.unit_of:
            return self._link_key(self.unit_of[a]) != unit
        index = self.pc_index[a]
        before, after = self._unit_near(index, -1), self._unit_near(index, 1)
        if before is None or after is None or unit in (before, after):
            return False
        return not (min(before, after) <= unit <= max(before, after))

    def size_sane(self, p: int, a: int) -> bool:
        ratio = math.log((self.F[a]["size"] + 8) / (self.P[p]["size"] + 8)) - self.K
        return abs(ratio) < 3 * self.spread + 0.3

    def similarity(self, p: int, a: int) -> tuple[float, int, int]:
        row, function = self.P[p], self.F[a]
        ratio = math.log((function["size"] + 8) / (row["size"] + 8)) - self.K
        score = max(0.0, 1.0 - abs(ratio) / (2.5 * self.spread + 0.2))
        pc_callees = set(function["callees"])
        good = sum(1 for q in row["callees"] if q in self.match and self.match[q] in pc_callees)
        bad = sum(1 for q in row["callees"] if q in self.match and self.match[q] not in pc_callees)
        score += 1.5 * good - 0.75 * bad
        if row["parsed"]:
            n_psx, n_pc = len(row["callees"]), len({x for x in pc_callees if x in self.F})
            score += 0.4 * (1 - abs(n_psx - n_pc) / max(n_psx, n_pc, 1))
        shared = len(row["strs"] & set(function["strs"]))
        return score + 2.0 * shared, good, shared

    def align(self, P: list[int], C: list[int]):
        n, m = len(P), len(C)
        dp = [[0.0] * (m + 1) for _ in range(n + 1)]
        back = [[0] * (m + 1) for _ in range(n + 1)]
        cache = {}
        for i in range(1, n + 1):
            p = P[i - 1]
            for j in range(1, m + 1):
                a = C[j - 1]
                best, how = dp[i - 1][j], 1
                if dp[i][j - 1] > best:
                    best, how = dp[i][j - 1], 2
                if (p in self.match and self.match[p] != a) or (a in self.rmatch and self.rmatch[a] != p):
                    score = None
                elif p in self.match:
                    score = 100.0
                else:
                    cache[(p, a)] = self.similarity(p, a)
                    score = cache[(p, a)][0] if cache[(p, a)][0] >= 0.9 else None
                if score is not None and dp[i - 1][j - 1] + score > best:
                    best, how = dp[i - 1][j - 1] + score, 3
                dp[i][j], back[i][j] = best, how
        i, j, pairs = n, m, []
        while i > 0 and j > 0:
            if back[i][j] == 3:
                pairs.append((P[i - 1], C[j - 1]))
                i, j = i - 1, j - 1
            elif back[i][j] == 1:
                i -= 1
            else:
                j -= 1
        return pairs, cache

    def propagate(self) -> None:
        while True:
            votes: Counter = Counter()
            for p, a in self.match.items():
                sequence_p = self.P[p]["callseq"]
                sequence_c = [x for x in dict.fromkeys(self.F[a]["callees"]) if x in self.F]
                if sequence_p and sequence_c:
                    keys = [self.match.get(q) for q in sequence_p]
                    shared = set(sequence_c)
                    common = [k for k in keys if k is not None and k in shared]
                    ip = jp = 0
                    for key in common + [None]:
                        segment_p, segment_c = [], []
                        while ip < len(sequence_p) and (key is None or keys[ip] != key):
                            if sequence_p[ip] not in self.match:
                                segment_p.append(sequence_p[ip])
                            ip += 1
                        while jp < len(sequence_c) and (key is None or sequence_c[jp] != key):
                            if sequence_c[jp] not in self.rmatch:
                                segment_c.append(sequence_c[jp])
                            jp += 1
                        ip, jp = ip + 1, jp + 1
                        if segment_p and len(segment_p) == len(segment_c):
                            for q, x in zip(segment_p, segment_c):
                                if self.size_sane(q, x):
                                    votes[(q, x)] += 2 if len(segment_p) == 1 else 1
                callers_p = [q for q in self.psx_callers[p] if q not in self.match]
                callers_c = [x for x in self.pc_callers[a] if x not in self.rmatch]
                if len(callers_p) == 1 and len(callers_c) == 1 and self.size_sane(callers_p[0], callers_c[0]):
                    votes[(callers_p[0], callers_c[0])] += 2
            per_p, per_c = defaultdict(set), defaultdict(set)
            for q, x in votes:
                per_p[q].add(x)
                per_c[x].add(q)
            added = 0
            for (q, x), count in sorted(votes.items(), key=lambda kv: -kv[1]):
                if count < 2 or q in self.match or x in self.rmatch or len(per_p[q]) > 1 or len(per_c[x]) > 1:
                    continue
                added += self._set(q, x, "callgraph")
            if not added:
                return

    def matched_spans(self) -> dict[str, set[int]]:
        """Extend unit membership with matched functions and the runs bracketed between them.

        Units without PC assert paths (e.g. maths/utility files) otherwise never get an
        ordered alignment. A run is only bracketed when no function in it is known to
        belong to another unit.
        """

        units: dict[str, set[int]] = defaultdict(set)
        for unit, members in self.pc_units.items():
            units[unit].update(members)
        owner = {a: Path(u).stem.upper() for a, u in self.unit_of.items()}
        for p, a in self.match.items():
            owner.setdefault(a, self.P[p]["unit"])
        by_unit = defaultdict(list)
        for p, a in self.match.items():
            by_unit[self.P[p]["unit"]].append(self.pc_index[a])
        for unit, indices in by_unit.items():
            indices.sort()
            units[unit].update(self.pc_sorted[k] for k in indices)
            for k1, k2 in pairwise(indices):
                run = self.pc_sorted[k1 + 1:k2]
                if 0 < len(run) <= 12 and all(owner.get(a, unit) == unit for a in run):
                    units[unit].update(run)
        return units

    def run(self, rounds: int = 4) -> None:
        for _ in range(rounds):
            before = len(self.match)
            spans = self.matched_spans()
            for unit in sorted(set(self.psx_units) & set(spans)):
                P = self.psx_units[unit]
                C = sorted(spans[unit])
                if len(C) < 2:
                    continue
                low, high = self.pc_index[C[0]], self.pc_index[C[-1]]
                pad = max(3, len(P) // 3)
                edge = self.pc_sorted[max(0, low - pad):low] + self.pc_sorted[high + 1:high + 1 + pad]
                C = sorted(set(C) | {a for a in edge if a not in self.unit_of})
                pairs, cache = self.align(P, C)
                for p, a in pairs:
                    if p in self.match:
                        continue
                    _score, good, shared = cache.get((p, a), (0, 0, 0))
                    if good or shared:
                        self._set(p, a, "align+callgraph" if good else "align+strings")
                    else:
                        self.tentative[p] = a
            self.propagate()
            if len(self.match) == before:
                break
        self.sandwich()

    def sandwich(self) -> None:
        position = {p: (unit, k) for unit, P in self.psx_units.items() for k, p in enumerate(P)}
        changed = True
        while changed:
            changed = False
            for p, a in list(self.tentative.items()):
                if p in self.match or a in self.rmatch:
                    continue
                unit, k = position[p]
                P, j = self.psx_units[unit], self.pc_index[a]
                before = k > 0 and j > 0 and self.match.get(P[k - 1]) == self.pc_sorted[j - 1]
                after = (k + 1 < len(P) and j + 1 < len(self.pc_sorted)
                         and self.match.get(P[k + 1]) == self.pc_sorted[j + 1])
                if before and after and self._set(p, a, "align+sandwich"):
                    changed = True
            if changed:
                self.propagate()


def _prepare():
    root = require_reference(REFERENCE)
    symbols = load_psx_symbols(root)
    parse_bodies(root, symbols)
    return symbols, load_features(CANONICAL)["functions"], load_units()


def holdout(fraction: float = 0.3, seeds: int = 4) -> dict:
    symbols, functions, units = _prepare()
    anchors = community_anchors(symbols)
    totals = Counter()
    by_method: dict[str, Counter] = defaultdict(Counter)
    for seed in range(1, seeds + 1):
        rng = random.Random(seed)
        hidden = set(rng.sample(sorted(anchors), int(len(anchors) * fraction)))
        matcher = PsxMatcher(symbols, functions, units, {p: a for p, a in anchors.items() if p not in hidden})
        matcher.run()
        for p in hidden:
            if p not in matcher.match:
                totals["unmatched"] += 1
            elif matcher.match[p] == anchors[p]:
                totals["correct"] += 1
                by_method[matcher.method[p]]["correct"] += 1
            else:
                totals["wrong"] += 1
                by_method[matcher.method[p]]["wrong"] += 1
    decided = totals["correct"] + totals["wrong"]
    return {
        "anchors": len(anchors), "fraction": fraction, "seeds": seeds, **totals,
        "precision": round(totals["correct"] / decided, 3) if decided else None,
        "by_method": {m: dict(c) for m, c in sorted(by_method.items())},
    }


def write_psx(validate: bool = True) -> dict:
    symbols, functions, units = _prepare()
    anchors = community_anchors(symbols)
    matcher = PsxMatcher(symbols, functions, units, anchors)
    matcher.run()
    rows = []
    for p, a in sorted(matcher.match.items(), key=lambda kv: kv[1]):
        row = symbols[p]
        _score, good, shared = matcher.similarity(p, a)
        confidence = "inferred" if matcher.method[p] in STRONG else "provisional"
        rows.append([a, row["name"], row["demangled"], row["unit"], matcher.method[p], good, shared, confidence])
    summary = {
        "psx_functions": len(symbols),
        "psx_bodies_parsed": sum(1 for r in symbols if r["parsed"]),
        "anchors": len(anchors),
        "matched": len(rows),
        "by_method": dict(Counter(matcher.method.values()).most_common()),
        "size_log_ratio_median": round(matcher.K, 3),
    }
    if validate:
        summary["holdout"] = holdout()
    header = {
        "version": 1,
        "generated_by": "tony xref psx",
        "reference": {"name": REFERENCE, "commit": config()["references"][REFERENCE]["commit"]},
        "summary": summary,
    }
    write_rows("re/xref/psx-demo.yml", header, {
        "functions": (("[canonical, psx_symbol, psx_demangled, psx_unit, method, call_edges, shared_strings, "
                       "confidence]"), rows),
    })
    return summary


def load_psx() -> list[dict]:
    from ..common import load_yaml

    keys = ("canonical", "symbol", "demangled", "unit", "method", "call_edges", "shared_strings", "confidence")
    return [dict(zip(keys, row)) for row in load_yaml("re/xref/psx-demo.yml").get("functions") or []]
