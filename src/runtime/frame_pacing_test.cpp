// frame_pacing.hpp: the default is retail's, 60 fps adds up to retail's per-frame counts and positions.
#include "frame_pacing.hpp"

#include <cstdint>
#include <cstdio>
#include <initializer_list>

namespace fp = opentony::runtime::framepacing;

static int g_failures = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #c);    \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

int main() {
    // the default is retail: two ticks a scene, one count a frame, no position offset, and the lazy read never runs
    CHECK(fp::limiter_ticks() == 2);
    bool read = false;
    CHECK(fp::frame_count_step([&] { read = true; return 0u; }) == 1u && !read);
    CHECK(fp::retail_step_offset(13000, 128) == 0);

    fp::Pacing sixty;
    sixty.limiter_ticks = 1;
    sixty.exact_clock = true;
    sixty.count_30hz = true;
    fp::set_pacing(sixty);
    CHECK(fp::limiter_ticks() == 1);
    // the 30 Hz frames of a 60 fps frame alternate 0 and 1 (the game's carry): the sum over two frames is the retail 1
    CHECK(fp::frame_count_step([] { return 1u; }) == 1u && fp::frame_count_step([] { return 0u; }) == 0u);
    // retail's scale (256) and anything above it: no offset; at 60 fps (128) the offset is -A / 8, and for two such frames the Euler error of one retail frame, -A / 4 in all
    CHECK(fp::retail_step_offset(13000, 256) == 0);
    CHECK(fp::retail_step_offset(13000, 128) == -13000 / 8);
    CHECK(fp::retail_step_offset(0, 128) == 0);
    // the trajectory: explicit Euler at 60 fps with the offset against retail's 30 fps, from the same launch speed, at every retail frame
    for (const std::int64_t v0 : {-114390, -60000}) {
        const std::int64_t pull = 13000;
        std::int64_t retail_y = 0, retail_v = v0;
        std::int64_t y = 0, v = v0;
        for (int frame = 1; frame <= 30; ++frame) {
            retail_y += retail_v;
            retail_v += pull;
            for (int half = 0; half < 2; ++half) {
                y += v / 2 + fp::retail_step_offset(static_cast<std::int32_t>(pull), 128);
                v += pull / 2;
            }
            const std::int64_t gap = y - retail_y;
            CHECK((gap < 0 ? -gap : gap) <= 2 * frame);   // a few units of rounding, not the 5 % of the uncorrected step
        }
    }
    // F1.5B, the point timer. At 60 fps the step is 0x800: a share of 1 credits a point every second call instead of never, and a whole 30 Hz frame (0x1000) credits exactly its share
    const std::uint32_t skater = 0x01234560;
    std::int32_t credited = 0;
    for (int call = 0; call < 60; ++call) {
        credited += fp::point_credit(skater, 0x800 * 1);
    }
    CHECK(credited == 30);                                                        // 1 point a 30 Hz frame, 60 calls = 30 frames
    CHECK(fp::point_credit(skater, 0x1000 * 7) == 7);                              // a retail frame: the whole share, the carry untouched
    credited = 0;
    for (int call = 0; call < 40; ++call) {
        credited += fp::point_credit(skater, 0x800 * 15);                          // an odd share: 15 a 30 Hz frame, not 14
    }
    CHECK(credited == 300);
    CHECK(fp::point_credit(skater + 4, 0x800 * 1) == 0 && fp::point_credit(skater + 4, 0x800 * 1) == 1);   // the carry is per skater
    CHECK(fp::point_credit(skater, -0x800 * 3) + fp::point_credit(skater, -0x800 * 3) == -3);              // negative shares floor like the shift
    CHECK(fp::script_rate_points(128, 1) == 1 && fp::script_rate_points(256, 28) == 28);   // the scale is left out: the step carries the time
    // F1.5C. An acceleration term is not scaled twice (the step scales it), a clock window keeps its length, a decay keeps its rate a second, a per-frame step is owed in proportion.
    CHECK(fp::acceleration_term(547, 128) == 547 && fp::acceleration_term(547, 256) == 547);
    CHECK(fp::clock_threshold(128, 20) == 20 && fp::clock_threshold(256, 20) == 20);
    CHECK(fp::decay_amount(184320, 2, [] { return 256; }) == 184320 >> 2);                                 // a whole retail frame: retail's shift
    {
        // a quarter taken off each retail frame: two frames of 128 leave the same as one of 256 (within the truncation)
        std::int32_t two = 184320, one = 184320;
        for (int half = 0; half < 2; ++half) {
            two -= fp::decay_amount(two, 2, [] { return 128; });
        }
        one -= fp::decay_amount(one, 2, [] { return 256; });
        CHECK(two >= one - 2 && two <= one + 2);
        std::int32_t half_rate = 184320, whole = 184320;
        for (int frame = 0; frame < 20; ++frame) {
            half_rate -= fp::decay_amount(half_rate, 1, [] { return 128; });
        }
        for (int frame = 0; frame < 10; ++frame) {
            whole -= fp::decay_amount(whole, 1, [] { return 256; });
        }
        CHECK(half_rate >= whole - 8 && half_rate <= whole + 8);
    }
    CHECK(fp::scaled_step(500, [] { return 256; }) == 500 && fp::scaled_step(500, [] { return 128; }) == 250 && fp::scaled_step(500, [] { return 384; }) == 500);
    fp::set_pacing(fp::Pacing{});
    // the flag off is retail's expressions, bit for bit
    CHECK(fp::acceleration_term(547, 128) == 273 && fp::acceleration_term(547, 256) == 547);
    CHECK(fp::clock_threshold(128, 20) == 10 && fp::clock_threshold(256, 20) == 20 && fp::clock_threshold(341, 6) == 7);
    CHECK(fp::decay_amount(184320, 2, [] { return 128; }) == 184320 >> 2 && fp::decay_amount(-5, 2, [] { return 128; }) == (-5 >> 2));
    CHECK(fp::scaled_step(500, [] { return 128; }) == 500);
    CHECK(fp::point_credit(skater, 0x800 * 1) == 0 && fp::point_credit(skater, 0x1000 * 7) == 7 && fp::point_credit(skater, -0x800 * 3) == -2);
    CHECK(fp::script_rate_points(128, 1) == 0 && fp::script_rate_points(256, 28) == 28 && fp::script_rate_points(384, 3) == 4);
    // F1.5F: the 30 Hz gate. Off: always runs and the frame word is never read. On: runs when the frame is worth a 30 Hz frame; the time of the skipped frames is kept for the run
    {
        bool read = false;
        CHECK(fp::runs_30hz_frame([&] { read = true; return 0; }) && !read);
        fp::Pacing gated;
        gated.count_30hz = true;
        fp::set_pacing(gated);
        CHECK(!fp::runs_30hz_frame([] { return 0; }) && fp::runs_30hz_frame([] { return 1; }) && fp::runs_30hz_frame([] { return 2; }));
        fp::set_pacing(fp::Pacing{});
        fp::gate_time() = {};
        fp::gate_time().scale += 128;
        fp::gate_time().scale += 128;
        CHECK(fp::gate_time().scale == 256);
        fp::gate_time() = {};
        CHECK(fp::gate_time().scale == 0 && fp::gate_time().step == 0);
    }
    // cars: the blend between the state before the update and after it, by the share of the step passed; a jump is not blended; angles take the short way round
    CHECK(fp::blend_position(1000, 3000, 128, 256) == 2000 && fp::blend_position(1000, 3000, 0, 256) == 1000 && fp::blend_position(1000, 3000, 256, 256) == 3000);
    CHECK(fp::blend_position(static_cast<std::uint32_t>(-2000), 2000, 128, 256) == 0);
    CHECK(fp::blend_position(0, 0x300000, 128, 256) == 0x300000);
    CHECK(fp::blend_angle(0x10, 0x30, 1, 2) == 0x20 && fp::blend_angle(0xff0, 0x010, 1, 2) == 0 && fp::blend_angle(0x010, 0xff0, 1, 2) == 0);
    // the Bullring helicopter's rotor (a platform): yaw 0x3c7 to 0x077 in one 30 Hz step (-0x350), a quarter of it at 120, across 0 the short way
    CHECK(fp::blend_angle(0x3c7, 0x077, 64, 256) == 0x2f3 && fp::blend_angle(0x077, 0xd27, 64, 256) == 0xfa3);
    // F1.5G: the music fade: off, 3 a call and the tick never read; on, 3 per two ticks with the half carried, a long gap counted as two ticks
    {
        bool read = false;
        CHECK(fp::per_call_amount(3, [&] { read = true; return 0u; }) == 3 && !read);
        fp::Pacing on;
        on.count_30hz = true;
        fp::set_pacing(on);
        std::int32_t total = 0;
        fp::per_call_amount(3, [] { return 99u; }); fp::per_call_amount(3, [] { return 99u; });   // prime the last tick (the first call has a long gap), and clear the carry
        for (std::uint32_t tick = 100; tick < 100 + 60; ++tick) total += fp::per_call_amount(3, [&] { return tick; });   // 60 one-tick calls = 30 retail frames
        if (total != 90) std::printf("total %d\n", total);
        CHECK(total == 90);
        CHECK(fp::per_call_amount(3, [] { return 200u; }) == 3 && fp::per_call_amount(3, [] { return 202u; }) == 3);
        // F2C: at 120 fps two calls share a tick (gaps 0, 1, 0, 1): 120 calls are 60 ticks = 30 retail frames, the same 90, and no call returns more than 3
        fp::per_call_amount(3, [] { return 299u; }); fp::per_call_amount(3, [] { return 299u; });
        std::int32_t total120 = 0, largest = 0;
        for (std::uint32_t call = 0; call < 120; ++call) {
            const std::int32_t amount = fp::per_call_amount(3, [&] { return 300u + call / 2; });
            total120 += amount;
            largest = amount > largest ? amount : largest;
        }
        CHECK(total120 == 90 && largest <= 3);
        fp::set_pacing(fp::Pacing{});
    }
    // F2A: the sub-tick clock. Off by default; at 120 fps (half a tick a scene) the limiter waits on the exact time, and the scale and step are 64 and 0x400 on every frame
    {
        CHECK(!fp::pacing().sub_tick && fp::pacing().period_ticks == 2.0);
        fp::Pacing fast;
        fast.limiter_ticks = 1;
        fast.count_30hz = true;
        fast.sub_tick = true;
        fast.period_ticks = 0.5;
        fp::set_pacing(fast);
        fp::sub_tick_state() = {};
        // the limiter: the first scene waits a period from where it starts; then one scene per half tick, whatever the whole tick does
        CHECK(fp::scene_waits(10.0) && fp::scene_waits(10.25) && !fp::scene_waits(10.5));
        fp::scene_begins(10.5);
        CHECK(fp::scene_waits(10.5) && !fp::scene_waits(11.0));
        fp::scene_begins(11.0);
        // lockstep: the delivery that reaches the period counts, the first one past it is held, the next ones count again, and a new scene starts the rule again
        CHECK(!fp::lockstep_holds(11.0) && fp::lockstep_holds(11.5) && !fp::lockstep_holds(11.5) && !fp::lockstep_holds(12.0));
        fp::scene_begins(12.0);
        CHECK(!fp::lockstep_holds(12.25) && fp::lockstep_holds(12.5));
        // realtime: the deadline is exactly the last scene + the period; none before a scene is known or when the time went back
        fp::scene_begins(12.5);
        CHECK(fp::scene_due(12.6) == 13.0 && fp::scene_due(13.4) == 13.0 && fp::scene_due(12.0) == 0.0);
        // at 60 on whole ticks: the last scene tick + the limiter, only when ahead by at most one limiter (a stale tick ahead of a zeroed clock gives none)
        CHECK(fp::whole_tick_due(40.3, 40, 1) == 41.0 && fp::whole_tick_due(41.0, 40, 1) == 0.0 && fp::whole_tick_due(41.7, 40, 1) == 0.0 && fp::whole_tick_due(0.2, 40, 1) == 0.0);
        // deliveries are counted from the last scene mark; a level frame's one or two stay under the idle count, a loop that draws nothing reaches it; a new mark starts again
        CHECK(fp::count_delivery(12.5) == 1 && fp::count_delivery(12.5) < fp::kIdleDelivery && fp::count_delivery(12.5) >= fp::kIdleDelivery);
        CHECK(fp::count_delivery(13.0) == 1 && fp::count_delivery(41.0) == 1);
        fp::scene_begins(11.0);
        CHECK(fp::scene_waits(0.0) && fp::scene_waits(0.25) && !fp::scene_waits(0.5));   // the accumulator was zeroed (a new level): start again from it
        // the time step: after fps_init, every frame of half a tick gives scale 64 and step 0x400
        fp::sub_tick_restart(100.0);
        for (int frame = 1; frame <= 8; ++frame) {
            const double length = fp::frame_length(100.0 + 0.5 * frame - 0.5, false);
            CHECK(length == 0.5 && fp::sub_tick_step(length) == 0x400 && fp::sub_tick_scale() == 64);
        }
        // a paused frame has no length and is not stored (the scale holds); a long frame counts, a sum above 15 ticks is 15 (retail: scale 640)
        CHECK(fp::frame_length(103.5, true) == 0.0 && fp::sub_tick_step(0.0) == 0 && fp::sub_tick_scale() == 64);
        CHECK(fp::frame_length(105.5, false) == 2.0 && fp::sub_tick_scale() == 128);   // (0.5 + 0.5 + 2) x 256 / 6
        CHECK(fp::frame_length(205.5, false) == 100.0 && fp::sub_tick_scale() == 640);
        // the game time went back (a new level without fps_init): one period, not a negative length
        CHECK(fp::frame_length(3.0, false) == 0.5);
        // three frames with no time: the scale is at least 1
        fp::frame_length(3.0, false);
        fp::frame_length(3.0, false);
        fp::frame_length(3.0, false);
        CHECK(fp::sub_tick_scale() == 1);
        // 60 fps on the sub-tick clock (OT_SUB_TICK=1) is what whole ticks give: 128 and 0x800
        fast.period_ticks = 1.0;
        fp::set_pacing(fast);
        fp::sub_tick_restart(0.0);
        for (int frame = 0; frame < 5; ++frame) {
            const double length = fp::frame_length(1.0 * frame, false);
            CHECK(fp::sub_tick_step(length) == 0x800 && fp::sub_tick_scale() == 128);
        }
        fp::set_pacing(fp::Pacing{});
        fp::sub_tick_state() = {};
    }
    // F2B: scaled_carry. Off (the default, 30, 60): retail's `product >> 8`, which floors, and nothing is kept. On (120): four frames of scale 64 add up to one retail frame's value, whatever its sign
    {
        using Site = fp::CarrySite;
        CHECK(fp::scaled_carry(Site::ground_turn, 1, 45 * 64) == (45 * 64) >> 8 && fp::scaled_carry(Site::ground_turn, 1, -45 * 64) == (-45 * 64) >> 8);
        fp::Pacing carry;
        carry.count_30hz = true;
        carry.carry_fraction = true;
        fp::set_pacing(carry);
        for (const std::int32_t value : {45, -45, 7, -7, 1, -1, 0x2d000}) {
            for (const Site site : {Site::ground_turn, Site::rail_push}) {
                std::int32_t sum = 0;
                for (int frame = 0; frame < 400; ++frame) {      // 100 retail frames
                    sum += fp::scaled_carry(site, 0x1000 + static_cast<std::uint32_t>(value & 0xff), value * 64);
                }
                CHECK(sum == value * 100 || sum == value * 100 - 1 || sum == value * 100 + 0);   // the last carry (under one unit) is still held back
                CHECK(sum >= value * 100 - 1 && sum <= value * 100);
            }
        }
        // a site and an object keep their own fraction
        fp::scaled_carry(Site::air_spin, 5, 100);                        // keeps 100
        CHECK(fp::scaled_carry(Site::air_spin, 6, 100) == 0 && fp::scaled_carry(Site::air_spin, 5, 156) == 1 && fp::scaled_carry(Site::air_spin, 6, 156) == 1 && fp::scaled_carry(Site::rail_swing, 5, 100) == 0);
        // the grind's 30 units: a frame of scale 64 owes 7 (whole numbers)
        CHECK(fp::scaled_step(0x1e, [] { return 64; }) == 7 && fp::scaled_step(0x1e, [] { return 256; }) == 0x1e);
        fp::set_pacing(fp::Pacing{});
    }
    // a menu's held direction (CMenu_Update, Pad_RepeatDirection): off, one count a call and the tick never read; on, the first call counts one and then one per two ticks,
    // so at 120 (half a tick a call) a hold of 64 calls makes 16 counts, the 16 calls of 30 fps in the same time; at 30 (two ticks a call) every call counts one
    {
        fp::HoldClock clock;
        bool read = false;
        CHECK(fp::hold_steps(clock, true, [&] { read = true; return 0u; }) == 1u && fp::hold_steps(clock, false, [&] { read = true; return 0u; }) == 1u && !read);
        fp::Pacing fast;
        fast.count_30hz = true;
        fast.period_ticks = 0.5;
        fp::set_pacing(fast);
        std::uint32_t counts = fp::hold_steps(clock, true, [] { return 1000u; });
        for (std::uint32_t call = 1; call < 64; ++call) {
            counts += fp::hold_steps(clock, false, [&] { return 1000u + call / 2; });
        }
        CHECK(counts == 16);
        std::uint32_t thirty = fp::hold_steps(clock, true, [] { return 2000u; });
        for (std::uint32_t call = 1; call < 16; ++call) {
            thirty += fp::hold_steps(clock, false, [&] { return 2000u + 2 * call; });
        }
        CHECK(thirty == 16 && fp::hold_steps(clock, false, [] { return 2100u; }) == 1u);   // a long gap counts as one frame
        fp::set_pacing(fp::Pacing{});
    }
    if (g_failures == 0) {
        std::printf("frame_pacing: ok\n");
    }
    return g_failures == 0 ? 0 : 1;
}
