#include "world_views.hpp"

// Phase 4.15c: the skater object (`bruce.cpp`, 0x0046c5f0..0x0046e4f0): the small methods of the skater class (vtable 0x00518dd8): the board bits, the destructor, the registry of reference-counted
// animation names, the fade of the skater's light colour, the geometry helpers of the shadow, the flip to the velocity, the grunt sounds and the water droplet object. `self` is the skater.

namespace opentony::runtime::worldkit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kObjectFree = 0x0047fd60;
constexpr Word kListRemove = 0x004801f0;
constexpr Word kStopVoice = 0x004ad9f0;
constexpr Word kPlaySound = 0x004ad620;
constexpr Word kFruVectors = 0x0049c7d0;               // CBruce__FRUVectorsFromRotMatrix (thiscall)
constexpr Word kReadHooks = 0x00464b90;                // M3D_ReadHooksPacket(object, table)
constexpr Word kFallingTextDestroy = 0x004b9770;       // thiscall
constexpr Word kZoomTextDestroy = 0x004ba060;          // thiscall
constexpr Word kFontDestruct = 0x0044a060;             // thiscall
constexpr Word kPanelMemberDestroy = 0x004311d0;       // thiscall

constexpr Word kPlayerList = 0x0056a960;
constexpr Word kPlayerCount = 0x0056a95c;
constexpr Word kSkaterVtable = 0x00518dd8;
constexpr Word kWaterVtable = 0x00518df4;

}  // namespace

// 0x0046c5f0: the skater's board parts (three indices) are flagged in the part mask
void WorldKit::board_off(Word self) {
    const Word mask = (1u << (skater_view(m_, self).appearance_resource_12().address & 31)) | (1u << (skater_view(m_, self).appearance_resource_11().address & 31)) | (1u << (skater_view(m_, self).appearance_resource_10().address & 31));
    skater_view(m_, self).set_field_148(static_cast<Word>(skater_view(m_, self).field_148() | mask));
}

// 0x0046c630
void WorldKit::board_on(Word self) {
    const Word mask = (1u << (skater_view(m_, self).appearance_resource_12().address & 31)) | (1u << (skater_view(m_, self).appearance_resource_11().address & 31)) | (1u << (skater_view(m_, self).appearance_resource_10().address & 31));
    skater_view(m_, self).set_field_148(static_cast<Word>(skater_view(m_, self).field_148() & ~mask));
}

// 0x0046c670: the skater faces +x/+z again: the orientation rows are the identity (0xf000 is -0x1000) and the vectors derived from them are rebuilt
void WorldKit::reset_orientation_rows(Word self) {
    const Word rows = self + 0x2e58;
    set_half_at(m_, rows, 0xf000);
    set_half_at(m_, self + 0x2e5e, 0);
    set_half_at(m_, self + 0x2e64, 0);
    set_half_at(m_, self + 0x2e5a, 0);
    set_half_at(m_, self + 0x2e60, 0xf000);
    set_half_at(m_, self + 0x2e66, 0);
    set_half_at(m_, self + 0x2e5c, 0);
    set_half_at(m_, self + 0x2e62, 0);
    set_half_at(m_, self + 0x2e68, 0xf000);
    thiscall(kFruVectors, self);
    const Word a = u(static_cast<std::int16_t>(half_at(m_, self + 0x2e5a)));
    const Word d = u(static_cast<std::int16_t>(half_at(m_, self + 0x2e66)));
    for (Word k = 0; k < 8; ++k) {
        set_word_at(m_, self + 0x2e38 + 4 * k, word_at(m_, rows + 4 * k));
    }
    const Word c = u(static_cast<std::int16_t>(half_at(m_, self + 0x2e60)));
    skater_view(m_, self).set_field_3130(0);
    set_word_at(m_, self + 0x3134, a);
    set_word_at(m_, self + 0x3138, c);
    set_word_at(m_, self + 0x313c, d);
    set_half_at(m_, self + 0x80, a);
    set_half_at(m_, self + 0x82, c);
    set_half_at(m_, self + 0x84, d);
}

// 0x0046cbe0
Word WorldKit::skater_scalar_delete(Word self, Word flags) {
    skater_destruct(self);
    if ((flags & 1u) != 0) {
        engine(kObjectFree, {self});
    }
    return self;
}

// 0x0046cc00: the exception-unwinding clean-up of the skater's panel members (the six text objects, last to first)
void WorldKit::skater_destroy_panel_members(Word self) {
    thiscall(kFallingTextDestroy, self + 0x68c);
    thiscall(kZoomTextDestroy, self + 0x66c);
    thiscall(kFallingTextDestroy, self + 0x548);
    thiscall(kFontDestruct, self + 0x3fc);
    thiscall(kFontDestruct, self + 0x2b0);
    thiscall(kFontDestruct, self + 0x164);
}

// 0x0046cca0
void WorldKit::skater_destruct(Word self) {
    skater_view(m_, self).set_field_0(static_cast<Word>(kSkaterVtable));
    Word voice = skater_view(m_, self).voice_3090();
    if (voice != 0xffffffffu) {
        engine(kStopVoice, {voice});
        skater_view(m_, self).set_voice_3090(0xffffffff);
    }
    voice = skater_view(m_, self).voice_3094();
    if (voice != 0xffffffffu) {
        engine(kStopVoice, {voice});
        skater_view(m_, self).set_voice_3094(0xffffffff);
    }
    if (skater_view(m_, self).field_e0() != 0) {
        mem_delete(skater_view(m_, self).field_e0());
    }
    if (skater_view(m_, self).field_e4() != 0) {
        mem_delete(skater_view(m_, self).field_e4());
    }
    thiscall(kListRemove, self, {kPlayerList});
    set_global_word(m_, kPlayerCount, global_word(m_, kPlayerCount) - 1);
    thiscall(kPanelMemberDestroy, self + 0x2c14);
    const Word panel = self + 0x168;
    thiscall(kFallingTextDestroy, panel + 0x68c);
    thiscall(kZoomTextDestroy, panel + 0x66c);
    thiscall(kFallingTextDestroy, panel + 0x548);
    thiscall(kFontDestruct, panel + 0x3fc);
    thiscall(kFontDestruct, panel + 0x2b0);
    thiscall(kFontDestruct, panel + 0x164);
    animated_destruct(self);
}

// 0x0046cdf0 (cdecl): a rotate-left-by-one hash of the bytes of a name, xor-ed in turn, from the seed's low bit
Word WorldKit::name_hash(Word string, Word seed) {
    Word hash = seed & 1u;
    for (Word at = string; byte_at(m_, at) != 0; ++at) {
        hash = ((hash << 1) | (hash >> 31)) ^ byte_at(m_, at);
    }
    return hash;
}

// 0x0046ce20 (thiscall, two arguments): the index of a name in the registry (-1 for none)
Word WorldKit::name_find(Word self, Word string, Word seed) {
    const Word hash = name_hash(string, seed);
    const std::int32_t count = s(word_at(m_, self + 0x3214));
    for (std::int32_t index = 0; index < count; ++index) {
        if (word_at(m_, self + 0x3218 + 8 * static_cast<Word>(index)) == hash) {
            return static_cast<Word>(index);
        }
    }
    return 0xffffffffu;
}

// 0x0046ce60
void WorldKit::name_reset(Word self) { set_word_at(m_, self + 0x3214, 0); }

// 0x0046ce70 (thiscall, two arguments): counts one more user of the name, adding it to the registry (of 100) when it is new
Word WorldKit::name_add(Word self, Word string, Word seed) {
    Word index = name_find(self, string, seed);
    if (index == 0xffffffffu) {
        index = word_at(m_, self + 0x3214);
        if (s(index) < 0x64) {
            set_word_at(m_, self + 0x3214, index + 1);
        } else {
            check(false, 0x00534808);
            index = 0;
        }
        set_word_at(m_, self + 0x3218 + 8 * index, name_hash(string, seed));
        set_half_at(m_, self + 0x321c + 8 * index, 0);
    }
    set_half_at(m_, self + 0x321c + 8 * index, half_at(m_, self + 0x321c + 8 * index) + 1);
    return half_at(m_, self + 0x321c + 8 * index);
}

// 0x0046cf20 (thiscall, two arguments): one user of the name less
void WorldKit::name_release(Word self, Word string, Word seed) {
    const Word index = name_find(self, string, seed);
    check(index != 0xffffffffu, 0x0053483c);
    check(half_at(m_, self + 0x321c + 8 * index) > 0, 0x00534820);
    if (s(index) >= 0) {
        set_half_at(m_, self + 0x321c + 8 * index, half_at(m_, self + 0x321c + 8 * index) - 1);
    }
}

// 0x0046cfe0 (thiscall, two arguments): how many users the name has
Word WorldKit::name_count(Word self, Word string, Word seed) {
    const Word index = name_find(self, string, seed);
    if (index == 0xffffffffu) {
        return 0;
    }
    return half_at(m_, self + 0x321c + 8 * index);
}

// 0x0046d010 (cdecl): `value` moved towards `target` by at most `step`
Word WorldKit::step_toward(Word value, Word target, Word step) {
    if (s(value) < s(target)) {
        const Word moved = value + step;
        return s(moved) > s(target) ? target : moved;
    }
    const Word moved = value - step;
    return s(moved) < s(target) ? target : moved;
}

// 0x0046d040: the skater's light colour (three bytes at +0x3174) fades towards its target (+0x3177) at the step of +0x317a; a pending next target (+0x317b) takes over when it is reached
void WorldKit::fade_light_colour(Word self) {
    skater_view(m_, self).set_field_3174(static_cast<std::uint8_t>(step_toward(skater_view(m_, self).field_3174(), skater_view(m_, self).field_3177(), skater_view(m_, self).field_317a())));
    skater_view(m_, self).set_field_3175(static_cast<std::uint8_t>(step_toward(skater_view(m_, self).field_3175(), skater_view(m_, self).field_3178(), skater_view(m_, self).field_317a())));
    skater_view(m_, self).set_field_3176(static_cast<std::uint8_t>(step_toward(skater_view(m_, self).field_3176(), skater_view(m_, self).field_3179(), skater_view(m_, self).field_317a())));
    const Word red = skater_view(m_, self).field_3174();
    const Word green = skater_view(m_, self).field_3175();
    const Word blue = skater_view(m_, self).field_3176();
    if (red == skater_view(m_, self).field_3177() && green == skater_view(m_, self).field_3178() && blue == skater_view(m_, self).field_3179() && skater_view(m_, self).field_317b() != 0) {
        const Word next = skater_view(m_, self).field_317b();
        skater_view(m_, self).set_field_3177(0);
        skater_view(m_, self).set_field_3178(0);
        skater_view(m_, self).set_field_3179(0);
        skater_view(m_, self).set_field_317a(static_cast<std::uint8_t>(next));
        skater_view(m_, self).set_field_317b(0);
    }
    if ((green | red | blue) == 0) {
        skater_view(m_, self).set_field_88(0);
        return;
    }
    skater_view(m_, self).set_field_8c(0x2000);
    skater_view(m_, self).set_field_8e(0xa);
    skater_view(m_, self).set_field_88(static_cast<Word>(((((blue | 0x200u) << 8) | green) << 8) | red));
}

// 0x0046d1d0 (cdecl, six arguments): the signed distance of the point (x, y) from the line through (x0, y0) and (x1, y1)
Word WorldKit::distance_to_line(Word x, Word y, Word x0, Word y0, Word x1, Word y1) {
    const std::int32_t dx = s(x1 - x0);
    const std::int32_t dy = s(y1 - y0);
    const Word length = sqrt_truncated(u(dy * dy + dx * dx));
    const Word numerator = u((s(y0 - y) * dx) - (s(x0 - x) * dy));
    return u(divide(s(numerator), s(length)));
}

// 0x0046d220 (cdecl, twelve arguments): the intersection of two lines; (x, y) and the numerator and denominator of the parameter are written through the last four pointers
void WorldKit::line_intersection(Word x0, Word y0, Word x1, Word y1, Word x2, Word y2, Word x3, Word y3, Word out_x, Word out_y, Word out_numerator, Word out_denominator) {
    const std::int32_t dx0 = s(x1 - x0);
    const std::int32_t dy0 = s(y1 - y0);
    const std::int32_t dx1 = s(x3 - x2);
    const std::int32_t dy1 = s(y3 - y2);
    const std::int32_t denominator = dy1 * dx0 - dx1 * dy0;
    if (denominator == 0) {
        set_word_at(m_, out_x, x2);
        set_word_at(m_, out_y, y2);
        set_word_at(m_, out_numerator, 0);
        set_word_at(m_, out_denominator, 1);
        log_line(0x00534860);
        return;
    }
    const std::int32_t numerator = s(y0 - y2) * dx0 - s(x0 - x2) * dy0;
    set_word_at(m_, out_x, u(divide(dx1 * numerator, denominator)) + x2);
    set_word_at(m_, out_y, u(divide(dy1 * numerator, denominator)) + y2);
    set_word_at(m_, out_numerator, u(numerator));
    set_word_at(m_, out_denominator, u(denominator));
}

// 0x0046d6e0: the pad profile bytes of the skater's input table (the second is mirrored in a two-player game when the screen is flipped)
void WorldKit::input_profile_prep(Word self) {
    const Word profile = skater_view(m_, self).action_record_bank().address;
    skater_view(m_, self).set_field_31a2(static_cast<std::uint8_t>(byte_at(m_, profile + 0x148)));
    Word second = byte_at(m_, profile + 0x149);
    skater_view(m_, self).set_field_31a1(static_cast<std::uint8_t>(second));
    if (global_word(m_, 0x0056a88c) != 0 && global_word(m_, 0x00533f3c) == 1) {
        skater_view(m_, self).set_field_31a1(static_cast<std::uint8_t>(0u - second));
    }
}

// 0x0046d750
void WorldKit::mark_dying_and_visible(Word self) {
    if (is_dying(self) == 0) {
        set_byte_at(m_, self + object::kState, byte_at(m_, self + object::kState) | 0x40);
        set_byte_at(m_, self + object::kFlags, byte_at(m_, self + object::kFlags) | 0x41);
    }
}

// 0x0046d940 (thiscall, one argument): the skater's model region and the hooks packet of its character
void WorldKit::player_bind_hook_packet(Word self, Word name) {
    select_psx_region(self, name);
    engine(kReadHooks, {self, 0x00534620 + 20 * word_at(m_, self + 0x2cc0)});
}

// 0x0046d970: if the skater now moves against the way it faces the board turns round (the front and side vectors are negated); answers whether it did
Word WorldKit::flip_to_velocity(Word self) {
    if (skater_view(m_, self).field_f6() == 0x22 || skater_view(m_, self).field_2c7c() != 0 || skater_view(m_, self).field_2c78() != 0) {
        return 0;
    }
    if (math_.dot_q12(self + 0x30f4, self + 0x4c) <= 0x5000) {
        return 0;
    }
    static constexpr Word kPairs[6][2] = {{0x30f4, 0x2e5c}, {0x30f8, 0x2e62}, {0x30fc, 0x2e68}, {0x3100, 0x2e58}, {0x3104, 0x2e5e}, {0x3108, 0x2e64}};
    for (const auto& pair : kPairs) {
        const Word negated = (0u - half_at(m_, self + pair[0])) & 0xffff;
        set_half_at(m_, self + pair[1], negated);
        set_word_at(m_, self + pair[0], u(static_cast<std::int16_t>(negated)));
    }
    skater_view(m_, self).set_field_d8(static_cast<Word>(skater_view(m_, self).field_d8() ^ 2u));
    return 1;
}

// 0x0046dd20 (thiscall, two arguments): the blood splat the skater asks for (unless its character's flags forbid it)
void WorldKit::request_blood_splat(Word self, Word a0, Word a1) {
    const Word character = skater_view(m_, self).skater_resource().address;
    const Word flags = word_at(m_, 0x0053a514 + (((character * 3) << 3) - character) * 16);
    if ((flags & 0x100) != 0) {
        return;
    }
    skater_view(m_, self).set_field_2c58(static_cast<Word>(a0));
    skater_view(m_, self).set_field_2c5c(static_cast<Word>(a1));
    skater_view(m_, self).set_field_2c54(1);
}

// 0x0046e1d0 (cdecl): the grunt a character makes, by the character (sounds 0x156, 0x157, 0x35 and 0x28 for the special ones) and always one of three others
void WorldKit::skater_grunt() {
    switch (s(global_word(m_, 0x0056b7e8)) - 7) {
        case 0:
            engine(kPlaySound, {0x35, 0x2000, 0});
            break;
        case 2:
            engine(kPlaySound, {0x28, 0x2000, 0});
            break;
        case 3:
            engine(kPlaySound, {0x156, 0x2000, 0});
            break;
        case 4:
            engine(kPlaySound, {0x157, 0x2000, 0});
            break;
        default:
            break;
    }
    engine(kPlaySound, {static_cast<Word>(rnd(3)) + 0x1c, 0x2000, 0});
}

// 0x0046e3f0 (thiscall)
Word WorldKit::droplet_scalar_delete(Word self, Word flags) {
    droplet_destruct(self);
    if ((flags & 1u) != 0) {
        engine(kObjectFree, {self});
    }
    return self;
}

// 0x0046e410 (the empty method 0x0046e460 is a hook)
void WorldKit::droplet_destruct(Word self) {
    set_word_at(m_, self, kWaterVtable);
    empty_hook();
    object_destruct(self);
}

// 0x0046e470: one frame of a water droplet: it spins, follows its anchor and rises or falls with a sine
void WorldKit::droplet_update(Word self) {
    vec3s_add(self + 0x14, self + 0x70);
    vec3s_mask12(self + 0x14);
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + 8 + 4 * k, word_at(m_, self + 0xd4 + 4 * k));
    }
    const std::int32_t sine = math_.sin_q12(static_cast<std::int32_t>(half_at(m_, self + 0xe0) & 0xfffu));
    const Word product = u(sine) * half_at(m_, self + 0xe4);
    const Word rounded = product + (static_cast<Word>(s(product) >> 31) & 0xfffu);
    set_half_at(m_, self + 0xe0, half_at(m_, self + 0xe0) + half_at(m_, self + 0xe6));
    set_word_at(m_, self + 0xc, word_at(m_, self + 0xc) + (static_cast<Word>(s(rounded) >> 12) << 12));
}

// 0x0046e4f0: the droplet's per-frame method in its vtable: the assertion macro's line (0x9f) and file name, an assertion that bit 2 of the flag byte at +0x6a is clear, then the update
void WorldKit::droplet_checked_update(Word self) {
    set_global_word(m_, 0x00568638, 0x9f);   // (the assertion macro's line and file name)
    set_string(0x0056881c, "H:\\TonyHawk\\Pc2\\medals.cpp");
    check((~(byte_at(m_, self + 0x6a) >> 2) & 1) != 0, 0x005348f4);
    droplet_update(self);
}

}  // namespace opentony::runtime::worldkit

namespace opentony::runtime::worldkit {

namespace {

constexpr std::uint32_t kPanelConstruct = 0x0048d990;      // thiscall on the panel (+0x168), one argument: the skater
constexpr std::uint32_t kPanelMemberConstruct = 0x00431190;     // thiscall on the member at +0x2c14
constexpr std::uint32_t kListPushHead = 0x004801d0;

// the dwords the constructor clears (the skater's counters, timers and trick state)
constexpr std::uint16_t kClearedDwords[] = {
    0x2da8, 0x2dac, 0x2db0, 0x2dbc, 0x2dc0, 0x2dc4, 0x2e00, 0x2e04, 0x2e08, 0x2e0c, 0x2e10, 0x2e14, 0x2e18, 0x2e1c, 0x2e20, 0x2ea8, 0x2eac, 0x2eb0, 0x2eb4, 0x2eb8,
    0x2ebc, 0x2ecc, 0x2ed0, 0x2ed4, 0x2ee0, 0x2ee4, 0x2ee8, 0x2f04, 0x2f08, 0x2f0c, 0x30e8, 0x30ec, 0x30f0, 0x30f4, 0x30f8, 0x30fc, 0x3100, 0x3104, 0x3108, 0x310c,
    0x3110, 0x3114, 0x3118, 0x311c, 0x3120, 0x3150, 0x3154, 0x3158, 0x315c, 0x3160, 0x3164};
constexpr std::uint16_t kClearedWords[] = {0x3168, 0x316a, 0x316c, 0x316e, 0x3170, 0x3172};
// cleared after the name registry (ahead of the first list add), and the second batch
constexpr std::uint16_t kClearedAfterOverrides[] = {0x2e90, 0x2e94, 0x2ec4, 0x2ec0, 0x30a4, 0x30b8, 0x30c0, 0x2dd8, 0x2ddc, 0x2de0, 0x2c68, 0x2df0, 0x3070};
constexpr std::uint16_t kClearedAfterRegistry[] = {0x2db8, 0x2db4, 0x3118, 0x311c, 0x3120, 0x3124, 0x2e84, 0x3144, 0x3148, 0x2f2c};

}  // namespace

// 0x0046c720: a skater object for the character in slot 0x0056a898: the panel, the light colour, the counters, the overrides of the game mode, the registry of names, the orientation, the
// place in the list of players (at most two), the pad profile of its number, and, in a one-player game without a second view, the two small buffers of the light fade
std::uint32_t WorldKit::skater_construct(std::uint32_t self) {
    animated_construct(self);
    thiscall(kPanelConstruct, self + 0x168, {self});
    thiscall(kPanelMemberConstruct, self + 0x2c14);
    for (const auto offset : kClearedDwords) {
        set_word_at(m_, self + offset, 0);
    }
    for (const auto offset : kClearedWords) {
        set_half_at(m_, self + offset, 0);
    }
    skater_view(m_, self).set_field_0(0x00518dd8);
    for (const std::uint32_t offset : {0x308cu, 0x3090u, 0x3094u, 0x3098u}) {
        set_word_at(m_, self + offset, 0xffffffff);
    }
    skater_view(m_, self).set_field_29f4(0x64);
    skater_view(m_, self).set_field_108(0x10000);
    const std::uint32_t slot = global_word(m_, 0x0056a898);
    const std::uint32_t mode = global_word(m_, 0x00533f38);
    if (byte_at(m_, 0x0053c240 + 4 * (slot * 107)) != 0 && mode == 1) {
        skater_view(m_, self).set_field_2cc8(0xe2e);
    } else if (mode == 7) {
        skater_view(m_, self).set_field_2cc8(0x276);
    } else {
        skater_view(m_, self).set_field_2cc8(0x1c3e);
    }
    if (mode == 5 || mode == 8 || mode == 9) {
        skater_view(m_, self).set_field_2cc8(static_cast<Word>(global_word(m_, 0x005685fc)));
    }
    set_byte_at(m_, self + 5, byte_at(m_, self + 5) | 2);
    const std::uint32_t scale = global_word(m_, 0x0056a890) == 0 ? 0x1000u : 0xb54u;
    skater_view(m_, self).set_field_28(static_cast<std::uint16_t>(scale));
    skater_view(m_, self).set_field_2a(static_cast<std::uint16_t>(scale));
    skater_view(m_, self).set_field_2c(static_cast<std::uint16_t>(scale));
    skater_view(m_, self).set_field_2dc8(0x2e9b6);
    skater_view(m_, self).set_field_d8(static_cast<Word>(skater_view(m_, self).field_d8() | 1u));
    for (const auto offset : kClearedAfterOverrides) {
        set_word_at(m_, self + offset, 0);
    }
    skater_view(m_, self).set_field_2ec8(0xffffffff);
    name_reset(self);
    for (const auto offset : kClearedAfterRegistry) {
        set_word_at(m_, self + offset, 0);
    }
    set_half_at(m_, self + 0x3128, 0);
    set_half_at(m_, self + 0x312a, 0xf000);
    set_half_at(m_, self + 0x312c, 0);
    reset_orientation_rows(self);
    thiscall(kListPushHead, self, {0x0056a960});
    const std::uint32_t players = global_word(m_, 0x0056a95c) + 1;
    set_global_word(m_, 0x0056a95c, players);
    check(s(players) < 3, 0x005347d8);
    skater_view(m_, self).set_field_6a(static_cast<std::uint16_t>(skater_view(m_, self).field_6a() & 0xfffdu));
    skater_view(m_, self).set_action_record_bank(GuestPtr<void>{static_cast<Word>(global_word(m_, 0x0056a95c) == 1 ? 0x0056aff8u : 0x0056b164u)});
    skater_view(m_, self).set_field_7c(5);
    skater_view(m_, self).set_field_7d(1);
    skater_view(m_, self).set_field_7e(5);
    skater_view(m_, self).set_field_31e0(0x0053db10);
    if (global_byte(m_, 0x005614c4) == 0) {
        skater_view(m_, self).set_field_31e0(static_cast<Word>(0x0053db80 + 4 * (13 * slot)));
    }
    for (std::uint32_t k = 0; k < 13; ++k) {
        set_word_at(m_, self + 0x31ac + 4 * k, word_at(m_, skater_view(m_, self).field_31e0() + 4 * k));
    }
    skater_view(m_, self).set_field_40(static_cast<Word>(self + 0x31ac));
    if (global_word(m_, 0x00533f3c) == 1 && global_word(m_, 0x0056a86c) == 0) {
        skater_view(m_, self).set_draw_in_front(1);
        skater_view(m_, self).set_field_e0(static_cast<Word>(alloc(0x20, 0, 1, 0)));
        skater_view(m_, self).set_field_e4(static_cast<Word>(alloc(0x10, 0, 1, 0)));
        const std::uint32_t first = skater_view(m_, self).field_e0();
        set_half_at(m_, first, 0);
        set_half_at(m_, first + 2, 0xf000);
        set_half_at(m_, first + 4, 0);
        const std::uint32_t second = skater_view(m_, self).field_e4();
        set_byte_at(m_, second, 0x80);
        set_byte_at(m_, second + 1, 0x80);
        set_byte_at(m_, second + 2, 0x80);
    } else {
        want_sprite(self);
        sprite_update(self);
    }
    const std::uint32_t camera = global_word(m_, 0x0055fa38);
    if (camera != 0) {
        set_word_at(m_, camera + 0x504, 1);
    }
    return self;
}

}  // namespace opentony::runtime::worldkit
