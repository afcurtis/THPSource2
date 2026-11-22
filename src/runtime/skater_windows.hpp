#pragma once

#include "career.hpp"
#include "create_color.hpp"
#include "create_palette.hpp"
#include "create_textures.hpp"
#include "frontend_text.hpp"
#include "frontend_window.hpp"
#include "menu_toolkit.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"
#include "skater_manager.hpp"
#include "skater_window_helpers.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

// Phase 4.10d: the create-a-skater editor's pages that work on a custom skater record (perwin.cpp, appwin.cpp, statwin.cpp), from the
// disassembly. The editor loop (4.10e) builds each page, polls it every frame (the result tells it to stay, to go on or to go back), updates
// and draws it. The pages are Windows of the 4.10a base holding items of the 4.10c menu toolkit and of the commonfe helpers.
//
// The custom skater record (0x80 bytes at 0x0056a488 + slot * 0x80, slot = SkaterManager_CurrentCustomSlot):
//   +0x00 name (0x10)   +0x10 hometown (0x10)   +0x20 age   +0x23 height (0x46..0x91, in steps of 5)   +0x24 stance (0..2)   +0x25 style (0..1)
//   +0x26 valid   +0x27..+0x2a four colour indices (the ColourText items)   +0x54 gender (1: halve the displayed weight)   +0x58
//   +0x64 + 4 * i seven texture-set ids: skin tone, shirt, pants, ... (the appearance page's +0xb4..+0xcc)
//
// PersonalWindow (vtable 0x005187c8, built by creedit.cpp): +0x14 hidden, +0x1fc the list, +0x200..+0x21c the rows, +0x208 the age row (a NumberRow),
//   +0x384 the font object (also the style of the rows' second column), +0x388 / +0x38c the arrow sprites, +0x390 the NameEntry, +0x394 the state
//   (0 browsing the list, 1 typing a name), +0x398..+0x3a8 five marker items (stance 0..2, style 0..1), +0xac the "name matched a preset" byte.
// AppearanceWindow (0x58 words, vtable 0x005187e8): +0xac the current texture-set id, +0xb0 the skin tone, +0xb4..+0xcc the seven record ids,
//   +0xd0 a ScrollList, +0xd4 + 4 * i up to 0x60 items, +0x254 / +0x258 two MenuProperties (the fonts s2menu1 and s2skata2), +0x25c / +0x260 two
//   arrow sprites, +0x264 the editor object that owns the skater's palettes (create_palette.hpp).
// StatWindow (built by statedit): +0x490 the list of ten sliders, +0x494 + 4 * i the sliders' sub-objects (the dots at -0x18), +0x61c a
//   StatPoints block, +0x620 a confirmation dialog, +0x624 what to return when it is confirmed.
// StatPoints (0x6d0 bytes, vtable 0x0051975c): a Window showing the points left to spend. +0xbc the ten stat bytes being edited, +0xc8 the
//   character's points, +0xcc the formatted text.
constexpr std::uint32_t kPersonalVt = 0x005187c8;
constexpr std::uint32_t kAppearanceVt = 0x005187e8;
constexpr std::uint32_t kStatPointsVt = 0x0051975c;
constexpr std::uint32_t kStatWindowVt = 0x0051973c;

constexpr std::uint32_t kCustomRecords = 0x0056a488;
constexpr std::uint32_t kCustomStride = 0x80;
constexpr std::uint32_t kPadBase = 0x0056aff9;          // pad record 0: back edge at +0 (0x16c per pad); confirm edge 0x0056b029 (+0x30)
constexpr std::uint32_t kPadConfirm = 0x0056b029;
constexpr std::uint32_t kPadBack = 0x0056aff9;
constexpr std::uint32_t kPadUpDown = 0x0056b099;
constexpr std::uint32_t kPadUpDown2 = 0x0056b0a9;
constexpr std::uint32_t kCurrentPad = 0x0056a888;       // the pad the editor listens to
constexpr std::uint32_t kTextureManagerPointer = 0x0056a87c;
constexpr std::uint32_t kPresetNameTable = 0x00526bb0;  // 0x80-byte rows: name, hometown, then the preset's fields
constexpr std::uint32_t kPresetsEnabled = 0x0056a904;

constexpr std::uint32_t kPersonalMsgState = 0x005278d4;
constexpr std::uint32_t kPersonalMsgNoEntry = 0x00527a04;
constexpr std::uint32_t kPersonalMsgBadStance = 0x00527a38;
constexpr std::uint32_t kPersonalMsgBadStyle = 0x00527a44;
constexpr std::uint32_t kPersonalMsgHometown = 0x00527a50;
constexpr std::uint32_t kPersonalMsgName = 0x00527a68;
constexpr std::uint32_t kAppMsgNoTextureSet = 0x00526638;
constexpr std::uint32_t kAppMsgNoTexture = 0x00526624;
constexpr std::uint32_t kAppMsgNoFilename = 0x00526610;
constexpr std::uint32_t kAppMsgNoSkintone = 0x00527a7c;
constexpr std::uint32_t kAppMsgNoSkater = 0x00526668;
constexpr std::uint32_t kAppMsgNoCamera = 0x00527abc;
constexpr std::uint32_t kAppMsgNoViewport = 0x00527ab0;
constexpr std::uint32_t kAppMsgNoTexturePart = 0x00527aec;
constexpr std::uint32_t kAppMsgNoTextureSetShort = 0x00527adc;
constexpr std::uint32_t kStatMsgDialog = 0x00539d1c;
constexpr std::uint32_t kStatMsgNoFont = 0x00539d70;
constexpr std::uint32_t kStatMsgPointsDecreased = 0x00539d7c;

class SkaterWindows final {
public:
    SkaterWindows(RetailMemory& memory, RetailServices& services, FontTools& fonts, WindowBase& window, MenuToolkit& menu,
                  SkaterWindowHelpers& helpers, skater::SkaterManager& skaters, create::TextureSets& textures, create::CreatePalette& palette,
                  career::Career& career)
        : m_(memory), s_(services), f_(fonts), w_(window), menu_(menu), h_(helpers), sk_(skaters), tex_(textures), pal_(palette), career_(career) {}

    // ---- personal information page (perwin.cpp) ----
    std::uint32_t personal_poll(std::uint32_t self);                                  // 0x004293b0
    void personal_update(std::uint32_t self);                                         // 0x00429680
    void personal_draw(std::uint32_t self, std::int32_t x, std::int32_t y);           // 0x00429910
    void personal_highlight_first(std::uint32_t self);                                // 0x00429cd0
    [[nodiscard]] bool personal_is_typing(std::uint32_t self) const;                  // 0x00429ce0
    [[nodiscard]] bool personal_apply_preset_name(std::uint32_t self);                // 0x00429cf0

    // ---- appearance page (appwin.cpp) ----
    std::uint32_t appearance_construct(std::uint32_t self, std::uint32_t skater, std::int32_t x, std::int32_t y);   // 0x00429e00
    std::uint32_t appearance_delete(std::uint32_t self, std::uint32_t flag);          // 0x0042a0f0
    void appearance_build(std::uint32_t self);                                        // 0x0042a110
    void appearance_destroy(std::uint32_t self);                                      // 0x0042a8c0
    std::uint32_t appearance_poll(std::uint32_t self);                                // 0x0042a9b0
    void appearance_update(std::uint32_t self);                                       // 0x0042aab0
    void appearance_camera(std::uint32_t self, std::uint32_t camera, std::uint32_t viewport);   // 0x0042ae30
    void appearance_draw(std::uint32_t self, std::int32_t x, std::int32_t y);         // 0x0042b070
    void appearance_refresh(std::uint32_t self);                                      // 0x0042b4d0
    void appearance_read_colours(std::uint32_t self);                                 // 0x0042b880
    [[nodiscard]] bool appearance_part_visible(std::uint32_t self, std::uint32_t part);   // 0x0042b910
    [[nodiscard]] bool appearance_texture_flag(std::uint32_t self, std::uint32_t part);   // 0x0042b960
    [[nodiscard]] bool appearance_load_preset(std::uint32_t self);                    // 0x0042ba00
    void appearance_dump(std::uint32_t self, std::uint32_t record);                   // 0x0042bcf0

    // ---- stat page (statwin.cpp) ----
    std::uint32_t stat_poll(std::uint32_t self);                                      // 0x004b6180
    void stat_draw(std::uint32_t self, std::int32_t x, std::int32_t y);               // 0x004b6490
    void stat_update(std::uint32_t self);                                             // 0x004b64e0
    void stat_build(std::uint32_t self);                                              // 0x004b6560
    void stat_highlight_first(std::uint32_t self);                                    // 0x004b6bf0
    void stat_reset_sliders(std::uint32_t self, std::int32_t character);              // 0x004b6c00
    void stat_apply(std::uint32_t self);                                              // 0x004b6d20
    std::uint32_t points_construct(std::uint32_t self, std::uint32_t font, std::uint32_t font2, std::int32_t x, std::int32_t y,
                                   std::int32_t start_x, std::int32_t start_y);       // 0x004b6d90
    std::uint32_t points_delete(std::uint32_t self, std::uint32_t flag);              // 0x004b7040
    void points_set_character(std::uint32_t self, std::int32_t character);            // 0x004b7060
    void points_destroy(std::uint32_t self);                                          // 0x004b70a0
    void points_update(std::uint32_t self);                                           // 0x004b70b0
    void points_adjust(std::uint32_t self, std::int32_t character);                   // 0x004b71c0
    [[nodiscard]] bool points_changed(std::uint32_t self);                            // 0x004b7290
    [[nodiscard]] std::uint32_t points_text(std::uint32_t self);                      // 0x004b7320

    bool dispatch(std::uint32_t object, std::uint32_t slot, const std::vector<std::uint32_t>& args);

private:
    void check(bool condition, std::uint32_t message) { s_.check(condition, message); }
    void engine(std::uint32_t address, std::vector<std::uint32_t> args = {}, std::string text = {}) {
        s_.note(engine_name(address), std::move(args), std::move(text));
    }
    std::uint32_t engine_value(std::uint32_t address, std::vector<std::uint32_t> args = {}, std::string text = {}) {
        engine(address, std::move(args), std::move(text));
        return static_cast<std::uint32_t>(s_.value(engine_name(address)));
    }
    std::uint32_t operator_new(std::uint32_t size) {
        s_.note(kOperatorNew, {size});
        return static_cast<std::uint32_t>(s_.value(kOperatorNew));
    }
    void operator_delete(std::uint32_t block) { s_.note(kOperatorDelete, {block}); }
    void sfx(std::uint32_t sound) { s_.note(kSvcSfx, {sound, 0x2000, 0}); }
    [[nodiscard]] std::uint32_t pad_edges(std::uint32_t pad, std::uint32_t b, std::uint32_t c) {
        s_.note(kSvcPadEdges, {pad, b, c});
        return static_cast<std::uint32_t>(s_.value(kSvcPadEdges));
    }
    [[nodiscard]] std::uint32_t custom_slot() { return static_cast<std::uint32_t>(sk_.current_custom_slot()); }
    [[nodiscard]] std::uint32_t record(std::uint32_t slot) const { return kCustomRecords + slot * kCustomStride; }
    void copy_text(std::uint32_t dest, const std::string& value);
    // 0x004c9f50: are two names equal ignoring case, at most `limit` characters (both null: equal)
    [[nodiscard]] bool names_equal(std::uint32_t a, std::uint32_t b, std::uint32_t limit) const;
    // A virtual call on a list the page owns (a MenuList or ScrollList: native, anything else a service) and on an item.
    void vcall_list(const char* name, std::uint32_t object, std::uint32_t slot, std::vector<std::uint32_t> args = {}) {
        menu_.vcall_list(name, object, slot, std::move(args));
    }
    void vcall_item(const char* name, std::uint32_t object, std::uint32_t slot, std::vector<std::uint32_t> args = {}) {
        menu_.vcall_item(name, object, slot, std::move(args));
    }
    void set_column(std::uint32_t row, const std::string& text, std::uint32_t offset, std::uint32_t props) { set_column_at(row, text, 1, offset, props); }
    void set_column_at(std::uint32_t row, const std::string& text, std::int32_t column, std::uint32_t offset, std::uint32_t props);
    std::uint32_t new_properties(std::uint32_t font_name, std::uint32_t justification, std::uint32_t vertical, std::uint32_t scale);
    std::uint32_t body_group_apply(std::uint32_t self, std::uint32_t group, std::uint32_t id, std::uint32_t tone);
    [[nodiscard]] static std::string format(const std::string& fmt, const std::vector<std::string>& strings, const std::vector<std::int32_t>& numbers);

    RetailMemory& m_;
    RetailServices& s_;
    FontTools& f_;
    WindowBase& w_;
    MenuToolkit& menu_;
    SkaterWindowHelpers& h_;
    skater::SkaterManager& sk_;
    create::TextureSets& tex_;
    create::CreatePalette& pal_;
    career::Career& career_;
};

}  // namespace opentony::runtime::frontend
