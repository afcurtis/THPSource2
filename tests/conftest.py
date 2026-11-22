"""Test-session setup shared by every test file."""

from __future__ import annotations

from pathlib import Path

import pytest
import yaml

# Many tests parse the multi-megabyte symbol and progress tables with `yaml.safe_load`. Pure-Python PyYAML needs
# seconds for each; libyaml's loader needs a fraction and builds the same safe objects (Phase 5.1p: most of the
# 19 minutes of a serial `pytest -q` were YAML parsing).
if hasattr(yaml, "CSafeLoader"):

    def _fast_safe_load(stream):
        return yaml.load(stream, Loader=yaml.CSafeLoader)

    yaml.safe_load = _fast_safe_load


# The retail fixtures (re/evidence/fixtures/) hold the retail game's memory: they are captured from the person's own copy and are not in the public
# repository (re/evidence/fixtures/README.md). Without them a test module that reads them is skipped, not failed.
_FIXTURES = Path(__file__).resolve().parents[1] / "re/evidence/fixtures"
_HAVE_FIXTURES = any(_FIXTURES.glob("*.txt"))  # the captures; the *.json notes are tracked and always present


def pytest_collection_modifyitems(config, items):
    if _HAVE_FIXTURES:
        return
    skip = pytest.mark.skip(reason="retail fixtures not present (re/evidence/fixtures/README.md)")
    reads: dict[Path, bool] = {}
    for item in items:
        path = Path(str(item.fspath))
        if path not in reads:
            try:
                reads[path] = "evidence/fixtures" in path.read_text(encoding="utf-8", errors="replace")
            except OSError:
                reads[path] = False
        if reads[path]:
            item.add_marker(skip)
