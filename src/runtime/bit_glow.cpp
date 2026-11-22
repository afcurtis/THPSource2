#include "bit_views.hpp"

// Phase 4.15d: the chunk bit's world coordinates, the glow (a fan of sections and fringes around a centre), the wobbly glow, the flat, linked and pixel bits (0x00405520..0x00405ff0) and the spark settings
// (0x00406070..0x004061e0).

namespace opentony::runtime::bitkit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kGlowVtable = 0x00518414;
constexpr Word kWobblyVtable = 0x0051841c;
constexpr Word kFlatVtable = 0x00518424;
constexpr Word kLinkedVtable = 0x0051842c;
constexpr Word kPixelVtable = 0x00518434;

// the glow record after the base: +0x3c the sections (radius, colour: 8 bytes each), +0x40 the fringes, +0x44 the number of sections, +0x48 the number of fringes, +0x4c the centre colour,
// +0x50 the step angle (short), +0x58 the mask
namespace glow {
constexpr Word kSections = 0x3c;
constexpr Word kFringes = 0x40;
constexpr Word kSectionCount = 0x44;
constexpr Word kFringeCount = 0x48;
constexpr Word kCentre = 0x4c;
constexpr Word kStep = 0x50;
constexpr Word kMask = 0x58;
}  // namespace glow

constexpr Word kSparkTrajectory = 0x0055f6c4;           // an angle triple: two shorts then one
constexpr Word kSparkCone = 0x0055f718;                 // three shorts
constexpr Word kSparkSize = 0x005244e0;
constexpr Word kSparkRgb = 0x005244e8;
constexpr Word kSparkFadeRgb = 0x005244ec;

}  // namespace

// 0x00405520 CChunkBit::CalculateWorldCoords: the four corners (three shorts each, +0x3c / +0x44 / +0x4c / +0x54) through the rotation of the bit's angles (+0x8c) and its position
void BitKit::chunk_world_coords(Word self) {
    constexpr Word kMatrix = kScratch + 0x40;
    m3d_.rotation_from_angles(self + 0x8c, kMatrix);
    for (Word k = 0; k < 9; ++k) {
        set_half_at(m_, mathkit::gte::kRotation + 2 * k, half_at(m_, kMatrix + 2 * k));
    }
    m3d_.set_translation(self + bit::kPosition);
    constexpr Word corners[4] = {0x3c, 0x44, 0x4c, 0x54};
    constexpr Word results[4] = {0x5c, 0x68, 0x74, 0x80};
    for (Word k = 0; k < 4; ++k) {
        for (Word j = 0; j < 3; ++j) {
            set_half_at(m_, mathkit::gte::kV0 + 2 * j, half_at(m_, self + corners[k] + 2 * j));
        }
        math().rtv0tr();
        for (Word j = 0; j < 3; ++j) {
            set_word_at(m_, self + results[k] + 4 * j, word_at(m_, mathkit::gte::kMac + 4 * j));
        }
    }
}

// 0x004056c0 CGlow::CGlow(points, fringes): 2 * points sections and `fringes` fringes of that many bands each, in one block
Word BitKit::glow_construct(Word self, std::int32_t points, std::int32_t fringes) {
    bit_construct(self);
    glow_at(m_, self).set_vtable(static_cast<Word>(kGlowVtable));
    check(points != 0, 0x005248c0);
    const Word sections = u(points) * 2;
    set_word_at(m_, self + glow::kSectionCount, sections);
    if (sections == 0) {
        throw std::runtime_error("division fault in CGlow");
    }
    set_half_at(m_, self + glow::kStep, 0x1000 / sections);
    set_word_at(m_, self + glow::kFringeCount, u(fringes));
    const Word block = alloc((u(fringes) + 1) * sections * 8);
    set_word_at(m_, self + glow::kSections, block);
    set_global_word(m_, kBitBytes, global_word(m_, kBitBytes) + 4 + block_size(block));
    set_word_at(m_, self + glow::kFringes, block + sections * 8);
    const Word total = u(fringes) * sections;
    if (total != 0) {
        Word k = 0;
        do {
            ++k;
            set_word_at(m_, word_at(m_, self + glow::kFringes) - 4 + k * 8, 0x3a000000);
        } while (k < total);
    }
    set_word_at(m_, self + glow::kCentre, 0x32000000);
    set_word_at(m_, self + glow::kMask, 0xffffffff);
    attach_to(self, lists::kGlow);
    return self;
}

// 0x004057f0
Word BitKit::glow_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::glow_destruct); }

// 0x00405810 CGlow::CGlow(position, radius, fringe, red, green, blue, centre red, green, blue): eight sections and one fringe, all alike
Word BitKit::glow_construct_simple(Word self, Word position, Word radius, Word fringe, Word red, Word green, Word blue, Word centre_red, Word centre_green, Word centre_blue) {
    bit_construct(self);
    glow_at(m_, self).set_vtable(static_cast<Word>(kGlowVtable));
    set_word_at(m_, self + glow::kSectionCount, 8);
    set_half_at(m_, self + glow::kStep, 0x200);
    set_word_at(m_, self + glow::kFringeCount, 1);
    const Word block = alloc(0x80);
    set_word_at(m_, self + glow::kSections, block);
    set_global_word(m_, kBitBytes, global_word(m_, kBitBytes) + 4 + block_size(block));
    set_word_at(m_, self + glow::kFringes, block + 8 * 8);
    attach_to(self, lists::kGlow);
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + bit::kPosition + 4 * k, word_at(m_, position + 4 * k));
    }
    for (Word k = 1; k <= 8; ++k) {
        set_word_at(m_, block - 8 + k * 8, radius);
    }
    set_word_at(m_, self + glow::kCentre, ((((0x32u << 8) | (blue & 0xff)) << 8) | (green & 0xff)) << 8 | (red & 0xff));
    for (Word k = 1; k <= 8; ++k) {
        set_word_at(m_, block - 4 + k * 8, ((((centre_blue & 0xff) << 8) | (centre_green & 0xff)) << 8) | (centre_red & 0xff));
    }
    const Word fringes = word_at(m_, self + glow::kFringes);
    for (Word k = 1; k <= 8; ++k) {
        set_word_at(m_, fringes - 8 + k * 8, fringe);
        set_word_at(m_, fringes - 4 + k * 8, 0x3a000000);
    }
    set_word_at(m_, self + glow::kMask, 0xffffffff);
    return self;
}

// 0x00405990
void BitKit::glow_destruct(Word self) {
    glow_at(m_, self).set_vtable(static_cast<Word>(kGlowVtable));
    const Word size = block_size(word_at(m_, self + glow::kSections));
    set_global_word(m_, kBitBytes, global_word(m_, kBitBytes) + (0u - 4 - size));
    mem_delete(word_at(m_, self + glow::kSections));
    delete_from(self, lists::kGlow);
    bit_destruct(self);
}

// 0x00405a70: the radius of every section
void BitKit::glow_set_radius(Word self, Word radius) {
    const Word count = word_at(m_, self + glow::kSectionCount);
    for (Word k = 1; k <= count; ++k) {
        set_word_at(m_, word_at(m_, self + glow::kSections) - 8 + k * 8, radius);
    }
}

// 0x00405aa0: the width of every band of one fringe
void BitKit::glow_set_fringe_width(Word self, Word fringe, Word width) {
    check(fringe < word_at(m_, self + glow::kFringeCount), 0x005248dc);
    const Word count = word_at(m_, self + glow::kSectionCount);
    Word at = word_at(m_, self + glow::kFringes) + count * fringe * 8;
    for (Word k = 0; k < count; ++k) {
        set_word_at(m_, at, width);
        at += 8;
    }
}

// 0x00405b20: the colour of the centre
void BitKit::glow_set_centre_rgb(Word self, Word red, Word green, Word blue) {
    set_word_at(m_, self + glow::kCentre, ((((0x32u << 8) | (blue & 0xff)) << 8) | (green & 0xff)) << 8 | (red & 0xff));
}

// 0x00405b50: the colour of every section
void BitKit::glow_set_rgb(Word self, Word red, Word green, Word blue) {
    const Word count = word_at(m_, self + glow::kSectionCount);
    for (Word k = 1; k <= count; ++k) {
        set_word_at(m_, word_at(m_, self + glow::kSections) - 4 + k * 8, ((((blue & 0xff) << 8) | (green & 0xff)) << 8) | (red & 0xff));
    }
}

// 0x00405b90: the colour of every band of one fringe
void BitKit::glow_set_fringe_rgb(Word self, Word fringe, Word red, Word green, Word blue) {
    check(fringe < word_at(m_, self + glow::kFringeCount), 0x00524900);
    const Word count = word_at(m_, self + glow::kSectionCount);
    Word at = word_at(m_, self + glow::kFringes) + count * fringe * 8 + 4;
    for (Word k = 0; k < count; ++k) {
        set_word_at(m_, at, ((((((0x3au << 8) | (blue & 0xff)) << 8) | (green & 0xff)) << 8) | (red & 0xff)));
        at += 8;
    }
}

// 0x00405c40: the colour of one spoke (a section and the band of every fringe in line with it)
void BitKit::glow_set_radial_rgb(Word self, Word spoke, Word red, Word green, Word blue) {
    check(!(s(spoke) < 0 || word_at(m_, self + glow::kSectionCount) <= spoke), 0x00524920);
    const Word colour = ((((((0x3au << 8) | (blue & 0xff)) << 8) | (green & 0xff)) << 8) | (red & 0xff));
    set_word_at(m_, word_at(m_, self + glow::kSections) + 4 + spoke * 8, colour);
    Word at = word_at(m_, self + glow::kFringes) + spoke * 8;
    const Word fringes = word_at(m_, self + glow::kFringeCount);
    for (Word k = 0; k < fringes; ++k) {
        set_word_at(m_, at + 4, colour);
        at += word_at(m_, self + glow::kSectionCount) * 8;
    }
}

// 0x00405d20: every section's colour reduced
void BitKit::glow_reduce(Word self, std::int32_t amount) {
    const Word count = word_at(m_, self + glow::kSectionCount);
    for (Word k = 0; k < count; ++k) {
        reduce_rgb(word_at(m_, self + glow::kSections) + 4 + k * 8, amount);
    }
}

// 0x00405d60 CWobblyGlow: a simple glow whose sections wobble (a random phase and period each)
Word BitKit::wobbly_construct(Word self, Word a2, std::int32_t radius, Word a4, std::int32_t scale, Word a6, Word a7, Word a8, Word a9, Word a10, Word a11) {
    glow_construct_simple(self, a2, u(radius), a4, a6, a7, a8, a9, a10, a11);
    wobbly_at(m_, self).set_vtable(static_cast<Word>(kWobblyVtable));
    const std::int32_t product = radius * scale;
    wobbly_at(m_, self).set_field_9c(static_cast<Word>(u((product + ((product >> 31) & 0xff)) >> 8)));
    wobbly_at(m_, self).set_field_a0(static_cast<Word>(u(radius)));
    const Word count = word_at(m_, self + glow::kSectionCount);
    for (Word k = 0; k < count; ++k) {
        set_word_at(m_, self + 0x7c + 4 * k, u(rnd(0x1000)));
        set_word_at(m_, self + 0x5c + 4 * k, u(rnd(0x32) + 0x32));
    }
    return self;
}

// 0x00405e30
Word BitKit::wobbly_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::wobbly_destruct); }

// 0x00405e50
void BitKit::wobbly_destruct(Word self) {
    wobbly_at(m_, self).set_vtable(static_cast<Word>(kWobblyVtable));
    glow_destruct(self);
}

// 0x00405e60 CWobblyGlow::Move: every section's phase (+0x7c) advances by its speed (+0x5c); the section's first word, its radius, becomes the centre (+0xa0) plus the sine of the phase times the
// amplitude (+0x9c), divided by 0x1000 rounding towards zero
void BitKit::wobbly_move(Word self) {
    const Word count = word_at(m_, self + 0x44);
    for (Word k = 0; k < count; ++k) {
        const Word phase = word_at(m_, self + 0x7c + 4 * k) + word_at(m_, self + 0x5c + 4 * k);
        set_word_at(m_, self + 0x7c + 4 * k, phase);
        const Word product = u(math().sin_q12(s(phase))) * word_at(m_, self + 0x9c);
        const Word rounded = product + ((product >> 31) != 0 ? 0xfffu : 0u);
        set_word_at(m_, word_at(m_, self + 0x3c) + 8 * k, u(s(rounded) >> 12) + word_at(m_, self + 0xa0));
    }
}

// 0x00405ec0 CFlatBit::CFlatBit
Word BitKit::flat_construct(Word self) {
    ft4_construct(self);
    flat_at(m_, self).set_vtable(static_cast<Word>(kFlatVtable));
    attach_to(self, lists::kFlat);
    flat_at(m_, self).set_field_61(0x20);
    flat_at(m_, self).set_field_5a(1);
    flat_at(m_, self).set_field_5c(0x10001000);
    return self;
}

// 0x00405ef0
Word BitKit::flat_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::flat_destruct); }

// 0x00405f10
void BitKit::flat_destruct(Word self) {
    flat_at(m_, self).set_vtable(static_cast<Word>(kFlatVtable));
    delete_from(self, lists::kFlat);
    ft4_destruct(self);
}

// 0x00405f30 CLinked2EndedBit::CLinked2EndedBit
Word BitKit::linked_construct(Word self) {
    ft4_construct(self);
    for (Word k = 0; k < 6; ++k) {
        set_word_at(m_, self + 0x54 + 4 * k, 0);
    }
    linked_at(m_, self).set_vtable(static_cast<Word>(kLinkedVtable));
    attach_to(self, lists::kLinked2Ended);
    return self;
}

// 0x00405f70
Word BitKit::linked_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::linked_destruct); }

// 0x00405f90
void BitKit::linked_destruct(Word self) {
    linked_at(m_, self).set_vtable(static_cast<Word>(kLinkedVtable));
    delete_from(self, lists::kLinked2Ended);
    ft4_destruct(self);
}

// 0x00405fb0 CPixel::CPixel
Word BitKit::pixel_construct(Word self) {
    bit_construct(self);
    pixel_at(m_, self).set_vtable(static_cast<Word>(kPixelVtable));
    attach_to(self, lists::kPixel);
    return self;
}

// 0x00405fd0
Word BitKit::pixel_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::pixel_destruct); }

// 0x00405ff0
void BitKit::pixel_destruct(Word self) {
    pixel_at(m_, self).set_vtable(static_cast<Word>(kPixelVtable));
    delete_from(self, lists::kPixel);
    bit_destruct(self);
}

}  // namespace opentony::runtime::bitkit
