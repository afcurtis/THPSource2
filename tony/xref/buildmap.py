"""Map a variant build's functions and data onto the canonical build.

Stages, in decreasing strength:

``hash``      identical normalized instruction stream, unique in both builds
``strings``   identical non-empty set of referenced strings, unique in both builds
``mnemonic``  identical mnemonic sequence (operands ignored), unique in both builds
``callgraph`` callee lists of matched pairs aligned by mnemonic hash
``order``     unmatched runs between two monotone anchors, paired by link order

Every pair is then scored by *consistency*: the fraction of its callees and absolute
references that translate onto the partner's callees/references through the final map.
"""

from __future__ import annotations

import bisect
import difflib
from collections import Counter, defaultdict
from itertools import pairwise
from typing import Any

from .common import CANONICAL, config, load_features, write_rows

METHOD_ORDER = ("hash", "strings", "mnemonic", "callgraph", "order")


class BuildMatcher:
    def __init__(self, variant: list[dict], canonical: list[dict]):
        self.A = {f["a"]: f for f in variant}
        self.B = {f["a"]: f for f in canonical}
        self.match: dict[int, int] = {}
        self.rmatch: dict[int, int] = {}
        self.method: dict[int, str] = {}

    # ------------------------------------------------------------------ helpers
    def add(self, a: int, b: int, method: str) -> bool:
        if a in self.match or b in self.rmatch:
            return False
        self.match[a], self.rmatch[b], self.method[a] = b, a, method
        return True

    def _unique_pairs(self, key):
        ka, kb = defaultdict(list), defaultdict(list)
        for f in self.A.values():
            if (k := key(f)) is not None:
                ka[k].append(f["a"])
        for f in self.B.values():
            if (k := key(f)) is not None:
                kb[k].append(f["a"])
        for k, la in ka.items():
            lb = kb.get(k)
            if lb and len(la) == 1 and len(lb) == 1:
                yield la[0], lb[0]

    @staticmethod
    def size_ok(fa: dict, fb: dict, low: float = 0.75, high: float = 1.33) -> bool:
        if fa["hm"] == fb["hm"]:
            return True
        return low <= (fb["size"] + 8) / (fa["size"] + 8) <= high

    # ------------------------------------------------------------------ stages
    def seed(self) -> None:
        for a, b in self._unique_pairs(lambda f: f["h"] if f["n"] >= 4 else None):
            self.add(a, b, "hash")
        for a, b in self._unique_pairs(lambda f: tuple(sorted(set(f["strs"]))) if f["strs"] else None):
            if self.size_ok(self.A[a], self.B[b], 0.5, 2.0):
                self.add(a, b, "strings")
        for a, b in self._unique_pairs(lambda f: f["hm"] if f["n"] >= 8 else None):
            self.add(a, b, "mnemonic")

    def propagate(self) -> None:
        changed = True
        while changed:
            changed = False
            for a, b in list(self.match.items()):
                ca, cb = self.A[a]["callees"], self.B[b]["callees"]
                if not ca or not cb:
                    continue
                ha = [self.A[x]["hm"] if x in self.A else "?" for x in ca]
                hb = [self.B[x]["hm"] if x in self.B else "?" for x in cb]
                matcher = difflib.SequenceMatcher(None, ha, hb, autojunk=False)
                for tag, i1, i2, j1, j2 in matcher.get_opcodes():
                    if tag != "equal" and not (tag == "replace" and i2 - i1 == j2 - j1):
                        continue
                    for x, y in zip(ca[i1:i2], cb[j1:j2]):
                        if (x in self.A and y in self.B and x not in self.match and y not in self.rmatch
                                and (tag == "equal" or self.size_ok(self.A[x], self.B[y]))
                                and self.add(x, y, "callgraph")):
                            changed = True

    def gapfill(self) -> None:
        la, lb = sorted(self.A), sorted(self.B)
        anchors = sorted(self.match.items())
        for (a1, b1), (a2, b2) in pairwise(anchors):
            if b1 >= b2:
                continue
            ga = [x for x in la[bisect.bisect_right(la, a1):bisect.bisect_left(la, a2)] if x not in self.match]
            gb = [y for y in lb[bisect.bisect_right(lb, b1):bisect.bisect_left(lb, b2)] if y not in self.rmatch]
            if not ga or not gb:
                continue
            if len(ga) == len(gb) and all(self.size_ok(self.A[x], self.B[y], 0.6, 1.66) for x, y in zip(ga, gb)):
                for x, y in zip(ga, gb):
                    self.add(x, y, "order")
                continue
            matcher = difflib.SequenceMatcher(None, [self.A[x]["hm"] for x in ga], [self.B[y]["hm"] for y in gb],
                                              autojunk=False)
            for tag, i1, i2, j1, j2 in matcher.get_opcodes():
                if tag == "equal" or (tag == "replace" and i2 - i1 == j2 - j1):
                    for x, y in zip(ga[i1:i2], gb[j1:j2]):
                        if tag == "equal" or self.size_ok(self.A[x], self.B[y], 0.6, 1.66):
                            self.add(x, y, "order")

    def run(self, rounds: int = 6) -> None:
        self.seed()
        for _ in range(rounds):
            before = len(self.match)
            self.propagate()
            self.gapfill()
            if len(self.match) == before:
                break

    # ------------------------------------------------------------------ results
    def monotone(self) -> set[int]:
        """Variant addresses on the longest link-order-preserving chain of matches."""

        pairs = sorted(self.match.items())
        tails: list[int] = []
        tail_index: list[int] = []
        previous: list[int | None] = [None] * len(pairs)
        for i, (_a, b) in enumerate(pairs):
            j = bisect.bisect_left(tails, b)
            if j == len(tails):
                tails.append(b)
                tail_index.append(i)
            else:
                tails[j] = b
                tail_index[j] = i
            previous[i] = tail_index[j - 1] if j else None
        chain: set[int] = set()
        k = tail_index[-1] if tail_index else None
        while k is not None:
            chain.add(pairs[k][0])
            k = previous[k]
        return chain

    def data_map(self) -> tuple[dict[int, int], int]:
        votes: dict[int, Counter] = defaultdict(Counter)
        for a, b in self.match.items():
            fa, fb = self.A[a], self.B[b]
            if fa["h"] == fb["h"] and len(fa["abs"]) == len(fb["abs"]):
                for x, y in zip(fa["abs"], fb["abs"]):
                    votes[x][y] += 1
        mapping, conflicts = {}, 0
        for x, counter in votes.items():
            (y, count), = counter.most_common(1)
            if count == sum(counter.values()):
                mapping[x] = y
            else:
                conflicts += 1
        return {x: y for x, y in mapping.items() if x not in self.A}, conflicts

    def consistency(self, data: dict[int, int]) -> dict[int, float | None]:
        translate = {**data, **self.match}
        scores: dict[int, float | None] = {}
        for a, b in self.match.items():
            fa, fb = self.A[a], self.B[b]
            refs = [translate.get(x) for x in fa["callees"] + fa["abs"]]
            if not refs:
                scores[a] = None
                continue
            partner = set(fb["callees"]) | set(fb["abs"])
            scores[a] = round(sum(1 for x in refs if x in partner) / len(refs), 2)
        return scores


def match_builds(variant: str) -> dict[str, Any]:
    A, B = load_features(variant), load_features(CANONICAL)
    matcher = BuildMatcher(A["functions"], B["functions"])
    matcher.run()
    chain = matcher.monotone()
    data, conflicts = matcher.data_map()
    scores = matcher.consistency(data)
    spec = config()["variants"][variant]
    from ..common import load_yaml

    canonical_sha = load_yaml("re/config/binaries.yml")["executables"]["thps2_pc"]["sha256"]
    by_method = Counter(matcher.method.values())
    header = {
        "version": 1,
        "generated_by": f"tony xref match-builds --variant {variant}",
        "variant": variant,
        "variant_sha256": spec["sha256"],
        "canonical_sha256": canonical_sha,
        "summary": {
            "variant_functions": len(matcher.A),
            "canonical_functions": len(matcher.B),
            "matched_functions": len(matcher.match),
            "by_method": {m: by_method.get(m, 0) for m in METHOD_ORDER},
            "non_monotone": len(matcher.match) - len(chain),
            "data_entries": len(data),
            "data_conflicts": conflicts,
        },
    }
    rows = [[a, b, matcher.method[a], matcher.A[a]["h"] == matcher.B[b]["h"],
             scores[a], a in chain] for a, b in sorted(matcher.match.items())]
    write_rows(f"re/xref/{variant}.yml", header, {
        "functions": ("[variant, canonical, method, identical_code, consistency, monotone]", rows),
        "data": ("[variant, canonical]", [[x, y] for x, y in sorted(data.items())]),
        "unmatched_variant": ("[variant]", [[a] for a in sorted(set(matcher.A) - set(matcher.match))]),
        "unmatched_canonical": ("[canonical]", [[b] for b in sorted(set(matcher.B) - set(matcher.rmatch))]),
    })
    return header["summary"]


def load_map(variant: str) -> tuple[dict[int, int], dict[int, int], dict[int, dict]]:
    """Return (function map, data map, per-function metadata) for a recorded variant map."""

    from ..common import load_yaml

    doc = load_yaml(f"re/xref/{variant}.yml")
    functions, meta = {}, {}
    for a, b, method, identical, score, monotone in doc.get("functions") or []:
        functions[a] = b
        meta[a] = {"method": method, "identical": identical, "consistency": score, "monotone": monotone}
    data = {a: b for a, b in doc.get("data") or []}
    return functions, data, meta
