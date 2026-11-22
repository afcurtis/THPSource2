#include "bit_views.hpp"

// Phase 4.15d: bit.cpp's lists, the base class and the simple derived classes (0x00403680..0x00405ff0). Every bit is allocated by the class `operator new` (zeroed), linked into the list of its class
// by AttachTo and unlinked by DeleteFrom; each class's destructor restores its own vtable, leaves its list and runs its base class's destructor. The allocator, the resource lookup and the random
// numbers are services.

namespace opentony::runtime::bitkit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kFindResource = 0x004b3680;          // BITS_FindNamedResource(name)
constexpr Word kFindMaterial = 0x004b2030;          // PSX_FindSceneMaterial(checksum)
constexpr Word kAnimationTimeScale = 0x0056865c;    // dword: the animation clock's scale (256 is one frame per frame)
constexpr Word kQuickAnims = 0x0055f6f4;            // four animations looked up by Bit_Init: shadow, smoke, ...
constexpr Word kQuickAnimNames = 0x005244d0;        // the four names
constexpr Word kButtonsAnim = 0x00561c18;
constexpr Word kButtonsName = 0x005244f0;
constexpr Word kShadowMaterial = 0x0055f730;
constexpr Word kBitsWord = 0x0055f6d4;

constexpr Word kBaseVtable = 0x005183d8;
constexpr Word kSpecialVtable = 0x005183e0;
constexpr Word kNonRenderedVtable = 0x005183ec;
constexpr Word kFt4Vtable = 0x005183f4;
constexpr Word kQuadVtable = 0x005183fc;
constexpr Word kGenPolyVtable = 0x00518404;
constexpr Word kChunkVtable = 0x0051840c;
constexpr Word kGlowVtable = 0x00518414;
constexpr Word kWobblyVtable = 0x0051841c;
constexpr Word kFlatVtable = 0x00518424;
constexpr Word kLinkedVtable = 0x0051842c;
constexpr Word kPixelVtable = 0x00518434;

// the thirteen lists in the order Bit_DeleteAll walks them, with the message of the "leftover" assertion that follows
struct ListEntry {
    Word head;
    Word message;
};
constexpr ListEntry kDeleteOrder[] = {
    {lists::kNonRendered, 0x00524704}, {lists::kFlat, 0x005246dc},   {lists::kLinked2Ended, 0x005246b0}, {lists::kPixel, 0x0052468c}, {lists::kPolyLine, 0x00524664},
    {lists::kGPolyLine, 0x0052463c},   {lists::kQuad, 0x00524614},   {lists::kGenPoly, 0x005245ec},      {lists::kChunk, 0x005245c4}, {lists::kGlow, 0x005245a0},
    {lists::kGlass, 0x0052457c},       {lists::kGLine, 0x00524558},  {lists::kSpecial, 0x00524528},
};

}  // namespace

// ---- the lists and the base class ----

// 0x00403680 Bit_Init
void BitKit::bit_init() {
    set_global_word(m_, kBitCount, 0);
    for (const ListEntry& entry : kDeleteOrder) {
        set_word_at(m_, entry.head, 0);
    }
    set_global_word(m_, kBitsWord, 0xe1000620);
    for (Word offset = 0; offset < 0x10; offset += 4) {
        const Word name = word_at(m_, kQuickAnimNames + offset);
        set_word_at(m_, kQuickAnims + offset, engine_value(kFindResource, {name}));
    }
    set_global_word(m_, kButtonsAnim, engine_value(kFindResource, {kButtonsName}));
    set_global_word(m_, kShadowMaterial, engine_value(kFindMaterial, {0x2a3b2550}));
}

// 0x00403730 DeleteBitList(head): every unprotected bit of the list is deleted through its vtable (slot 0 with flag 1); the next bit is read first
void BitKit::delete_bit_list(Word head) {
    if (head == 0) {
        return;
    }
    Word bit = head;
    Word next = bit_at(m_, bit).next();
    for (;;) {
        if (bit_at(m_, bit).kept_by_owner() == 0) {
            virtual_call(bit, 0, {1});
        }
        if (next == 0) {
            break;
        }
        bit = next;
        next = bit_at(m_, bit).next();
    }
}

// 0x00403760 Bit_DeleteAll: every list is emptied, then each list head is asserted empty (the protected bits are the owners' to delete)
void BitKit::bit_delete_all() {
    for (const ListEntry& entry : kDeleteOrder) {
        delete_bit_list(word_at(m_, entry.head));
    }
    for (const ListEntry& entry : kDeleteOrder) {
        check(word_at(m_, entry.head) == 0, entry.message);
    }
    check(global_word(m_, kBitCount) == 0, 0x00524510);
}

// 0x00403bd0 Bit_RemoveDeadBits
void BitKit::remove_dead_bits() {
    for (const ListEntry& entry : kDeleteOrder) {
        remove_dead_list(word_at(m_, entry.head));
    }
}

// 0x00403c70 RemoveDeadBits(head): the bits marked dead are deleted
void BitKit::remove_dead_list(Word head) {
    if (head == 0) {
        return;
    }
    Word bit = head;
    Word next = bit_at(m_, bit).next();
    for (;;) {
        if (bit_at(m_, bit).dead() != 0) {
            virtual_call(bit, 0, {1});
        }
        if (next == 0) {
            break;
        }
        bit = next;
        next = bit_at(m_, bit).next();
    }
}

// 0x00403ca0 Bit_ReduceRGB(colour, amount): each of the three bytes is lowered by the amount (not below 0)
void BitKit::reduce_rgb(Word colour, std::int32_t amount) {
    const Word value = word_at(m_, colour);
    const auto lower = [&](unsigned shift) -> Word {
        const std::int32_t channel = (value >> shift) & 0xff;
        return channel < amount ? 0 : static_cast<Word>((channel - amount) & 0xff);
    };
    set_word_at(m_, colour, (lower(16) << 16) | (lower(8) << 8) | (value & 0xff000000) | lower(0));
}

// 0x00403d30 CBit::operator new(size): a zeroed block from the loader's heap
Word BitKit::bit_new(Word size) {
    check(size < 0x200, 0x0052474c);
    const Word block = alloc(size);
    set_global_word(m_, kBitBytes, global_word(m_, kBitBytes) + 4 + ((size + 3) & 0xfffffffcu));
    for (Word k = 0; k < (size + 3) >> 2; ++k) {
        set_word_at(m_, block + 4 * k, 0);
    }
    return block;
}

// 0x00403dc0 CBit::operator delete(block)
void BitKit::bit_delete(Word block) {
    const Word size = block_size(block);
    set_global_word(m_, kBitBytes, global_word(m_, kBitBytes) + (0u - 4 - size));
    mem_delete(block);
}

// 0x00403df0 AttachTo(head): the bit becomes the head of the list
void BitKit::attach_to(Word self, Word head) {
    const Word old = word_at(m_, head);
    bit_at(m_, self).set_previous(0);
    bit_at(m_, self).set_next(static_cast<Word>(old));
    set_word_at(m_, head, self);
    if (bit_at(m_, self).next() != 0) {
        set_word_at(m_, bit_at(m_, self).next() + bit::kPrevious, self);
    }
}

// 0x00403e10 DeleteFrom(head)
void BitKit::delete_from(Word self, Word head) {
    const Word next = bit_at(m_, self).next();
    const Word previous = bit_at(m_, self).previous();
    if (next != 0) {
        set_word_at(m_, next + bit::kPrevious, previous);
    }
    if (previous != 0) {
        set_word_at(m_, previous + bit::kNext, bit_at(m_, self).next());
    }
    if (word_at(m_, head) == self) {
        set_word_at(m_, head, bit_at(m_, self).next());
    }
}

// 0x00403e40 Die: a protected bit must not die by itself
void BitKit::die(Word self) {
    check(bit_at(m_, self).kept_by_owner() == 0, 0x00524760);
    bit_at(m_, self).set_dead(1);
}

// 0x00403ea0 SetPos(position)
void BitKit::set_pos(Word self, Word position) {
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + bit::kPosition + 4 * k, word_at(m_, position + 4 * k));
    }
}

// 0x00403ec0: the velocity in a random cube of half-width `range` whole units
void BitKit::set_random_velocity(Word self, std::int32_t range) {
    const Word limit = u(range * 2 + 1);
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + bit::kVelocity + 4 * k, u((rnd(limit) - range) * 0x1000));
    }
}

// 0x00403f00 CBit::CBit
void BitKit::bit_construct(Word self) {
    for (Word k = 0; k < 9; ++k) {
        set_word_at(m_, self + bit::kPosition + 4 * k, 0);
    }
    for (Word k = 0; k < 3; ++k) {
        set_byte_at(m_, self + bit::kFriction + k, 0);
    }
    bit_at(m_, self).set_vtable(static_cast<Word>(kBaseVtable));
    for (Word k = 0; k < 3; ++k) {
        set_byte_at(m_, self + bit::kFriction + k, 1);
    }
    set_global_word(m_, kBitCount, global_word(m_, kBitCount) + 1);
}

// 0x00403f50 (thiscall)
Word BitKit::bit_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::bit_destruct); }

// 0x00403f70
void BitKit::bit_destruct(Word self) {
    bit_at(m_, self).set_vtable(static_cast<Word>(kBaseVtable));
    set_global_word(m_, kBitCount, global_word(m_, kBitCount) - 1);
}

void BitKit::vec3_add(Word self, Word rhs) {
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + 4 * k, word_at(m_, self + 4 * k) + word_at(m_, rhs + 4 * k));
    }
}

void BitKit::vec3_decay_shift(Word self, Word shifts) {
    for (Word k = 0; k < 3; ++k) {
        const std::int32_t v = int_at(m_, self + 4 * k);
        set_word_at(m_, self + 4 * k, u(v - (v >> (byte_at(m_, shifts + k) & 0x1f))));
    }
}

// 0x00403f90 CBit::Move: the position follows the velocity, which follows the acceleration and decays
void BitKit::bit_move(Word self) {
    vec3_add(self + bit::kPosition, self + bit::kVelocity);
    vec3_add(self + bit::kVelocity, self + bit::kAcceleration);
    vec3_decay_shift(self + bit::kVelocity, self + bit::kFriction);
}

// ---- the simple classes ----

// 0x00403fc0
Word BitKit::special_construct(Word self) {
    bit_construct(self);
    special_at(m_, self).set_vtable(static_cast<Word>(kSpecialVtable));
    attach_to(self, lists::kSpecial);
    return self;
}

// 0x00403fe0
Word BitKit::special_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::special_destruct); }

// 0x00404000
void BitKit::special_destruct(Word self) {
    special_at(m_, self).set_vtable(static_cast<Word>(kSpecialVtable));
    delete_from(self, lists::kSpecial);
    bit_destruct(self);
}

// 0x00404020
Word BitKit::nonrendered_construct(Word self) {
    bit_construct(self);
    nonrendered_at(m_, self).set_vtable(static_cast<Word>(kNonRenderedVtable));
    attach_to(self, lists::kNonRendered);
    return self;
}

// 0x00404040
Word BitKit::nonrendered_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::nonrendered_destruct); }

// 0x00404060
void BitKit::nonrendered_destruct(Word self) {
    nonrendered_at(m_, self).set_vtable(static_cast<Word>(kNonRenderedVtable));
    delete_from(self, lists::kNonRendered);
    bit_destruct(self);
}

// 0x00404080 CFT4Bit::CFT4Bit
Word BitKit::ft4_construct(Word self) {
    bit_construct(self);
    ft4_at(m_, self).set_vtable(static_cast<Word>(kFt4Vtable));
    ft4_at(m_, self).set_anim_speed(0x80);
    ft4_at(m_, self).set_scale(400);
    ft4_at(m_, self).set_colour(0x2c808080);
    return self;
}

// 0x004040b0
Word BitKit::ft4_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::ft4_destruct); }

// 0x004040d0
void BitKit::ft4_destruct(Word self) {
    ft4_at(m_, self).set_vtable(static_cast<Word>(kFt4Vtable));
    bit_destruct(self);
}

// 0x00404150 SetScale
void BitKit::set_scale(Word self, Word scale) { set_half_at(m_, self + bit::kScale, scale); }

// 0x00404170 SetSemiTransparent
void BitKit::set_semi_transparent(Word self) { set_word_at(m_, self + bit::kColour, word_at(m_, self + bit::kColour) | 0x2000000); }

// 0x004041c0 SetTint(red, green, blue)
void BitKit::set_tint(Word self, Word red, Word green, Word blue) {
    ft4_at(m_, self).set_colour(static_cast<Word>(((((blue & 0xff) << 8) | (green & 0xff)) << 8) | (ft4_at(m_, self).colour() & 0xff000000) | (red & 0xff)));
}

// 0x00404210 SetTransDecay
void BitKit::set_trans_decay(Word self, Word decay) { set_half_at(m_, self + bit::kTransDecay, decay); }

// 0x00404220 SetAnim(name): the animation of a resource name
void BitKit::set_anim_by_name(Word self, Word name) {
    const Word animation = engine_value(kFindResource, {name});
    ft4_at(m_, self).set_anim(static_cast<Word>(animation));
    ft4_at(m_, self).set_frame_count(static_cast<std::uint8_t>(byte_at(m_, animation - 4)));
    ft4_at(m_, self).set_frame_entry(static_cast<Word>(animation));
    ft4_at(m_, self).set_frame(0);
    ft4_at(m_, self).set_frame_fraction(0);
}

// 0x00404250: one of the four animations Bit_Init looked up
void BitKit::set_anim(Word self, std::int32_t index) {
    check(index >= 0 && index <= 3, 0x00524778);
    const Word animation = word_at(m_, kQuickAnims + 4 * u(index));
    ft4_at(m_, self).set_anim(static_cast<Word>(animation));
    ft4_at(m_, self).set_frame_count(static_cast<std::uint8_t>(byte_at(m_, animation - 4)));
    ft4_at(m_, self).set_frame(0);
    ft4_at(m_, self).set_frame_fraction(0);
    ft4_at(m_, self).set_frame_entry(static_cast<Word>(animation));
}

// 0x004042d0 SetFrame
void BitKit::set_frame(Word self, std::int32_t frame) {
    check(!(frame < 0 || static_cast<std::int32_t>(ft4_at(m_, self).frame_count()) <= frame), 0x005247bc);
    check(ft4_at(m_, self).anim() != 0, 0x0052479c);
    ft4_at(m_, self).set_frame(static_cast<std::uint8_t>(u(frame)));
    ft4_at(m_, self).set_frame_fraction(0);
    ft4_at(m_, self).set_frame_entry(static_cast<Word>(ft4_at(m_, self).anim() + u(static_cast<std::int8_t>(frame) * 8)));
}

// the animation clock of IncFrame / IncFrameWithWrap: (speed * scale >> 8) added to the 16-bit (frame, fraction); the frame is the high byte as a signed byte
namespace {
std::int8_t advance_clock(RetailMemory& m, Word self) {
    const std::int32_t step = (static_cast<std::int32_t>(guest::read_scalar<std::int16_t>(m, self + bit::kAnimSpeed)) * guest::read_scalar<std::int32_t>(m, kAnimationTimeScale)) >> 8;
    const Word clock = (static_cast<Word>(guest::read_scalar<std::uint8_t>(m, self + bit::kFrame)) << 8) | guest::read_scalar<std::uint8_t>(m, self + bit::kFrameFraction);
    const Word total = static_cast<Word>(step) + clock;
    guest::write_scalar<std::uint8_t>(m, self + bit::kFrameFraction, static_cast<std::uint8_t>(total));
    const auto frame = static_cast<std::int8_t>(static_cast<std::int16_t>(total & 0xffff) >> 8);
    guest::write_scalar<std::uint8_t>(m, self + bit::kFrame, static_cast<std::uint8_t>(static_cast<std::uint8_t>(frame)));
    return frame;
}
}  // namespace

// 0x00404390
void BitKit::inc_frame(Word self) {
    const std::int8_t frame = advance_clock(m_, self);
    ft4_at(m_, self).set_frame_entry(static_cast<Word>(ft4_at(m_, self).anim() + u(frame * 8)));
}

// 0x004043d0
void BitKit::inc_frame_with_wrap(Word self) {
    const std::int8_t frame = advance_clock(m_, self);
    if (static_cast<std::int32_t>(frame) >= static_cast<std::int32_t>(ft4_at(m_, self).frame_count())) {
        ft4_at(m_, self).set_frame(0);
    }
    ft4_at(m_, self).set_frame_entry(static_cast<Word>(ft4_at(m_, self).anim() + u(static_cast<std::int8_t>(ft4_at(m_, self).frame()) * 8)));
}

// 0x004068a0 CFrag::Move: the three positions (+0x10, +0x14, +0x18) move by the velocities at +0x1c, +0x20 and +0x24; each velocity gains its acceleration (+0x28, +0x2c, +0x30) and
// loses a share of the sum: the sum shifted right (arithmetically) by the byte at +0x34, +0x35 or +0x36. The frame (+0x4e) steps on and stays below the count (+0x4d), the age
// (+0xc, a signed word) steps on, and a fragment whose age has reached its limit (+0xe) dies.
void BitKit::frag_move(Word self) {
    const Word velocity_a = word_at(m_, self + 0x1c);
    const Word velocity_b = word_at(m_, self + 0x20);
    const Word velocity_c = word_at(m_, self + 0x24);
    set_word_at(m_, self + 0x14, word_at(m_, self + 0x14) + velocity_b);
    set_word_at(m_, self + 0x18, word_at(m_, self + 0x18) + velocity_c);
    set_word_at(m_, self + 0x10, word_at(m_, self + 0x10) + velocity_a);
    const auto drag = [&](Word velocity, Word acceleration, Word shift_at) {
        const Word sum = velocity + acceleration;
        return static_cast<Word>(sum - static_cast<Word>(static_cast<std::int32_t>(sum) >> (byte_at(m_, shift_at) & 0x1f)));
    };
    set_word_at(m_, self + 0x1c, drag(velocity_a, word_at(m_, self + 0x28), self + 0x34));
    set_word_at(m_, self + 0x20, drag(velocity_b, word_at(m_, self + 0x2c), self + 0x35));
    set_word_at(m_, self + 0x24, drag(velocity_c, word_at(m_, self + 0x30), self + 0x36));
    const auto frame = static_cast<std::uint8_t>(byte_at(m_, self + 0x4e) + 1);
    set_byte_at(m_, self + 0x4e, frame);
    if (static_cast<std::int32_t>(static_cast<std::int8_t>(frame)) >= static_cast<std::int32_t>(byte_at(m_, self + 0x4d))) {
        set_byte_at(m_, self + 0x4e, static_cast<std::uint8_t>(byte_at(m_, self + 0x4d) - 1));
    }
    const auto age = static_cast<std::uint16_t>(half_at(m_, self + 0xc) + 1);
    set_half_at(m_, self + 0xc, age);
    if (static_cast<std::int32_t>(static_cast<std::int16_t>(age)) >= static_cast<std::int32_t>(half_at(m_, self + 0xe))) {
        die(self);
    }
}

// 0x00404460
void BitKit::choose_random_frame(Word self) {
    const Word frame = static_cast<Word>(rnd(ft4_at(m_, self).frame_count()));
    ft4_at(m_, self).set_frame(static_cast<std::uint8_t>(frame));
    ft4_at(m_, self).set_frame_entry(static_cast<Word>(ft4_at(m_, self).anim() + u(static_cast<std::int8_t>(frame) * 8)));
}

// 0x00404490 Shrink(die): the scale shrinks by its decay; at zero the bit is done (1)
Word BitKit::shrink(Word self, Word die_flag) {
    if (static_cast<std::int16_t>(ft4_at(m_, self).scale()) == 0) {
        if (die_flag != 0) {
            die(self);
        }
        return 1;
    }
    const auto scale = static_cast<std::int16_t>(static_cast<std::int16_t>(ft4_at(m_, self).scale()) - ft4_at(m_, self).scale_decay());
    ft4_at(m_, self).set_scale(static_cast<std::uint16_t>(static_cast<std::uint16_t>(scale)));
    if (scale < 0) {
        ft4_at(m_, self).set_scale(0);
    }
    return 0;
}

// 0x004044f0 Fade(die): the colour fades by the transparency decay (each channel not below 0); at black the bit is done (1)
Word BitKit::fade(Word self, Word die_flag) {
    const Word colour = ft4_at(m_, self).colour();
    if ((colour & 0xffffff) == 0) {
        if (die_flag != 0) {
            die(self);
        }
        return 1;
    }
    const std::int16_t decay = ft4_at(m_, self).trans_decay();
    const auto lowered = [&](Word channel) -> Word { return decay > static_cast<std::int16_t>(channel) ? 0 : ((channel - decay) & 0xff); };
    const Word red = lowered(colour & 0xff);
    const Word green = lowered((colour >> 8) & 0xff);
    const Word blue = lowered((colour >> 16) & 0xff);
    ft4_at(m_, self).set_colour(static_cast<Word>((((blue << 8) | green) << 8) | (colour & 0xff000000) | red));
    return 0;
}

// 0x00404590 CQuadBit::CQuadBit
Word BitKit::quad_construct(Word self) {
    bit_construct(self);
    for (Word k = 0; k < 9; ++k) {
        set_word_at(m_, self + 0x3c + 4 * k, 0);
    }
    quad_at(m_, self).set_vtable(static_cast<Word>(kQuadVtable));
    quad_at(m_, self).set_field_60(0x2c808080);
    attach_to(self, lists::kQuad);
    return self;
}

// 0x004045e0
Word BitKit::quad_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::quad_destruct); }

// 0x00404600
void BitKit::quad_destruct(Word self) {
    quad_at(m_, self).set_vtable(static_cast<Word>(kQuadVtable));
    delete_from(self, lists::kQuad);
    bit_destruct(self);
}

// 0x00404620: the texture of frame `frame` of one of the four looked-up animations
void BitKit::quad_set_texture_index(Word self, std::int32_t animation, std::int32_t frame) {
    check(animation >= 0 && animation <= 3, 0x00524800);
    const Word entry = word_at(m_, kQuickAnims + 4 * u(animation));
    const std::int32_t count = int_at(m_, entry - 4);
    check(!(frame < 0 || count <= frame), 0x005247d8);
    quad_at(m_, self).set_field_64(static_cast<Word>(word_at(m_, entry + 4 + u(frame) * 8)));
}

// 0x004046f0 CQuadBit::SetTexture(name, frame)
void BitKit::quad_set_texture(Word self, Word name, std::int32_t frame) {
    const Word entry = engine_value(kFindResource, {name});
    const std::int32_t count = int_at(m_, entry - 4);
    check(!(frame < 0 || count <= frame), 0x005247d8);
    quad_at(m_, self).set_field_64(static_cast<Word>(word_at(m_, entry + 4 + u(frame) * 8)));
}

// 0x00404810
void BitKit::quad_flag_on(Word self, Word bits) { set_byte_at(m_, self + 0x68, byte_at(m_, self + 0x68) | bits); }

// 0x00404840
void BitKit::quad_set_semi_transparent(Word self) { set_word_at(m_, self + 0x60, word_at(m_, self + 0x60) | 0x2000000); }

// 0x00404860: the grey level of the quad's colour
void BitKit::quad_set_transparency(Word self, Word level) {
    const Word v = level & 0xff;
    quad_at(m_, self).set_field_60(static_cast<Word>((((v << 8) | v) << 8) | (quad_at(m_, self).field_60() & 0xff000000) | v));
}

// 0x004048c0 CQuadBit::OrientUsing(centre, normal, width, height, extra): the four corners of a quad of the given half-sizes in the plane of a short normal, around the centre
void BitKit::quad_orient(Word self, Word centre, Word normal, Word half_width, Word half_height) {
    // the scratch locals: the first axis A, the second axis B, their lengths and the half-sizes (the scalar helpers take their scalars by address)
    constexpr Word kA = kScratch + 0x00;
    constexpr Word kB = kScratch + 0x0c;
    constexpr Word kLenA = kScratch + 0x18;
    constexpr Word kLenB = kScratch + 0x1c;
    constexpr Word kWidth = kScratch + 0x20;
    constexpr Word kHeight = kScratch + 0x24;
    constexpr Word kTmp1 = kScratch + 0x30;
    constexpr Word kTmp2 = kScratch + 0x3c;
    constexpr Word kTmp3 = kScratch + 0x48;
    const std::int32_t nx = short_at(m_, normal);
    const std::int32_t ny = short_at(m_, normal + 2);
    const std::int32_t nz = short_at(m_, normal + 4);
    set_global_word(m_, kA, 0);
    set_global_word(m_, kA + 4, u(-nz));
    set_global_word(m_, kA + 8, u(ny));
    set_global_word(m_, kB, u(ny * ny + nz * nz));
    set_global_word(m_, kB + 4, u(-(nx * ny)));
    set_global_word(m_, kB + 8, u(-(nx * nz)));
    set_global_word(m_, kLenA, core_.vector_length(kA));
    set_global_word(m_, kLenB, core_.vector_length(kB));
    check(global_word(m_, kLenA) != 0 && global_word(m_, kLenB) != 0, 0x00524830);
    set_global_word(m_, kWidth, half_width);
    set_global_word(m_, kHeight, half_height);
    core_.vector_div_out(kA, core_.vector_mul_out(kTmp1, kA, kWidth), kLenA);
    core_.vector_div_out(kB, core_.vector_mul_out(kTmp1, kB, kHeight), kLenB);
    core_.vector_subtract_out(self + bit::kPosition, core_.vector_subtract_out(kTmp2, centre, kA), kB);
    core_.vector_subtract_out(self + 0x3c, core_.vector_add_out(kTmp2, centre, kA), kB);
    core_.vector_add_out(self + 0x48, core_.vector_subtract_out(kTmp2, centre, kA), kB);
    core_.vector_add_out(self + 0x54, core_.vector_add_out(kTmp2, centre, kA), kB);
    (void)kTmp3;
}

// 0x00404af0 (four corners) / 0x00404bf0 (three): the polygon with its corners as three shorts each
Word BitKit::genpoly4_construct(Word self, Word a, Word b, Word c, Word d) {
    bit_construct(self);
    for (Word k = 0; k < 12; ++k) {
        set_word_at(m_, self + 0x60 + 4 * k, 0);
    }
    genpoly_at(m_, self).set_field_90(0);
    genpoly_at(m_, self).set_field_92(0);
    genpoly_at(m_, self).set_field_94(0);
    genpoly_at(m_, self).set_vtable(static_cast<Word>(kGenPolyVtable));
    genpoly_at(m_, self).set_kind(6);
    const Word corners[4] = {a, b, c, d};
    for (Word k = 0; k < 4; ++k) {
        for (Word j = 0; j < 3; ++j) {
            set_half_at(m_, self + 0x40 + 8 * k + 2 * j, half_at(m_, corners[k] + 2 * j));
        }
    }
    attach_to(self, lists::kGenPoly);
    return self;
}

Word BitKit::genpoly3_construct(Word self, Word a, Word b, Word c) {
    bit_construct(self);
    for (Word k = 0; k < 12; ++k) {
        set_word_at(m_, self + 0x60 + 4 * k, 0);
    }
    genpoly_at(m_, self).set_field_90(0);
    genpoly_at(m_, self).set_field_92(0);
    genpoly_at(m_, self).set_field_94(0);
    genpoly_at(m_, self).set_vtable(static_cast<Word>(kGenPolyVtable));
    genpoly_at(m_, self).set_kind(5);
    const Word corners[3] = {a, b, c};
    for (Word k = 0; k < 3; ++k) {
        for (Word j = 0; j < 3; ++j) {
            set_half_at(m_, self + 0x40 + 8 * k + 2 * j, half_at(m_, corners[k] + 2 * j));
        }
    }
    attach_to(self, lists::kGenPoly);
    return self;
}

// 0x00404bd0
Word BitKit::genpoly_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::genpoly_destruct); }

// 0x00404cb0
void BitKit::genpoly_destruct(Word self) {
    genpoly_at(m_, self).set_vtable(static_cast<Word>(kGenPolyVtable));
    delete_from(self, lists::kGenPoly);
    bit_destruct(self);
}

// 0x004051c0 CChunkBit::CChunkBit(a, b, c): a triangle of shorts, with a velocity along its normal (scaled by 0x80..0xdf out of 0x1000)
Word BitKit::chunk_construct(Word self, Word a, Word b, Word c) {
    bit_construct(self);
    for (Word k = 0; k < 12; ++k) {
        set_word_at(m_, self + 0x5c + 4 * k, 0);
    }
    chunk_at(m_, self).set_field_8c(0);
    chunk_at(m_, self).set_field_8e(0);
    chunk_at(m_, self).set_field_90(0);
    chunk_at(m_, self).set_vtable(static_cast<Word>(kChunkVtable));
    chunk_at(m_, self).set_kind(6);
    // the corners are copied to +0x3c, +0x44 and +0x4c (three shorts each)
    const Word corners[3] = {a, b, c};
    constexpr Word offsets[3] = {0x3c, 0x44, 0x4c};
    for (Word k = 0; k < 3; ++k) {
        for (Word j = 0; j < 3; ++j) {
            set_half_at(m_, self + offsets[k] + 2 * j, half_at(m_, corners[k] + 2 * j));
        }
    }
    const auto sh = [&](Word address) { return short_at(m_, address); };
    const std::int32_t cx = sh(c) - sh(a);
    const std::int32_t cy = sh(c + 2) - sh(a + 2);
    const std::int32_t cz = sh(c + 4) - sh(a + 4);
    const std::int32_t bx = sh(b) - sh(a);
    const std::int32_t by = sh(b + 2) - sh(a + 2);
    const std::int32_t bz = sh(b + 4) - sh(a + 4);
    constexpr Word kNormal = kScratch + 0x00;
    constexpr Word kUnit = kScratch + 0x10;
    set_global_word(m_, kNormal, u(bz * cy - cz * by));
    set_global_word(m_, kNormal + 4, u(cz * bx - bz * cx));
    set_global_word(m_, kNormal + 8, u(by * cx - cy * bx));
    set_global_word(m_, kUnit, 0);
    set_global_word(m_, kUnit + 4, 0);
    set_global_word(m_, kUnit + 8, 0);
    core_.unit_vector(kUnit, kNormal);
    const std::int32_t speed = rnd(0x60) + 0x80;
    chunk_at(m_, self).set_field_54(static_cast<std::uint16_t>(u((speed * global_int(m_, kUnit)) >> 12)));
    chunk_at(m_, self).set_field_b4(0);
    chunk_at(m_, self).set_field_56(static_cast<std::uint16_t>(u((global_int(m_, kUnit + 4) * speed) >> 12)));
    chunk_at(m_, self).set_field_58(static_cast<std::uint16_t>(u((global_int(m_, kUnit + 8) * speed) >> 12)));
    attach_to(self, lists::kChunk);
    return self;
}

// 0x00405390
Word BitKit::chunk_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::chunk_destruct); }

// 0x004053b0
void BitKit::chunk_destruct(Word self) {
    chunk_at(m_, self).set_vtable(static_cast<Word>(kChunkVtable));
    delete_from(self, lists::kChunk);
    bit_destruct(self);
}

// 0x004053d0 CChunkBit::SetRGB(red, green, blue): the colour and three shaded copies (a random fraction of it each)
void BitKit::chunk_set_rgb(Word self, Word red, Word green, Word blue) {
    blue &= 0xff;
    green &= 0xff;
    red &= 0xff;
    chunk_at(m_, self).set_field_a0(static_cast<Word>((((blue << 8) | green) << 8) | red));
    for (Word k = 0; k < 3; ++k) {
        const Word fraction = static_cast<Word>(rnd(0x1000));
        const Word r = static_cast<Word>(static_cast<std::int32_t>(red * fraction) >> 12);
        const Word g = static_cast<Word>(static_cast<std::int32_t>(green * fraction) >> 12) & 0xffffff;
        const Word b = ((blue * fraction) & 0xfffff000u) << 4;
        set_word_at(m_, self + 0xa4 + 4 * k, (g << 8) | b | r);
    }
}

// 0x004054b0 (thiscall, eight arguments): the texture page and clut and the u, v of three corners
void BitKit::chunk_set_texture(Word self, Word page, Word clut, Word u0, Word v0, Word u1, Word v1, Word u2, Word v2) {
    chunk_at(m_, self).set_field_94(static_cast<Word>(((((page & 0xffff) << 8) | (v0 & 0xff)) << 8) | (u0 & 0xff)));
    chunk_at(m_, self).set_field_98(static_cast<Word>(((((clut & 0xffff) << 8) | (v1 & 0xff)) << 8) | (u1 & 0xff)));
    chunk_at(m_, self).set_field_9c(static_cast<std::uint16_t>(((v2 & 0xff) << 8) | (u2 & 0xff)));
}

}  // namespace opentony::runtime::bitkit
