#pragma once

#include "frontend_text.hpp"
#include "guest_view.hpp"
#include "frontend_window.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

// Phase 4.10c: the menu widget toolkit (menu.cpp, 0x0046f680..0x00473500), from the disassembly. Every pad-driven frontend list
// (options, trick list, card screens, the name entry) is built from these objects. The classes, all with the vtable pointer at +0:
//
//   MenuProperties (0x17c bytes, vtable 0x00518e50): the style a menu item draws with. +0x04 an embedded Font object (the
//     font code of 4.10a works on it), +0x08 the font's justification, +0x0c, +0x18/+0x1c/+0x20 the font's colour, +0x34 the font's
//     scale, +0x150 normal and +0x154 small scale, +0x158..+0x160 highlighted colour, +0x164..+0x16c normal colour,
//     +0x170..+0x178 disabled colour.
//   MenuControl (vtable 0x00518e10 / 0x00518e30): +0x04 word, +0x08 id, +0x0c "can be selected" byte, +0x0d byte, +0x0e "highlighted"
//     byte, +0x10 value, +0x14 width, +0x18 height, +0x1c x, +0x20 y, +0x24 pulse timer, +0x28 pulse flag, +0x2c justification
//     (0 left, 1 centred), +0x30 text (0x20 bytes), +0x50 its MenuProperties.
//   MenuText (0x00518e54): one line of text. MenuRow (0x00518e78): up to three text columns (+0x54 + 0x20 * column the text,
//     +0xd4 + 4 * column the x offset, +0xe8 + 4 * column its MenuProperties, +0xe4 the column count, +0xf8 / +0xfc the arrows).
//   MenuDots (0x00518ea0): a row of 0x0c-pixel dots drawn from a sprite (+4 value, +8 minimum, +0xc maximum, +0x10 sprite, +0x14 blink
//     timer). MenuSlider (0x00518ea8): a MenuDots at +0x18 and a MenuText. MenuTextScaled (0x00518ed4): a MenuText whose font scale
//     follows its own value.
//   MenuList (0x00518ef8): up to 0x60 children at +0x14, +0x194 the count, +0x198 the highlighted index, +0x19c..+0x1a8 the
//     rectangle, +0x1ac / +0x1b0 / +0x1b4 / +0x1b8 layout flags, the arrow sprites at +0x08 / +0x0c. ScrollList (0x00518f18): a MenuList
//     that scrolls (+0x1bc a y offset for each child, +0x340 / +0x344 the visible window).
//   MenuGrid (0x00518f38): a width x height table of children (+0x0c the cell array, +0x10 / +0x14 the highlighted cell).
//   NameEntry (0x00518f40): the on-screen keyboard window. MenuWrapper (0x00518f60): a list built from an id / text table.
//
// Virtual calls on a child or on the object itself go through its vtable; the port reports each one as a service call carrying the
// object first, named after the family and the slot ("item_v1" is slot 1 of a menu item). A call into a font or a sprite uses
// the names the window base of 4.10a already has ("font_draw", "sprite_draw", "virtual_delete").
constexpr std::uint32_t kMenuVtControl = 0x00518e10;
constexpr std::uint32_t kMenuVtControlB = 0x00518e30;
constexpr std::uint32_t kMenuVtProperties = 0x00518e50;
constexpr std::uint32_t kMenuVtText = 0x00518e54;
constexpr std::uint32_t kMenuVtRow = 0x00518e78;
constexpr std::uint32_t kMenuVtDots = 0x00518ea0;
constexpr std::uint32_t kMenuVtSlider = 0x00518ea8;
constexpr std::uint32_t kMenuVtSliderDots = 0x00518eb0;
constexpr std::uint32_t kMenuVtTextScaled = 0x00518ed4;
constexpr std::uint32_t kMenuVtList = 0x00518ef8;
constexpr std::uint32_t kMenuVtScrollList = 0x00518f18;
constexpr std::uint32_t kMenuVtGrid = 0x00518f38;
constexpr std::uint32_t kMenuVtNameEntry = 0x00518f40;
constexpr std::uint32_t kMenuVtWrapper = 0x00518f60;
constexpr std::uint32_t kMenuVtWrapperSub = 0x00518f70;

// Retail globals the toolkit reads
constexpr std::uint32_t kScreenFillHeight = 0x00527d54;   // the height every full-screen fill uses
constexpr std::uint32_t kSoftwareRenderer = 0x029da3a0;   // non-zero when the software renderer is active (D3DSoftwareRenderer)
constexpr std::uint32_t kSimClock = 0x0056e320;
constexpr std::uint32_t kPadPressA = 0x0056aff9;          // pad 0, the "back" edge; the pad record is 0x16c bytes
constexpr std::uint32_t kPadPressB = 0x0056b029;          // pad 0, the "confirm" edge
constexpr std::uint32_t kPadPressC = 0x0056b0d9;          // pad 0, a third edge
constexpr std::uint32_t kPadStride = 0x16c;
constexpr std::uint32_t kEmptyText = 0x0055fe10;

// Assertion messages (addresses of the original's format strings)
constexpr std::uint32_t kMenuMsgStringTooLongForMenu = 0x00534c44;
constexpr std::uint32_t kMenuMsgNoProperties = 0x00534c34;
constexpr std::uint32_t kMenuMsgScaleOutOfRange = 0x00534c7c;
constexpr std::uint32_t kMenuMsgBadColumnCount = 0x00534c90;
constexpr std::uint32_t kMenuMsgInvalidColumn = 0x00534cb4;
constexpr std::uint32_t kMenuMsgStringTooLong = 0x00534ca4;
constexpr std::uint32_t kMenuMsgNoDotsSprite = 0x00534cc4;
constexpr std::uint32_t kMenuMsgMinOutOfRange = 0x00534cec;
constexpr std::uint32_t kMenuMsgValueOutOfRange = 0x00534cd4;
constexpr std::uint32_t kMenuMsgNoneHighlighted = 0x00534d08;
constexpr std::uint32_t kMenuMsgItemDoesNotExist = 0x00534d24;
constexpr std::uint32_t kMenuMsgFirstItemMissing = 0x00534d44;
constexpr std::uint32_t kMenuMsgOutOfMenuSlots = 0x00534d64;
constexpr std::uint32_t kMenuMsgNoSelectable = 0x00534d78;
constexpr std::uint32_t kMenuMsgJustification = 0x00525744;
constexpr std::uint32_t kMenuMsgNoFont = 0x00534e08;
constexpr std::uint32_t kMenuMsgTooManyChars = 0x00534ddc;
constexpr std::uint32_t kMenuMsgTooManyEntries = 0x00534db8;
constexpr std::uint32_t kMenuMsgNoPropertiesToDelete = 0x00534e34;
constexpr std::uint32_t kMenuMsgNoGridToDelete = 0x00534e1c;
constexpr std::uint32_t kMenuMsgNoName = 0x00534e6c;
constexpr std::uint32_t kMenuMsgNameTooLongFmt = 0x00534e4c;
constexpr std::uint32_t kMenuMsgNameTooLong = 0x00534e74;
constexpr std::uint32_t kMenuMsgBadIndex = 0x00534e9c;
constexpr std::uint32_t kMenuMsgTooManyItems = 0x00534ea8;

// Names of the services and virtual calls the toolkit reaches (RetailServices); see the header comment.
constexpr const char* kSvcFontConstruct = "font_construct";      // 0x00449ca0 (font object)
constexpr const char* kSvcFontDestroy = "font_destroy";          // 0x0044a060 (font object)
constexpr const char* kSvcFontFind = "font_find";                // 0x0044b000 (name) -> font (scripted), text = the name
constexpr const char* kSvcFillRect = "fill_rect";                // 0x004568d0 (x, y, width, height, 0)
constexpr const char* kSvcSelectionBox = "selection_box";        // 0x00456600 (x, y, w, h, r, g, b)
constexpr const char* kSvcFrameBox = "frame_box";                // 0x00456790 (x, y, w, h, r, g, b)
constexpr const char* kSvcSfx = "sfx";                           // SFX_PlayX 0x004ad620 (sound, 0x2000, 0)
constexpr const char* kSvcPrintfLog = "printf_log";              // 0x004cf850 (format)
constexpr const char* kSvcPromptConstruct = "prompt_window_construct";   // 0x004a1b40 (window, file, props, x, y, ?, height) -> window (scripted)
constexpr const char* kSvcPadEdges = "pad_edges";                // 0x0046f550 is native in 4.12; here a seam (a, b, c) -> edge
constexpr const char* kSvcOptionDecrease = "option_decrease";    // 0x004156f0 (option id): lower the option the slider stands for
constexpr const char* kSvcOptionLimit = "option_limit";          // 0x00415820 () -> the highest value the options allow (scripted)
constexpr const char* kSvcOptionCanIncrease = "option_can_increase";   // 0x004155a0 (option id, limit) -> byte (scripted)
constexpr const char* kSvcSliderRefresh = "slider_refresh";     // 0x004dc7b0 (slider): the engine's highlight refresh of a slider
constexpr const char* kSvcTextboxConstruct = "textbox_construct";   // 0x00425a40 (box, font, text, width, height) -> box (scripted)
constexpr const char* kSvcTextboxSetText = "textbox_set_text";      // 0x00425ca0 (box), the text is recorded
constexpr const char* kSvcTextboxUpdate = "textbox_v1";             // the text box's vtable slot 1 (box)
constexpr const char* kSvcAtexit = "atexit";                    // 0x00500260 (function)

// The service name of an engine function the screens reach that has no better name: the address makes it unique.
std::string engine_name(std::uint32_t address);

class MenuToolkit final {
public:
    MenuToolkit(RetailMemory& memory, RetailServices& services, FontTools& fonts, WindowBase& window)
        : m_(memory), s_(services), f_(fonts), w_(window) {}

    // ---- MenuControl and friends (0x0046f680..0x0046f770, 0x00470530, 0x00470540) ----
    std::uint32_t control_construct(std::uint32_t self, std::uint32_t id);            // 0x0046f680
    std::uint32_t control_delete(std::uint32_t self, std::uint32_t flag);             // 0x0046f6a0
    void control_destroy(std::uint32_t self);                                         // 0x0046f6c0
    void control_set_highlighted(std::uint32_t self, std::uint32_t value);            // 0x0046f6d0
    void control_set_value(std::uint32_t self, std::uint32_t value);                  // 0x0046f6e0
    void control_log_navigate(std::uint32_t self);                                    // 0x0046f6f0
    std::uint32_t control_b_construct(std::uint32_t self, std::uint32_t id);          // 0x0046f720
    std::uint32_t control_b_delete(std::uint32_t self, std::uint32_t flag);           // 0x0046f740
    [[nodiscard]] std::uint32_t control_id(std::uint32_t self) const;                 // 0x0046f760

    // ---- MenuProperties ----
    std::uint32_t properties_construct(std::uint32_t self);                           // 0x0046f770
    std::uint32_t properties_delete(std::uint32_t self, std::uint32_t flag);          // 0x0046f820
    void properties_destroy(std::uint32_t self);                                      // 0x0046f840

    // ---- MenuText / MenuRow ----
    std::uint32_t text_construct(std::uint32_t self, std::uint32_t id, std::uint32_t text, std::uint32_t props);   // 0x0046f890
    // The same with the text given as a value (the name entry builds its one-character labels on the stack).
    std::uint32_t text_construct_from(std::uint32_t self, std::uint32_t id, const std::string& text, std::uint32_t props);
    std::uint32_t text_delete(std::uint32_t self, std::uint32_t flag);                // 0x0046f910
    void text_destroy(std::uint32_t self);                                            // 0x0046f930
    void item_set_position(std::uint32_t self, std::int32_t x, std::int32_t y);       // 0x0046f940
    void text_set_text(std::uint32_t self, std::uint32_t text);                       // 0x0046f960
    void text_draw(std::uint32_t self);                                               // 0x0046fa60
    std::uint32_t row_construct(std::uint32_t self, std::uint32_t id, std::uint32_t text, std::uint32_t props, std::int32_t columns,
                                std::uint32_t arrow_left, std::uint32_t arrow_right); // 0x0046fc50
    std::uint32_t row_delete(std::uint32_t self, std::uint32_t flag);                 // 0x0046fd70
    void row_set_column(std::uint32_t self, std::uint32_t text, std::int32_t column, std::uint32_t offset, std::uint32_t props);   // 0x0046fd90
    void row_set_column_from(std::uint32_t self, const std::string& text, std::int32_t column, std::uint32_t offset, std::uint32_t props);
    void row_destroy(std::uint32_t self);                                             // 0x0046fe80
    void row_draw(std::uint32_t self);                                                // 0x0046fe90

    // ---- MenuDots / MenuSlider / MenuTextScaled ----
    std::uint32_t dots_construct(std::uint32_t self, std::uint32_t sprite, std::uint32_t value, std::uint32_t maximum);   // 0x004701d0
    std::uint32_t dots_delete(std::uint32_t self, std::uint32_t flag);                // 0x00470280
    void dots_destroy(std::uint32_t self);                                            // 0x004702a0
    void dots_draw(std::uint32_t self, std::int32_t x, std::int32_t y);               // 0x004702b0
    void dots_set_range(std::uint32_t self, std::uint32_t minimum, std::int32_t value);   // 0x00470550
    std::uint32_t slider_construct(std::uint32_t self, std::uint32_t id, std::uint32_t text, std::uint32_t props, std::uint32_t sprite,
                                   std::uint32_t value, std::uint32_t arrow_left, std::uint32_t arrow_right);   // 0x00470630
    std::uint32_t slider_delete(std::uint32_t self, std::uint32_t flag);              // 0x004706c0
    void slider_destroy(std::uint32_t self);                                          // 0x004706e0
    std::uint32_t operator_new_thunk(std::uint32_t size);                             // 0x00470710
    void operator_delete_thunk(std::uint32_t block);                                  // 0x00470720
    void slider_draw(std::uint32_t sub);                                              // 0x00470730 (on the MenuText sub-object, self + 0x18)
    void slider_adjust(std::uint32_t sub, std::uint32_t direction);                   // 0x004707e0
    std::uint32_t slider_sub_delete_thunk(std::uint32_t sub, std::uint32_t flag);     // 0x00473450
    void slider_sub_thunk_b(std::uint32_t sub);                                       // 0x00473460
    std::uint32_t text_scaled_construct(std::uint32_t self, std::uint32_t id, std::uint32_t text, std::uint32_t props, std::uint32_t grow);   // 0x004708d0
    std::uint32_t text_scaled_delete(std::uint32_t self, std::uint32_t flag);         // 0x00470910
    void text_scaled_destroy(std::uint32_t self);                                     // 0x00470930
    void text_scaled_draw(std::uint32_t self);                                        // 0x00470940

    // ---- MenuList ----
    void list_draw_box(std::uint32_t self);                                           // 0x00470a30
    void list_set_arrows(std::uint32_t self, std::uint32_t left, std::uint32_t right, std::uint32_t margin);   // 0x00470a70
    std::uint32_t list_highlighted_id(std::uint32_t self);                            // 0x00470ab0
    void list_highlight(std::uint32_t self, std::uint32_t id);                        // 0x00470b20
    std::uint32_t page_mouse_hook(std::uint32_t self);                                      // 0x004dd200 (slot 7 of the menu pages; no function of its own in the symbol table)
    void list_highlight_first(std::uint32_t self);                                    // 0x00470bd0
    std::uint32_t list_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height,
                                 std::uint32_t centre, std::uint32_t centre_v, std::uint32_t gap, std::uint32_t arg);   // 0x00470c60
    std::uint32_t list_delete(std::uint32_t self, std::uint32_t flag);                // 0x00470cf0
    void list_destroy(std::uint32_t self);                                            // 0x00470d10
    void list_layout(std::uint32_t self);                                             // 0x00470d20
    void list_draw(std::uint32_t self);                                               // 0x00470ec0
    void list_snap_all(std::uint32_t self);                                           // 0x00470f50
    void list_navigate(std::uint32_t self, std::uint32_t direction);                  // 0x00470f80
    void list_add(std::uint32_t self, std::uint32_t item);                            // 0x00471050

    // ---- ScrollList ----
    std::uint32_t scroll_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height,
                                   std::uint32_t centre, std::uint32_t gap, std::uint32_t pad, std::uint32_t arg);   // 0x004710d0
    std::uint32_t scroll_delete(std::uint32_t self, std::uint32_t flag);              // 0x00471180
    void scroll_destroy(std::uint32_t self);                                          // 0x004711a0
    void scroll_layout(std::uint32_t self);                                           // 0x004711b0
    void scroll_draw(std::uint32_t self);                                             // 0x00471370
    void scroll_follow(std::uint32_t self);                                           // 0x00471550
    void scroll_navigate(std::uint32_t self, std::uint32_t direction);                // 0x00471650
    void scroll_add(std::uint32_t self, std::uint32_t item);                          // 0x00471750

    // ---- MenuGrid ----
    std::uint32_t grid_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t columns, std::int32_t rows,
                                 std::uint32_t gap_x, std::uint32_t gap_y, std::uint32_t arg);   // 0x00471800
    std::uint32_t grid_delete(std::uint32_t self, std::uint32_t flag);                // 0x004718c0
    void grid_destroy(std::uint32_t self);                                            // 0x004718e0
    void grid_layout(std::uint32_t self);                                             // 0x00471930
    void grid_draw(std::uint32_t self);                                               // 0x00471b40
    void grid_snap_all(std::uint32_t self);                                           // 0x00471b80
    void grid_navigate(std::uint32_t self, std::uint32_t direction);                  // 0x00471bb0
    void grid_add(std::uint32_t self, std::uint32_t item);                            // 0x00471c80
    std::uint32_t grid_highlighted_id(std::uint32_t self);                            // 0x00471cf0
    void grid_highlight(std::uint32_t self, std::uint32_t id);                        // 0x00471d70

    // ---- NameEntry (the on-screen keyboard) ----
    std::uint32_t name_entry_construct(std::uint32_t self, std::uint32_t font, std::int32_t limit, std::uint32_t x, std::uint32_t y,
                                       std::uint32_t grid_x, std::uint32_t grid_y, std::uint32_t minimum, std::uint32_t with_space,
                                       std::uint32_t with_digits, std::uint32_t wide, std::uint32_t with_prompts);   // 0x00471e30
    std::uint32_t name_entry_delete(std::uint32_t self, std::uint32_t flag);          // 0x004727d0
    void name_entry_destroy(std::uint32_t self);                                      // 0x004727f0
    [[nodiscard]] std::uint32_t name_entry_name(std::uint32_t self) const;            // 0x00472920
    void name_entry_set_name(std::uint32_t self, std::uint32_t name);                 // 0x00472930
    void name_entry_set_prompt(std::uint32_t self, std::uint32_t text, std::uint32_t x, std::uint32_t y);   // 0x00472a70
    void name_entry_draw(std::uint32_t self, std::uint32_t x, std::uint32_t y);       // 0x00472b50
    std::uint32_t name_entry_handle_input(std::uint32_t self, std::uint32_t direction);   // 0x00472cc0

    // ---- MenuWrapper ----
    std::uint32_t wrapper_construct(std::uint32_t self, std::uint32_t texts, std::uint32_t ids, std::int32_t count, std::uint32_t font_name,
                                    std::uint32_t scrolling);                         // 0x00472ef0
    std::uint32_t wrapper_delete(std::uint32_t self, std::uint32_t flag);             // 0x00473240
    void wrapper_destroy_root(std::uint32_t self);                                    // 0x00473260
    std::uint32_t wrapper_sub_delete(std::uint32_t self, std::uint32_t flag);         // 0x00473270
    void wrapper_destroy(std::uint32_t self);                                         // 0x00473290
    void wrapper_test_complete(std::uint32_t self);                                   // 0x00473330
    std::uint32_t wrapper_poll(std::uint32_t self);                                   // 0x00473380
    void nothing();                                                                   // 0x00473220
    std::uint32_t false_result();                                                     // 0x00473230

    // ---- statics (0x00473470..0x004734a0) ----
    void statics_init();                                                              // 0x00473470
    void mess_font_construct();                                                       // 0x00473480
    void mess_font_atexit();                                                          // 0x00473490
    void mess_font_destroy();                                                         // 0x004734a0

    // A virtual call on a menu item / on a list, grid, window or wrapper: an object of one of this toolkit's classes (its vtable pointer
    // says which) runs the method natively, anything else (a fake object of a probe, an engine class) is reported as the service `name`.
    // Phase 4.10d: a class the toolkit does not know (the skater windows' own items and windows) is offered to the extra dispatcher first.
    void vcall_item(const char* name, std::uint32_t object, std::uint32_t slot, std::vector<std::uint32_t> args = {});
    void vcall_list(const char* name, std::uint32_t object, std::uint32_t slot, std::vector<std::uint32_t> args = {});
    using ExtraDispatch = std::function<bool(std::uint32_t object, std::uint32_t slot, const std::vector<std::uint32_t>& args)>;
    void set_extra_dispatch(ExtraDispatch dispatch) { extra_ = std::move(dispatch); }

    // The font copy every menu constructor makes: a Font object's 0x14c-byte state is copied field by field (the original copies +4..+0x4a
    // then the 0x100-byte character table from +0x4b) from `source` to `dest`.
    void copy_font_fields(std::uint32_t dest, std::uint32_t source);

private:
    void check(bool condition, std::uint32_t message) { s_.check(condition, message); }
    // A class the toolkit does not route: reported as the service `name` (a replay's), or with a host attached as the call of the address the object's vtable holds in the slot
    // (`vcall_<address>`, a thiscall with the object in ECX), which the host resolves to the native or retail function there.
    void vcall(const char* name, std::uint32_t slot, std::uint32_t object, std::vector<std::uint32_t> args = {}, std::string text = {}) {
        std::vector<std::uint32_t> all{object};
        all.insert(all.end(), args.begin(), args.end());
        if (s_.live()) {
            char target[24];
            std::snprintf(target, sizeof target, "vcall_%08x", guest::read_scalar<std::uint32_t>(m_, guest::read_scalar<std::uint32_t>(m_, object) + 4 * slot));
            s_.note(target, std::move(all), std::move(text));
            s_.rename_pending(name);
            return;
        }
        s_.note(name, std::move(all), std::move(text));
    }
    void fill_rect(std::int32_t a, std::int32_t b, std::int32_t c, std::uint32_t d) {
        s_.note(kSvcFillRect, {static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(b), static_cast<std::uint32_t>(c), d, 0});
    }
    void sfx(std::uint32_t sound) { s_.note(kSvcSfx, {sound, 0x2000, 0}); }
    void operator_delete(std::uint32_t block) { s_.note(kOperatorDelete, {block}); }
    std::uint32_t operator_new(std::uint32_t size) {
        s_.note(kOperatorNew, {size});
        return static_cast<std::uint32_t>(s_.value(kOperatorNew));
    }
    void copy_string(std::uint32_t dest, std::uint32_t source);
    void write_string(std::uint32_t dest, const std::string& value);
    void text_set_text_from(std::uint32_t self, const std::string& value);
    [[nodiscard]] std::int32_t text_width_of(std::uint32_t props, std::uint32_t text) const { return f_.text_width(props + 4, text); }
    // The highlight scale a pulsing item draws with: the small scale grown toward the normal one by `step` quarters.
    [[nodiscard]] std::int32_t pulse_scale(std::uint32_t props, std::int32_t step);
    // The common colour / scale / justification setup of the item draws.
    void prepare_item_font(std::uint32_t self, std::uint32_t props, bool set_justification);

    RetailMemory& m_;
    RetailServices& s_;
    FontTools& f_;
    WindowBase& w_;
    ExtraDispatch extra_;
};

}  // namespace opentony::runtime::frontend
