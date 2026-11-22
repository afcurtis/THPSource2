#include "auto_views.hpp"
#include "player_select.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

constexpr std::uint32_t kMsgNoFont = 0x0052576c;
constexpr std::uint32_t kMsgUnavailable = 0x00537220;      // "Why isn't this skater %d available?"
constexpr std::uint32_t kMsgNoFace = 0x0055fe10;

// SkaterView fields (0x13dc bytes, vtable 0x005195e4)
constexpr std::uint32_t kObject = 0xac;         // the animated skater object (0x168 bytes)
constexpr std::uint32_t kMode = 0xb0;
constexpr std::uint32_t kStatSprites = 0xb4;    // eight sprite elements (seven stat bars and, in the last slot, the "Loading..." text)
constexpr std::uint32_t kLoadingText = 0xd0;
constexpr std::uint32_t kFont = 0xd4;
constexpr std::uint32_t kMulti = 0xd8;          // the multi-player layout
constexpr std::uint32_t kFrame = 0xdc;          // the window picture (s2win1) of the single-player layout
constexpr std::uint32_t kSpool = 0xe0;          // the spool state (player_spool.cpp's object), embedded
constexpr std::uint32_t kFaces = 0x878;         // three face sprites: the default, the Hawk's second costume, the secret one
constexpr std::uint32_t kFaceNow = 0x884;
constexpr std::uint32_t kArrowA = 0x888;
constexpr std::uint32_t kArrowB = 0x88c;
constexpr std::uint32_t kSelected = 0x890;
constexpr std::uint32_t kAnimRegion = 0x894;
constexpr std::uint32_t kModelSlot = 0x898;
constexpr std::uint32_t kModelView = 0x89c;
constexpr std::uint32_t kManifest = 0x8a0;
constexpr std::uint32_t kManifestFlag = 0x920;           // the part count the manifest loader writes
constexpr std::uint32_t kRecords = 0x9a4;       // twenty records of 0x84 bytes
constexpr std::uint32_t kPartCount = 0x1374;
constexpr std::uint32_t kCustoms = 0x1378;      // the custom definition holder: four skater objects from +4
constexpr std::uint32_t kCustomFlag = 0x138c;
constexpr std::uint32_t kRect = 0x1390;         // the view rectangle (seven 16-bit words)
constexpr std::uint32_t kLights = 0x13a4;
constexpr std::uint32_t kLast = 0x13d8;

constexpr std::uint32_t kGlobalPreManager = 0x0056b834;
constexpr std::uint32_t kGlobalSpoolManager = 0x0056db4c;
constexpr std::uint32_t kRegionNames = 0x0056d428;      // PsxRegionNamesBySlot: 0x44 bytes a slot; the byte at +0xa says the region is loaded
constexpr std::uint32_t kRegionStride = 0x44;
constexpr std::uint32_t kRegionTables = 0x0056d444;     // the animation tables (two words a slot at +0x1c and +0x20)
constexpr std::uint32_t kCamera = 0x0055f7c8;
constexpr std::uint32_t kCameraWords = 0x0055f7cc;
constexpr std::uint32_t kFogLight = 0x0055f7fc;
constexpr std::uint32_t kOtPushback = 0x00564368;       // three 16-bit words
constexpr std::uint32_t kSoftRenderer = 0x029da3a0;
constexpr std::uint32_t kFillHeight = 0x00527d54;
constexpr std::uint32_t kStatTable = 0x00533ea8;        // the stat sprites' table: five words each from 0x533ea0 (file, x, y, dx, dy)
constexpr std::uint32_t kAnimationScale = 0x0056865c;
constexpr std::uint32_t kPlayerPre2 = 0x0053720c;       // "player2.pre"

}  // namespace

// ---- the spool state's small methods (playsel.cpp 0x004a61f0, 0x004a6260, 0x004a6e60..0x004a6ea0) -----------------------------------------------------

// 0x004a61f0: on the embedded spool state: clear its per-skater slots, note which entries are custom skaters and the costumes the manager remembers.
void PlayerSelect::view_init_slots(std::uint32_t spool) {
    for (std::uint32_t i = 0; i < 20; ++i) {
        set_word_at(m_, spool + 4 * i, 0);
        set_word_at(m_, spool + 0x6f4 + 4 * i, 0);
        set_word_at(m_, spool + 0x6a4 + 4 * i, 0);
        if ((byte_at(m_, skater::row(static_cast<int>(i)) + skater::kRowFlags) & 2) != 0) {
            set_word_at(m_, spool + 0x6a4 + 4 * i, 1);
        }
        set_word_at(m_, spool + 0x744 + 4 * i, u(sk_.remembered_costume(static_cast<int>(i))));
    }
    set_word_at(m_, spool + 0x794, 0);
    engine(0x004a67c0, {spool});
}

// 0x004a6260: free the slots' buffers and objects.
void PlayerSelect::view_free_slots(std::uint32_t spool) {
    for (std::uint32_t i = 0; i < 20; ++i) {
        if (word_at(m_, spool + 4 * i) != 0) {
            s_.note(kMemDelete, {word_at(m_, spool + 4 * i)});
            set_word_at(m_, spool + 4 * i, 0);
        }
        if (word_at(m_, spool + 0x6f4 + 4 * i) != 0) {
            delete_object(word_at(m_, spool + 0x6f4 + 4 * i));
            set_word_at(m_, spool + 0x6f4 + 4 * i, 0);
        }
    }
    engine(0x004a67c0, {spool});
}

bool PlayerSelect::view_spool_busy(std::uint32_t spool) {
    return !(word_at(m_, spool + 0x644) == 0xffffffffu && word_at(m_, spool + 0x648) == 0xffffffffu && word_at(m_, spool + 0x64c) == 0xffffffffu);
}

void PlayerSelect::view_set_spool_flag(std::uint32_t spool, std::uint32_t value) { set_word_at(m_, spool + 0x794, value); }

// 0x004a6ea0: remember the costume, restart the spool and note nothing is loading.
void PlayerSelect::view_reset_spool(std::uint32_t spool) {
    const std::int32_t costume = sk_.current_costume();
    set_word_at(m_, spool + 0x744 + 4 * u(sk_.current_character()), u(costume));
    engine(0x004a62b0, {spool, u(sk_.current_character()), 0});
    engine(0x004b52b0, {global_word(m_, kGlobalSpoolManager)});
    set_word_at(m_, spool + 0x644, 0xffffffff);
    set_word_at(m_, spool + 0x648, 0xffffffff);
    set_word_at(m_, spool + 0x64c, 0xffffffff);
    set_word_at(m_, spool + 0x654 + 4 * u(sk_.current_character()), 0);
}

// ---- the view ---------------------------------------------------------------------------------------------------------------------------------

// 0x004a3e80: `multi` picks the multi-player layout.
std::uint32_t PlayerSelect::view_construct(std::uint32_t self, std::uint32_t multi, std::uint32_t font, std::int32_t x, std::int32_t y, std::int32_t start_x,
                                           std::int32_t start_y) {
    w_.window_construct(self, x, y, start_x, start_y);
    view_init_slots(self + kSpool);
    player_select_view_view_at(m_, self).set_field_0(static_cast<Word>(kVtSkaterView));
    check(font != 0, kMsgNoFont);
    player_select_view_view_at(m_, self).set_field_d8(static_cast<Word>(multi));
    player_select_view_view_at(m_, self).set_field_d4(static_cast<Word>(font));
    view_build_sprites(self);
    engine(0x004a90f0, {global_word(m_, kGlobalPreManager), kPlayerPre2, 1});
    std::uint32_t memory = operator_new(0x2c);
    std::uint32_t holder = 0;
    if (memory != 0) {
        holder = engine_value(0x0042f880, {memory});
    }
    player_select_view_view_at(m_, self).set_field_1378(static_cast<Word>(holder));
    engine(0x0042fa10, {holder, 0});
    player_select_view_view_at(m_, self).set_field_890(static_cast<Word>(u(sk_.current_character())));
    player_select_view_view_at(m_, self).set_field_13d8(0xffffffff);
    // the view rectangle (width, height, x, y, near, far, field of view)
    set_half_at(m_, self + kRect + 2, 0xf0);
    player_select_view_view_at(m_, self).set_field_1390(0x200);
    set_half_at(m_, self + kRect + 6, 0);
    set_half_at(m_, self + kRect + 4, 0);
    player_select_view_view_at(m_, self).set_field_1390(static_cast<std::uint16_t>(global_word(m_, kGlobalScreenWidth) & 0x7fffff));
    set_half_at(m_, self + kRect + 6, 0);
    set_half_at(m_, self + kRect + 8, 0xa);
    set_half_at(m_, self + kRect + 10, 0x7530);
    set_half_at(m_, self + kRect + 2, static_cast<std::uint32_t>((static_cast<std::uint64_t>(global_word(m_, kGlobalScreenHeight) * 0xf0u) * 0x88888889u) >> 39));
    set_global_word(m_, kCameraWords, 0);
    if (player_select_view_view_at(m_, self).field_d8() != 0) {
        set_global_word(m_, kCameraWords + 4, 0xffffffb6);
        set_global_word(m_, kCameraWords + 8, 0xfffffee4);
        set_half_at(m_, self + kRect + 12, 0x6aa);
    } else {
        set_global_word(m_, kCameraWords + 4, 0xffffffc0);
        set_global_word(m_, kCameraWords + 8, 0xfffffe6c);
        set_half_at(m_, self + kRect + 12, 0x369);
    }
    engine(0x00418200, {self + kLights});
    view_set_spool_flag(self + kSpool, self);
    player_select_view_view_at(m_, self).set_field_894(0xffffffff);
    player_select_view_view_at(m_, self).set_field_138c(1);
    auto sprite = [&](std::uint32_t file, std::int32_t at_x, std::int32_t at_y) {
        const std::uint32_t block = operator_new(0x2c);
        return block != 0 ? w_.sprite_construct(block, file, at_x, at_y, -1, -1, 0, 0) : 0u;
    };
    if (player_select_view_view_at(m_, self).field_d8() != 0) {
        player_select_view_view_at(m_, self).set_field_888(static_cast<Word>(sprite(0x0052542c, -0x44, 0x28)));
        player_select_view_view_at(m_, self).set_field_88c(static_cast<Word>(sprite(0x0052543c, 0x50, 0x28)));
    } else {
        player_select_view_view_at(m_, self).set_field_888(static_cast<Word>(sprite(0x00527c10, -0x44, 0x28)));
        player_select_view_view_at(m_, self).set_field_88c(static_cast<Word>(sprite(0x00527c20, 0x96, 0x28)));
    }
    w_.window_add(self, player_select_view_view_at(m_, self).field_888());
    w_.window_add(self, player_select_view_view_at(m_, self).field_88c());
    memory = operator_new(0x40);
    std::uint32_t model = 0;
    if (memory != 0) {
        model = engine_value(0x00418fc0, {memory, 0x00535c84, kCamera, self + kRect, self + kLights, 0, u(-0x50), 0x3c});
    }
    player_select_view_view_at(m_, self).set_field_89c(static_cast<Word>(model));
    w_.window_add(self, model);
    player_select_view_view_at(m_, self).set_field_dc(0);
    if (player_select_view_view_at(m_, self).field_d8() == 0) {
        const std::uint32_t frame = sprite(0x00537200, 0, 0);
        w_.window_add(self, frame);
        set_word_at(m_, frame + kElemArg, 1);
        player_select_view_view_at(m_, self).set_field_dc(static_cast<Word>(frame));
    }
    player_select_view_view_at(m_, self).set_field_1374(0);
    player_select_view_view_at(m_, self).set_field_920(0);
    for (std::uint32_t i = 0; i < 20; ++i) {
        set_word_at(m_, self + kRecords + 0x84 * i, 0);
    }
    view_create_object(self);
    engine(0x004f5430, {kFogLight});
    engine(0x004e8460, {kFogLight, kCameraWords});
    engine(0x00464710, {0xa, 0x1770, 0x800});
    engine(0x004645f0);
    auto face = [&](std::uint32_t slot, std::uint32_t file) {
        const std::uint32_t block = operator_new(0x5c);
        std::uint32_t result = 0;
        if (block != 0) {
            s_.note(kSpriteConstruct, {block, file, 0x40, 0x40, 0}, m_.cstring(file));
            result = static_cast<std::uint32_t>(s_.value(kSpriteConstruct));
        }
        set_word_at(m_, self + slot, result);
    };
    face(kFaces, 0x00535ef4);
    face(kFaces + 4, 0x005371f0);
    face(kFaces + 8, 0x00535f04);
    player_select_view_view_at(m_, self).set_field_884(0);
    player_select_view_view_at(m_, self).set_field_b0(0);
    engine(0x004a9330, {global_word(m_, kGlobalPreManager), kPlayerPre2});
    return self;
}

std::uint32_t PlayerSelect::view_delete(std::uint32_t self, std::uint32_t flag) {
    view_destroy(self);
    if ((flag & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x004a4380: the custom definitions, the face sprites, the model slot's region, the skater object and the animation region.
void PlayerSelect::view_destroy(std::uint32_t self) {
    player_select_view_view_at(m_, self).set_field_0(static_cast<Word>(kVtSkaterView));
    if (player_select_view_view_at(m_, self).field_1378() != 0) {
        delete_object(player_select_view_view_at(m_, self).field_1378());
    }
    for (std::uint32_t slot : {kFaces, kFaces + 4, kFaces + 8}) {
        if (word_at(m_, self + slot) != 0) {
            delete_object(word_at(m_, self + slot));
        }
    }
    if (player_select_view_view_at(m_, self).field_884() != 0) {
        player_select_view_view_at(m_, self).set_field_884(0);
    }
    const std::uint32_t model_slot = word_at(m_, self + kModelSlot);
    if (byte_at(m_, kRegionNames + 0xa + model_slot * kRegionStride) != 0) {
        engine(0x004b32f0, {model_slot, 1, 0});
    }
    set_byte_at(m_, kRegionNames + word_at(m_, self + kModelSlot) * kRegionStride, 0);
    s_.note(kMemDelete, {player_select_view_view_at(m_, self).field_1374()});
    if (player_select_view_view_at(m_, self).field_ac() != 0) {
        delete_object(player_select_view_view_at(m_, self).field_ac());
    }
    engine(0x004b32f0, {player_select_view_view_at(m_, self).field_894(), 1, 0});
    set_byte_at(m_, kRegionNames + player_select_view_view_at(m_, self).field_894() * kRegionStride, 0);
    view_free_slots(self + kSpool);
    w_.window_destroy(self);
}

// 0x004a44f0: the "Loading..." text and the stat sprites (the table's rows give a picture and a place; the multi-player layout adds an offset).
void PlayerSelect::view_build_sprites(std::uint32_t self) {
    const bool multi = player_select_view_view_at(m_, self).field_d8() != 0;
    std::uint32_t memory = operator_new(0x30);
    std::uint32_t text = 0;
    if (memory != 0) {
        text = w_.text_construct(memory, global_word(m_, 0x0052e61c), player_select_view_view_at(m_, self).field_d4(), multi ? -15 : 0x24, 0x38, -1, -1);
    }
    w_.window_add(self, text);
    player_select_view_view_at(m_, self).set_field_d0(static_cast<Word>(text));
    for (std::uint32_t i = 0, entry = kStatTable; entry < 0x00533f48; entry += 0x14, ++i) {
        if (entry == 0x00533f34) {
            continue;
        }
        std::int32_t x = int_at(m_, entry - 4);
        std::int32_t y = int_at(m_, entry);
        if (multi) {
            x += int_at(m_, entry + 4);
            y += int_at(m_, entry + 8);
        }
        memory = operator_new(0x2c);
        std::uint32_t sprite = 0;
        if (memory != 0) {
            sprite = w_.sprite_construct(memory, word_at(m_, entry - 8), x, y, -1, -1, 0, 0);
        }
        set_word_at(m_, self + kStatSprites + 4 * i, sprite);
        w_.window_add(self, sprite);
    }
}

// 0x004a4630: place the viewport (a fixed one in the multi-player layout, else under the frame), render the skater into it and draw the face and the window.
void PlayerSelect::view_draw(std::uint32_t self, std::int32_t x, std::int32_t y) {
    const std::uint32_t rect = self + kRect;
    if (player_select_view_view_at(m_, self).field_d8() != 0) {
        set_half_at(m_, rect + 4, 0xe2);
        set_half_at(m_, rect, 0x1e2);
        set_half_at(m_, rect + 6, 5);
        set_half_at(m_, rect + 2, 0xd7);
    } else {
        const std::uint32_t frame = player_select_view_view_at(m_, self).field_dc();
        const std::uint32_t left = w_.element_x(frame) + half_at(m_, self + kElemX) + u(x) + 6;
        set_half_at(m_, rect + 4, left);
        set_half_at(m_, rect, left + 0x83);
        const std::uint32_t top = w_.element_y(frame) + half_at(m_, self + kElemY) + u(y) + 0xf;
        set_half_at(m_, rect + 6, top);
        set_half_at(m_, rect + 2, top + 0x70);
    }
    const std::uint32_t width = global_word(m_, kGlobalScreenWidth);
    const std::uint32_t height = global_word(m_, kGlobalScreenHeight);
    auto vertical = [&](std::uint32_t value) { return static_cast<std::uint32_t>((static_cast<std::uint64_t>(value * height) * 0x88888889u) >> 39); };
    set_half_at(m_, rect + 4, (half_at(m_, rect + 4) * width) >> 9);
    set_half_at(m_, rect, (half_at(m_, rect) * width) >> 9);
    set_half_at(m_, rect + 6, vertical(half_at(m_, rect + 6)));
    set_half_at(m_, rect + 2, vertical(half_at(m_, rect + 2)));
    const std::int32_t character = sk_.current_character();
    if (engine_value(0x004a63b0, {self + kSpool, u(character)}) != 0) {
        if (player_select_view_view_at(m_, self).field_138c() != 0 && (word_at(m_, skater::row(character) + skater::kRowFlags) & 2) != 0 && !view_is_custom(self)) {
            // (the model is not shown: the custom skater is still being built)
        } else {
            engine(0x0045e8e0, {kCamera, rect, word_at(m_, global_word(m_, kGlobalRenderer) + 0x88)});
            if (!view_is_custom(self)) {
                engine(0x00461bf0, {player_select_view_view_at(m_, self).field_ac()});
                const std::uint32_t a = global_half(m_, kOtPushback);
                const std::uint32_t b = global_half(m_, kOtPushback + 2);
                const std::uint32_t c = global_half(m_, kOtPushback + 4);
                set_global_half(m_, kOtPushback, 0xffe0);
                set_global_half(m_, kOtPushback + 2, 0x20);
                set_global_half(m_, kOtPushback + 4, 0x40);
                engine(0x0045f530, {player_select_view_view_at(m_, self).field_ac()});
                set_global_half(m_, kOtPushback + 4, c);
                set_global_half(m_, kOtPushback, a);
                set_global_half(m_, kOtPushback + 2, b);
            }
            engine(0x0045f3f0);
        }
    }
    const std::int32_t offset_x = player_select_view_view_at(m_, self).field_d8() != 0 ? 0x84 : 0;
    const std::int32_t offset_y = player_select_view_view_at(m_, self).field_d8() != 0 ? 0x3e : 0;
    if (!view_is_custom(self)) {
        s_.note(kSpriteDrawService, {player_select_view_view_at(m_, self).field_884(), u(int_at(m_, self + kElemX) + offset_x + x - 0x26), u(int_at(m_, self + kElemY) + offset_y + y + 0x20), 0});
    }
    if (global_word(m_, kSoftRenderer) != 0) {
        s_.note(kSvcFillRect, {0, 0, 0x200, global_word(m_, kFillHeight), 1});
    }
    w_.window_draw(self, x, y);
    if (global_word(m_, kSoftRenderer) == 0) {
        s_.note(kSvcFillRect, {0, 0, 0x200, global_word(m_, kFillHeight), 1});
    }
}

// 0x004a48e0 (virtual slot 4): refresh everything the selected skater needs: the spool's model, the object's parts or custom build, the animation, the
// face picture, and the face sprite to show.
void PlayerSelect::view_load_skater(std::uint32_t self) {
    const std::int32_t character = sk_.current_character();
    const bool custom_row = (word_at(m_, skater::row(character) + skater::kRowFlags) & 2) != 0;
    if (!custom_row) {
        engine(0x004a6440, {self + kSpool, word_at(m_, self + kModelSlot), u(sk_.current_character())});
    } else {
        engine(0x004a6440, {self + kSpool, word_at(m_, self + kModelSlot), 0});
    }
    if (!view_is_custom(self)) {
        if ((word_at(m_, skater::row(sk_.current_character()) + skater::kRowFlags) & 2) != 0) {
            const std::int32_t index = sk_.custom_index(sk_.current_character());
            engine(0x00423be0, {word_at(m_, player_select_view_view_at(m_, self).field_1378() + 4 * u(index) + 4), player_select_view_view_at(m_, self).field_ac()});
        } else {
            set_byte_at(m_, player_select_view_view_at(m_, self).field_ac() + 0x1f, byte_at(m_, self + kModelSlot));
            engine(0x004a6720, {self + kSpool, player_select_view_view_at(m_, self).field_ac(), u(sk_.current_character())});
        }
    }
    const std::uint32_t row = skater::row(sk_.current_character());
    set_half_at(m_, player_select_view_view_at(m_, self).field_ac() + 0x16, half_at(m_, row + 0x160));
    engine(0x00480890, {player_select_view_view_at(m_, self).field_ac(), word_at(m_, row + 0x164), 1});
    if (!view_is_custom(self) && engine_value(0x004a63b0, {self + kSpool, u(sk_.current_character())}) != 0) {
        std::uint32_t face = 0;
        if ((word_at(m_, skater::row(sk_.current_character()) + skater::kRowFlags) & 2) != 0) {
            const std::int32_t index = sk_.custom_index(sk_.current_character());
            face = word_at(m_, word_at(m_, player_select_view_view_at(m_, self).field_1378() + 4 * u(index) + 4) + 0x6c);
        } else {
            face = engine_value(0x00418360, {u(sk_.current_character()), player_select_view_view_at(m_, self).field_ac()});
        }
        check(face != 0, kMsgNoFace);
        if (face != 0) {
            engine(0x00418480, {face, u(sk_.current_character()), global_word(m_, kPadCurrent)});
        }
    }
    if (player_select_view_view_at(m_, self).field_884() != 0) {
        player_select_view_view_at(m_, self).set_field_884(0);
    }
    std::uint32_t now = 0;
    if ((word_at(m_, skater::row(sk_.current_character()) + skater::kRowFlags) & 2) != 0) {
        now = word_at(m_, self + kFaces + 8);
    } else if (sk_.current_character() == 0 && sk_.current_costume() == 2) {
        now = word_at(m_, self + kFaces + 4);
    } else {
        now = engine_value(0x004a6340, {self + kSpool, u(sk_.current_character())});
    }
    if (now == 0) {
        now = player_select_view_view_at(m_, self).field_878();
    }
    check(now != 0, kMsgNoFace);
    player_select_view_view_at(m_, self).set_field_884(static_cast<Word>(now));
}

// 0x004a4bc0: show the stat sprites the skater's records call for (a custom skater's come from its record), hide the arrows and the model view as the
// state says, and update the window.
void PlayerSelect::view_update(std::uint32_t self) {
    set_word_at(m_, player_select_view_view_at(m_, self).field_888() + kElemHidden, 1);
    set_word_at(m_, player_select_view_view_at(m_, self).field_88c() + kElemHidden, 1);
    const std::int32_t character = sk_.current_character();
    if (player_select_view_view_at(m_, self).field_138c() != 0 && (word_at(m_, skater::row(character) + skater::kRowFlags) & 2) != 0) {
        set_word_at(m_, player_select_view_view_at(m_, self).field_d0() + kElemHidden, 0);
    } else {
        const std::uint32_t ready = engine_value(0x004a63b0, {self + kSpool, u(sk_.current_character())});
        if (ready == 0 && word_at(m_, skater::row(sk_.current_character()) + skater::kRowUnlocked) != 0) {
            set_word_at(m_, player_select_view_view_at(m_, self).field_d0() + kElemHidden, 0);
        } else {
            set_word_at(m_, player_select_view_view_at(m_, self).field_d0() + kElemHidden, 1);
        }
    }
    const std::uint32_t flags = word_at(m_, skater::row(sk_.current_character()) + skater::kRowFlags);
    const std::uint32_t sprite_a = player_select_view_view_at(m_, self).field_b4();
    set_word_at(m_, sprite_a + kElemHidden, (~flags >> 1) & 1);
    set_word_at(m_, word_at(m_, self + kStatSprites + 4) + kElemHidden, word_at(m_, sprite_a + kElemHidden) == 0 ? 1 : 0);
    std::int32_t stance = 0;
    std::int32_t style = 0;
    if ((word_at(m_, skater::row(sk_.current_character()) + skater::kRowFlags) & 2) == 0) {
        stance = byte_at(m_, skater::row(sk_.current_character()) + 0x14e);
        style = byte_at(m_, skater::row(sk_.current_character()) + 0x14f);
    } else {
        const std::uint32_t record = skater::kCustomRecords + skater::kCustomStride * u(sk_.custom_index(sk_.current_character()));
        stance = sbyte_at(m_, record + 0x24);
        style = sbyte_at(m_, record + 0x25);
    }
    set_word_at(m_, word_at(m_, self + kStatSprites + 8) + kElemHidden, stance != 1 ? 1 : 0);
    set_word_at(m_, word_at(m_, self + kStatSprites + 12) + kElemHidden, stance != 0 ? 1 : 0);
    set_word_at(m_, word_at(m_, self + kStatSprites + 16) + kElemHidden, stance != 2 ? 1 : 0);
    set_word_at(m_, word_at(m_, self + kStatSprites + 20) + kElemHidden, style != 0 ? 1 : 0);
    set_word_at(m_, word_at(m_, self + kStatSprites + 24) + kElemHidden, style != 1 ? 1 : 0);
    if (view_is_custom(self)) {
        for (std::uint32_t i = 0; i < 8; ++i) {
            set_word_at(m_, word_at(m_, self + kStatSprites + 4 * i) + kElemHidden, 1);
        }
    }
    set_word_at(m_, player_select_view_view_at(m_, self).field_89c() + kElemHidden, view_is_custom(self) ? 0 : 1);
    engine(0x00480fa0, {player_select_view_view_at(m_, self).field_ac()});
    const std::uint32_t object = player_select_view_view_at(m_, self).field_ac();
    if ((word_at(m_, skater::row(sk_.current_character()) + skater::kRowFlags) & 2) == 0) {
        const std::uint32_t slot_flag = byte_at(m_, skater::row(sk_.current_character()) + 0x14f);
        set_word_at(m_, object + 0xd8, slot_flag == 0 ? (word_at(m_, object + 0xd8) | 2) : (word_at(m_, object + 0xd8) & 0xfffffffd));
    } else {
        set_word_at(m_, object + 0xd8, word_at(m_, object + 0xd8) & 0xfffffffd);
    }
    w_.window_update(self);
}

// 0x004a4e40: the animated skater object, the front end's animation resource and the model slot's region (named "GENERIC").
void PlayerSelect::view_create_object(std::uint32_t self) {
    if (word_at(m_, skater::row(sk_.current_character()) + skater::kRowUnlocked) == 0) {
        check(false, kMsgUnavailable);
        sk_.select_current(0);
    }
    const std::uint32_t storage = engine_value(0x0047fd30, {0x168});
    std::uint32_t object = 0;
    if (storage != 0) {
        object = engine_value(0x00480610, {storage});
    }
    player_select_view_view_at(m_, self).set_field_ac(static_cast<Word>(object));
    player_select_view_view_at(m_, self).set_field_894(static_cast<Word>(engine_value(0x004b46a0, {0x00525ffc, 0}, m_.cstring(0x00525ffc))));
    for (std::uint32_t at = 4; at < 0x14; at += 4) {
        if (word_at(m_, player_select_view_view_at(m_, self).field_1378() + at) != 0) {
            const std::uint32_t region = engine_value(0x004203c0, {word_at(m_, player_select_view_view_at(m_, self).field_1378() + at)});
            engine(0x00452390, {region, player_select_view_view_at(m_, self).field_894(), 1});
        }
    }
    // the manifest loader writes the part count through its fourth argument (the stub's poke) and answers the block the destructor frees
    const PartManifest manifest = load_part_manifest(0x00525ffc, self + kManifest, self + kManifestFlag);
    player_select_view_view_at(m_, self).set_field_920(static_cast<Word>(manifest.count));
    player_select_view_view_at(m_, self).set_field_1374(static_cast<Word>(manifest.block));
    const std::uint32_t slot = engine_value(0x004b4640);
    set_word_at(m_, self + kModelSlot, slot);
    const std::string generic = m_.cstring(0x00537218, 0x10);
    std::vector<std::uint8_t> name(generic.begin(), generic.end());
    name.push_back(0);
    m_.write(kRegionNames + slot * kRegionStride, name);
    const std::uint32_t anim = player_select_view_view_at(m_, self).field_894();
    set_word_at(m_, kRegionTables + 4 + slot * kRegionStride, word_at(m_, kRegionTables + 4 + anim * kRegionStride));
    set_word_at(m_, kRegionTables + slot * kRegionStride, word_at(m_, kRegionTables + anim * kRegionStride));
    object = player_select_view_view_at(m_, self).field_ac();
    set_word_at(m_, object + 0x40, self + kLights);
    set_word_at(m_, object + 8, 0);
    set_word_at(m_, object + 0xc, 0);
    set_word_at(m_, object + 0x10, 0);
    set_half_at(m_, object + 0x16, global_half(m_, 0x0053a518));
    set_byte_at(m_, object + 0x1f, byte_at(m_, self + kModelSlot));
    set_global_word(m_, kAnimationScale, 0x100);
    set_word_at(m_, player_select_view_view_at(m_, self).field_ac() + 0x108, 0x10000);
    engine(0x00480890, {player_select_view_view_at(m_, self).field_ac(), 0, 1});
    engine(0x0046f480);
}

// 0x004a5080: rewrite a model packet: the colour chunk (if the buffer has one), a terminator, the count words and then the trailing tables the engine finds
// after the packet (counted lists of words, of nine-word and of 0x81-word records, then relative offsets and the texture records, whose data is sized by the
// width, height and colour depth); the offsets are made relative to the new position.
void PlayerSelect::view_copy_packet(std::uint32_t packet) {
    std::uint32_t dest = packet + word_at(m_, packet + 4);
    const std::uint32_t count = word_at(m_, packet + 0xc + word_at(m_, packet + 8) * 0x24);
    std::uint32_t source = engine_value(0x004b20f0, {packet});
    check(int_at(m_, source - 4) == -1, 0x00537270);
    const std::uint32_t chunk = engine_value(0x00418330, {dest});
    auto copy_words = [&](std::uint32_t n) {
        for (; n != 0; --n) {
            set_word_at(m_, dest, word_at(m_, source));
            source += 4;
            dest += 4;
        }
    };
    if (chunk != 0) {
        const std::uint32_t colors = word_at(m_, chunk - 4) >> 2;
        check(colors != 0 && colors < 0x101, 0x0053725c);
        set_word_at(m_, dest, word_at(m_, chunk - 8));
        set_word_at(m_, dest + 4, word_at(m_, chunk - 4));
        dest += 8;
        for (std::uint32_t i = 0; i < colors; ++i) {
            set_word_at(m_, dest, word_at(m_, chunk + 4 * i));
            dest += 4;
        }
    }
    set_word_at(m_, dest, 0xffffffff);
    dest += 4;
    copy_words(count);
    for (std::uint32_t words_per_record : {1u, 9u, 0x81u}) {
        const std::int32_t n = int_at(m_, source);
        set_word_at(m_, dest, word_at(m_, source));
        source += 4;
        dest += 4;
        for (std::int32_t i = 0; i < n; ++i) {
            copy_words(words_per_record);
        }
    }
    const std::int32_t records = int_at(m_, source);
    set_word_at(m_, dest, word_at(m_, source));
    source += 4;
    dest += 4;
    if (records <= 0) {
        return;
    }
    for (std::int32_t i = 0; i < records; ++i) {
        set_word_at(m_, dest, word_at(m_, source) - source + dest);
        source += 4;
        dest += 4;
    }
    for (std::int32_t i = 0; i < records; ++i) {
        set_word_at(m_, dest, word_at(m_, source));
        dest += 4;
        source += 4;
        const std::uint32_t depth = word_at(m_, source);
        check(depth == 0x10 || depth == 0x100, 0x00537244);
        for (int k = 0; k < 3; ++k) {
            set_word_at(m_, dest, word_at(m_, source));
            dest += 4;
            source += 4;
        }
        const std::uint32_t dims = word_at(m_, source);
        set_word_at(m_, dest, dims);
        dest += 4;
        source += 4;
        std::int32_t size = static_cast<std::int32_t>((dims & 0xffff) * (dims >> 16));
        size >>= (depth == 0x10) ? 1 : 0;
        size += 4 - (size & 3);
        const std::int32_t words = ((size + ((size >> 31) & 3)) >> 2) - 1;
        for (std::int32_t w = 0; w < words; ++w) {
            set_word_at(m_, dest, word_at(m_, source));
            dest += 4;
            source += 4;
        }
    }
}

}  // namespace opentony::runtime::frontend
