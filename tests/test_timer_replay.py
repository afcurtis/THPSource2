from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "re/gdb"))

from opentony.timer import (
    TIMER_PAUSE_GATE_A,
    TIMER_PAUSE_GATE_B,
    TIMER_PUBLIC_ACCUMULATOR,
    TIMER_PUBLIC_TICK,
    TIMER_SIMULATION_ACCUMULATOR,
    TIMER_SIMULATION_TIME,
    TIMER_STATE_ADDRESS,
    TimerReplayService,
    infer_completed_timer_deliveries,
    infer_timer_delivery_count,
)


class FakeMemory:
    def __init__(self):
        self.values = {
            TIMER_PAUSE_GATE_A: 0,
            TIMER_PAUSE_GATE_B: 0,
        }
        self.bytes_values = {}

    def readable(self, address, size=1):
        if size == 8:
            return address in self.bytes_values
        return address in self.values

    def u8(self, address):
        return self.values[address] & 0xFF

    def u32(self, address):
        return self.values[address]

    def write_u32(self, address, value):
        self.values[address] = value & 0xFFFFFFFF

    def write(self, address, data):
        self.bytes_values[address] = bytes(data)

    def double(self, address):
        return struct.unpack("<d", self.bytes_values[address])[0]


def _initial_timer_state():
    return {
        "timer_handle": 1,
        "interval_ms": 16,
        "opaque_08": 16,
        "accumulated_ms": 0,
        "opaque_10": 0,
        "public_accumulator": {"raw": 0, "value": 0.0},
        "simulation_accumulator": {"raw": 0, "value": 0.0},
        "public_tick": 0,
        "simulation_time": 0,
    }


def _delivery():
    return {
        "type": "timer_callback_delivery",
        "interval_ms": 16,
        "callback_arg0": 16,
        "callback_arg1": 0,
    }


def test_timer_replay_advances_only_recorded_deliveries():
    memory = FakeMemory()
    service = TimerReplayService(_initial_timer_state())

    results = service.apply_frame(
        {"events": [_delivery(), _delivery()]},
        memory,
    )

    assert len(results) == 2
    assert service.deliveries == 2
    assert service.state.accumulated_ms == 32
    assert service.state.public_accumulator == 1.92
    assert service.state.simulation_accumulator == 1.92
    assert service.state.public_tick == 1
    assert service.state.simulation_time == 1
    assert memory.u32(TIMER_STATE_ADDRESS + 0x0C) == 32
    assert memory.u32(TIMER_PUBLIC_TICK) == 1
    assert memory.u32(TIMER_SIMULATION_TIME) == 1
    assert memory.double(TIMER_PUBLIC_ACCUMULATOR) == 1.92
    assert memory.double(TIMER_SIMULATION_ACCUMULATOR) == 1.92


def test_timer_replay_asserts_recorded_boundary_counter():
    memory = FakeMemory()
    service = TimerReplayService(_initial_timer_state())
    event = _delivery()
    event.update(
        timer_boundary_before={"interval_ms": 16, "accumulated_ms": 0},
        timer_boundary_after={"interval_ms": 16, "accumulated_ms": 16},
        timer_boundary_delivery_count=1,
    )

    assert len(service.apply_frame({"events": [event]}, memory)) == 1

    event["timer_boundary_before"]["accumulated_ms"] = 16
    event["timer_boundary_after"]["accumulated_ms"] = 48
    with pytest.raises(ValueError, match="boundary end"):
        service.apply_frame({"events": [event]}, memory)


def test_timer_replay_asserts_zero_delivery_boundary():
    memory = FakeMemory()
    service = TimerReplayService(_initial_timer_state())
    event = {
        "type": "timer_boundary_sample",
        "timer_boundary_phase": "physics_entry",
        "timer_boundary_before": {"interval_ms": 16, "accumulated_ms": 0},
        "timer_boundary_after": {"interval_ms": 16, "accumulated_ms": 0},
        "timer_boundary_delivery_count": 0,
    }

    assert service.apply_frame({"events": [event]}, memory) == []
    assert service.deliveries == 0


def test_timer_replay_accepts_multiple_boundary_segments_in_one_phase():
    memory = FakeMemory()
    service = TimerReplayService(_initial_timer_state())
    first = _delivery()
    first.update(
        timer_boundary_before={"interval_ms": 16, "accumulated_ms": 0},
        timer_boundary_after={"interval_ms": 16, "accumulated_ms": 32},
        timer_boundary_delivery_count=2,
    )
    second = _delivery()
    second.update(
        timer_boundary_before={"interval_ms": 16, "accumulated_ms": 32},
        timer_boundary_after={"interval_ms": 16, "accumulated_ms": 48},
        timer_boundary_delivery_count=1,
    )

    assert len(service.apply_frame({"events": [first, first.copy(), second]}, memory)) == 3
    assert service.state.accumulated_ms == 48


def test_timer_replay_uses_callback_pause_gate_observation():
    memory = FakeMemory()
    service = TimerReplayService(_initial_timer_state())
    event = _delivery()
    event["simulation_pause_gate_a"] = True
    event["simulation_pause_gate_b"] = False

    service.apply_frame({"events": [event]}, memory)

    assert service.state.public_accumulator == 0.96
    assert service.state.simulation_accumulator == 0.0
    assert service.state.public_tick == 0
    assert service.state.simulation_time == 0


def test_timer_replay_releases_legacy_partial_delivery_at_next_boundary():
    memory = FakeMemory()
    service = TimerReplayService(_initial_timer_state())
    partial = _delivery()
    partial.update(
        timer_boundary_before={
            "interval_ms": 16,
            "accumulated_ms": 0,
            "public_accumulator": 0.0,
            "public_tick": 0,
            "simulation_accumulator": 0.0,
            "simulation_time": 0,
        },
        timer_boundary_after={
            "interval_ms": 16,
            "accumulated_ms": 16,
            "public_accumulator": 0.96,
            "public_tick": 0,
            "simulation_accumulator": 0.0,
            "simulation_time": 0,
        },
        timer_boundary_delivery_count=1,
        timer_boundary_phase="timing_producer",
    )
    completed = _delivery()
    completed.update(
        timer_boundary_before={
            "interval_ms": 16,
            "accumulated_ms": 16,
            "public_accumulator": 0.96,
            "public_tick": 0,
            "simulation_accumulator": 0.0,
            "simulation_time": 0,
        },
        timer_boundary_after={
            "interval_ms": 16,
            "accumulated_ms": 48,
            "public_accumulator": 2.88,
            "public_tick": 2,
            "simulation_accumulator": 2.88,
            "simulation_time": 2,
        },
        timer_boundary_delivery_count=2,
        timer_boundary_phase="timing_delta",
    )

    # Replay passes the same frame object to every boundary of that frame.
    frame = {"events": [partial, completed, completed.copy()]}
    results = service.apply_frame(frame, memory, phase="timing_producer")
    assert len(results) == 1
    results = service.apply_frame(frame, memory, phase="timing_delta")

    assert len(results) == 2
    assert service.state.accumulated_ms == 48
    assert service.state.public_accumulator == 2.88
    assert service.state.simulation_accumulator == 2.88
    assert service.state.public_tick == 2
    assert service.state.simulation_time == 2


def test_timer_replay_applies_only_requested_frame_phase():
    memory = FakeMemory()
    service = TimerReplayService(_initial_timer_state())
    entry = _delivery()
    clock_read = _delivery()
    clock_read["timer_boundary_phase"] = "clock_read"

    frame = {"events": [entry, clock_read]}
    assert len(service.apply_frame(frame, memory)) == 1
    assert service.state.accumulated_ms == 16
    assert len(service.apply_frame(frame, memory, phase="clock_read")) == 1
    assert service.state.accumulated_ms == 32
    # A boundary never applies a segment twice.
    assert service.apply_frame(frame, memory, phase="clock_read") == []


def test_timer_initial_state_is_read_from_recording_header():
    initial = _initial_timer_state()
    assert TimerReplayService.initial_from_recording(
        {"initial_timer_state": initial}, []
    ) == initial


def test_timer_delivery_count_uses_atomic_millisecond_counter():
    assert infer_timer_delivery_count(432, 480, 16) == 3
    assert infer_timer_delivery_count(0xFFFFFFF0, 0x10, 16) == 2


def test_timer_delivery_count_rejects_partial_interval():
    with pytest.raises(ValueError, match="not divisible"):
        infer_timer_delivery_count(0, 15, 16)


def _timer_boundary_simulation(value: float, *, gate_a=False, gate_b=False):
    raw = int.from_bytes(struct.pack("<d", value), "little")
    return {
        "simulation_accumulator": {"raw": raw, "value": value},
        "simulation_pause_gate_a": gate_a,
        "simulation_pause_gate_b": gate_b,
    }


def test_timer_boundary_completion_defers_counter_tick_before_simulation_store():
    assert infer_completed_timer_deliveries(
        _timer_boundary_simulation(0.0),
        _timer_boundary_simulation(0.0),
        counter_deliveries=1,
        pending_deliveries=0,
        interval_ms=16,
        previous_interval_ms=16,
    ) == (0, 1)


def test_timer_boundary_completion_releases_pending_and_new_ticks_together():
    assert infer_completed_timer_deliveries(
        _timer_boundary_simulation(0.0),
        _timer_boundary_simulation(2.88),
        counter_deliveries=2,
        pending_deliveries=1,
        interval_ms=16,
        previous_interval_ms=16,
    ) == (3, 0)


def test_timer_replay_validates_sampled_counter_with_pending_delivery():
    memory = FakeMemory()
    service = TimerReplayService(_initial_timer_state())
    event = {
        "type": "timer_boundary_sample",
        "timer_boundary_before": {"interval_ms": 16, "accumulated_ms": 0},
        "timer_boundary_after": {"interval_ms": 16, "accumulated_ms": 0},
        "timer_boundary_delivery_count": 0,
        "timer_boundary_sampled_accumulated_ms": 16,
        "timer_boundary_pending_delivery_count": 1,
    }

    assert service.apply_frame({"events": [event]}, memory) == []
    event["timer_boundary_sampled_accumulated_ms"] = 32
    with pytest.raises(ValueError, match="sampled timer counter"):
        service.apply_frame({"events": [event]}, memory)


def _limiter_frame(delivery_count: int) -> dict:
    """A frame shaped like an in-process capture around the present limiter.

    The timing-update seam samples once (no delivery since the previous
    physics return); the deliveries that span the present limiter are then
    observed at physics entry, which tony/capture.py labels with the same
    timing-update phase.  A clock-read sample follows.
    """

    def sample(phase: str, accumulated: int) -> dict:
        return {
            "type": "timer_boundary_sample",
            "timer_boundary_phase": phase,
            "timer_boundary_before": {"interval_ms": 16, "accumulated_ms": accumulated},
            "timer_boundary_after": {"interval_ms": 16, "accumulated_ms": accumulated},
            "timer_boundary_delivery_count": 0,
        }

    events = [sample("timer_update", 0)]
    for _ in range(delivery_count):
        event = _delivery()
        event.update(
            timer_boundary_phase="timer_update",
            timer_boundary_before={"interval_ms": 16, "accumulated_ms": 0},
            timer_boundary_after={
                "interval_ms": 16,
                "accumulated_ms": 16 * delivery_count,
            },
            timer_boundary_delivery_count=delivery_count,
        )
        events.append(event)
    events.append(sample("clock_read", 16 * delivery_count))
    return {"events": events}


def test_timer_replay_timing_update_visit_closes_only_its_own_sample():
    memory = FakeMemory()
    service = TimerReplayService(_initial_timer_state())
    frame = _limiter_frame(3)

    # The deliveries spanning the limiter must not be published before it.
    assert service.apply_frame(frame, memory, phase="timer_update", max_segments=1) == []
    assert service.state.accumulated_ms == 0
    # Physics entry closes the relabelled physics-entry sample.
    results = service.apply_frame(
        frame, memory, phase="physics_entry", also=("timer_update",)
    )
    assert len(results) == 3
    assert service.state.accumulated_ms == 48
    assert service.apply_frame(frame, memory, phase="clock_read") == []


def test_timer_replay_delivers_ahead_of_boundary_one_at_a_time():
    memory = FakeMemory()
    service = TimerReplayService(_initial_timer_state())
    frame = _limiter_frame(3)

    # Before the timing-update sample is reached nothing may be delivered.
    assert service.deliver_ahead(frame, memory) is None
    service.apply_frame(frame, memory, phase="timer_update", max_segments=1)

    first = service.deliver_ahead(frame, memory)
    assert first is not None
    assert service.state.accumulated_ms == 16
    # Published like the callback itself, so a limiter reading the public
    # tick sees each delivery as it happens.
    assert memory.u32(TIMER_STATE_ADDRESS + 0x0C) == 16
    assert service.deliver_ahead(frame, memory) is not None
    assert memory.u32(TIMER_PUBLIC_TICK) == 1

    # The boundary applies only the remainder and still checks its end state.
    results = service.apply_frame(
        frame, memory, phase="physics_entry", also=("timer_update",)
    )
    assert len(results) == 1
    assert service.state.accumulated_ms == 48
    assert service.deliveries == 3
    assert service.early_deliveries == 2


def test_timer_replay_never_delivers_more_than_recorded_ahead_of_boundary():
    memory = FakeMemory()
    service = TimerReplayService(_initial_timer_state())
    frame = _limiter_frame(2)

    service.apply_frame(frame, memory, phase="timer_update", max_segments=1)
    assert service.deliver_ahead(frame, memory) is not None
    assert service.deliver_ahead(frame, memory) is not None
    # The open segment is exhausted; the clock-read sample is not crossed.
    assert service.deliver_ahead(frame, memory) is None
    assert service.apply_frame(
        frame, memory, phase="physics_entry", also=("timer_update",)
    ) == []
    assert service.state.accumulated_ms == 32
    assert service.deliver_ahead(frame, memory) is None
    assert service.apply_frame(frame, memory, phase="clock_read") == []
    assert service.deliver_ahead(frame, memory) is None


def test_timer_replay_boundary_without_recorded_sample_changes_nothing():
    memory = FakeMemory()
    service = TimerReplayService(_initial_timer_state())
    frame = _limiter_frame(1)

    assert service.apply_frame(frame, memory, phase="timing_producer") == []
    assert service.state.accumulated_ms == 0
    assert service.deliver_ahead(frame, memory) is None


def test_timer_replay_ahead_delivery_checks_the_recorded_start():
    memory = FakeMemory()
    service = TimerReplayService(_initial_timer_state())
    frame = _limiter_frame(1)
    frame["events"][1]["timer_boundary_before"]["accumulated_ms"] = 16
    service.apply_frame(frame, memory, phase="timer_update", max_segments=1)

    with pytest.raises(ValueError, match="boundary start"):
        service.deliver_ahead(frame, memory)


_PHASE_BY_LETTER = {
    "P": "post_physics",
    "T": "timer_update",
    "E": "physics_entry",
    "C": "clock_read",
}


def _fixture_frames():
    fixture = json.loads(
        (Path(__file__).resolve().parents[1]
         / "re/evidence/fixtures/timer-warehouse-idle.json").read_text()
    )
    interval = fixture["interval_ms"]
    accumulated = fixture["initial_timer_state"]["accumulated_ms"]
    frames = []
    for entry_clock, samples in fixture["frames"]:
        events = []
        for sample in samples.split():
            phase = _PHASE_BY_LETTER[sample[0]]
            count = int(sample[1:])
            metadata = {
                "timer_boundary_phase": phase,
                "timer_boundary_before": {"interval_ms": interval, "accumulated_ms": accumulated},
                "timer_boundary_after": {
                    "interval_ms": interval,
                    "accumulated_ms": accumulated + count * interval,
                },
                "timer_boundary_delivery_count": count,
            }
            if count == 0:
                events.append({"type": "timer_boundary_sample", **metadata})
            for _ in range(count):
                events.append({**_delivery(), **metadata})
            accumulated += count * interval
        frames.append(({"events": events}, entry_clock))
    return fixture["initial_timer_state"], frames


@pytest.mark.parametrize("limiter_deliveries", [0, 2])
def test_timer_model_reproduces_warehouse_idle_entry_clocks(limiter_deliveries):
    """Replay the recorded boundary samples in the GDB replay's closing order.

    ``limiter_deliveries`` additionally hands up to that many deliveries to a
    spinning frame limiter between the timing update and physics entry, as
    the retail present limiter needs; the entry clock must not change.
    """

    initial, frames = _fixture_frames()
    service = TimerReplayService(initial)
    memory = FakeMemory()
    for frame, entry_clock in frames:
        service.apply_frame(frame, memory, phase="post_physics")
        service.apply_frame(frame, memory, phase="timer_update", max_segments=1)
        for _ in range(limiter_deliveries):
            service.deliver_ahead(frame, memory)
        service.apply_frame(frame, memory, phase="physics_entry", also=("timer_update",))
        assert service.state.simulation_time == entry_clock
        # Nothing is published before the first delivery; the recorded
        # initial SimulationTime is zero.
        assert memory.values.get(TIMER_SIMULATION_TIME, 0) == entry_clock
        service.apply_frame(frame, memory, phase="clock_read")
    assert service.deliveries == 545
    assert service.state.accumulated_ms == 9248
