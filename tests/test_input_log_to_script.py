import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("input_log_to_script", Path(__file__).resolve().parents[1] / "scripts" / "input_log_to_script.py")
tool = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(tool)


def test_old_log_uses_time_and_rate():
    lines = [
        "   1.000  pad button down A/South (0)\n",
        "   1.500  pad button up   A/South (0)\n",
        "   2.000  pad button down DRight (14)\n",
    ]
    script, skipped = tool.convert(lines, rate=60)
    assert script == ["60 pad down 0", "90 pad up 0", "120 pad down 14"]
    assert skipped == []


def test_stamped_poll_wins_over_time():
    script, _ = tool.convert(["   9.999  poll 42      pad button down Start (6)\n"], rate=60)
    assert script == ["42 pad down 6"]


def test_axes_and_triggers():
    script, _ = tool.convert([
        "   1.000  pad axis 0 -> -\n",
        "   1.100  pad axis 0 -> rest\n",
        "   1.200  pad axis 5 -> +\n",
        "   1.300  pad axis 4 -> -\n",
    ], rate=10)
    assert script == ["10 pad axis 0 -32768", "11 pad axis 0 0", "12 pad axis 5 32767", "13 pad axis 4 0"]


def test_keys_mapped_and_unknown_reported():
    script, skipped = tool.convert([
        "   1.000  poll 3  key down Return (scancode 40)\n",
        "   1.100  poll 4  key up   Return (scancode 40)\n",
        "   1.200  poll 5  key down Q (scancode 20)\n",
        "   1.300  window: focus gained\n",
    ])
    assert script == ["3 key down 1c", "4 key up 1c"]
    assert skipped == [20]


def test_events_sorted_by_poll():
    script, _ = tool.convert([
        "   2.000  poll 9  pad button down A/South (0)\n",
        "   1.000  poll 2  pad button down B/East (1)\n",
    ])
    assert script == ["2 pad down 1", "9 pad down 0"]
