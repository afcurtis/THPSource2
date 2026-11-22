#include "auto_views.hpp"
#include "psx_spool.hpp"

#include <array>
#include <string_view>
#include <vector>

namespace opentony::runtime::psx {

using namespace opentony::runtime::autoviews;

namespace {

constexpr const char* kAlloc = "alloc";
constexpr const char* kPaletteClear = "palette_clear";
constexpr const char* kFileOpenLoad = "file_open_load";          // FileIO_OpenLoad(name, 1): the loaded image (the name is the call's text)
constexpr const char* kPartsManifest = "parts_manifest";          // PlayerSpool_ParsePartsManifest(image, name, a, b, c, d)
constexpr const char* kTrickScriptInit = "trickscript_init";      // TrickScript_Initialize()
constexpr const char* kPaletteEntries = "palette_entries";        // D3DTex_CountUsedPaletteEntries(texture)
constexpr const char* kModelRoll = "model_roll";                  // FUN_0048fbe0(0x10): Skater_FindPartIndex on the skater (which model of a region it is given)

}  // namespace

// 0x00448dd0 FileIO_OpenLoad(name, 1). The probe's stub kept the heap and took the name as the call's text; live the function wants the name in memory, as the
// retail caller's sprintf buffer holds it.
std::uint32_t PsxSpool::open_load_text(const std::string& text) {
    if (s_.live()) {
        constexpr std::uint32_t kName = 0x7ff08800;   // pc_platform's scratch window, past every Kit's area (TextureKit's is 0x7ff06000)
        m_.write(kName, std::vector<std::uint8_t>(text.begin(), text.end()));
        set_byte_at(m_, kName + static_cast<std::uint32_t>(text.size()), 0);
        s_.note("engine_00448dd0", {kName, 1}, text);
        return static_cast<std::uint32_t>(s_.value("engine_00448dd0"));
    }
    s_.note(kFileOpenLoad, {1}, text);
    return static_cast<std::uint32_t>(s_.value(kFileOpenLoad));
}

void PsxSpool::strip_anim(std::int32_t region) {
    const std::uint32_t slot = slot_addr(region);
    const std::uint32_t raw = word_at(m_, slot + kSlotRaw);
    const std::uint32_t hierarchy = word_at(m_, slot + kSlotAnimHierarchy);
    std::uint32_t out = raw + word_at(m_, raw + 4);
    s_.check(out < hierarchy, kMsgAnimAfterHierarchy);
    std::uint32_t from = trailing_tables(raw);
    s_.check(word_at(m_, from - 4) == 0xffffffff, kMsgNoPacketTerminator);
    s_.check(from == word_at(m_, slot + kSlotChecksums), kMsgChecksumsNotAfterPacket);
    if (const std::uint32_t rgb = word_at(m_, slot + kSlotRgb); rgb != 0) {
        const std::int32_t colours = static_cast<std::int32_t>(word_at(m_, rgb - 4) >> 2);
        s_.check(colours > 0 && colours < 0x101, kMsgBadNumColors);
        set_word_at(m_, out, word_at(m_, rgb - 8));
        set_word_at(m_, out + 4, word_at(m_, rgb - 4));
        set_word_at(m_, slot + kSlotRgb, out + 8);
        out += 8;
        std::uint32_t at = rgb;
        for (std::int32_t k = 0; k < colours; ++k, at += 4, out += 4) {
            set_word_at(m_, out, word_at(m_, at));
        }
    }
    set_word_at(m_, out, 0xffffffff);
    out += 4;
    set_word_at(m_, slot + kSlotChecksums, out);
    for (std::int32_t k = half_at(m_, slot + kSlotModelCount); k > 0; --k, from += 4, out += 4) {
        set_word_at(m_, out, word_at(m_, from));
    }
    std::int32_t count = int_at(m_, from);
    from += 4;
    set_word_at(m_, out, static_cast<std::uint32_t>(count));
    out += 4;
    for (; count > 0; --count, from += 4, out += 4) {
        set_word_at(m_, out, word_at(m_, from));
    }
    s_.check(byte_at(m_, slot + kSlotTexturesShrunk) == 0, kMsgShrinkLoadedTextures);
}

void PsxSpool::build_animation_tables(std::int32_t region, std::uint32_t list) {
    s_.check(list != 0, kMsgNoDefaultAnimations);
    const std::uint32_t slot = slot_addr(region);
    if (byte_at(m_, slot + kSlotState) == 2) {
        return;
    }
    decrement_material_usage(region);
    free_unused_materials();
    s_.note(kPaletteClear);
    const std::uint32_t source = word_at(m_, slot + kSlotAnimTables);
    const std::uint32_t raw = word_at(m_, slot + kSlotRaw);
    const std::uint32_t hierarchy = word_at(m_, slot + kSlotAnimHierarchy);
    const std::int32_t hierarchy_bytes = int_at(m_, hierarchy - 4);
    set_word_at(m_, raw + 0xc, word_at(m_, source - 8));
    set_word_at(m_, raw + 0x10, word_at(m_, source - 4));
    set_word_at(m_, slot + kSlotAnimTables, raw + 0x14);
    const std::int32_t animations = int_at(m_, source);
    set_word_at(m_, raw + 0x14, static_cast<std::uint32_t>(animations));
    const std::uint32_t pairs_from = source + 4;
    const std::uint32_t pairs_to = raw + 0x18;
    std::uint32_t data_from = pairs_from + static_cast<std::uint32_t>(animations) * 8;
    std::uint32_t data_to = pairs_to + static_cast<std::uint32_t>(animations) * 8;
    for (std::int32_t k = 0; k < animations; ++k) {
        set_word_at(m_, pairs_to + 8 * static_cast<std::uint32_t>(k), word_at(m_, pairs_from + 8 * static_cast<std::uint32_t>(k)));
        set_word_at(m_, pairs_to + 8 * static_cast<std::uint32_t>(k) + 4, word_at(m_, pairs_from + 8 * static_cast<std::uint32_t>(k) + 4));
    }
    std::array<std::uint8_t, 0x200> wanted{};
    auto mark = [&](std::uint32_t message) {
        for (std::int16_t number = short_at(m_, list); number != -1; number = short_at(m_, list)) {
            s_.check(number < 0x200, message);
            list += 2;
            if (number >= 0 && number < 0x200) {
                wanted[static_cast<std::size_t>(number)] = 1;
            }
        }
    };
    mark(kMsgBadAnimNumber);
    list += 2;
    mark(kMsgBadExtraAnimNumber);
    for (std::int32_t k = 0; k < animations; ++k) {
        const std::uint32_t pair = pairs_to + 8 * static_cast<std::uint32_t>(k);
        s_.check(data_from == word_at(m_, pair) - 4 + pairs_from, kMsgSourceBytes);
        std::uint32_t end = hierarchy;
        if (k != animations - 1) {
            end = word_at(m_, pair + 8) - 4 + pairs_from;
        }
        if (wanted[static_cast<std::size_t>(k)] == 0) {
            set_word_at(m_, pair, 0);
            set_word_at(m_, pair + 4, 0);
            data_from = end;
        } else {
            set_word_at(m_, pair, data_to + 4 - pairs_to);
            for (; data_from < end; ++data_from, ++data_to) {
                set_byte_at(m_, data_to, byte_at(m_, data_from));
            }
        }
    }
    std::uint32_t at = (data_to + 3) & ~3u;
    set_word_at(m_, slot + kSlotAnimHierarchy, at);
    std::int32_t words = (hierarchy_bytes + ((hierarchy_bytes >> 31) & 3)) >> 2;
    for (std::uint32_t from = hierarchy; words > 0; --words, from += 4, at += 4) {
        set_word_at(m_, at, word_at(m_, from));
    }
}

std::int32_t PsxSpool::load_model_file(std::uint32_t name, std::uint32_t kind) {
    s_.check(name != 0, kMsgNoModelFilePointer);
    s_.check(byte_at(m_, name) != 0, kMsgNoModelFile);
    std::int32_t index = find_region_by_name(name);
    if (index != -1) {
        return index;
    }
    const std::uint32_t image = open_load_text(m_.cstring(name) + ".psx");
    s_.check(image != 0, kMsgModelFileMissing);
    relocate_definition_pointers(image, player_model_slot(kind, 0));
    index = init_free_slot();
    const std::uint32_t slot = slot_addr(index);
    set_word_at(m_, slot + kSlotRaw, image);
    parse_environment(static_cast<std::uint32_t>(index));
    const std::string text = m_.cstring(name);
    m_.write(slot + kSlotName, std::vector<std::uint8_t>(text.begin(), text.end()));
    set_byte_at(m_, slot + kSlotName + static_cast<std::uint32_t>(text.size()), 0);
    set_byte_at(m_, slot + kSlotName, kind + '0');
    s_.check(word_at(m_, slot + kSlotHooks) == 0, kMsgModelHasHooks);
    s_.check(word_at(m_, slot + kSlotWib) == 0, kMsgModelHasWib);
    s_.check(word_at(m_, slot + kSlotPulse) == 0, kMsgModelHasPulse);
    strip_anim(index);
    set_word_at(m_, slot + kSlotAnimHierarchy, 0);
    set_word_at(m_, slot + kSlotAnimTables, 0);
    return index;
}

std::int32_t PsxSpool::load_animation_resource(std::uint32_t name, std::int32_t skater) {
    std::int32_t index = find_region_by_name(name);
    if (index == -1) {
        index = init_free_slot();
        const std::uint32_t slot = slot_addr(index);
        const std::string text = m_.cstring(name);
        m_.write(slot + kSlotName, std::vector<std::uint8_t>(text.begin(), text.end()));      // sprintf(slot name, "%s", name)
        set_byte_at(m_, slot + kSlotName + static_cast<std::uint32_t>(text.size()), 0);
        s_.check(word_at(m_, slot + kSlotRaw) == 0, kMsgPossibleLeak);
        set_word_at(m_, slot + kSlotRaw, open_load_text(text + ".psx"));
        parse_environment(static_cast<std::uint32_t>(index));
    }
    std::uint32_t list = kDefaultAnimations;
    if (skater != 0) {
        if (global_short(m_, kReplaySkaters) == 0) {
            s_.note(kTrickScriptInit);
        }
        list = kReplayAnimations;
    }
    build_animation_tables(index, list);
    set_byte_at(m_, slot_addr(index) + kSlotState, 2);
    return index;
}

std::uint32_t PsxSpool::load_part_manifest(std::uint32_t name, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
    s_.check(name != 0, kMsgNoFilenamePointer);
    s_.check(byte_at(m_, name) != 0, kMsgNoFilename);
    const std::string text = m_.cstring(name);
    s_.check(text.size() < 9, kMsgFilenameTooLong);
    s_.check(a != 0, kMsgNoSpaceForNames);
    s_.check(c != 0, kMsgNoSpaceForPartCount);
    const std::uint32_t image = open_load_text(text + ".psh");
    s_.note(kPartsManifest, {image, name, a, b, c, d});
    return image;
}

// 0x004b4940: the loaded `.psh` text is lower-cased in place and cut up where it stands. For every line that holds `<name>part_<model>_<part> <index>` the part's name (the
// text after the model's underscore, ended at its blank) is `names[count]`, the text from after `part_` is `starts[count]` when that array is given, and the underscore, the
// blank and the line's end are the text's own NUL / newline. Answers the image (the caller frees it).
std::uint32_t PsxSpool::parse_parts_manifest(std::uint32_t image, std::uint32_t name, std::uint32_t names, std::uint32_t starts, std::uint32_t count, std::int32_t limit) {
    std::string token = m_.cstring(name) + "part_";
    for (char& c : token) {
        c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + ' ' : c);
    }
    s_.note("engine_0046f510", {image});   // Mem_GetBlockSize
    const std::uint32_t size = static_cast<std::uint32_t>(s_.value("engine_0046f510"));
    std::vector<std::uint8_t> text = size != 0 ? m_.bytes(image, size) : std::vector<std::uint8_t>{};
    if (!text.empty()) {
        text.back() = 0;
        for (std::uint8_t& c : text) {
            if (c == 0) {
                break;
            }
            c = static_cast<std::uint8_t>(c >= 'A' && c <= 'Z' ? c + ' ' : c);
        }
    }
    // the text as retail's string routines see it: up to the first NUL
    std::size_t length = 0;
    while (length < text.size() && text[length] != 0) {
        ++length;
    }
    const std::string_view whole(reinterpret_cast<const char*>(text.data()), length);
    const auto at = [&](std::size_t k) -> std::uint8_t { return k < text.size() ? text[k] : 0; };
    // retail walks on over a missing delimiter; a manifest without one would run off the block, so stop there
    const auto until = [&](std::size_t p, auto stop) {
        while (p < text.size() && !stop(text[p])) {
            ++p;
        }
        return p;
    };
    std::uint32_t parts = 0;
    std::size_t from = 0;
    while (at(from) != 0) {
        s_.check(static_cast<std::int32_t>(parts) < limit, kMsgTooManyPieces);
        const std::size_t found = whole.find(token, from);
        if (found == std::string_view::npos) {
            break;
        }
        std::size_t p = found + token.size();
        if (starts != 0) {
            set_word_at(m_, starts + parts * 4, image + static_cast<std::uint32_t>(p));
        }
        p = until(p, [](std::uint8_t c) { return c == '_'; });
        if (p >= text.size()) {
            break;
        }
        text[p++] = 0;
        set_word_at(m_, names + parts * 4, image + static_cast<std::uint32_t>(p));
        p = until(p, [](std::uint8_t c) { return c == ' ' || c == '\t'; });
        if (p >= text.size()) {
            break;
        }
        text[p++] = 0;
        p = until(p, [](std::uint8_t c) { return c == '\n'; });
        ++parts;
        from = p;
    }
    if (!text.empty()) {
        m_.write(image, text);
    }
    set_word_at(m_, count, parts);
    s_.check(parts != 0, kMsgNoPartsFound);
    return image;
}

template <typename Count, typename Item, typename Push>
void PsxSpool::collect_core(std::uint32_t slot, std::uint32_t model, bool have_count, bool have_list, Count count, Item item, Push push) {
    s_.check(slot != 0, kMsgNoRegion);
    s_.check(model != 0, kMsgNoModelToSearch);
    s_.check(have_count, kMsgNeedChecksumCount);
    s_.check(have_list, kMsgNeedChecksumList);
    const std::uint32_t raw = word_at(m_, slot + kSlotRaw);
    s_.check(raw != 0, kMsgNoRegionB);
    const std::uint32_t models = word_at(m_, word_at(m_, slot + kSlotModels) - 4);
    const std::uint32_t tables = trailing_tables(raw);
    const std::uint32_t materials = word_at(m_, tables + models * 4);
    const std::uint32_t list = tables + 4 + models * 4;
    std::uint32_t face = model + 0x1c + (static_cast<std::uint32_t>(half_at(m_, model + 4)) + half_at(m_, model + 2)) * 8;
    for (std::uint32_t n = 0; n < half_at(m_, model + 6); ++n) {
        bool found = false;
        std::uint32_t at = 0;
        for (std::uint32_t k = 0; k < materials; ++k) {
            const std::uint32_t candidate = word_at(m_, list + 4 * k);
            const bool same = word_at(m_, word_at(m_, face + 0x10) + 0x18) == word_at(m_, candidate + 0x18);
            found = (word_at(m_, face) & 0x20) != 0 ? same && word_at(m_, candidate + 0xc) == word_at(m_, face + 0x1c) : same;
            if (found) {
                at = k;
                break;
            }
        }
        if (found) {
            const std::uint32_t checksum = word_at(m_, word_at(m_, list + 4 * at) + 0x18);
            bool listed = false;
            for (std::int32_t k = 0, have = count(); k < have; ++k) {
                listed = listed || item(k) == checksum;
            }
            if (!listed) {
                s_.check(count() < 0x20, kMsgTooManyChecksums);
                push(checksum);
            }
        }
        s_.check(found, kMsgReverseEngineer);
        face += static_cast<std::uint32_t>(word_at(m_, face) >> 18) * 4;
    }
}

void PsxSpool::collect_model_materials(std::uint32_t slot, std::uint32_t model, std::uint32_t checksums, std::uint32_t count) {
    collect_core(
        slot, model, count != 0, checksums != 0, [&] { return int_at(m_, count); }, [&](std::int32_t k) { return word_at(m_, checksums + 4 * static_cast<std::uint32_t>(k)); },
        [&](std::uint32_t checksum) {
            set_word_at(m_, checksums + 4 * word_at(m_, count), checksum);
            set_word_at(m_, count, word_at(m_, count) + 1);
        });
}

std::uint32_t PsxSpool::score_material_palette_usage(std::uint32_t checksum) {
    const std::uint32_t material = find_material(checksum);
    s_.check(material != 0, kMsgBoardTextureMissing);
    if (material == 0) {
        return 0;
    }
    s_.note(kPaletteEntries, {word_at(m_, material + 0x14)});
    return static_cast<std::uint32_t>(s_.value(kPaletteEntries));
}

std::uint32_t PsxSpool::select_material_for_model(std::uint32_t skater) {
    const std::uint32_t selection = kSkaterSelectionRecords + word_at(m_, skater + 0x2cc0) * 0x170;
    if ((byte_at(m_, selection) & 2) != 0) {
        if (global_word(m_, kGameModeSelector) == 7) {
            return word_at(m_, word_at(m_, kSkaterPartTables + global_word(m_, kCustomSkaterIndex) * 4) + 0x6c);
        }
        if (skater == global_word(m_, kPlayer)) {
            return word_at(m_, global_word(m_, kSkaterPartTables) + 0x6c);
        }
        if (skater == global_word(m_, kPlayerTwo)) {
            return word_at(m_, global_word(m_, kSkaterPartTables + 4) + 0x6c);
        }
        s_.check(false, kMsgNoCustomSkater);
        return 0;
    }
    const std::uint32_t region = byte_at(m_, skater + 0x1f);
    const std::uint32_t slot = slot_addr(static_cast<std::int32_t>(region));
    const std::int32_t models = int_at(m_, word_at(m_, slot + kSlotModels) - 4);
    // (a thiscall on the skater, which retail leaves in ECX from the first line: the probe's stub took the argument only)
    s_.note(kModelRoll, s_.live() ? std::vector<std::uint32_t>{skater, 0x10} : std::vector<std::uint32_t>{0x10});
    const std::int32_t pick = s_.value(kModelRoll);
    s_.check(pick >= 0 && pick < models, kMsgModelIndexRange);
    std::array<std::uint32_t, 32> list{};
    std::int32_t listed = 0;
    // (the list and its count are locals of the retail function; the model-pointer read happens before the collection starts)
    const std::uint32_t model = word_at(m_, word_at(m_, slot + kSlotModels) + static_cast<std::uint32_t>(pick) * 4);
    collect_core(
        slot, model, true, true, [&] { return listed; }, [&](std::int32_t k) { return list[static_cast<std::size_t>(k)]; },
        [&](std::uint32_t checksum) {
            if (listed < 32) {
                list[static_cast<std::size_t>(listed)] = checksum;
            }
            ++listed;
        });
    std::uint32_t best = 0;
    std::int32_t best_score = 0;
    for (std::int32_t k = 0; k < listed; ++k) {
        const std::int32_t score = static_cast<std::int32_t>(score_material_palette_usage(list[static_cast<std::size_t>(k)]));
        if (score > best_score) {
            best = list[static_cast<std::size_t>(k)];
            best_score = score;
        }
    }
    return best;
}

void PsxSpool::relocate_definition_pointers(std::uint32_t region_image, std::uint32_t delta) {
    const std::uint32_t models = word_at(m_, region_image + 0xc + word_at(m_, region_image + 8) * 0x24);
    const std::uint32_t tables = trailing_tables(region_image);
    std::uint32_t at = tables + 4 + models * 4;
    std::uint32_t count = word_at(m_, at - 4);
    for (std::uint32_t k = 0, p = at; k < count; ++k, p += 4) {
        set_word_at(m_, p, word_at(m_, p) + delta);
    }
    std::uint32_t entries = word_at(m_, at + count * 4);
    at += (count + 1) * 4;
    for (; entries != 0; --entries, at += 9 * 4) {
        set_word_at(m_, at, word_at(m_, at) + delta);
    }
    std::uint32_t wide = word_at(m_, at);
    at += 4;
    for (; wide != 0; --wide, at += 0x81 * 4) {
        set_word_at(m_, at, word_at(m_, at) + delta);
    }
    for (std::uint32_t records = word_at(m_, at); records != 0; --records) {
        at += 4;
        const std::uint32_t record = word_at(m_, at) + region_image + 8;
        set_word_at(m_, record, word_at(m_, record) + delta);
    }
}

std::uint32_t PsxSpool::player_model_slot(std::uint32_t character, std::uint32_t skater) {
    const std::uint32_t base = character * 5;
    if ((byte_at(m_, kSkaterSelectionRecords + skater * 0x170) & 2) != 0) {
        s_.note("custom_slot_index", {skater});
        return base + 1 + static_cast<std::uint32_t>(s_.value("custom_slot_index"));
    }
    return base;
}

}  // namespace opentony::runtime::psx
