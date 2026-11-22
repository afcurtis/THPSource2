#pragma once

#include "create_color.hpp"
#include "frontend_text.hpp"
#include "frontend_window.hpp"
#include "menu_toolkit.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

// Phase 4.10d: the helpers the create-a-skater windows share (commonfe.cpp, 0x00418200..0x0041945f), from the disassembly. They are the
// widget classes the appearance, personal-information and stat windows are built from, on top of the menu toolkit of 4.10c:
//
//   ColourText (0x58-0x7b bytes, vtable 0x00518528): a MenuText that also shows a colour swatch. +0x54..+0x56 the swatch colour (red, green,
//     blue bytes), +0x58 the selected preset index, +0x5c the number of presets, +0x60 the preset table (rows of three words: hue,
//     saturation, value), +0x64 / +0x68 / +0x6c the custom colour (hue, saturation factor, value factor) shown for preset 0, +0x70 / +0x74 the
//     left and right arrow sprites, +0x78 the arrow blink timer (30 frames).
//   StepperRow (0x100 bytes, vtable 0x0051854c): a MenuControlB showing up to a few words in a row, one of which is lit. +0xf4 the word count,
//     +0xfc its MenuProperties, +0x31 + 0x20 * i the words, +0x10 the lit word.
//   NumberRow (0x10c bytes, vtable 0x0051856c): a two-column MenuRow whose second column is a number kept between a minimum and a maximum.
//     +0x100 the value, +0x104 the minimum, +0x108 the maximum, +0xd8 / +0xec the second column's offset and style.
//   ModelView (0x40 bytes, vtable 0x00518594): a ScreenElement that renders one PSX model: +0x2c a static object record, +0x30 an animated
//     one (0x168 bytes), +0x34 the PSX region the model came from, +0x38 / +0x3c the view position, +0x28 the turn per frame (0x3c).
//   InitialsWindow (vtable 0x005185b4): a Window with three one-letter text elements at +0xac, +0xb0, +0xb4.
//
// Services (RetailServices): the engine functions the helpers reach are reported under engine_XXXXXXXX names (the address), with the arguments
// retail passed; the swatch maths calls the colour code of 4.9 natively.
constexpr std::uint32_t kVtColourText = 0x00518528;
constexpr std::uint32_t kVtStepperRow = 0x0051854c;
constexpr std::uint32_t kVtNumberRow = 0x0051856c;
constexpr std::uint32_t kVtModelView = 0x00518594;
constexpr std::uint32_t kVtInitialsWindow = 0x005185b4;

constexpr std::uint32_t kMsgNoLight = 0x00525690;
constexpr std::uint32_t kMsgModelIndexRange = 0x005256bc;
constexpr std::uint32_t kMsgNoChecksum = 0x005256e0;
constexpr std::uint32_t kMsgNoPointerHue = 0x00525718;
constexpr std::uint32_t kMsgNoPointerSat = 0x00525708;
constexpr std::uint32_t kMsgNoPointerVal = 0x005256f8;
constexpr std::uint32_t kMsgValueRange = 0x00525728;
constexpr std::uint32_t kMsgNoFontName = 0x0052576c;
constexpr std::uint32_t kMsgFinalCount = 0x00525784;

class SkaterWindowHelpers final {
public:
    SkaterWindowHelpers(RetailMemory& memory, RetailServices& services, FontTools& fonts, WindowBase& window, MenuToolkit& menu,
                        create::CreateColor& colour)
        : m_(memory), s_(services), f_(fonts), w_(window), menu_(menu), colour_(colour) {}

    // ---- plain helpers ----
    void light_defaults(std::uint32_t light);                                                    // 0x00418200
    void colour_indices(std::uint32_t a, std::uint32_t b, std::uint32_t out_a, std::uint32_t out_b);   // 0x004182e0
    [[nodiscard]] std::uint32_t find_rgbs_chunk(std::uint32_t chunks);                           // 0x00418330
    [[nodiscard]] std::uint32_t busiest_material(std::int32_t character, std::uint32_t entry);   // 0x00418360
    void load_face_bitmap(std::uint32_t bitmap, std::uint32_t a, std::uint32_t b);               // 0x00418480
    [[nodiscard]] bool in_replay_career();                                                       // 0x00419420

    // ---- ColourText ----
    std::uint32_t colour_text_construct(std::uint32_t self, std::uint32_t id, std::uint32_t text, std::uint32_t props, std::uint32_t table,
                                        std::uint32_t count, std::uint32_t left, std::uint32_t right);   // 0x00418540
    std::uint32_t colour_text_delete(std::uint32_t self, std::uint32_t flag);                    // 0x004185e0
    void colour_text_destroy(std::uint32_t self);                                                // 0x00418600 (jumps to MenuText_Destroy)
    void colour_text_draw(std::uint32_t self);                                                   // 0x00418610
    void colour_text_set_custom(std::uint32_t self, std::uint32_t hue, std::uint32_t sat, std::uint32_t val);   // 0x004186f0
    void colour_text_swatch(std::uint32_t self, std::uint32_t index, std::uint32_t out_r, std::uint32_t out_g, std::uint32_t out_b);   // 0x00418720
    void colour_text_colour(std::uint32_t self, std::uint32_t out_h, std::uint32_t out_s, std::uint32_t out_v);   // 0x004187d0
    void colour_text_set_index(std::uint32_t self, std::int32_t index);                          // 0x00418900
    [[nodiscard]] std::uint32_t colour_text_index(std::uint32_t self) const;                     // 0x00418970
    void colour_text_step(std::uint32_t self, std::uint32_t direction);                          // 0x00418980

    // ---- StepperRow ----
    std::uint32_t stepper_construct(std::uint32_t self, std::uint32_t id, std::uint32_t props, std::uint32_t words, std::int32_t count,
                                    std::uint32_t lit);                                          // 0x004189c0
    std::uint32_t stepper_delete(std::uint32_t self, std::uint32_t flag);                        // 0x00418b20
    void stepper_destroy(std::uint32_t self);                                                    // 0x00418b50
    void control_destroy_thunk(std::uint32_t self);                                              // 0x00418b40
    void stepper_position(std::uint32_t self, std::int32_t x, std::int32_t y);                   // 0x00418b60
    void stepper_draw(std::uint32_t self);                                                       // 0x00418b80
    void stepper_step(std::uint32_t self, std::uint32_t direction);                              // 0x00418d30

    // ---- NumberRow ----
    std::uint32_t number_construct(std::uint32_t self, std::uint32_t id, std::uint32_t text, std::int32_t value, std::int32_t minimum,
                                   std::int32_t maximum, std::uint32_t offset, std::uint32_t props, std::uint32_t props2, std::uint32_t left,
                                   std::uint32_t right);                                         // 0x00418d80
    std::uint32_t number_delete(std::uint32_t self, std::uint32_t flag);                         // 0x00418e40
    void number_destroy(std::uint32_t self);                                                     // 0x00418e60
    [[nodiscard]] std::uint32_t number_value(std::uint32_t self) const;                          // 0x00418e70
    void number_set_value(std::uint32_t self, std::int32_t value);                               // 0x00418e80
    void number_step(std::uint32_t self, std::uint32_t direction);                               // 0x00418f20

    // ---- ModelView ----
    std::uint32_t model_view_construct(std::uint32_t self, std::uint32_t name, std::uint32_t view_x, std::uint32_t view_y, std::uint32_t tag,
                                       std::int32_t x, std::int32_t y, std::int32_t z);          // 0x00418fc0
    std::uint32_t model_view_delete(std::uint32_t self, std::uint32_t flag);                     // 0x00419140
    void model_view_destroy(std::uint32_t self);                                                 // 0x00419160
    void model_view_update(std::uint32_t self);                                                  // 0x004191f0
    void model_view_draw(std::uint32_t self);                                                    // 0x00419220

    // ---- InitialsWindow ----
    std::uint32_t initials_construct(std::uint32_t self, std::uint32_t font, std::int32_t x, std::int32_t y, std::int32_t start_x,
                                     std::int32_t start_y);                                      // 0x00419290
    std::uint32_t initials_delete(std::uint32_t self, std::uint32_t flag);                       // 0x00419390
    void initials_destroy(std::uint32_t self);                                                   // 0x004193b0
    void initials_set_letters(std::uint32_t self, std::uint32_t a, std::uint32_t b, std::uint32_t c);   // 0x004193c0

    // Virtual dispatch for objects of these classes (the toolkit's extra dispatcher): returns false for any other object.
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
    void sprintf_number(std::uint32_t self, std::int32_t value);
    void refresh_number(std::uint32_t self);

    RetailMemory& m_;
    RetailServices& s_;
    FontTools& f_;
    WindowBase& w_;
    MenuToolkit& menu_;
    create::CreateColor& colour_;
};

}  // namespace opentony::runtime::frontend
