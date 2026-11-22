// Frame pacing of the standalone (docs: THPS2 post_phase_5/framerate.md). The game limits a scene to one per two simulation ticks (1/60 s each), which is its 30 fps.
// `limiter_ticks` is that two. The default reproduces retail; the launcher's frame rate (OT_FRAME_RATE) sets it. No host state lives in guest memory.
#pragma once

#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace opentony::runtime::framepacing {

struct Pacing {
    std::uint32_t limiter_ticks = 2;   // PcSystem::begin_scene waits until this many ticks have passed since the last scene
    bool exact_clock = false;          // realtime delivery: the tick words follow the wall clock exactly, not 0.96 a timer call
    bool count_30hz = false;           // a counter the game keeps in frames adds the 30 Hz frames this frame is worth (0x0056a940: 1 at 30 fps, 0 or 1 at 60), not 1
    double period_ticks = 2.0;         // a scene in ticks: 2 (30 fps), 1 (60), 0.5 (120)
    bool sub_tick = false;             // F2 (120 fps): a scene is shorter than a tick, so the limiter and the time step read the tick accumulators (doubles), not the whole tick words
    bool carry_fraction = false;       // F2B (120 fps only): a small value times the frame scale keeps the part the shift drops for the next frame (scaled_carry)
};

inline Pacing& pacing() {
    static Pacing value;
    return value;
}

inline void set_pacing(const Pacing& value) { pacing() = value; }
inline std::uint32_t limiter_ticks() { return pacing().limiter_ticks; }

// F2A: the sub-tick clock. At 120 fps a scene is half a tick, and the whole tick words give frames of 0 and 1 ticks: a limiter that cannot wait half a tick and a frame scale that jumps
// between 42 and 85. With `sub_tick` the limiter (PcSystem::begin_scene) and the time step (CoreKit::animation_clock_update) work on the accumulators the tick words are the truncations
// of (0x006a0590 frame, 0x006a0598 game: doubles, in ticks). What they must remember is here, host-side: the exact time of the last scene, the exact game time of the last frame and
// the last three frame lengths (retail keeps three whole ones at 0x0056868c). A time that is behind the stored one (a new level or a restarted timer zeroes the accumulators) starts again.
struct SubTick {
    bool scene_known = false;
    double last_scene = 0.0;                 // the frame accumulator when the last scene began
    bool frame_known = false;
    double last_game = 0.0;                  // the game accumulator at the last time step
    double lengths[3] = {0.0, 0.0, 0.0};     // the last three frame lengths in ticks (not stored while paused, as retail)
    std::uint32_t index = 0;
    bool held = false;                       // lockstep: a delivery since the last scene was already held (see lockstep_holds)
};

inline SubTick& sub_tick_state() {
    static SubTick value;
    return value;
}

// the limiter: true while the scene must still wait (retail: tick < last scene tick + 2)
inline bool scene_waits(double frame_time) {
    SubTick& state = sub_tick_state();
    if (!state.scene_known || frame_time < state.last_scene) {
        state.scene_known = true;
        state.last_scene = frame_time;
    }
    return frame_time < state.last_scene + pacing().period_ticks;
}

// F2D: the frame accumulator value at which the limiter lets the next scene begin, exactly (the last scene + the period), or 0 when no scene is known or the time went back.
// Realtime delivery waits for it (a precise wait) and sets the accumulator to it; the limiter's own deadline, so the two cannot disagree.
inline double scene_due(double frame_time) {
    const SubTick& state = sub_tick_state();
    if (!state.scene_known || frame_time < state.last_scene) {
        return 0.0;
    }
    return state.last_scene + pacing().period_ticks;
}

// The same on whole ticks (60 fps, PcSystem::begin_scene's retail test: tick < last scene tick + limiter): the frame accumulator value of the deadline, or 0 when it is not ahead
// by at most one period (a last scene tick that is ahead of the clock is a stale one: the accumulators were zeroed).
inline double whole_tick_due(double frame_time, std::uint32_t last_scene_tick, std::uint32_t limiter) {
    const double due = static_cast<double>(last_scene_tick) + static_cast<double>(limiter);
    return due > frame_time && due - frame_time <= static_cast<double>(limiter) ? due : 0.0;
}

inline void scene_begins(double frame_time) {
    sub_tick_state().scene_known = true;
    sub_tick_state().last_scene = frame_time;
    sub_tick_state().held = false;
}

// Realtime with a frame rate set: count this delivery and return how many there have been since the last scene, this one included. `scene_mark` names the last scene (at 120 its
// exact time, at 60 the last scene tick word 0x0056e840: both change with every scene). A frame in a level has one delivery (Game_FrameTick) or two (the music update's message
// pump, every 32 frames). A loop that waits on the whole tick words and draws no scene (the menus' own two-tick limiters, a loading screen) delivers again and again: from the
// third delivery on, one that brought no whole tick waits for the next one (precisely) instead of spinning (measured: 75 % of a core in the menus at 120 after F2D).
struct IdleCount {
    double mark = -1.0;
    std::uint32_t since = 0;
};

inline IdleCount& idle_count() {
    static IdleCount value;
    return value;
}

inline std::uint32_t count_delivery(double scene_mark) {
    IdleCount& state = idle_count();
    if (scene_mark != state.mark) {
        state.mark = scene_mark;
        state.since = 0;
    }
    return ++state.since;
}
constexpr std::uint32_t kIdleDelivery = 3;

// Lockstep only. A delivery is one scene period of time, and a frame has one (Game_FrameTick). Some frames have a second: the music update pumps the window's messages through
// Game_FrameTick every 32 frames, which in retail takes no time. On whole ticks the limiter absorbs it at 30 fps (and 60 fps gets a two-tick frame every 32); here it would make
// that frame two periods long. So the first delivery that finds the scene's period already passed adds no time. Only the first: a loop that waits for ticks and draws no scene
// (a loading screen) still sees the clock move.
inline bool lockstep_holds(double frame_time) {
    SubTick& state = sub_tick_state();
    if (!state.scene_known || state.held || frame_time < state.last_scene + pacing().period_ticks) {
        return false;
    }
    state.held = true;
    return true;
}

// CoreKit::fps_init: retail stores three frames of two ticks and a last frame two ticks ago; here three periods and a last frame one period ago
inline void sub_tick_restart(double game_time) {
    SubTick& state = sub_tick_state();
    for (double& length : state.lengths) {
        length = pacing().period_ticks;
    }
    state.index = 0;
    state.frame_known = true;
    state.last_game = game_time - pacing().period_ticks;
}

// the length of this frame in ticks (the game time since the last call), stored as one of the three unless paused
inline double frame_length(double game_time, bool paused) {
    SubTick& state = sub_tick_state();
    double length = pacing().period_ticks;
    if (state.frame_known && game_time >= state.last_game) {
        length = game_time - state.last_game;
    }
    state.frame_known = true;
    state.last_game = game_time;
    if (!paused) {
        state.lengths[state.index] = length;
        state.index = (state.index + 1) % 3;
    }
    return length;
}

// the frame scale (0x0056865c) from the three lengths: retail is sum x 256 / 6 with a sum above 15 counted as 15 and an empty sum as 1 tick; here rounded, and at least 1
inline std::uint32_t sub_tick_scale() {
    const SubTick& state = sub_tick_state();
    double sum = state.lengths[0] + state.lengths[1] + state.lengths[2];
    if (sum > 15.0) {
        sum = 15.0;
    }
    const long scale = std::lround(sum * 256.0 / 6.0);
    return scale < 1 ? 1u : static_cast<std::uint32_t>(scale);
}

// the frame step (0x0056a93c): retail is whole ticks << 11
inline std::uint32_t sub_tick_step(double length) { return static_cast<std::uint32_t>(std::lround(length * 2048.0)); }

// What a frame counter adds each frame. Retail adds 1 (every frame is two ticks); with the flag, the whole 30 Hz frames this frame is worth, which the game's own clock
// (CoreKit::animation_clock_update) keeps in `frames_this_frame`, the odd tick carried to the next frame.
// The air move is explicit Euler: position += velocity x dt, and the pull is added to the velocity after it. Over time t that leaves the position A t²/2 - A dt t / 2
// (A the pull for one retail frame, dt the frame in retail frames = scale / 256), so a frame half as long has half the error and a jump at 60 fps comes out about 5.5 %
// lower than retail's. This is the displacement a frame of `scale` must add (A dt (dt - 1) / 2) to follow retail's own trajectory at every retail frame; 0 with the
// flag off or at scale 256 (retail).
inline std::int32_t retail_step_offset(std::int32_t pull, std::int32_t scale) {
    if (!pacing().count_30hz || scale >= 256 || scale <= 0) {
        return 0;
    }
    return static_cast<std::int32_t>(-(static_cast<std::int64_t>(pull) * scale * (256 - scale)) / (2 * 256 * 256));
}

template <typename Frames>
inline std::uint32_t frame_count_step(Frames&& frames_this_frame) { return pacing().count_30hz ? static_cast<std::uint32_t>(frames_this_frame()) : 1u; }   // lazy: retail's path reads nothing new

// F1.5B: the point timer. TrickScore_AdvancePointTimer credits (step x share) >> 12 points a call, step being this frame's ticks << 11 (0x1000 at 30 fps, 0x800 at 60) and share the points
// the caller worked out for a retail frame. A frame half as long credits half of it, and the shift throws the half point away (a share of 1 earned nothing at 60). With the flag the part
// below a point is carried to the next call, per skater and host-side; a step of 0x1000 adds no fraction, so a whole 30 Hz frame credits what retail does. `product` is retail's step x share.
inline std::int32_t point_credit(std::uint32_t skater, std::int32_t product) {
    if (!pacing().count_30hz) {
        return product >> 12;
    }
    static std::unordered_map<std::uint32_t, std::int32_t> carry;
    const std::int64_t total = static_cast<std::int64_t>(product) + carry[skater];
    carry[skater] = static_cast<std::int32_t>(total & 0xfff);
    return static_cast<std::int32_t>(total >> 12);
}

// F1.5B: a trick script's rate points (command 0x17, a manual's 1 a frame) are scaled by the frame (scale x a >> 8: 256 is a 30 fps frame) and then again by the point timer's step, so at
// 60 fps a point was halved twice and a = 1 became 0. With the flag the first factor is left out: the step alone carries the time.
inline std::int32_t script_rate_points(std::int32_t scale, std::int32_t a) {
    return pacing().count_30hz ? a : static_cast<std::int32_t>(static_cast<std::uint32_t>(scale) * static_cast<std::uint32_t>(a)) >> 8;
}

// F1.5C. The game's accelerations are in units of a retail frame (the move and the speed update scale them once: v += a x scale >> 8, x += a x scale^2 >> 17). A brake that multiplies its
// own acceleration by the scale as well (`brake` in the ground physics: the autokick's drag, the stops) is scaled twice, which is invisible at scale 256 and halves the brake at 60.
// With the flag the term is left as it is (the step scales it once).
inline std::int32_t acceleration_term(std::int32_t value, std::int32_t scale) {
    return pacing().count_30hz ? value : static_cast<std::int32_t>(static_cast<std::uint32_t>(value) * static_cast<std::uint32_t>(scale)) >> 8;
}

// A threshold on the frame clock written `scale x n >> 8` (retail: a window of n clock units, the clock itself advancing scale x 2 / 256 a frame). The clock already counts real time,
// so the scale in the threshold halves the window at 60 fps. With the flag the window is n clock units whatever the frame length.
inline std::int32_t clock_threshold(std::int32_t scale, std::int32_t n) {
    return pacing().count_30hz ? n : static_cast<std::int32_t>(static_cast<std::uint32_t>(scale) * static_cast<std::uint32_t>(n)) >> 8;
}

// A smoothing written "take a 2^-shift of the value away each frame" (a decay by 1 - r in a retail frame). A frame of `scale` / 256 retail frames takes q = 1 - (1 - r)^(scale / 256),
// so the decay a second keeps retail's rate. Returns the amount to take away; retail's `value >> shift` with the flag off and for a whole retail frame (scale 256 or more).
template <typename Scale>
inline std::int32_t decay_amount(std::int32_t value, int shift, Scale&& scale_of) {      // lazy: retail's path reads no scale
    const std::int32_t scale = pacing().count_30hz ? static_cast<std::int32_t>(scale_of()) : 256;
    if (scale >= 256 || scale <= 0) {
        return value >> shift;
    }
    const double share = 1.0 - std::pow(1.0 - std::ldexp(1.0, -shift), scale / 256.0);
    return static_cast<std::int32_t>(value * share);
}

// A step that retail makes every frame and that has no scale in it (an eased vector, a random kick): with the flag, the part of it a frame of `scale` / 256 retail frames owes. Whole numbers only:
// `amount x scale >> 8` for scale under 256, retail's amount otherwise and with the flag off.
template <typename Scale>
inline std::int32_t scaled_step(std::int32_t amount, Scale&& scale_of) {      // lazy: retail's path reads no scale
    const std::int32_t scale = pacing().count_30hz ? static_cast<std::int32_t>(scale_of()) : 256;
    if (scale >= 256 || scale <= 0) {
        return amount;
    }
    return static_cast<std::int32_t>((static_cast<std::int64_t>(amount) * scale) >> 8);
}

// F2B (120 fps). Retail's `value x scale >> 8` on a small value. At scale 64 the shift drops up to three quarters of a unit every frame and floors a negative value a whole unit down:
// a turn of 45 a retail frame became 4 x 11 = 44 to the right and 4 x 12 = 48 to the left, and a balance bar's small terms vanished one way and quadrupled the other. With
// `carry_fraction` (set at 120 only: 30 and 60 keep retail's expression) the low 8 bits are kept for the next call, per site and object, host-side, so the frames of a retail frame
// add up to retail's value. `product` is retail's value x scale.
enum class CarrySite : std::uint32_t { ground_turn, air_spin, rail_swing, rail_push, manual_lean, manual_push };

inline std::int32_t scaled_carry(CarrySite site, std::uint32_t object, std::int32_t product) {
    if (!pacing().carry_fraction) {
        return product >> 8;
    }
    static std::unordered_map<std::uint64_t, std::int32_t> carry;
    std::int32_t& kept = carry[(static_cast<std::uint64_t>(site) << 32) | object];
    const std::int64_t total = static_cast<std::int64_t>(product) + kept;
    kept = static_cast<std::int32_t>(total & 0xff);
    return static_cast<std::int32_t>(total >> 8);
}

// F1.5F: the 30 Hz gate. Effects and world objects were written for one update per retail frame, with rates and lifetimes counted in updates and no scale. Instead of editing each one, the
// update runs only on frames worth a whole 30 Hz frame (0x0056a940 > 0: every other frame at 60 fps) and, on the frames it skips, the time it missed is kept (`GateTime`) and handed to
// the next run in the frame scale and step words (0x0056865c, 0x0056a93c), so what is scaled by them moves as at 30 fps. The flag off: always runs, nothing read, nothing kept.
template <typename Frames>
inline bool runs_30hz_frame(Frames&& frames_this_frame) { return !pacing().count_30hz || frames_this_frame() != 0; }   // lazy: retail's path reads nothing new

struct GateTime {
    std::uint32_t scale = 0;   // the frame scales (0x0056865c) of the frames since the gated update last ran, this one included
    std::uint32_t step = 0;    // and their frame steps (0x0056a93c)
};

inline GateTime& gate_time() {
    static GateTime value;
    return value;
}

// F1.5G: an amount retail applies once a call, a call being a 30 fps frame (the music fade, 3 a call). The calls come at the scene rate (30 fps in a menu, 60 in a level), so with the flag the amount
// follows the public ticks since the previous call instead (2 ticks = retail's whole amount; a longer gap counts as 2), the part below a whole number carried, host-side. Flag off: `per_call`, and
// the tick is never read.
struct PerCall {
    std::uint32_t last = 0;    // the public tick of the previous call
    std::int64_t carry = 0;    // in units of a half call
};

inline PerCall& per_call_state() {
    static PerCall value;
    return value;
}

template <typename Tick>
inline std::int32_t per_call_amount(std::int32_t per_call, Tick&& tick_of) {   // lazy: retail's path reads nothing new
    if (!pacing().count_30hz) {
        return per_call;
    }
    PerCall& state = per_call_state();
    const std::uint32_t tick = static_cast<std::uint32_t>(tick_of());
    std::uint32_t gap = tick - state.last;
    state.last = tick;
    gap = gap > 2 ? 2 : gap;
    state.carry += static_cast<std::int64_t>(per_call) * gap;
    const std::int64_t whole = state.carry / 2;
    state.carry -= whole * 2;
    return static_cast<std::int32_t>(whole);
}

// A held direction's count in the menus a level runs (CMenu_Update 0x0044bbc0: the pause and end-of-run menus; Pad_RepeatDirection 0x004ac370: the high score letters).
// Retail adds 1 a call, moves at once on the first and then on every fourth count after sixteen; in a level these screens are called once a scene, so at 120 a hold repeated
// after 0.13 s and every 33 ms, and a slow tap moved twice. With count_30hz the count goes up once per two ticks of the public tick word (a 30 Hz frame of time) and the
// caller runs retail's test once per step: `first` (the count is 0) is one step, as retail's first call; at 30 fps every call is two ticks, one step, retail's count.
struct HoldClock {
    std::uint32_t last = 0;
    std::uint32_t carry = 0;
};
inline HoldClock& menu_hold_clock() {
    static HoldClock value;
    return value;
}
inline HoldClock& pad_repeat_clock() {
    static HoldClock value;
    return value;
}
template <typename Tick>
inline std::uint32_t hold_steps(HoldClock& clock, bool first, Tick&& tick_of) {   // lazy: retail's path reads nothing new
    if (!pacing().count_30hz) {
        return 1u;
    }
    const std::uint32_t tick = static_cast<std::uint32_t>(tick_of());
    if (first) {
        clock.last = tick;
        clock.carry = 0;
        return 1u;
    }
    const std::uint32_t gap = tick - clock.last;
    clock.last = tick;
    clock.carry += gap > 2 ? 2 : gap;
    const std::uint32_t whole = clock.carry / 2;
    clock.carry -= whole * 2;
    return whole;
}

// Cars and platforms at 60 and 120 fps. The gate above makes them move in 30 Hz steps, which shows as choppiness. Their real state is left alone (so their speed is retail's); on the frames
// the gate skips, the object is drawn from the point between where it stood before its last update and where it stands now. `MotionSnap` is the state before the update (host-side).
struct MotionSnap {
    std::uint32_t pos[3] = {0, 0, 0};   // the object's position words (+8, +0xc, +0x10)
    std::uint32_t yaw = 0;              // +0x16, 12 bits
    std::uint32_t pitch = 0;            // +0x14, 12 bits
    std::uint32_t roll = 0;             // +0x18, 12 bits (platforms only: a car's is not blended)
    bool platform = false;              // a platform (vtable 0x005194f8, drawn from its own words) rather than a car (its transform rebuilt)
};

struct MotionState {
    std::unordered_map<std::uint32_t, MotionSnap> before;   // by object address, taken before the gated update ran
    std::unordered_map<std::uint32_t, MotionSnap> real;     // the real state of an object whose drawn point is in its words, to put back at the start of the next frame
    std::unordered_map<std::uint32_t, MotionSnap> drawn;    // the point written for the draw: the real state goes back only while the words still hold it (nothing else moved the object)
    std::uint32_t ran_scale = 0;                            // the frame scale that update was handed (the length of the step it made)
};

inline MotionState& motion_state() {
    static MotionState value;
    return value;
}

// a + (b - a) x num / den on position words (two's complement); a step too long to be motion (a car put back at the start of its road) is not blended: b is returned
inline std::uint32_t blend_position(std::uint32_t a, std::uint32_t b, std::uint32_t num, std::uint32_t den) {
    const std::int64_t delta = static_cast<std::int32_t>(b - a);
    if (den == 0 || delta > 0x200000 || delta < -0x200000) {
        return b;
    }
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(a) + static_cast<std::int32_t>(delta * num / den));
}

// the same on a 12-bit angle, along the short way round
inline std::uint32_t blend_angle(std::uint32_t a, std::uint32_t b, std::uint32_t num, std::uint32_t den) {
    std::int32_t delta = static_cast<std::int32_t>((b - a) & 0xfff);
    if (delta > 0x7ff) {
        delta -= 0x1000;
    }
    if (den == 0) {
        return b & 0xfff;
    }
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(a) + static_cast<std::int32_t>(static_cast<std::int64_t>(delta) * num / den)) & 0xfff;
}

}  // namespace opentony::runtime::framepacing
