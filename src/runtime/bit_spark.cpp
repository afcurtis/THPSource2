#include "bit_views.hpp"

// Phase 4.15d: the spark settings and the animated classes of bit.cpp (0x00406070..0x00407050): the spark pixel, the animated flat bits (simple animation, motion blur, fragment), the ribbon of
// linked bits and the smoke trail on it, and the glass shard.

namespace opentony::runtime::bitkit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kSparkTrajectory = 0x0055f6c4;           // three shorts: an angle triple (the first two as a dword)
constexpr Word kSparkCone = 0x0055f718;                 // three shorts
constexpr Word kSparkSize = 0x005244e0;
constexpr Word kSparkSemi = 0x005244e4;                 // byte
constexpr Word kSparkRgb = 0x005244e8;
constexpr Word kSparkFadeRgb = 0x005244ec;

constexpr Word kSparkVtable = 0x0051843c;
constexpr Word kSimpleAnimVtable = 0x00518444;
constexpr Word kMotionBlurVtable = 0x0051844c;
constexpr Word kImmortalVtable = 0x00518454;
constexpr Word kFragVtable = 0x0051845c;
constexpr Word kRibbonBitVtable = 0x00518464;
constexpr Word kRibbonVtable = 0x0051846c;
constexpr Word kSmokeTrailVtable = 0x00518474;
constexpr Word kGlassVtable = 0x0051847c;
constexpr Word kRibbonName = 0x00524950;

}  // namespace

// 0x00406070 Bit_SetSparkTrajectory(angles)
void BitKit::set_spark_trajectory(Word angles) {
    set_global_word(m_, kSparkTrajectory, word_at(m_, angles));
    set_global_half(m_, kSparkTrajectory + 4, half_at(m_, angles + 4));
}

// 0x00406090 Bit_SetSparkTrajectoryCone(angles)
void BitKit::set_spark_cone(Word angles) {
    set_global_word(m_, kSparkCone, word_at(m_, angles));
    set_global_half(m_, kSparkCone + 4, half_at(m_, angles + 4));
}

// 0x004060b0 Bit_CalculateSparkVelocity(out, magnitude): the direction of the trajectory, each angle spread by a random part of the cone
void BitKit::calculate_spark_velocity(Word out, Word magnitude) {
    constexpr Word kAngles = kScratch + 0x80;
    set_global_word(m_, kAngles, global_word(m_, kSparkTrajectory));
    set_global_half(m_, kAngles + 4, global_half(m_, kSparkTrajectory + 4));
    for (Word k = 0; k < 3; ++k) {
        const std::int16_t cone = short_at(m_, kSparkCone + 2 * k);
        if (cone != 0) {
            const std::int32_t r = rnd(u(cone));
            const auto delta = static_cast<std::uint16_t>(r - (cone >> 1));
            set_half_at(m_, kAngles + 2 * k, half_at(m_, kAngles + 2 * k) + delta);
        }
    }
    core_.direction_from_angles(out, s(magnitude), kAngles);
}

// 0x00406160 Bit_SetSparkSize(size)
void BitKit::set_spark_size(Word size) {
    check(size < 0x10, 0x00524940);
    set_global_word(m_, kSparkSize, (size << 16) | size);
}

// 0x004061c0 / 0x004061e0
void BitKit::set_spark_rgb(Word red, Word green, Word blue) {
    set_global_byte(m_, kSparkRgb, red);
    set_global_byte(m_, kSparkRgb + 1, green);
    set_global_byte(m_, kSparkRgb + 2, blue);
}

void BitKit::set_spark_fade_rgb(Word red, Word green, Word blue) {
    set_global_byte(m_, kSparkFadeRgb, red);
    set_global_byte(m_, kSparkFadeRgb + 1, green);
    set_global_byte(m_, kSparkFadeRgb + 2, blue);
}

// 0x00406200 CSpark::CSpark(position, speed, gravity, life)
Word BitKit::spark_construct(Word self, Word position, Word speed, Word gravity, Word life) {
    pixel_construct(self);
    spark_at(m_, self).set_vtable(static_cast<Word>(kSparkVtable));
    if ((global_word(m_, kSparkSize) & 0xf) == 1) {
        spark_at(m_, self).set_field_43(0x68);
        spark_at(m_, self).set_trans_decay(0x2000000);
        spark_at(m_, self).set_anim(1);
    } else {
        spark_at(m_, self).set_field_43(0x60);
        spark_at(m_, self).set_trans_decay(0x3000000);
        spark_at(m_, self).set_anim(static_cast<Word>(global_word(m_, kSparkSize)));
    }
    for (Word k = 0; k < 3; ++k) {
        set_byte_at(m_, self + 0x40 + k, byte_at(m_, kSparkRgb + k));
    }
    for (Word k = 0; k < 3; ++k) {
        set_byte_at(m_, self + 0x48 + k, byte_at(m_, kSparkFadeRgb + k));
    }
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + bit::kPosition + 4 * k, word_at(m_, position + 4 * k));
    }
    calculate_spark_velocity(self + bit::kVelocity, speed);
    spark_at(m_, self).set_field_2c(static_cast<Word>(gravity));
    for (Word k = 0; k < 3; ++k) {
        set_byte_at(m_, self + bit::kFriction + k, 3);
    }
    spark_at(m_, self).set_lifetime(static_cast<std::uint16_t>(u(rnd(life))));
    if (global_byte(m_, kSparkSemi) != 0) {
        spark_at(m_, self).set_field_43(static_cast<std::uint8_t>(spark_at(m_, self).field_43() | 2));
    }
    return self;
}

// 0x00406300
Word BitKit::spark_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::spark_destruct); }

// 0x00406320
void BitKit::spark_destruct(Word self) {
    spark_at(m_, self).set_vtable(static_cast<Word>(kSparkVtable));
    pixel_destruct(self);
}

// 0x00406330 CSpark::Move: gravity on the height (a floor stops it), the position follows the velocity which decays by an eighth; the colour fades by its rates; at the end of its life it dies
void BitKit::spark_move(Word self) {
    std::int32_t y = static_cast<std::int32_t>(spark_at(m_, self).field_20()) + static_cast<std::int32_t>(spark_at(m_, self).field_14());
    const std::int32_t floor = spark_at(m_, self).flags();
    if (floor == 0 || y <= floor) {
        spark_at(m_, self).set_field_14(static_cast<Word>(u(y)));
        spark_at(m_, self).set_field_18(static_cast<Word>(spark_at(m_, self).field_18() + spark_at(m_, self).field_24()));
        const std::int32_t vx = s(spark_at(m_, self).field_28() + spark_at(m_, self).field_1c());
        const std::int32_t vz = s(spark_at(m_, self).field_30() + spark_at(m_, self).field_24());
        spark_at(m_, self).set_field_10(static_cast<Word>(spark_at(m_, self).field_10() + spark_at(m_, self).field_1c()));
        const std::int32_t vy = s(spark_at(m_, self).field_2c() + spark_at(m_, self).field_20());
        spark_at(m_, self).set_field_1c(static_cast<Word>(u(vx - (vx >> 3))));
        spark_at(m_, self).set_field_20(static_cast<Word>(u(vy - (vy >> 3))));
        spark_at(m_, self).set_field_24(static_cast<Word>(u(vz - (vz >> 3))));
    } else {
        spark_at(m_, self).set_field_14(static_cast<Word>(u(floor)));
    }
    spark_at(m_, self).set_age(static_cast<std::uint16_t>(spark_at(m_, self).age() + 1));
    if (static_cast<std::int32_t>(spark_at(m_, self).lifetime()) <= static_cast<std::int32_t>(static_cast<std::int16_t>(spark_at(m_, self).age()))) {
        die(self);
    }
    for (Word k = 0; k < 3; ++k) {
        set_byte_at(m_, self + 0x40 + k, byte_at(m_, self + 0x40 + k) - byte_at(m_, self + 0x48 + k));
    }
}

// 0x004063e0 (animation by name) / 0x004064b0 (by index): CSimpleAnim::CSimpleAnim(position, animation, scale, semi, ?, last frame)
Word BitKit::simple_anim_construct(Word self, Word position, Word animation, Word scale, Word semi, Word a5, std::int32_t last, bool by_index) {
    flat_construct(self);
    simple_anim_at(m_, self).set_vtable(static_cast<Word>(kSimpleAnimVtable));
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + bit::kPosition + 4 * k, word_at(m_, position + 4 * k));
    }
    if (by_index) {
        set_anim(self, s(animation));
    } else {
        set_anim_by_name(self, animation);
    }
    simple_anim_at(m_, self).set_scale(static_cast<std::uint16_t>(scale));
    if (s(semi) != 0) {
        set_semi_transparent(self);
    }
    simple_anim_at(m_, self).set_field_64(static_cast<Word>(a5));
    if (last == -1) {
        simple_anim_at(m_, self).set_field_68(static_cast<Word>(u(static_cast<std::int32_t>(simple_anim_at(m_, self).frame_count()) - 1)));
    } else {
        simple_anim_at(m_, self).set_field_68(static_cast<Word>(u(last)));
    }
    return self;
}

// 0x00406490
Word BitKit::simple_anim_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::simple_anim_destruct); }

// 0x00406560
void BitKit::simple_anim_destruct(Word self) {
    simple_anim_at(m_, self).set_vtable(static_cast<Word>(kSimpleAnimVtable));
    flat_destruct(self);
}

// 0x00406570 CSimpleAnim::Move: the animation runs once (to the last frame: dies at it) or loops (back to its first frame, or to its last)
void BitKit::simple_anim_move(Word self) {
    inc_frame(self);
    const std::int32_t frame = static_cast<std::int8_t>(simple_anim_at(m_, self).frame());
    const std::int32_t count = simple_anim_at(m_, self).frame_count();
    if (simple_anim_at(m_, self).field_64() == 0) {
        if (count <= frame) {
            if (static_cast<std::int32_t>(simple_anim_at(m_, self).field_68()) == -2) {
                set_frame(self, count - 1);
                return;
            }
            set_frame(self, 0);
        }
    } else if (static_cast<std::int32_t>(simple_anim_at(m_, self).field_68()) <= frame) {
        die(self);
    }
}

// 0x004065c0 CMotionBlur::CMotionBlur(position, velocity, animation, scale, scale decay, tint decay)
Word BitKit::motion_blur_construct(Word self, Word position, Word velocity, Word animation, Word scale, Word scale_decay, Word tint_decay) {
    flat_construct(self);
    motion_blur_at(m_, self).set_vtable(static_cast<Word>(kMotionBlurVtable));
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + bit::kPosition + 4 * k, word_at(m_, position + 4 * k));
    }
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + bit::kVelocity + 4 * k, word_at(m_, velocity + 4 * k));
    }
    set_anim_by_name(self, animation);
    set_semi_transparent(self);
    motion_blur_at(m_, self).set_trans_decay(static_cast<std::uint16_t>(tint_decay));
    motion_blur_at(m_, self).set_scale_decay(static_cast<std::uint16_t>(scale_decay));
    motion_blur_at(m_, self).set_scale(static_cast<std::uint16_t>(scale));
    return self;
}

// 0x00406660
Word BitKit::motion_blur_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::motion_blur_destruct); }

// 0x00406680
void BitKit::motion_blur_destruct(Word self) {
    motion_blur_at(m_, self).set_vtable(static_cast<Word>(kMotionBlurVtable));
    flat_destruct(self);
}

// 0x00406690 CMotionBlur::Move (die when shrunk or faded) / 0x00406740 CMotionBlurImmortal::Move (neither kills it)
void BitKit::motion_blur_move(Word self, bool immortal) {
    vec3_add(self + bit::kPosition, self + bit::kVelocity);
    if (shrink(self, immortal ? 0 : 1) == 0) {
        if (fade(self, immortal ? 0 : 1) == 0) {
            inc_frame_with_wrap(self);
        }
    }
}

// 0x00406710
Word BitKit::immortal_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::immortal_destruct); }

// 0x00406730
void BitKit::immortal_destruct(Word self) {
    set_word_at(m_, self, kImmortalVtable);
    motion_blur_destruct(self);
}

// 0x00406780 CFrag::CFrag(position, red, green, blue, animation, scale, semi, spread, gravity, life)
Word BitKit::frag_construct(Word self, Word position, Word red, Word green, Word blue, Word animation, Word scale, Word semi, std::int32_t spread, Word gravity, Word life) {
    flat_construct(self);
    frag_at(m_, self).set_vtable(static_cast<Word>(kFragVtable));
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + bit::kPosition + 4 * k, word_at(m_, position + 4 * k));
    }
    set_anim(self, s(animation));
    if (s(semi) != 0) {
        set_semi_transparent(self);
    }
    set_tint(self, red, green, blue);
    frag_at(m_, self).set_scale(static_cast<std::uint16_t>(scale));
    const Word spread_range = u(spread * 2 + 1);
    frag_at(m_, self).set_field_1c(static_cast<Word>(u((rnd(spread_range) - spread) * 0x1000)));
    set_word_at(m_, self + bit::kVelocity + 4, u(rnd(u(spread)) * -0x1000));
    const std::int32_t z = rnd(spread_range);
    frag_at(m_, self).set_field_2c(static_cast<Word>(gravity));
    set_word_at(m_, self + bit::kVelocity + 8, u((z - spread) * 0x1000));
    for (Word k = 0; k < 3; ++k) {
        set_byte_at(m_, self + bit::kFriction + k, 3);
    }
    frag_at(m_, self).set_lifetime(static_cast<std::uint16_t>(u(rnd(life))));
    return self;
}

// 0x00406870
Word BitKit::frag_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::frag_destruct); }

// 0x00406890
void BitKit::frag_destruct(Word self) {
    frag_at(m_, self).set_vtable(static_cast<Word>(kFragVtable));
    flat_destruct(self);
}

// 0x00406940 CRibbonBit::CRibbonBit
Word BitKit::ribbon_bit_construct(Word self) {
    linked_construct(self);
    set_word_at(m_, self, kRibbonBitVtable);
    return self;
}

// 0x00406960
Word BitKit::ribbon_bit_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::ribbon_bit_destruct); }

// 0x00406980
void BitKit::ribbon_bit_destruct(Word self) {
    set_word_at(m_, self, kRibbonBitVtable);
    linked_destruct(self);
}

// 0x004069a0 CRibbon::CRibbon(position, bits, points per bit, animation, last animation, scale, semi): a ring of points the bits follow, a bit for each pair of points
Word BitKit::ribbon_construct(Word self, Word position, std::int32_t bits, std::int32_t points, Word animation, Word last_animation, Word scale, Word semi) {
    nonrendered_construct(self);
    ribbon_at(m_, self).set_colour(static_cast<Word>(u(points - 1)));
    const std::int32_t total = (points - 1) * bits + 1;
    ribbon_at(m_, self).set_vtable(static_cast<Word>(kRibbonVtable));
    ribbon_at(m_, self).set_trans_decay(static_cast<Word>(u(bits)));
    ribbon_at(m_, self).set_anim(static_cast<Word>(u(total)));
    const Word ring = alloc(u(total) * 12);
    ribbon_at(m_, self).set_flags(static_cast<Word>(ring));
    set_global_word(m_, kBitBytes, global_word(m_, kBitBytes) + block_size(ring) + 4);
    for (std::int32_t k = 0; k < total; ++k) {
        for (Word j = 0; j < 3; ++j) {
            set_word_at(m_, ribbon_at(m_, self).flags() + 12 * u(k) + 4 * j, word_at(m_, position + 4 * j));
        }
    }
    const Word array = alloc(u(bits) * 4);
    ribbon_at(m_, self).set_anim_speed(static_cast<Word>(array));
    set_global_word(m_, kBitBytes, global_word(m_, kBitBytes) + block_size(array) + 4);
    for (std::int32_t k = s(ribbon_at(m_, self).trans_decay()) - 1; k >= 0; --k) {
        const Word block = bit_new(0x6c);
        const Word bit_address = block != 0 ? ribbon_bit_construct(block) : 0;
        set_word_at(m_, ribbon_at(m_, self).anim_speed() + 4 * u(k), bit_address);
        const Word piece = word_at(m_, ribbon_at(m_, self).anim_speed() + 4 * u(k));
        set_byte_at(m_, piece + bit::kProtected, 1);
        set_anim_by_name(piece, animation);
        set_scale(piece, scale);
        if (semi != 0) {
            set_semi_transparent(piece);
        }
    }
    const Word first = word_at(m_, ribbon_at(m_, self).anim_speed());
    set_byte_at(m_, first + bit::kFlags, byte_at(m_, first + bit::kFlags) | 0x10);
    set_anim_by_name(first, last_animation);
    return self;
}

// 0x00406b10
Word BitKit::ribbon_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::ribbon_destruct); }

// 0x00406b30
void BitKit::ribbon_destruct(Word self) {
    ribbon_at(m_, self).set_vtable(static_cast<Word>(kRibbonVtable));
    for (std::int32_t k = 0; k < s(ribbon_at(m_, self).trans_decay()); ++k) {
        const Word piece = word_at(m_, ribbon_at(m_, self).anim_speed() + 4 * u(k));
        if (piece != 0) {
            virtual_call(piece, 0, {1});
        }
    }
    set_global_word(m_, kBitBytes, global_word(m_, kBitBytes) + (0u - 4 - block_size(ribbon_at(m_, self).anim_speed())));
    set_global_word(m_, kBitBytes, global_word(m_, kBitBytes) + (0u - 4 - block_size(ribbon_at(m_, self).flags())));
    mem_delete(ribbon_at(m_, self).anim_speed());
    mem_delete(ribbon_at(m_, self).flags());
    nonrendered_destruct(self);
}

// 0x00406bf0: the tint of every bit
void BitKit::ribbon_set_tint(Word self, Word red, Word green, Word blue) {
    for (std::int32_t k = 0; k < s(ribbon_at(m_, self).trans_decay()); ++k) {
        set_tint(word_at(m_, ribbon_at(m_, self).anim_speed() + 4 * u(k)), red, green, blue);
    }
}

// 0x00406c30: the scale of every bit
void BitKit::ribbon_set_scale(Word self, Word scale) {
    for (std::int32_t k = 0; k < s(ribbon_at(m_, self).trans_decay()); ++k) {
        set_scale(word_at(m_, ribbon_at(m_, self).anim_speed() + 4 * u(k)), scale);
    }
}

// 0x00406c60 CRibbon::SetPos(position): the position enters the ring; each bit's ends are the ring's points a bit's length apart
void BitKit::ribbon_set_pos(Word self, Word position) {
    const Word ring_size = ribbon_at(m_, self).anim();
    Word head = ribbon_at(m_, self).frame_entry() + 1;
    ribbon_at(m_, self).set_frame_entry(static_cast<Word>(head));
    if (head == ring_size) {
        ribbon_at(m_, self).set_frame_entry(0);
    }
    const Word slot = ribbon_at(m_, self).flags() + ribbon_at(m_, self).frame_entry() * 12;
    for (Word j = 0; j < 3; ++j) {
        set_word_at(m_, slot + 4 * j, word_at(m_, position + 4 * j));
    }
    std::int32_t cursor = s(ribbon_at(m_, self).frame_entry());
    for (std::int32_t k = s(ribbon_at(m_, self).trans_decay()) - 1; k >= 0; --k) {
        const Word piece = word_at(m_, ribbon_at(m_, self).anim_speed() + 4 * u(k));
        const Word end = ribbon_at(m_, self).flags() + 12 * u(cursor);
        for (Word j = 0; j < 3; ++j) {
            set_word_at(m_, piece + 0x60 + 4 * j, word_at(m_, end + 4 * j));
        }
        cursor -= s(ribbon_at(m_, self).colour());
        if (cursor < 0) {
            cursor += s(ribbon_at(m_, self).anim());
        }
        const Word start = ribbon_at(m_, self).flags() + 12 * u(cursor);
        for (Word j = 0; j < 3; ++j) {
            set_word_at(m_, piece + 0x54 + 4 * j, word_at(m_, start + 4 * j));
        }
    }
}

// 0x00406d00 CSmokeTrail::CSmokeTrail(position, bits, red, green, blue): a ribbon of two points per bit that fades from the colour to black along its length
Word BitKit::smoke_trail_construct(Word self, Word position, std::int32_t bits, std::int32_t red, std::int32_t green, std::int32_t blue) {
    ribbon_construct(self, position, bits, 2, kRibbonName, kRibbonName, 400, 1);
    const std::int32_t count = s(smoke_trail_at(m_, self).trans_decay());
    if (count == 0) {
        throw std::runtime_error("division fault in CSmokeTrail");
    }
    const std::int32_t step_red = red / count;
    smoke_trail_at(m_, self).set_vtable(static_cast<Word>(kSmokeTrailVtable));
    const std::int32_t step_green = green / count;
    const std::int32_t step_blue = blue / count;
    for (std::int32_t k = 0; k < count; ++k) {
        const auto rest = static_cast<std::int8_t>(count - k - 1);
        const auto product = [&](std::int32_t step) { return static_cast<std::int8_t>(static_cast<std::int8_t>(step) * rest); };
        const Word r = static_cast<std::uint8_t>(static_cast<std::uint8_t>(red) - static_cast<std::uint8_t>(product(step_red)));
        const Word g = static_cast<std::uint8_t>(static_cast<std::uint8_t>(green) - static_cast<std::uint8_t>(product(step_green)));
        const Word b = static_cast<std::uint8_t>(static_cast<std::uint8_t>(blue) - static_cast<std::uint8_t>(product(step_blue)));
        const Word piece = word_at(m_, smoke_trail_at(m_, self).anim_speed() + 4 * u(k));
        set_tint(piece, r, g, b);
        set_trans_decay(piece, 8);
    }
    return self;
}

// 0x00406db0
Word BitKit::smoke_trail_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::smoke_trail_destruct); }

// 0x00406dd0
void BitKit::smoke_trail_destruct(Word self) {
    smoke_trail_at(m_, self).set_vtable(static_cast<Word>(kSmokeTrailVtable));
    ribbon_destruct(self);
}

// 0x00406de0 CSmokeTrail::Move: once the trail is released (+0x54) its bits fade; when all are gone it dies
void BitKit::smoke_trail_move(Word self) {
    if (smoke_trail_at(m_, self).field_54() == 0) {
        return;
    }
    bool all_faded = true;
    const std::int32_t count = s(smoke_trail_at(m_, self).trans_decay());
    if (count > 0) {
        for (std::int32_t k = 0; k < count; ++k) {
            if (fade(word_at(m_, smoke_trail_at(m_, self).anim_speed() + 4 * u(k)), 0) == 0) {
                all_faded = false;
            }
        }
        if (!all_faded) {
            return;
        }
    }
    die(self);
}

// 0x00406e30 CGlassBit::CGlassBit(position, velocity, floor, red, green, blue, spread x, y, z): three corners scattered around the position
Word BitKit::glass_construct(Word self, Word position, Word velocity, Word floor, Word red, Word green, Word blue, std::int32_t dx, std::int32_t dy, std::int32_t dz) {
    bit_construct(self);
    for (Word k = 0; k < 9; ++k) {
        set_word_at(m_, self + 0x3c + 4 * k, 0);
    }
    glass_at(m_, self).set_vtable(static_cast<Word>(kGlassVtable));
    attach_to(self, lists::kGlass);
    const Word range_x = u(dx * 2 + 1);
    const std::int32_t px = int_at(m_, position);
    const std::int32_t py = int_at(m_, position + 4);
    const std::int32_t pz = int_at(m_, position + 8);
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + bit::kPosition + 4 * k, word_at(m_, position + 4 * k));
    }
    for (Word corner = 0; corner < 3; ++corner) {
        const Word at = self + 0x3c + 12 * corner;
        set_word_at(m_, at, u((rnd(range_x) - dx) * 0x1000 + px));
        set_word_at(m_, at + 4, u((rnd(u(dy * 2 + 1)) - dy) * 0x1000 + py));
        set_word_at(m_, at + 8, u((rnd(u(dz * 2 + 1)) - dz) * 0x1000 + pz));
    }
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + bit::kVelocity + 4 * k, word_at(m_, velocity + 4 * k));
    }
    glass_at(m_, self).set_field_60(static_cast<Word>(floor));
    glass_at(m_, self).set_field_64(static_cast<std::uint8_t>((red & 0xff) >> 2));
    glass_at(m_, self).set_field_65(static_cast<std::uint8_t>((green & 0xff) >> 2));
    glass_at(m_, self).set_field_66(static_cast<std::uint8_t>((blue & 0xff) >> 2));
    glass_at(m_, self).set_field_67(static_cast<std::uint8_t>((red & 0xff) >> 2));
    glass_at(m_, self).set_field_68(static_cast<std::uint8_t>((green & 0xff) >> 2));
    glass_at(m_, self).set_field_69(static_cast<std::uint8_t>((blue & 0xff) >> 2));
    const Word life = u(rnd(0x1e));
    glass_at(m_, self).set_field_6a(4);
    glass_at(m_, self).set_lifetime(static_cast<std::uint16_t>(life + 0x1e));
    return self;
}

// 0x00407010
Word BitKit::glass_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::glass_destruct); }

// 0x00407030
void BitKit::glass_destruct(Word self) {
    glass_at(m_, self).set_vtable(static_cast<Word>(kGlassVtable));
    delete_from(self, lists::kGlass);
    bit_destruct(self);
}

// 0x00407050 CGlassBit::Move: the three corners fall with the velocity (gravity 10000 on the height); under the floor a corner bounces up (twice at most), the horizontal speed halves; it fades by 4
// a frame once its life is over and dies when black
void BitKit::glass_move(Word self) {
    std::int32_t vz = s(glass_at(m_, self).field_24());
    std::int32_t vy = s(glass_at(m_, self).field_20());
    std::int32_t vx = s(glass_at(m_, self).field_1c());
    for (Word corner = 0; corner < 3; ++corner) {
        const Word at = self + 0x3c + 12 * corner;
        set_word_at(m_, at, word_at(m_, at) + u(vx));
        set_word_at(m_, at + 4, word_at(m_, at + 4) + u(vy));
        set_word_at(m_, at + 8, word_at(m_, at + 8) + u(vz));
    }
    vy += 10000;
    if (static_cast<std::int32_t>(glass_at(m_, self).field_60()) < static_cast<std::int32_t>(glass_at(m_, self).colour())) {
        if (static_cast<std::int16_t>(glass_at(m_, self).age()) < 2) {
            glass_at(m_, self).set_colour(static_cast<Word>(u(static_cast<std::int32_t>(glass_at(m_, self).field_60()) + rnd(0x32) * -0x1000)));
            glass_at(m_, self).set_flags(static_cast<Word>(u(static_cast<std::int32_t>(glass_at(m_, self).field_60()) + rnd(0x32) * -0x1000)));
            const std::int32_t z = rnd(0x32);
            vy = (-vy) >> 2;
            glass_at(m_, self).set_age(static_cast<std::uint16_t>(glass_at(m_, self).age() + 1));
            glass_at(m_, self).set_field_58(static_cast<Word>(u(static_cast<std::int32_t>(glass_at(m_, self).field_60()) + z * -0x1000)));
        } else {
            vy = 0;
        }
        vx >>= 1;
        vz >>= 1;
    }
    glass_at(m_, self).set_field_1c(static_cast<Word>(u(vx)));
    glass_at(m_, self).set_field_20(static_cast<Word>(u(vy)));
    glass_at(m_, self).set_field_24(static_cast<Word>(u(vz)));
    bool flash = false;
    if (static_cast<std::int16_t>(glass_at(m_, self).age()) < 2) {
        if (rnd(5) == 0) {
            glass_at(m_, self).set_field_69(0x40);
            glass_at(m_, self).set_field_67(0x30);
            glass_at(m_, self).set_field_68(0x30);
            flash = true;
        }
    }
    if (!flash) {
        glass_at(m_, self).set_field_67(static_cast<std::uint8_t>(glass_at(m_, self).field_64()));
        glass_at(m_, self).set_field_68(static_cast<std::uint8_t>(glass_at(m_, self).field_65()));
        glass_at(m_, self).set_field_69(static_cast<std::uint8_t>(glass_at(m_, self).field_66()));
    }
    if (glass_at(m_, self).lifetime() != 0) {
        glass_at(m_, self).set_lifetime(static_cast<std::uint16_t>(glass_at(m_, self).lifetime() - 1));
    }
    if (glass_at(m_, self).lifetime() == 0) {
        const Word fade_step = glass_at(m_, self).field_6a();
        for (Word k = 0; k < 3; ++k) {
            const Word channel = byte_at(m_, self + 0x64 + k);
            set_byte_at(m_, self + 0x64 + k, fade_step < channel ? channel - fade_step : 0);
        }
    }
    if (glass_at(m_, self).field_66() == 0 && glass_at(m_, self).field_65() == 0 && glass_at(m_, self).field_64() == 0) {
        die(self);
    }
}

}  // namespace opentony::runtime::bitkit
