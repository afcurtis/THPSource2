#include "bit_views.hpp"

#include <algorithm>

// Phase 4.15d: the skater's dust (dust.cpp, 0x00431260..0x00432180) and the dynamic logos (0x004321d0..0x00432330). The dust object keeps up to twelve flat smoke bits; the terrain the skater is on
// decides their size, tint and fade (one table for each kind of dust: landing, bailing, braking).

namespace opentony::runtime::bitkit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kHookPosition = 0x00464e90;         // M3D_GetHookPosition(out, object, offset)
constexpr Word kRegionTable = 0x0056d460;          // per PSX region (0x44 bytes each): a short, the bound of a random offset
constexpr Word kSmokeName = 0x0052808c;

namespace dust {
constexpr Word kCount = 0x34;                      // dword: the live clouds
constexpr Word kType = 0x38;                       // dword: 0 landing, 1 bailing, 2 braking
constexpr Word kCloudsLeft = 0x3c;                 // dword: clouds still to make
}  // namespace dust

}  // namespace

// 0x00431260 Dust::Reset(terrain, type): the number of clouds this terrain gives
void BitKit::dust_reset(Word self, Word terrain, Word type) {
    check(type == 0 || type == 1 || type == 2, 0x00528058);
    constexpr Word kScale = kScratch + 0x180;
    constexpr Word kTint = kScratch + 0x184;
    constexpr Word kFade = kScratch + 0x188;
    constexpr Word kScaleDecay = kScratch + 0x18c;
    constexpr Word kOffset = kScratch + 0x190;
    set_word_at(m_, self + dust::kCloudsLeft, 0);
    set_word_at(m_, self + dust::kType, type);
    set_global_word(m_, kScale, 0);
    set_global_byte(m_, kTint, 0x40);
    set_global_word(m_, kFade, 1);
    set_global_word(m_, kScaleDecay, 8);
    set_global_word(m_, kOffset, 0x10);
    dust_terrain_parameters(self, terrain, kScale, kTint, kFade, kScaleDecay, self + dust::kCloudsLeft, kOffset);
}

// 0x00431320 Dust::Update(skater): the clouds drift with the skater and fade; while clouds are left, a new one is made at the skater; when none are left and all have faded the clouds are deleted
void BitKit::dust_update(Word self, Word skater) {
    check(skater != 0, 0x00524a18);
    for (std::int32_t k = 0; k < s(word_at(m_, self + dust::kCount)); ++k) {
        const Word cloud = word_at(m_, self + 4 + 4 * u(k));
        check(cloud != 0, 0x00528094);
        if (short_at(m_, cloud + bit::kScale) < 0x200) {
            set_half_at(m_, cloud + bit::kScale, half_at(m_, cloud + bit::kScaleDecay) + half_at(m_, cloud + bit::kScale));
        }
        fade(cloud, 0);
        const unsigned shift = word_at(m_, self + dust::kType) == 1 ? 0 : 1;
        set_word_at(m_, cloud + bit::kPosition, word_at(m_, cloud + bit::kPosition) + u(int_at(m_, skater + 0x4c) >> shift));
        set_word_at(m_, cloud + bit::kPosition + 8, word_at(m_, cloud + bit::kPosition + 8) + u(int_at(m_, skater + 0x54) >> shift));
        set_word_at(m_, cloud + bit::kPosition + 4, word_at(m_, cloud + bit::kPosition + 4) - (u(rnd(3)) << 12));
    }
    constexpr Word kScale = kScratch + 0x180;
    constexpr Word kTint = kScratch + 0x184;
    constexpr Word kFade = kScratch + 0x188;
    constexpr Word kScaleDecay = kScratch + 0x18c;
    constexpr Word kOffset = kScratch + 0x190;
    constexpr Word kClouds = kScratch + 0x194;
    set_global_word(m_, kScale, 0);
    set_global_byte(m_, kTint, 0x40);
    set_global_word(m_, kFade, 1);
    set_global_word(m_, kScaleDecay, 8);
    set_global_word(m_, kOffset, 0x10);
    if (int_at(m_, self + dust::kCloudsLeft) <= 0) {
        const std::int32_t count = s(word_at(m_, self + dust::kCount));
        if (count > 0) {
            bool all_faded = true;
            for (std::int32_t k = 0; k < count; ++k) {
                if (word_at(m_, word_at(m_, self + 4 + 4 * u(k)) + bit::kColour) != 0) {
                    all_faded = false;
                }
            }
            if (!all_faded) {
                return;
            }
        }
        for (std::int32_t k = 0; k < count; ++k) {
            const Word cloud = word_at(m_, self + 4 + 4 * u(k));
            if (cloud != 0) {
                virtual_call(cloud, 0, {1});
            }
            set_word_at(m_, self + 4 + 4 * u(k), 0);
        }
        set_word_at(m_, self + dust::kCount, 0);
        return;
    }
    set_word_at(m_, self + dust::kCloudsLeft, word_at(m_, self + dust::kCloudsLeft) - 1);
    dust_terrain_parameters(self, word_at(m_, skater + 0x30b0), kScale, kTint, kFade, kScaleDecay, kClouds, kOffset);
    const Word block = bit_new(100);
    const Word cloud = block != 0 ? flat_construct(block) : 0;
    const Word position = cloud + bit::kPosition;
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, position + 4 * k, word_at(m_, skater + 8 + 4 * k));
    }
    std::int32_t scale = global_int(m_, kScale);
    std::int32_t scale_decay = global_int(m_, kScaleDecay);
    if (word_at(m_, self + dust::kType) == 1) {
        constexpr Word kVector = kScratch + 0x1a0;
        set_global_word(m_, kVector, 0);
        set_global_word(m_, kVector + 4, 0);
        set_global_word(m_, kVector + 8, 0);
        random_offset(kVector, 0x20, 0);
        const Word region = byte_at(m_, skater + 0x1f);
        const std::int32_t pad = rnd(half_at(m_, kRegionTable + region * 0x44));
        constexpr Word kHook = kScratch;
        set_global_half(m_, kHook, global_half(m_, kVector));
        set_global_half(m_, kHook + 2, 0);
        set_global_half(m_, kHook + 4, global_half(m_, kVector + 8));
        set_global_half(m_, kHook + 6, u(pad));
        std::string local;
        for (Word k = 0; k < 8; ++k) {
            local.push_back(static_cast<char>(byte_at(m_, kHook + k)));
        }
        engine(kHookPosition, {position, skater, kHook}, local);
        scale *= 2;
        scale_decay *= 2;
    } else {
        random_offset(position, global_word(m_, kOffset), 0xc);
    }
    set_anim_by_name(cloud, kSmokeName);
    set_semi_transparent(cloud);
    choose_random_frame(cloud);
    set_half_at(m_, cloud + bit::kAnimSpeed, 0x55);
    set_half_at(m_, cloud + bit::kScale, u(scale));
    const Word tint = global_byte(m_, kTint);
    set_tint(cloud, tint, tint, tint);
    set_half_at(m_, cloud + bit::kTransDecay, global_half(m_, kFade));
    set_half_at(m_, cloud + bit::kScaleDecay, u(scale_decay));
    dust_insert(self, cloud);
}

// 0x00431650 Dust::insert_dust(cloud): into the next free place, or over the dimmest cloud when all twelve are used
void BitKit::dust_insert(Word self, Word cloud) {
    check(cloud != 0, 0x005280ac);
    const Word count = word_at(m_, self + dust::kCount);
    if (count > 11) {
        std::int32_t lowest = -1;
        Word brightness = 0xffff;
        for (Word k = 0; k < count; ++k) {
            const Word colour = word_at(m_, word_at(m_, self + 4 + 4 * k) + bit::kColour);
            const Word sum = ((colour >> 16) & 0xff) + ((colour >> 8) & 0xff) + (colour & 0xff);
            if (sum < brightness) {
                brightness = sum;
                lowest = static_cast<std::int32_t>(k);
            }
        }
        check(lowest != -1, 0x005280a0);
        const Word old = word_at(m_, self + 4 + 4 * u(lowest));
        if (old != 0) {
            virtual_call(old, 0, {1});
        }
        set_word_at(m_, self + 4 + 4 * u(lowest), cloud);
        return;
    }
    check(word_at(m_, self + 4 + 4 * count) == 0, 0x00526874);
    set_word_at(m_, self + 4 + 4 * count, cloud);
    set_word_at(m_, self + dust::kCount, count + 1);
}

// 0x004317c0 Dust::get_terrain_parameters(terrain, scale*, tint*, fade*, scale decay*, clouds*, offset*): the table of the dust's kind
void BitKit::dust_terrain_parameters(Word self, Word terrain, Word scale, Word tint, Word fade_rate, Word scale_decay, Word clouds, Word offset) {
    switch (int_at(m_, self + dust::kType)) {
    case 0:
        dust_land_parameters(self, terrain, scale, tint, fade_rate, scale_decay, clouds, offset);
        return;
    case 1:
        dust_bail_parameters(self, terrain, scale, tint, fade_rate, scale_decay, clouds, offset);
        return;
    case 2:
        dust_brake_parameters(self, terrain, scale, tint, fade_rate, scale_decay, clouds, offset);
        return;
    default:
        check(false, 0x005280c4);
        return;
    }
}

namespace {
struct Parameters {
    Word scale, tint, fade, scale_decay, clouds, offset;
};
}  // namespace

static void store_parameters(RetailMemory& m, Word scale, Word tint, Word fade_rate, Word scale_decay, Word clouds, Word offset, const Parameters& p) {
    guest::write_scalar<std::uint16_t>(m, scale, static_cast<std::uint16_t>(p.scale));
    guest::write_scalar<std::uint8_t>(m, tint, static_cast<std::uint8_t>(p.tint));
    guest::write_scalar<std::uint16_t>(m, fade_rate, static_cast<std::uint16_t>(p.fade));
    guest::write_scalar<std::uint16_t>(m, scale_decay, static_cast<std::uint16_t>(p.scale_decay));
    guest::write_scalar<std::uint32_t>(m, clouds, static_cast<std::uint32_t>(p.clouds));
    guest::write_scalar<std::uint32_t>(m, offset, static_cast<std::uint32_t>(p.offset));
}

void BitKit::check_dust_pointers(Word scale, Word tint, Word fade_rate, Word scale_decay, Word clouds, Word offset) {
    check(scale != 0, 0x005281d0);
    check(tint != 0, 0x005281b4);
    check(fade_rate != 0, 0x00528194);
    check(scale_decay != 0, 0x00528170);
    check(clouds != 0, 0x00528150);
    check(offset != 0, 0x0052812c);
}

// 0x004318a0: landing dust (grass-like terrain 3 and 0xe: a little; 6: a lot)
void BitKit::dust_land_parameters(Word self, Word terrain, Word scale, Word tint, Word fade_rate, Word scale_decay, Word clouds, Word offset) {
    check_dust_pointers(scale, tint, fade_rate, scale_decay, clouds, offset);
    check(int_at(m_, self + dust::kType) == 0, 0x005280ec);
    if (terrain != 3 && terrain != 0xe) {
        if (terrain == 6) {
            store_parameters(m_, scale, tint, fade_rate, scale_decay, clouds, offset, {0x400, 0x40, 8, 0x40, 0xc, 0x20});
        } else {
            store_parameters(m_, scale, tint, fade_rate, scale_decay, clouds, offset, {0, 0, 0, 0, 0, 0});
        }
    } else {
        store_parameters(m_, scale, tint, fade_rate, scale_decay, clouds, offset, {0x100, 0x40, 1, 8, 8, 0x10});
    }
    check(int_at(m_, clouds) < 0xd, 0x005280dc);
}

// 0x00431b80: bailing dust
void BitKit::dust_bail_parameters(Word self, Word terrain, Word scale, Word tint, Word fade_rate, Word scale_decay, Word clouds, Word offset) {
    check_dust_pointers(scale, tint, fade_rate, scale_decay, clouds, offset);
    check(int_at(m_, self + dust::kType) == 1, 0x005281ec);
    switch (terrain) {
    case 3:
    case 0xe:
        store_parameters(m_, scale, tint, fade_rate, scale_decay, clouds, offset, {0x80, 0x40, 2, 8, 10, 4});
        break;
    case 5:
    case 8:
    case 0xd:
        store_parameters(m_, scale, tint, fade_rate, scale_decay, clouds, offset, {0x40, 0x40, 2, 8, 10, 0});
        break;
    default:
        store_parameters(m_, scale, tint, fade_rate, scale_decay, clouds, offset, {0, 0, 0, 0, 0, 0});
        break;
    }
    check(int_at(m_, clouds) < 0xd, 0x005280dc);
}

// 0x00431e80: braking dust
void BitKit::dust_brake_parameters(Word self, Word terrain, Word scale, Word tint, Word fade_rate, Word scale_decay, Word clouds, Word offset) {
    check_dust_pointers(scale, tint, fade_rate, scale_decay, clouds, offset);
    check(int_at(m_, self + dust::kType) == 2, 0x0052822c);
    switch (terrain) {
    case 3:
    case 0xe:
        store_parameters(m_, scale, tint, fade_rate, scale_decay, clouds, offset, {0x40, 0x40, 2, 8, 10, 0xc});
        break;
    case 8:
    case 0xd:
        store_parameters(m_, scale, tint, fade_rate, scale_decay, clouds, offset, {0x40, 0x40, 2, 8, 6, 6});
        break;
    default:
        store_parameters(m_, scale, tint, fade_rate, scale_decay, clouds, offset, {0, 0, 0, 0, 0, 0});
        break;
    }
    check(int_at(m_, clouds) < 0xd, 0x005280dc);
}

// 0x00432180 apply_random_offset(vector, range, shift): x and z moved by a random amount about zero
void BitKit::random_offset(Word vector, std::int32_t range, Word shift) {
    range *= 4;
    const std::int32_t first = rnd(u(range));
    set_word_at(m_, vector, word_at(m_, vector) + (u(first - (range >> 1)) << (shift & 0x1f)));
    const std::int32_t second = rnd(u(range));
    set_word_at(m_, vector + 8, word_at(m_, vector + 8) + (u(second - (range >> 1)) << (shift & 0x1f)));
}

// 0x004321d0: does the file name (before its '.') end with the digit that follows `index` ('1' + index)
bool BitKit::logo_name_matches(Word name, std::int32_t index) {
    Word p = name;
    char c = static_cast<char>(byte_at(m_, p));
    while (c != '\0' && c != '.') {
        ++p;
        c = static_cast<char>(byte_at(m_, p));
    }
    check(static_cast<char>(byte_at(m_, p)) == '.', 0x005282c4);
    return static_cast<std::int32_t>(static_cast<std::int8_t>(byte_at(m_, p - 1))) == index + 0x31;
}

// 0x00432250: the next entry of the ring of names (`list` .. a null, then `first`) whose name carries the logo's digit; none when the list is null
Word BitKit::logo_next(Word list, Word first, std::int32_t index) {
    if (list == 0) {
        return 0;
    }
    check(first != 0, 0x00528338);
    check(!(index < 0 || 3 < index), 0x0052830c);
    Word entry = list;
    do {
        const Word next = entry + 4;
        entry = next;
        if (word_at(m_, next) == 0) {
            entry = first;
        }
        if (logo_name_matches(word_at(m_, entry), index)) {
            return entry;
        }
    } while (entry != list);
    return 0;
}


// 0x00432330: the placeholder logos of the level are replaced by bitmaps picked from the skaters' lists of names: for each of the four widths (32, 64, 96, 128) the next name of the lists with that width's
// digit, a random number of steps into the ring; each scene material of the level that is such a placeholder gets the next bitmap of its width (found in the "logos" folder)
void BitKit::logo_load_all() {
    constexpr Word kMaterials = 0x00539e1c;
    constexpr Word kNameTable = 0x0053a524;
    constexpr Word kMerged = kScratch + 0x600;
    constexpr Word kSubDirLocal = kScratch + 0x300;                // (the stack local the retail code passes: the probe maps it here)
    constexpr Word kLogosFolder = 0x0052835c;
    constexpr Word kGetSubDir = 0x004496f0;
    constexpr Word kSetSubDir = 0x00449700;
    constexpr Word kLoadBitmap = 0x004674d0;
    constexpr Word kFindMaterial = 0x004b2030;
    constexpr Word kSelectSlot = 0x004b18a0;
    Word list;
    if (global_word(m_, 0x00533f3c) == 1) {
        list = word_at(m_, kNameTable + engine_value(kSelectSlot, {0}) * 0x170);
    } else {
        Word out = kMerged;
        const Word slot0 = engine_value(kSelectSlot, {0});
        Word first = word_at(m_, kNameTable + slot0 * 0x170);
        const Word slot1 = engine_value(kSelectSlot, {1});
        Word second = word_at(m_, kNameTable + slot1 * 0x170);
        std::int32_t count = 0;
        Word a = word_at(m_, first);
        // (the first list's current entry, then the second's, alternately, until both end or 0x62 are taken)
        for (;;) {
            a = word_at(m_, first);
            if (!(a != 0 || word_at(m_, second) != 0) || count >= 0x62) {
                break;
            }
            if (a != 0) {
                set_word_at(m_, out, a);
                out += 4;
                first += 4;
                ++count;
            }
            const Word b = word_at(m_, second);
            if (b != 0) {
                set_word_at(m_, out, b);
                out += 4;
                second += 4;
                ++count;
            }
        }
        set_word_at(m_, out, 0);
        list = kMerged;
    }
    Word anchor[4];
    Word current[4];
    for (std::int32_t type = 0; type < 4; ++type) {
        current[type] = 0;
        anchor[type] = logo_next(list, list, type);
        current[type] = anchor[type];
        s_.note("rand", {10});
        std::int32_t steps = s_.value("rand");
        while (steps > 0) {
            current[type] = logo_next(current[type], anchor[type], type);
            --steps;
        }
    }
    for (Word entry = kMaterials; word_at(m_, entry) != 0; entry += 4) {
        const Word checksum = word_at(m_, entry);
        const Word material = engine_value(kFindMaterial, {checksum});
        if (material == 0) {
            continue;
        }
        const auto minimum = [&](std::initializer_list<Word> offsets) {
            Word best = 0xff;
            for (const Word offset : offsets) {
                best = std::min<Word>(best, byte_at(m_, material + offset));
            }
            return best;
        };
        const auto maximum = [&](std::initializer_list<Word> offsets) {
            Word best = 0;
            for (const Word offset : offsets) {
                best = std::max<Word>(best, byte_at(m_, material + offset));
            }
            return best;
        };
        const Word width = maximum({0, 4, 8, 0xa}) - minimum({0, 4, 8, 0xa}) + 1;
        Word type = 0;
        switch (width) {
        case 0x20: type = 0; break;
        case 0x40: type = 1; break;
        case 0x60: type = 2; break;
        case 0x80: type = 3; break;
        default:
            s_.note("assert", {0x00528364});
            break;
        }
        const Word found = logo_next(current[type], anchor[type], s(type));
        if (found == 0) {
            continue;
        }
        current[type] = found;
        const Word folder = engine_value(kGetSubDir);
        const std::string saved = m_.cstring(folder, 256);
        engine(kSetSubDir, {kLogosFolder}, m_.cstring(kLogosFolder, 256));
        engine(kLoadBitmap, {checksum, word_at(m_, found)}, {});
        // retail copied the old sub directory into its stack local before this call: a live run must put the text there too (the scratch bytes at kSubDirLocal are another
        // Kit's query record, zero only in the first level of a process, so the second level's sub directory became "\x01" and every later file name was wrong)
        for (std::size_t i = 0; i <= saved.size(); ++i) {
            set_byte_at(m_, kSubDirLocal + static_cast<Word>(i), i < saved.size() ? static_cast<std::uint8_t>(saved[i]) : 0);
        }
        engine(kSetSubDir, {kSubDirLocal}, saved);
    }
}

}  // namespace opentony::runtime::bitkit
