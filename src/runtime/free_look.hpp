// The free look (docs: THPS2 post_phase_5/camera.md): the player turns the in-level camera about the skater with a stick. Host-side state only, nothing in guest memory: the game,
// the replays and the end-of-run swing own the camera's spin word (+0x5b4) and the pitch and distance globals, and the look is added where the camera code reads them.
//
// The camera code calls `advance` once per camera update (CameraKit::update, before its mode switch) and reads the look at four places, each through a helper here whose
// at-rest path returns the game's value untouched and reads nothing more (not even the camera's player number):
//   yaw       CameraKit::turn_target_about_y (camera_modes.cpp): added to the spin, the turn about the camera's own up axis. An orbit about the skater.
//             +yaw puts the camera on the skater's right, looking left; -yaw looks right (so a stick pushed right is a negative yaw). C1.
//   pitch     CameraKit::smooth_and_validate (camera_smooth.cpp, the plain branch): added to the pitch half word 0x00524a94. +pitch raises the camera and looks down.
//   distance  smooth_and_validate: added to the distance 0x00524a98 where it enters the running average (camera_distance_q4). For C8 (pulling the camera in front of a wall).
//   step      smooth_and_validate, the plain branch's steering: the larger of the game's step and this one (0: the game's).
// Angles are the game's 12-bit ones (0x1000 a turn). The sums are not wrapped at 12 bits: the quaternion helpers take a short and halve it, and a sum that went past 0xfff
// would come out as the negated quaternion (the same turn, other bits). Integers only, so the 32-bit and 64-bit builds agree.
//
// The standalone sets the configuration (hooks/kits/standalone_freelook.cpp); the hooked build never does, so it reads the game's values everywhere.
#pragma once

#include <cstdint>

namespace opentony::runtime::freelook {

constexpr int kPlayers = 2;   // a camera's player and viewport number (CCamera +0x0c8): 0 or 1

enum class Mode { spring, stay };   // spring: the stick's position is the look, released it goes home (Tony Hawk 3 and 4). stay (C9): the stick turns it and it stays

struct Config {
    bool enabled = false;
    Mode mode = Mode::spring;
    int sensitivity = 5;        // 1 to 10
    bool invert_x = false;
    bool invert_y = false;
};

// what the host's poll reads from the look stick: the HAL's values, -32768 to 32767, x positive right, y positive down (hal/input.h)
struct Input {
    std::int32_t x = 0;
    std::int32_t y = 0;
    bool recentre = false;
};

// the offsets a camera takes this frame
struct Look {
    std::int32_t yaw = 0;
    std::int32_t pitch = 0;
    std::int32_t distance = 0;
    std::int32_t step = 0;
    [[nodiscard]] bool offsets_home() const { return yaw == 0 && pitch == 0 && distance == 0; }   // the picture is the game's (the step may still be tapering)
    [[nodiscard]] bool at_rest() const { return offsets_home() && step == 0; }
    bool operator==(const Look&) const = default;
};

struct State {
    Config config;
    Input input[kPlayers];
    Look look[kPlayers];
    bool fixed = false;         // OT_FREELOOK_FIXED (a test switch): this look, held, for every player while allowed
    Look fixed_look;
    bool moved = false;         // some player's look is not at rest: only then do the read sites ask for the camera's player
    bool polled[kPlayers] = {}; // an input poll came since the player's camera last stepped the look: the look steps only then (C5: the ten warm-up updates of a level start have no poll)
    std::int32_t tail[kPlayers] = {};   // C7: what is left of the camera's quick steering after the look came home, in 1/256 of a 30 Hz frame (kTail at the start); Look::step tapers with it
};

inline State& state() {
    static State value;
    return value;
}

inline void refresh_moved() {
    State& s = state();
    s.moved = false;
    for (const Look& look : s.look) {
        s.moved = s.moved || !look.at_rest();
    }
}

inline void set_config(const Config& config) {
    State& s = state();
    s.config = config;
    if (!config.enabled) {
        for (Look& look : s.look) {
            look = Look{};
        }
        for (std::int32_t& tail : s.tail) {
            tail = 0;
        }
        s.moved = false;
    }
}

inline void set_input(int player, const Input& input) {
    if (player >= 0 && player < kPlayers) {
        state().input[player] = input;
        state().polled[player] = true;
    }
}

// A camera was placed (built, or reset behind its skater: CameraKit::construct, reset_orientation): its player's look is home at once, and the next step waits for a poll.
inline void place(int player) {
    State& s = state();
    if (player >= 0 && player < kPlayers) {
        s.look[player] = Look{};
        s.tail[player] = 0;
        s.polled[player] = false;
        refresh_moved();
    }
}

// a held look (OT_FREELOOK_FIXED); it switches the look on
inline void set_fixed(const Look& look) {
    State& s = state();
    s.fixed = true;
    s.fixed_look = look;
    s.config.enabled = true;
}

inline void reset() { state() = State{}; }

[[nodiscard]] inline bool enabled() { return state().config.enabled; }

[[nodiscard]] inline Look look(int player) {
    return player >= 0 && player < kPlayers ? state().look[player] : Look{};
}

// ---- the control law (C4): the stick to the wanted look; pure, integers only ----

constexpr std::int32_t kStickMax = 32767;
constexpr std::int32_t kDeadZone = 8000;        // of 32767, radial
constexpr std::int32_t kYawMax = 0x555;         // a third of a turn each side, at sensitivity 5
constexpr std::int32_t kPitchUpMax = 0x100;     // stick up: pitch -0x100 (looks up)
constexpr std::int32_t kPitchDownMax = 0x200;   // stick down: pitch +0x200 (looks down on the skater)
constexpr std::int32_t kPitchLimitUp = 0x180;   // what sensitivity may reach (C1: sane range -0x180 .. +0x300)
constexpr std::int32_t kPitchLimitDown = 0x300;

[[nodiscard]] inline std::int32_t isqrt(std::int64_t value) {
    std::int64_t root = 0;
    for (std::int64_t bit = std::int64_t{1} << 31; bit != 0; bit >>= 1) {
        if ((root + bit) * (root + bit) <= value) {
            root += bit;
        }
    }
    return static_cast<std::int32_t>(root);
}

// The stick after the radial dead zone and the squared curve: each component -32767 to 32767, the edge of the dead zone zero, full deflection full.
struct Stick {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

[[nodiscard]] inline Stick shape_stick(std::int32_t x, std::int32_t y) {
    const std::int64_t magnitude = isqrt(static_cast<std::int64_t>(x) * x + static_cast<std::int64_t>(y) * y);
    if (magnitude <= kDeadZone) {
        return {};
    }
    const std::int64_t clipped = magnitude > kStickMax ? kStickMax : magnitude;
    const std::int64_t rescaled = (clipped - kDeadZone) * kStickMax / (kStickMax - kDeadZone);
    const std::int64_t curved = rescaled * rescaled / kStickMax;
    return {static_cast<std::int32_t>(x * curved / magnitude), static_cast<std::int32_t>(y * curved / magnitude)};
}

// The look the stick asks for in spring mode. A stick pushed right is a negative yaw (+yaw looks left); the HAL's y is positive down, and down is +pitch (looks down on the
// skater). Sensitivity 5 is the base reach; 10 doubles it. Invert flips an axis.
[[nodiscard]] inline Look wanted(const Config& config, const Input& input) {
    const Stick stick = shape_stick(input.x, input.y);
    Look look;
    const std::int64_t sens = config.sensitivity < 1 ? 1 : config.sensitivity > 10 ? 10 : config.sensitivity;
    std::int64_t yaw = -static_cast<std::int64_t>(stick.x) * kYawMax * sens / (5 * kStickMax);
    const std::int64_t pitch_max = stick.y < 0 ? kPitchUpMax : kPitchDownMax;
    std::int64_t pitch = static_cast<std::int64_t>(stick.y) * pitch_max * sens / (5 * kStickMax);
    if (config.invert_x) {
        yaw = -yaw;
    }
    if (config.invert_y) {
        pitch = -pitch;
    }
    pitch = pitch < -kPitchLimitUp ? -kPitchLimitUp : pitch > kPitchLimitDown ? kPitchLimitDown : pitch;
    look.yaw = static_cast<std::int32_t>(yaw);
    look.pitch = static_cast<std::int32_t>(pitch);
    return look;
}

// ---- feel (C7): one smoothing the same at 30, 60 and 120 fps ----
// A share of the gap closed in one 30 Hz frame (a 30 fps frame, time scale 256), in 1/0x10000, and the share a frame of `time_scale` closes so that the gap left after any time is the
// same whatever the frame rate: remaining = (1 - share) ^ (time_scale / 256), by one integer square root per bit of the scale. (The game's own slerp step is linear in the scale
// instead, which at 120 fps leaves 8 % more of a swing after the same time than at 30: a swing the game's camera makes is not this one.) Scales above 256 (a slow frame) count as 256.
[[nodiscard]] inline std::int32_t frame_share(std::int32_t share, std::int32_t time_scale) {
    if (time_scale <= 0 || share <= 0) {
        return 0;
    }
    if (time_scale >= 256 || share >= 0x10000) {
        return share > 0x10000 ? 0x10000 : share;
    }
    std::int64_t root = 0x10000 - share;
    std::int64_t power = 0x10000;
    for (std::int32_t bit = 128; bit != 0; bit >>= 1) {
        root = isqrt(root << 16);   // remaining ^ (bit / 256)
        if ((time_scale & bit) != 0) {
            power = power * root >> 16;
        }
    }
    return static_cast<std::int32_t>(0x10000 - power);
}

constexpr std::int32_t kEaseShare = 0x6000;   // the look follows the wanted look by 3/8 of the gap a 30 Hz frame (before the camera's own steering, below)
constexpr std::int32_t kStepStock = 0x137;    // the game's steering step in the plain branch: 7.6 % of the gap a 30 Hz frame
constexpr std::int32_t kStepHigh = 0x3a0;     // while the look is out: 23 % a 30 Hz frame (steps are 1/0x1000 of the gap at scale 256)
constexpr std::int32_t kTail = 16 * 256;      // the quick steering tapers back to the game's over 16 30 Hz frames (0.53 s) once the look is home, so the picture arrives before it stops

// One value towards its target in a frame of `time_scale` / 256 of a 30 Hz frame: kEaseShare of the gap (frame_share), at least one unit, and exactly the target once within one
// unit, so a released stick ends on zero, which is retail's path.
[[nodiscard]] inline std::int32_t ease(std::int32_t value, std::int32_t target, std::int32_t time_scale) {
    const std::int32_t gap = target - value;
    if (time_scale <= 0) {   // (no time passed)
        return value;
    }
    if (gap >= -1 && gap <= 1) {
        return target;
    }
    std::int64_t move = static_cast<std::int64_t>(gap) * frame_share(kEaseShare, time_scale) / 0x10000;
    if (move == 0) {
        move = gap > 0 ? 1 : -1;
    }
    return static_cast<std::int32_t>(value + move);
}

// The steering step (the game's: slerp factor = time scale x step >> 8, out of 0x1000) that closes the share `step30` / 0x1000 of the gap a 30 Hz frame, in a frame of `time_scale`.
[[nodiscard]] inline std::int32_t step_for(std::int32_t step30, std::int32_t time_scale) {
    const std::int32_t scale = time_scale < 1 ? 1 : time_scale > 256 ? 256 : time_scale;
    const std::int32_t factor = frame_share(step30 * 16, scale) >> 4;
    return (factor * 256 + scale - 1) / scale;
}

// the step while `tail` is left: the quick one at the full tail, the game's at none (a straight taper in the 30 Hz step, then made right for the frame rate)
[[nodiscard]] inline std::int32_t tapered_step(std::int32_t tail, std::int32_t time_scale) {
    const std::int32_t t = tail < 0 ? 0 : tail > kTail ? kTail : tail;
    return t == 0 ? 0 : step_for(kStepStock + (kStepHigh - kStepStock) * t / kTail, time_scale);
}

// ---- when the look may move (C5) ----

// What the camera is doing, read by CameraKit::update only while the look is switched on.
struct Situation {
    int player = 0;              // CCamera +0x0c8
    bool following = false;      // it follows a skater (the tripod +0x3a4 is set)
    std::uint32_t mode = 1;      // +0x504: 1 follow, 2 a simpler follow, 0x17 point (a level point to the skater), 0x18 death, 0x19 landing follow
    bool end_swing = false;      // the end-of-run swing turns the spin (latch 0x00568658 == 1; it runs unfrozen in mode 1)
    bool replay = false;         // a replay plays (0x0056e5d8 == 2): the replay director owns the camera
    bool settling = false;       // the camera's first 12 ticks (+0x510 < 0xc: the level start's warm-up and first frames, where smooth_and_validate copies the target into the picture)
};

// Player one's camera while it follows its skater in one of the follow modes, once settled, outside the end-of-run swing and the replays. (The front end, the skate shop, the
// menu model views and the park editor's edit mode never run CameraKit::update; the editor's Test Play runs the normal camera, so the look works there as in a level. C1.)
[[nodiscard]] inline bool allowed(const Situation& situation) {
    const bool follow_mode = situation.mode == 1 || situation.mode == 2 || situation.mode == 0x19;
    return situation.player == 0 && situation.following && follow_mode && !situation.end_swing && !situation.replay && !situation.settling;
}

// Once per camera update, for the camera's player; the look steps only when an input poll came since its last step (one poll per update in play at 30 and at 60 fps; the
// warm-up updates of a level start have none, so a held stick does not step there). `time_scale` is AnimationTimeScale (0x0056865c: 256 a 30 fps frame, 128 at 60).
// Switched off: home at once. Not allowed: the stick is ignored and the look eases home. The fixed look (a test switch) is held as is while allowed, home at once when not.
// Otherwise the stick's wanted look is eased in (stay mode is spring until C9). `allowed` is a callable, asked only when its answer can change the look (a look at rest with a
// centred stick reads nothing more than today).
template <typename Allowed>
inline void advance(int player, std::int32_t time_scale, Allowed&& allowed) {
    State& s = state();
    if (player < 0 || player >= kPlayers) {
        return;
    }
    Look& look = s.look[player];
    if (!s.config.enabled) {
        look = Look{};
    } else if (s.fixed) {
        look = allowed() ? s.fixed_look : Look{};
    } else {
        if (s.polled[player]) {
            const Look stick = wanted(s.config, s.input[player]);
            if (!(look.offsets_home() && stick.at_rest())) {
                const Look target = allowed() ? stick : Look{};
                look.yaw = ease(look.yaw, target.yaw, time_scale);
                look.pitch = ease(look.pitch, target.pitch, time_scale);
            }
        }
        // promptness: while the look is out the camera steers to it quickly; once it is home the quick steering tapers off (it never stops short of a picture the game's easing would
        // take seconds to finish), and the step goes back to 0, the game's own
        if (!look.offsets_home()) {
            s.tail[player] = kTail;
        } else if (s.tail[player] > 0 && time_scale > 0) {
            s.tail[player] = time_scale < s.tail[player] ? s.tail[player] - time_scale : 0;
        }
        look.step = tapered_step(s.tail[player], time_scale);
    }
    s.polled[player] = false;
    refresh_moved();
}

inline void advance(int player, std::int32_t time_scale, bool allowed) {
    advance(player, time_scale, [allowed] { return allowed; });
}

// ---- the air lean (C5): where the game's own camera would be ----
// SkaterKit's state 1 lean (skater_action_step.cpp) turns up/down around while the camera is behind the skater's +0x30f4 plane. The look is visual only, so while it is not at
// rest the lean asks for the camera without the look. The camera sits at look-at + R x pull (camera_smooth.cpp, R the render quaternion's matrix, pull its local offset);
// the look made the target Qb Y(spin + yaw) X(pitch + look pitch), the game's Qb Y(spin) X(pitch), so the game's is R' = R X(-(pitch + look pitch)) Y(-yaw) X(pitch) and its
// offset R' x pull = R' x R^T x (position - look-at). Not exact: the render quaternion's easing lag is the looked camera's, the landing branch (no pitch turn) is treated as
// the plain one and the effects' small turns are ignored. They matter only when the game's camera would be side-on, where the lean's sign is a toss-up anyway.
// The same quaternion and matrix forms as CameraKit (compose, about_x, about_y, to_matrix; Fixed_MatrixConvertQ12's rows), with the game's own cosine and sine passed in.

struct Quat {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    std::int32_t w = 0x1000;
};

using Vec3 = std::int32_t[3];

[[nodiscard]] inline Quat compose(const Quat& a, const Quat& b) {
    const auto m = [](std::int32_t p, std::int32_t q) { return static_cast<std::int64_t>(p) * q; };
    return {static_cast<std::int32_t>((m(a.x, b.w) - m(a.y, b.z) + m(a.z, b.y) + m(a.w, b.x)) >> 12),
            static_cast<std::int32_t>((m(a.x, b.z) - m(a.z, b.x) + m(a.y, b.w) + m(a.w, b.y)) >> 12),
            static_cast<std::int32_t>((m(a.z, b.w) - m(a.x, b.y) + m(a.y, b.x) + m(a.w, b.z)) >> 12),
            static_cast<std::int32_t>((m(a.w, b.w) - m(a.y, b.y) - m(a.x, b.x) - m(a.z, b.z)) >> 12)};
}

// about_x / about_y: the angle is a short, halved (arithmetic)
template <typename Cos, typename Sin>
[[nodiscard]] inline Quat about(bool y_axis, std::int32_t angle, Cos&& cos_q12, Sin&& sin_q12) {
    const std::int32_t half = static_cast<std::int16_t>(angle) >> 1;
    Quat q;
    q.w = cos_q12(half);
    (y_axis ? q.y : q.x) = sin_q12(half);
    return q;
}

// to_matrix's nine entries, row major
inline void to_matrix(const Quat& q, std::int32_t (&out)[9]) {
    const auto p = [](std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::int16_t>((static_cast<std::int64_t>(a) * b) >> 11)); };
    const std::int32_t xx = 0x1000 - p(q.x, q.x);
    out[0] = 0x1000 - p(q.z, q.z) - p(q.y, q.y);
    out[1] = p(q.z, q.w) + p(q.y, q.x);
    out[2] = p(q.z, q.x) - p(q.y, q.w);
    out[3] = p(q.y, q.x) - p(q.z, q.w);
    out[4] = xx - p(q.z, q.z);
    out[5] = p(q.x, q.w) + p(q.z, q.y);
    out[6] = p(q.y, q.w) + p(q.z, q.x);
    out[7] = p(q.z, q.y) - p(q.x, q.w);
    out[8] = xx - p(q.y, q.y);
}

// The game's camera offset (Q12 world units, like position - look-at) for a camera at `offset` from its look-at, rendering with `render`, under `look`; `game_pitch` is the half
// word 0x00524a94 the camera turned its target by.
template <typename Cos, typename Sin>
inline void stock_offset(const Quat& render, std::int32_t game_pitch, const Look& look, const Vec3& offset, Vec3& out, Cos&& cos_q12, Sin&& sin_q12) {
    const Quat undo = compose(compose(about(false, -(game_pitch + look.pitch), cos_q12, sin_q12), about(true, -look.yaw, cos_q12, sin_q12)),
                              about(false, game_pitch, cos_q12, sin_q12));
    std::int32_t r[9];
    std::int32_t g[9];
    to_matrix(render, r);
    to_matrix(compose(render, undo), g);
    std::int64_t pull[3];
    for (int j = 0; j < 3; ++j) {
        pull[j] = (static_cast<std::int64_t>(r[j]) * offset[0] + static_cast<std::int64_t>(r[3 + j]) * offset[1] + static_cast<std::int64_t>(r[6 + j]) * offset[2]) >> 12;
    }
    for (int i = 0; i < 3; ++i) {
        out[i] = static_cast<std::int32_t>((g[3 * i] * pull[0] + g[3 * i + 1] * pull[1] + g[3 * i + 2] * pull[2]) >> 12);
    }
}

// ---- the read sites. `player_of` reads the camera's player (cam_at(m_, self).field_0c8()); it is called only while a look is moved ----

template <typename Player>
[[nodiscard]] inline std::int32_t offset(std::int32_t Look::*field, Player&& player_of) {
    if (!state().moved) {
        return 0;
    }
    return look(static_cast<int>(player_of())).*field;
}

// the spin (a half word) the camera turns its target by
template <typename Player>
[[nodiscard]] inline std::uint32_t spin(std::uint32_t game, Player&& player_of) {
    const std::int32_t yaw = offset(&Look::yaw, player_of);
    return yaw == 0 ? game : game + static_cast<std::uint32_t>(yaw);
}

// the pitch half word 0x00524a94
template <typename Player>
[[nodiscard]] inline std::uint32_t pitch(std::uint32_t game, Player&& player_of) {
    const std::int32_t value = offset(&Look::pitch, player_of);
    return value == 0 ? game : game + static_cast<std::uint32_t>(value);
}

// the distance word 0x00524a98 where it enters the running average
template <typename Player>
[[nodiscard]] inline std::uint32_t distance(std::uint32_t game, Player&& player_of) {
    const std::int32_t value = offset(&Look::distance, player_of);
    return value == 0 ? game : game + static_cast<std::uint32_t>(value);
}

// the steering step of the plain branch
template <typename Player>
[[nodiscard]] inline std::uint32_t step(std::uint32_t game, Player&& player_of) {
    const std::int32_t value = offset(&Look::step, player_of);
    return value <= 0 || static_cast<std::uint32_t>(value) <= game ? game : static_cast<std::uint32_t>(value);
}

}  // namespace opentony::runtime::freelook
