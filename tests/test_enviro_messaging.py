"""Phase 4.4c: the retail crate-word transitions of SendVisible and a hit-kill
(re/evidence/fixtures/enviro-warehouse.txt) against the rules the native trigger
state implements (src/trg/level_trigger_state.cpp, unit-tested in
level_trigger_state_test.cpp)."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "re/evidence/fixtures/enviro-warehouse.txt"
HIDDEN = 0x41


def _transitions():
    rows = []
    for line in FIXTURE.read_text().splitlines():
        if line.startswith("T "):
            _, kind, argument, before, after, count = line.split()
            rows.append((kind, int(argument), int(before), int(after), int(count)))
    return rows


def native_word(kind, argument, before):
    """The rule of LevelTriggerState::on_visible / on_kill for a crate."""
    if kind == "visible":
        return before & ~HIDDEN if argument != 0 else before | HIDDEN
    assert kind == "kill" and argument == 1
    return before | 0x1


def test_every_retail_transition_follows_the_native_rule():
    rows = _transitions()
    assert len(rows) == 9
    assert sum(count for *_, count in rows) >= 500
    for kind, argument, before, after, _ in rows:
        assert native_word(kind, argument, before) == after, (kind, argument, before, after)


def test_fixture_covers_show_hide_and_hit():
    kinds = {(kind, argument) for kind, argument, *_ in _transitions()}
    assert kinds == {("visible", 0), ("visible", 1), ("kill", 1)}
