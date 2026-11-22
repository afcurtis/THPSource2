// free_look.hpp: the default is no look and the read sites return the game's values without asking for the camera's player; a fixed look comes out of look() once advanced,
// for both players, while allowed; switched off it is gone at once.
#include "free_look.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace fl = opentony::runtime::freelook;

static int g_failures = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #c);    \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

// one camera update after one input poll, as the standalone does it (one poll per update in play)
static void poll_and_advance(int player, const fl::Input& input, int scale, bool allowed) {
    fl::set_input(player, input);
    fl::advance(player, scale, allowed);
}

static std::int32_t cos_q12(std::int32_t a) { return static_cast<std::int32_t>(std::lround(std::cos(a * 6.283185307179586 / 4096.0) * 4096.0)); }
static std::int32_t sin_q12(std::int32_t a) { return static_cast<std::int32_t>(std::lround(std::sin(a * 6.283185307179586 / 4096.0) * 4096.0)); }

int main() {
    // the default: off, no look, and the read sites return the game's value without reading the player
    CHECK(!fl::enabled());
    CHECK(fl::look(0).at_rest() && fl::look(1).at_rest());
    bool read = false;
    const auto player = [&] { read = true; return 0u; };
    CHECK(fl::spin(0x9c0, player) == 0x9c0u && fl::pitch(0x96, player) == 0x96u && fl::distance(0x122, player) == 0x122u && fl::step(0x137, player) == 0x137u);
    CHECK(!read);
    fl::advance(0, 256, true);
    CHECK(fl::look(0).at_rest() && !fl::state().moved);

    // switched on with nothing held: still at rest, still no read
    fl::Config on;
    on.enabled = true;
    fl::set_config(on);
    fl::advance(0, 256, true);
    fl::advance(1, 256, true);
    CHECK(fl::look(0).at_rest() && !fl::state().moved);
    CHECK(fl::spin(0x10, player) == 0x10u && !read);

    // a fixed look: copied into the look of the camera that advances, while allowed
    fl::Look fixed;
    fixed.yaw = 0x400;
    fixed.pitch = -0x100;
    fixed.distance = -0x20;
    fl::set_fixed(fixed);
    CHECK(fl::enabled());
    CHECK(fl::look(0).at_rest());          // (not before its camera advances)
    fl::advance(0, 256, true);
    CHECK(fl::look(0) == fixed && fl::look(1).at_rest() && fl::state().moved);
    fl::advance(1, 128, true);
    CHECK(fl::look(1) == fixed);
    CHECK(fl::look(2).at_rest() && fl::look(-1).at_rest());

    // the read sites add the offsets: not wrapped at 12 bits (the quaternion helpers take a short)
    read = false;
    CHECK(fl::spin(0x9c0, player) == 0xdc0u && read);
    CHECK(fl::spin(0xe00, player) == 0x1200u);
    CHECK(fl::pitch(0x1e, player) == static_cast<std::uint32_t>(0x1e - 0x100));
    CHECK(static_cast<std::int16_t>(fl::pitch(0x1e, player)) == 0x1e - 0x100);
    CHECK(fl::distance(0x122, player) == 0x102u);
    CHECK(fl::step(0x137, player) == 0x137u);   // (no step in this look)
    // a player out of range reads no look
    CHECK(fl::spin(0x9c0, [] { return 7u; }) == 0x9c0u);

    // the step: the larger of the game's and the look's
    fixed.step = 0x500;
    fl::set_fixed(fixed);
    fl::advance(0, 256, true);
    CHECK(fl::step(0x137, [] { return 0u; }) == 0x500u);
    CHECK(fl::step(0x600, [] { return 0u; }) == 0x600u);

    // not allowed: that camera's look goes home; the other keeps its own
    fl::advance(0, 256, false);
    CHECK(fl::look(0).at_rest() && fl::look(1).yaw == 0x400 && fl::state().moved);
    fl::advance(1, 256, false);
    CHECK(!fl::state().moved);
    CHECK(fl::spin(0x9c0, player) == 0x9c0u);

    // switched off: every look home at once
    fl::advance(0, 256, true);
    fl::advance(1, 256, true);
    CHECK(fl::state().moved);
    fl::set_config(fl::Config{});
    CHECK(!fl::enabled() && fl::look(0).at_rest() && fl::look(1).at_rest() && !fl::state().moved);
    fl::advance(0, 256, true);
    CHECK(fl::look(0).at_rest());

    // input is kept per player, out of range ignored
    fl::set_input(0, fl::Input{-32768, 32767, true});
    fl::set_input(5, fl::Input{1, 1, false});
    CHECK(fl::state().input[0].x == -32768 && fl::state().input[0].y == 32767 && fl::state().input[0].recentre && fl::state().input[1].x == 0);

    fl::reset();
    CHECK(!fl::enabled() && !fl::state().fixed);

    // ---- C4: the control law ----
    // dead zone: radial, so a diagonal inside the circle is nothing, and the edge is zero
    CHECK(fl::shape_stick(0, 0).x == 0);
    CHECK(fl::shape_stick(7999, 0).x == 0 && fl::shape_stick(5000, 5000).x == 0 && fl::shape_stick(5000, 5000).y == 0);
    CHECK(fl::shape_stick(8000, 0).x == 0);
    CHECK(fl::shape_stick(9000, 0).x > 0 && fl::shape_stick(9000, 0).x < 100);
    // full deflection is full, either sign, and the curve is squared (half way is a quarter)
    CHECK(fl::shape_stick(32767, 0).x == 32767 && fl::shape_stick(-32768, 0).x == -32767 && fl::shape_stick(0, 32767).y == 32767);
    const int half = fl::shape_stick(8000 + (32767 - 8000) / 2, 0).x;
    CHECK(half > 32767 / 4 - 20 && half < 32767 / 4 + 20);
    CHECK(fl::shape_stick(32767, 32767).x == fl::shape_stick(32767, 32767).y && fl::shape_stick(32767, 32767).x < 32767 + 1);
    // maxima: stick right is -yaw, stick down is +pitch, up reaches less than down; sensitivity 5 is the base, 10 doubles (the pitch within the limits)
    fl::Config c;
    c.enabled = true;
    fl::Look w = fl::wanted(c, fl::Input{32767, 0, false});
    CHECK(w.yaw == -0x555);
    CHECK(w.pitch == 0);
    w = fl::wanted(c, fl::Input{-32768, 0, false});
    CHECK(w.yaw == 0x555);
    w = fl::wanted(c, fl::Input{0, 32767, false});
    CHECK(w.yaw == 0 && w.pitch == 0x200);
    w = fl::wanted(c, fl::Input{0, -32768, false});
    CHECK(w.pitch == -0x100);
    c.sensitivity = 10;
    w = fl::wanted(c, fl::Input{0, 32767, false});
    CHECK(w.pitch == 0x300 && fl::wanted(c, fl::Input{32767, 0, false}).yaw == -0xaaa);
    w = fl::wanted(c, fl::Input{0, -32768, false});
    CHECK(w.pitch == -0x180);   // -0x200 limited to -0x180
    c.sensitivity = 1;
    w = fl::wanted(c, fl::Input{32767, 0, false});
    CHECK(w.yaw >= -0x111 && w.yaw <= -0x110);
    // invert flips each axis alone
    c.sensitivity = 5;
    c.invert_x = true;
    CHECK(fl::wanted(c, fl::Input{32767, 0, false}).yaw == 0x555 && fl::wanted(c, fl::Input{0, 32767, false}).pitch == 0x200);
    c.invert_x = false;
    c.invert_y = true;
    CHECK(fl::wanted(c, fl::Input{32767, 0, false}).yaw == -0x555 && fl::wanted(c, fl::Input{0, 32767, false}).pitch == -0x180);   // (-0x200 limited)
    // the easing: always closer, never past, within one unit it lands, a gap of 0 stays
    CHECK(fl::ease(0, 1000, 256) > 0 && fl::ease(0, 1000, 256) < 1000 && fl::ease(0, 1000, 1) >= 1 && fl::ease(0, 1000, 1) <= 3);
    CHECK(fl::ease(1000, 0, 256) < 1000 && fl::ease(1000, 0, 256) >= 0 && fl::ease(1, 0, 256) == 0 && fl::ease(-1, 0, 256) == 0 && fl::ease(5, 5, 256) == 5);
    CHECK(fl::ease(0, 1000, 0) == 0 && fl::ease(1, 0, -5) == 1);   // no time passed: no step
    // the stick drives advance: held, the look reaches the wanted one; released, it ends exactly at rest, in both frame rates, and 60 fps takes twice the steps for the same swing
    for (const int scale : {256, 128}) {
        fl::reset();
        fl::Config spring;
        spring.enabled = true;
        fl::set_config(spring);
        for (int i = 0; i < 60 * 256 / scale; ++i) {
            poll_and_advance(0, fl::Input{32767, 0, false}, scale, true);
        }
        CHECK(fl::look(0).yaw == -0x555 && fl::look(0).pitch == 0 && fl::state().moved);
        CHECK(fl::look(1).at_rest());
        for (int i = 0; i < 60 * 256 / scale; ++i) {
            poll_and_advance(0, fl::Input{}, scale, true);
        }
        CHECK(fl::look(0).at_rest() && !fl::state().moved);
    }
    {   // the same elapsed time gives the same look within a few units
        fl::Config spring;
        spring.enabled = true;
        fl::reset();
        fl::set_config(spring);
        for (int i = 0; i < 3; ++i) poll_and_advance(0, fl::Input{32767, 0, false}, 256, true);
        const int at30 = fl::look(0).yaw;
        fl::reset();
        fl::set_config(spring);
        for (int i = 0; i < 6; ++i) poll_and_advance(0, fl::Input{32767, 0, false}, 128, true);
        const int at60 = fl::look(0).yaw;
        CHECK(at30 - at60 < 100 && at60 - at30 < 100);   // (linear in the scale: a little apart mid-swing, the same at the end)
    }
    fl::reset();

    // ---- C7: the feel, the same at 30, 60 and 120 fps ----
    {
        // frame_share: the gap left after one 30 Hz frame is the same however it is cut into frames (scale 256, 128, 64), within rounding; no time, no share
        const std::int32_t share = 0xa000;
        CHECK(fl::frame_share(share, 256) == share && fl::frame_share(share, 300) == share && fl::frame_share(share, 0) == 0 && fl::frame_share(share, -4) == 0 && fl::frame_share(0, 128) == 0);
        for (const std::int32_t scale : {128, 64, 32}) {
            double left = 1.0;
            for (std::int32_t i = 0; i < 256 / scale; ++i) {
                left *= 1.0 - fl::frame_share(share, scale) / 65536.0;
            }
            CHECK(std::fabs(left - (1.0 - share / 65536.0)) < 0.002);
        }
        CHECK(fl::frame_share(share, 128) < fl::frame_share(share, 256) && fl::frame_share(share, 64) < fl::frame_share(share, 128) && fl::frame_share(share, 1) >= 0);
        // step_for: at a whole frame it is the step itself; cut into frames the slerp factor (scale x step >> 8, out of 0x1000) compounds to the same share
        CHECK(fl::step_for(0x500, 256) == 0x500 && fl::step_for(0x137, 256) == 0x137);
        for (const std::int32_t scale : {128, 64}) {
            double left = 1.0;
            for (std::int32_t i = 0; i < 256 / scale; ++i) {
                left *= 1.0 - ((scale * fl::step_for(0x500, scale)) >> 8) / 4096.0;
            }
            CHECK(std::fabs(left - (1.0 - 0x500 / 4096.0)) < 0.01);
        }
        // the taper: the quick step at the full tail, the game's at none, never below it between, and 0 (the game's own word) when the tail is gone
        CHECK(fl::tapered_step(fl::kTail, 256) == fl::kStepHigh && fl::tapered_step(0, 256) == 0 && fl::tapered_step(1, 256) >= fl::kStepStock - 1);
        for (std::int32_t tail = fl::kTail; tail > 0; tail -= 64) {
            CHECK(fl::tapered_step(tail, 256) >= fl::tapered_step(tail - 64 > 0 ? tail - 64 : 1, 256));
        }
    }
    {
        // The whole path at each frame rate: the stick, the look's own easing (advance) and the camera's steering (the game's step, or the larger quick one), as a pure turn about the
        // skater, so the picture's yaw moves a factor / 4096 of its gap a camera update (CameraKit::smooth_and_validate: factor = scale x step >> 8; the game's step 0x137).
        // A full push held 2 s, then released. Reported in seconds so the three rates can be compared.
        struct Result {
            double rise90 = 0;      // seconds from the push to 90 % of the reach
            double fall_to_5 = 0;   // seconds from the release until the picture is within 5 % of the reach of home
            double at_tail_end = 0; // the picture's distance from home, as a share of the reach, when the quick steering is gone
            double settled = 0;     // the same a second later (the game's easing carries on)
        };
        Result results[3];
        int index = 0;
        for (const int rate : {30, 60, 120}) {
            const int scale = 256 * 30 / rate;
            fl::reset();
            fl::Config spring;
            spring.enabled = true;
            fl::set_config(spring);
            const double reach = 0x555;
            double picture = 0;   // the picture's yaw offset (the render quaternion's turn about the skater)
            Result& r = results[index++];
            int updates = 0;
            const auto update = [&](const fl::Input& input) {
                poll_and_advance(0, input, scale, true);
                const std::int32_t game_step = fl::kStepStock;
                const std::int32_t step = fl::step(game_step, [] { return 0u; });
                const std::int32_t factor = (scale * step) >> 8;
                picture += (-fl::look(0).yaw - picture) * factor / 4096.0;
                ++updates;
            };
            const int hold = 2 * rate;   // seconds x rate = updates
            bool rose = false;
            for (int i = 0; i < hold; ++i) {
                update(fl::Input{32767, 0, false});
                if (!rose && picture >= 0.9 * reach) {
                    rose = true;
                    r.rise90 = updates / static_cast<double>(rate);
                }
            }
            CHECK(rose && fl::look(0).yaw == -0x555);
            const int released_at = updates;
            bool fell = false;
            bool tail_done = false;
            for (int i = 0; i < 6 * rate; ++i) {
                update(fl::Input{});
                if (!fell && std::fabs(picture) <= 0.05 * reach) {
                    fell = true;
                    r.fall_to_5 = (updates - released_at) / static_cast<double>(rate);
                }
                if (!tail_done && fl::look(0).at_rest()) {
                    tail_done = true;
                    r.at_tail_end = std::fabs(picture) / reach;
                    for (int j = 0; j < rate; ++j) {
                        update(fl::Input{});
                    }
                    r.settled = std::fabs(picture) / reach;
                }
            }
            CHECK(fell && tail_done);
            std::printf("free look feel at %3d fps: 90 %% of the swing in %.3f s, back within 5 %% in %.3f s, at the end of the quick steering %.1f %% of the reach out, a second later %.2f %%\n", rate,
                        r.rise90, r.fall_to_5, 100 * r.at_tail_end, 100 * r.settled);
        }
        // prompt (the game's own easing alone takes ~0.5 s to 90 %), and the same at the three rates
        for (const Result& r : results) {
            CHECK(r.rise90 < 0.45 && r.fall_to_5 < 0.60 && r.at_tail_end < 0.05);
            CHECK(std::fabs(r.rise90 - results[0].rise90) <= 0.04 && std::fabs(r.fall_to_5 - results[0].fall_to_5) <= 0.06);
        }
        fl::reset();
    }

    // ---- C5: when the look stands down ----
    {
        fl::Config spring;
        spring.enabled = true;
        fl::set_config(spring);
        const fl::Input right{32767, 0, false};
        // no poll since the last step: the look holds (the ten warm-up updates of a level start); a poll steps it once
        poll_and_advance(0, right, 256, true);
        const fl::Look first = fl::look(0);
        CHECK(first.yaw < 0);
        for (int i = 0; i < 10; ++i) fl::advance(0, 256, true);
        CHECK(fl::look(0) == first);
        poll_and_advance(0, right, 256, true);
        CHECK(fl::look(0).yaw < first.yaw);
        // polls while the camera does not update (the pause): one step on resume, not one per poll
        for (int i = 0; i < 5; ++i) fl::set_input(0, right);
        const fl::Look before = fl::look(0);
        fl::advance(0, 256, true);
        CHECK(fl::look(0).yaw == fl::ease(before.yaw, -0x555, 256));
        // not allowed: the stick is ignored and the look eases home, ending exactly at rest
        const fl::Look held_before = fl::look(0);
        poll_and_advance(0, right, 256, false);
        CHECK(fl::look(0).yaw < 0 && fl::look(0).yaw > held_before.yaw);
        for (int i = 0; i < 40; ++i) poll_and_advance(0, right, 256, false);
        CHECK(fl::look(0).at_rest() && !fl::state().moved);
        // a placed camera (built, or reset behind the skater): home at once, and the next step waits for a poll
        for (int i = 0; i < 30; ++i) poll_and_advance(0, right, 256, true);
        CHECK(fl::look(0).yaw == -0x555);
        fl::set_input(0, right);
        fl::place(0);
        CHECK(fl::look(0).at_rest() && !fl::state().moved);
        fl::advance(0, 256, true);
        CHECK(fl::look(0).at_rest());
        fl::place(1);
        fl::place(7);
        CHECK(fl::look(0).at_rest());
        // the fixed test look ignores polls; not allowed it is home at once
        fl::Look held;
        held.yaw = 0x800;
        fl::set_fixed(held);
        fl::advance(0, 256, true);
        CHECK(fl::look(0) == held);
        fl::advance(0, 256, false);
        CHECK(fl::look(0).at_rest());
        fl::reset();
    }
    {   // allowed: player one, following a skater, in a follow mode, outside the end-of-run swing and the replays
        const fl::Situation play{0, true, 1, false, false};
        CHECK(fl::allowed(play));
        struct Row {
            fl::Situation situation;
            bool allowed;
        };
        const Row rows[] = {
            {{0, true, 2, false, false}, true},       // the simpler follow
            {{0, true, 0x19, false, false}, true},    // the landing follow
            {{1, true, 1, false, false}, false},      // player two (the standalone writes only the first pad)
            {{0, false, 1, false, false}, false},     // no skater followed (a replay's point shot)
            {{0, true, 0x17, false, false}, false},   // the point camera: a level point swings to the skater
            {{0, true, 0x18, false, false}, false},   // the death camera
            {{0, true, 1, true, false}, false},       // the end-of-run swing
            {{0, true, 1, false, true}, false},       // a replay
            {{0, true, 1, false, false, true}, false},   // the camera's first ticks (the level start's warm-up)
        };
        for (const Row& row : rows) {
            CHECK(fl::allowed(row.situation) == row.allowed);
        }
    }
    {   // the air lean's camera: at rest the offset comes back; a looked camera's offset comes back as the game's own camera's
        const auto turned = [&](const fl::Quat& base, std::int32_t spin, std::int32_t pitch) {
            return fl::compose(fl::compose(base, fl::about(true, spin, cos_q12, sin_q12)), fl::about(false, pitch, cos_q12, sin_q12));
        };
        const auto place_at = [](const fl::Quat& q, const std::int32_t (&pull)[3], std::int32_t (&out)[3]) {
            std::int32_t m[9];
            fl::to_matrix(q, m);
            for (int i = 0; i < 3; ++i) {
                out[i] = static_cast<std::int32_t>((static_cast<std::int64_t>(m[3 * i]) * pull[0] + static_cast<std::int64_t>(m[3 * i + 1]) * pull[1] +
                                                    static_cast<std::int64_t>(m[3 * i + 2]) * pull[2]) >> 12);
            }
        };
        const std::int32_t pull[3] = {0, -0x6c000, -0x122000};
        const fl::Quat bases[] = {fl::Quat{}, fl::about(true, 0x3a0, cos_q12, sin_q12),
                                  fl::compose(fl::about(false, 0x120, cos_q12, sin_q12), fl::about(true, -0x2c0, cos_q12, sin_q12))};
        const std::int32_t looks[][2] = {{0, 0}, {0x555, 0}, {-0x555, 0}, {0x800, 0}, {0x300, -0x100}, {-0x200, 0x200}, {0x7ff, 0x180}};
        int worst = 0;
        for (const fl::Quat& base : bases) {
            for (const std::int32_t game_pitch : {0x96, 0x1e, 0xf80}) {
                for (const auto& l : looks) {
                    fl::Look look;
                    look.yaw = l[0];
                    look.pitch = l[1];
                    std::int32_t looked[3];
                    std::int32_t stock[3];
                    std::int32_t back[3];
                    place_at(turned(base, 0x40 + look.yaw, game_pitch + look.pitch), pull, looked);
                    place_at(turned(base, 0x40, game_pitch), pull, stock);
                    fl::stock_offset(turned(base, 0x40 + look.yaw, game_pitch + look.pitch), game_pitch, look, looked, back, cos_q12, sin_q12);
                    for (int i = 0; i < 3; ++i) {
                        const int error = std::abs(back[i] - stock[i]);
                        worst = error > worst ? error : worst;
                    }
                    if (look.at_rest()) {
                        CHECK(std::abs(back[0] - looked[0]) < 0x2000 && std::abs(back[1] - looked[1]) < 0x2000 && std::abs(back[2] - looked[2]) < 0x2000);
                    }
                }
            }
        }
        std::printf("free_look_test: stock_offset worst error %d (Q12)\n", worst);
        CHECK(worst < 0x3000);   // (a few world units of a 0x122 distance: the Q12 rounding of the matrices)
    }
    fl::reset();

    if (g_failures == 0) {
        std::printf("free_look_test: all checks passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
