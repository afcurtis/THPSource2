#include "auto_views.hpp"
#include "level_gen.hpp"

namespace opentony::runtime::levelgen {

using namespace opentony::runtime::autoviews;

namespace {
constexpr std::uint32_t kModels = 0x18;
constexpr std::uint32_t kModelBytes = 0x24;
constexpr std::uint32_t kTransparentPalette = 0x0056b700;
constexpr std::uint32_t kLevelName = 0x006a0548;         // D3DLevelName
constexpr std::uint32_t kNumNodes = 0x0056e214;          // the trigger file's nodes and their offsets
constexpr std::uint32_t kOffsetList = 0x0056e210;
constexpr std::uint32_t kMsgLoading = 0x0052ab10;
constexpr std::uint32_t kMsgLevelName = 0x0052aaf0;
}  // namespace

// 0x00439b70: load the park library `name`: three PSX regions are queued (the models "<name>_L", the world "<name>_W<size>" and the source "<name>") and drained, the
// object table gives each source model its position (in 4096ths), every model is read by the in-model, and the rails of "<name>_T" are loaded.
void LevelGen::load_models(std::uint32_t self, std::uint32_t name) {
    if (byte_at(m_, self + 0x3744) != 0) {
        set_global_byte(m_, kTransparentPalette, 1);
    }
    unload_models(self);
    g_.model_info_clear(0);
    s_.check(int_at(m_, self + 0x14) == -1, kMsgSourceLoaded);
    const std::string base = m_.cstring(name);
    put_string(self + 0x235c, base + "_L");
    set_word_at(m_, self + 0x236c, engine_value(0x004b37a0, {self + 0x235c, 0, 0, 1}));
    engine(0x004b3df0);
    put_string(self + 0x2370, base + "_W" + std::to_string(static_cast<std::int32_t>(global_word(m_, kVersion)) + 1));
    set_word_at(m_, self + 0x2380, engine_value(0x004b37a0, {self + 0x2370, 0, 0, 0}));
    engine(0x004b3df0);
    set_word_at(m_, self + 0x14, engine_value(0x004b37a0, {name, 0, 0, 0}));
    put_string(self + 4, base);
    engine(0x004b3df0);
    const std::uint32_t slot = word_at(m_, self + 0x14);
    const std::uint32_t region = word_at(m_, kObjectTables + 8 + slot * 0x44);
    const std::int32_t count = int_at(m_, region + 8);
    s_.check(count <= 0xfa, kMsgTooManyItems);
    const std::uint32_t table = region + 0x10 + static_cast<std::uint32_t>(count) * 0x24;
    set_word_at(m_, self + 0x2340, word_at(m_, table - 4));
    s_.check(word_at(m_, self + 0x2340) <= 0xfa, kMsgTooManyModels);
    if (count > 0) {
        const std::uint32_t objects = word_at(m_, kObjectTables + slot * 0x44) + 0xc;
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(count); ++i) {
            const std::uint32_t at = self + 0x30 + i * kModelBytes;
            set_half_at(m_, at, static_cast<std::uint32_t>(int_at(m_, objects + i * 0x4c - 4) >> 12));
            set_half_at(m_, at + 2, static_cast<std::uint32_t>(int_at(m_, objects + i * 0x4c) >> 12));
            set_half_at(m_, at + 4, static_cast<std::uint32_t>(int_at(m_, objects + i * 0x4c + 4) >> 12));
        }
    }
    engine(0x004b20f0, {region});
    for (std::uint32_t i = 0; i < word_at(m_, self + 0x2340); ++i) {
        g_.in_set_data(self + kModels + i * kModelBytes, word_at(m_, table + 4 * i), i);
    }
    const std::string rails = base + "_T";
    put_string(kScratchName, rails);
    load_rails(self, kScratchName);
    set_global_byte(m_, kTransparentPalette, 0);
}

// 0x0043a280: change the theme and the size of the park: nothing is rebuilt when neither changes (the maps are emptied again), otherwise the maps and the source models go and
// the library of the new theme "SkEd<n>" is loaded.
void LevelGen::select_variant(std::uint32_t self, std::uint32_t theme, std::uint32_t version, std::uint32_t reset) {
    const std::uint32_t old_theme = global_word(m_, kTheme);
    const bool theme_changed = !(theme == old_theme || theme == 0xffffffff);
    const std::uint32_t old_version = global_word(m_, kVersion);
    const bool version_changed = !(version == old_version || version == 0xffffffff);
    if (theme == 0xffffffff) {
        theme = old_theme;
    }
    if (version == 0xffffffff) {
        version = old_version;
    }
    unload_out_psx(1);
    release_pieces(self);
    free_floor_pieces(self);
    free_gaps(self);
    free_starts(self);
    g_.out_cleanup();
    if (!theme_changed && !version_changed) {
        g_.out_init();
        init_statics(reset);
        set_fog(self, 0);
        return;
    }
    free_maps(self);
    set_global_word(m_, kVersion, version);
    set_global_word(m_, kGridW, word_at(m_, kVersionTable + version * 8));
    set_global_word(m_, kGridD, word_at(m_, kVersionTable + 4 + version * 8));
    log(kMsgLoading);
    set_global_word(m_, kTheme, theme);
    put_string(kLevelName, "Ed" + std::to_string(static_cast<std::int32_t>(theme) + 1));
    log(kMsgLevelName);
    put_string(kScratchLibrary, "SkEd" + std::to_string(static_cast<std::int32_t>(global_word(m_, kTheme)) + 1));
    load_models(self, kScratchLibrary);
    g_.out_init();
    alloc_maps(self);
    init_statics(reset);
    translate_references(global_word(m_, kTheme));
    set_fog(self, 0);
}

// 0x0043bcd0: the rail nodes of the trigger file `name` become outlines in the in-model of the rail's model: for every rail node that links to a model node
// (type 0xc), the model whose id matches is found and the nodes along the rail make a polygon of points (in model units, relative to the model's offset).
void LevelGen::load_rails(std::uint32_t self, std::uint32_t name) {
    const std::uint32_t slot = word_at(m_, self + 0x14);
    const std::uint32_t table = engine_value(0x004b20f0, {word_at(m_, kObjectTables + 8 + slot * 0x44)});
    engine(0x004c5130, {name}, m_.cstring(name));
    for (std::uint32_t node = 0; static_cast<std::int32_t>(node) < global_int(m_, kNumNodes);) {
        if (engine_value(0x004aa040, {node}) != 0) {
            const std::int16_t links = static_cast<std::int16_t>(engine_value(0x004aa1c0, {node}));
            std::uint32_t found = 0xffffffff;
            for (std::int16_t i = 0; i < links; ++i) {
                const std::uint32_t link = engine_value(0x004aa090, {node, static_cast<std::uint32_t>(static_cast<std::int32_t>(i))}) & 0xffff;
                if (half_at(m_, word_at(m_, global_word(m_, kOffsetList) + 4 * link)) == 0xc) {
                    found = link;
                }
            }
            if (found != 0xffffffff) {
                const std::uint32_t data = word_at(m_, global_word(m_, kOffsetList) + 4 * found);
                std::uint32_t key_at = data + 4 + half_at(m_, data + 2) * 2u;
                if ((key_at & 3) != 0) {
                    key_at += 2;
                }
                const std::uint32_t key = word_at(m_, key_at);
                std::uint32_t model = 0;
                if (word_at(m_, self + 0x2340) != 0) {
                    while (word_at(m_, table + 4 * model) != key) {
                        ++model;
                        if (model >= word_at(m_, self + 0x2340)) {
                            break;
                        }
                    }
                }
                s_.check(model < word_at(m_, self + 0x2340), kMsgNoTrickob);
                const std::uint32_t in = self + model * kModelBytes;
                const std::uint32_t polygon = g_.plist_new(word_at(m_, in + 0x38));
                std::uint32_t current = node;
                std::uint32_t next;
                do {
                    set_global_word(m_, kScratchPosition, 0);
                    set_global_word(m_, kScratchPosition + 4, 0);
                    set_global_word(m_, kScratchPosition + 8, 0);
                    // TRG_DecodeNodePosition(&position, node): the engine writes the node's x, y, z into the caller's local and returns a pointer to its record
                    s_.note(pspool::service_name(0x004c8650), {kScratchPosition, current & 0xffff});
                    // (a replay scripts the three words the engine stored as values before the answer; live the engine wrote them itself and the one value is the answer)
                    for (std::uint32_t k = 0; !s_.live() && k < 3; ++k) {
                        set_word_at(m_, kScratchPosition + 4 * k, static_cast<std::uint32_t>(s_.value(pspool::service_name(0x004c8650))));
                    }
                    const std::uint32_t record = static_cast<std::uint32_t>(s_.value(pspool::service_name(0x004c8650)));
                    const std::uint32_t block = g_.new_zeroed(0x18);
                    const std::uint32_t point = block != 0 ? g_.point_init(block) : 0;
                    set_half_at(m_, point, static_cast<std::uint32_t>(global_int(m_, kScratchPosition) >> 12) - half_at(m_, in + 0x30));
                    set_half_at(m_, point + 2, static_cast<std::uint32_t>(global_int(m_, kScratchPosition + 4) >> 12) - half_at(m_, in + 0x32));
                    set_half_at(m_, point + 4, static_cast<std::uint32_t>(global_int(m_, kScratchPosition + 8) >> 12) - half_at(m_, in + 0x34));
                    set_word_at(m_, point + 0x10, half_at(m_, record));
                    next = engine_value(0x004aa220, {current});
                    s_.check(polygon != 0, kMsgNullString);
                    g_.poly_append(polygon, point, (next & 0xffff) == (node & 0xffff) ? 1 : 0);
                    current = next;
                } while ((next & 0xffff) != 0 && (next & 0xffff) != (node & 0xffff));
                g_.poly_fix_winding(polygon);
            }
        }
        node = (node + 1) & 0xffff;
        if (static_cast<std::int32_t>(node) >= global_int(m_, kNumNodes)) {
            break;
        }
    }
    engine(0x004c5100);
}

// ---- the fog -----------------------------------------------------------------------------------------------------------------

// 0x0043f5e0: the fog range of the theme and the size (halved when asked) goes to the engine.
void LevelGen::set_fog(std::uint32_t self, std::uint32_t halve) {
    const std::uint32_t theme = global_word(m_, kTheme);
    const std::uint32_t outside = byte_at(m_, self + 0x3744) == 0 ? 1 : 0;
    const std::uint32_t row = outside + theme * 4;
    set_global_word(m_, 0x00563a60, word_at(m_, kFogTable + 4 * row));
    set_global_word(m_, 0x0056114c, word_at(m_, kFogTable + 8 + 4 * row));
    engine(0x0042fc70);
    std::uint32_t kind;
    if (byte_at(m_, self + 0x3744) == 0) {
        kind = 2;
    } else {
        const std::uint32_t state = global_word(m_, kLevelState);
        kind = (state == 8 || state == 5 || state == 9 || state == 4) ? 1 : 0;
    }
    const std::uint32_t version = global_word(m_, kVersion);
    set_word_at(m_, self + 0x373c, word_at(m_, kFogSize + 4 * (kind + 6 * version)));
    set_word_at(m_, self + 0x3740, word_at(m_, kFogSize + 0xc + 4 * (kind + 6 * version)));
    if ((halve & 0xff) != 0) {
        set_word_at(m_, self + 0x373c, static_cast<std::uint32_t>(int_at(m_, self + 0x373c) >> 1));
        set_word_at(m_, self + 0x3740, static_cast<std::uint32_t>(int_at(m_, self + 0x3740) >> 1));
    }
    engine(0x00464710, {0xa, word_at(m_, self + 0x373c), word_at(m_, self + 0x3740)});
}

// 0x0043f6c0: with little memory left the fog is pulled in to 95 per cent.
void LevelGen::dim_fog(std::uint32_t self) {
    const std::uint32_t limit = global_word(m_, kFreeA) - 0x40;
    if (global_word(m_, kFreeB) <= limit) {
        return;
    }
    const std::int32_t near_value = int_at(m_, self + 0x373c);
    const std::int32_t scaled = 95 * near_value;
    std::int32_t quotient = static_cast<std::int32_t>((static_cast<std::int64_t>(scaled) * 0x51eb851f) >> 32) >> 5;
    quotient += static_cast<std::int32_t>(static_cast<std::uint32_t>(quotient) >> 31);
    set_word_at(m_, self + 0x373c, static_cast<std::uint32_t>(quotient));
    engine(0x00464710, {0xa, static_cast<std::uint32_t>(quotient), word_at(m_, self + 0x3740)});
}

// 0x0043f710: how many zone items there is room for (4000 less the live count, not below zero).
std::uint32_t LevelGen::zone_room() {
    const std::int32_t room = 0xfa0 - global_int(m_, kLiveCount);
    return room < 0 ? 0 : static_cast<std::uint32_t>(room);
}

}  // namespace opentony::runtime::levelgen
