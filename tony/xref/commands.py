"""`tony xref ...` command handlers (heavy imports stay lazy)."""

from __future__ import annotations

import json

from ..common import resolve

VARIANT = "original_2000"


def _show(summary) -> None:
    print(json.dumps(summary, indent=2))


def fetch_references(args) -> int:
    from .community import fetch_references as fetch

    fetch(args.name or None)
    return 0


def features(args) -> int:
    from .features import export

    export(args.build, reanalyze=args.reanalyze)
    return 0


def match_builds(args) -> int:
    from .buildmap import match_builds as run

    _show(run(args.variant))
    return 0


def units(_args) -> int:
    from .units import write_units

    _show(write_units())
    return 0


def community(args) -> int:
    from .community import write_community

    _show(write_community(args.variant))
    return 0


def psx(args) -> int:
    from .psx import holdout, write_psx

    if args.holdout:
        _show(holdout(args.fraction, args.seeds))
    else:
        _show(write_psx(validate=not args.no_validate))
    return 0


def types(_args) -> int:
    from .types import write_types

    _show(write_types())
    return 0


def apply(_args) -> int:
    from .apply import write_imported

    _show(write_imported())
    return 0


def coverage(args) -> int:
    from .coverage import render_markdown, write_coverage

    report = write_coverage(resolve(args.output) if args.output else None)
    print(render_markdown(report))
    return 0


def run_all(args) -> int:
    """Regenerate every re/xref artifact from the recorded inputs."""

    from types import SimpleNamespace

    if not args.skip_features:
        features(SimpleNamespace(build="canonical", reanalyze=False))
        features(SimpleNamespace(build=args.variant, reanalyze=False))
    match_builds(args)
    units(args)
    community(args)
    psx(SimpleNamespace(holdout=False, no_validate=False))
    types(args)
    apply(args)
    coverage(SimpleNamespace(output=None))
    return 0


def _static_recipe(args) -> int:
    from .. import static_variant

    return static_variant.static_recipe(args)


def register(sub) -> None:
    xref = sub.add_parser("xref", help="cross-reference external builds/projects onto the canonical executable")
    xs = xref.add_subparsers(dest="xref_command", required=True)
    p = xs.add_parser("fetch-references", help="clone/check out the pinned reference projects under build/xref")
    p.add_argument("name", nargs="*")
    p.set_defaults(func=fetch_references)
    p = xs.add_parser("features", help="export normalized per-function fingerprints from Ghidra")
    p.add_argument("--build", default="canonical", help="'canonical' or a variant from re/xref/config.yml")
    p.add_argument("--reanalyze", action="store_true", help="re-import and re-analyze a variant project")
    p.set_defaults(func=features)
    p = xs.add_parser("match-builds", help="map a variant build onto the canonical build (re/xref/<variant>.yml)")
    p.add_argument("--variant", default=VARIANT)
    p.set_defaults(func=match_builds)
    p = xs.add_parser("units", help="attribute functions to source files via assert paths")
    p.set_defaults(func=units)
    p = xs.add_parser("community", help="harvest names from PARTYMOD and the Cumulative Patch")
    p.add_argument("--variant", default=VARIANT)
    p.set_defaults(func=community)
    p = xs.add_parser("psx", help="match PSX demo decomp symbols onto canonical functions")
    p.add_argument("--holdout", action="store_true", help="only report anchor hold-out validation")
    p.add_argument("--fraction", type=float, default=0.3)
    p.add_argument("--seeds", type=int, default=4)
    p.add_argument("--no-validate", action="store_true", help="skip the hold-out run when writing results")
    p.set_defaults(func=psx)
    p = xs.add_parser("types", help="cross-reference recovered types with PSX/Cumulative Patch structs")
    p.set_defaults(func=types)
    p = xs.add_parser("apply", help="merge xref evidence into re/symbols/imported.yml")
    p.set_defaults(func=apply)
    p = xs.add_parser("coverage", help="report named-function coverage per subsystem")
    p.add_argument("--output", help="JSON output (Markdown is written beside it)")
    p.set_defaults(func=coverage)
    p = xs.add_parser("static-recipe", help="rebuild the canonical static-data image from a variant (re/xref/<variant>_static.yml and the launcher's copy)")
    p.add_argument("--variant", default=VARIANT)
    p.add_argument("--check", action="store_true", help="only report whether the committed recipe is current")
    p.set_defaults(func=_static_recipe)
    p = xs.add_parser("all", help="run every xref stage in order")
    p.add_argument("--variant", default=VARIANT)
    p.add_argument("--skip-features", action="store_true")
    p.set_defaults(func=run_all)
