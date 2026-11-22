#pragma once

#include "career.hpp"
#include "guest_view.hpp"
#include "screen_kit.hpp"
#include "skater_manager.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opentony::runtime::frontend {

// Phase 4.10f2: the equipment-select screen (equipsel.cpp) and the classes it builds, from the disassembly. The screen is where a player spends career cash on
// the decks (and picks the trucks) of a skater: a window holds the rack of decks with their picture, the stat dots, the cash and the name windows; a
// spool queue loads each deck's texture a few at a time. Like PlayerSelect it works on retail memory (the window base, the menu toolkit, the font code, the
// skater manager and the career records run natively on the same memory); everything else (the engine's sprite, model, spool and render calls, the dialog) is a
// seam reported as a service with the arguments retail passed. Each class's constructor, as another class's initialiser calls it, is a seam of its own.
//
// The player's choice lives in globals: kEqCharacter the character who is buying, kEqPlayer the pad, kEqSet and kEqIndex the highlighted deck (a deck is named by a
// set, one per character, and an index into the set's eight), kEqTruck the truck style, kEqCareer whether buying costs cash. The career record's +8 is the cash and
// its +0x34 a bit per deck index the character owns.
//
// Classes and their vtables:
//   EquipWindow (0x3f4 bytes, 0x00518914): a Window. +0x0ac the page (0 decks, 1 trucks)   +0x0b0 the EquipView   +0x0b4 the NameWindow   +0x0b8 the CashWindow
//     +0x0bc a dialog is open   +0x0c0 the screen is done (byte)   +0x0c4 the dialog (a ConfirmBox)   +0x0c8 and +0x214 two Font objects (s2font2, s2font1; 0x14c bytes)
//     +0x360 the background sprite   +0x364..+0x378 six PromptWindows (select, sponsor, trucks, accept, back, decks)   +0x37c twenty deck-logo sprites (0 when the
//     character's row says it has none). Slot 4 is a no-op; slot 7 the engine's hook.
//   StatDots (0x48, 0x00518934): a ScreenElement. +0x28 four MenuDots (the stat rows)   +0x38 the dots sprite   +0x3c, +0x40, +0x44 -1.
//   CashWindow (0x36c, 0x00518954): a Window. +0x0ac and +0x1f8 two Fonts   +0x344, +0x348 the deck last shown   +0x34c the cash last shown   +0x350 its x   +0x354 shown
//     +0x358 "cash" and +0x35c "cost" captions   +0x360 and +0x364 two ScoreCounters   +0x368 the frame sprite.
//   EquipView (0x50, 0x00518974): a ScreenElement. +0x28 the page   +0x2c the DeckRack   +0x30 the TruckRack   +0x34, +0x38 its size   +0x3c..+0x48 four arrow sprites
//     +0x4c the arrows' blink counter.
//   NameWindow (0x21c, 0x00518994): a Window. +0x0ac the TextFlagElement   +0x0b0 a Font   +0x1fc its x   +0x204, +0x208 the deck shown   +0x20c, +0x210 the deck asked for
//     +0x214 the slide state (0 sliding out, 1 shown, 3 waiting).
//   RackBase (0x18 bytes of state, 0x005189b4): +0x04 (byte) a rack with one set only   +0x06 (byte) active   +0x08 the slide state (0 in, 1 shown, 2 out, 3 hidden)
//     +0x0c the offset (600 off screen)   +0x10 its target. DeckRack (0x6e0, 0x005189c4) and TruckRack (0x2f8, 0x005189d8) derive from it.
//   DeckRack: +0x14 (byte) needs its first update   +0x18, +0x1c, +0x20 the three marker sprites   +0x24 the loaded placeholder picture   +0x28 the slot being turned to
//     +0x2c the slot at the front   +0x30 and +0x34 the target and shown angle   +0x38 the angle between slots   +0x3c the spool region   +0x40 five 0x4c-byte object records
//     +0x1bc the sets (0x5c bytes each: +0 the character, +4 the number of decks, +8 the deck last shown, +0xc the DeckItem pointers)   +0x6c4 the number of sets
//     +0x6c8 and +0x6cc the set shown   +0x6d0 and +0x6d4 the vertical target and position   +0x6d8 the DeckSpool   +0x6dc the deck at the front.
//   TruckRack: +0x14 a Font   +0x160 the truck   +0x164 the truck before   +0x168 the state   +0x16c the height   +0x170 the spin   +0x174 the turn   +0x178 the spool region
//     +0x17c two object records.
//   DeckItem (0x28): +4 the picture data (spooled in)   +8 the file name   +0x18 data held   +0x19 held back   +0x1a wanted   +0x1b not wanted   +0x1c shown   +0x1d replaced
//     +0x20 the default picture   +0x24 the texture set.   DeckSpool (0x18): +0 the number kept   +4 the queue (list nodes of {item, next})   +8 the current node
//     +0xc the first node past the kept ones   +0x10 the buffer being read   +0x14 the state (0 idle, 1 spooling, 2 restart).
constexpr std::uint32_t kVtEquipWindow = 0x00518914;
constexpr std::uint32_t kVtStatDots = 0x00518934;
constexpr std::uint32_t kVtCashWindow = 0x00518954;
constexpr std::uint32_t kVtEquipView = 0x00518974;
constexpr std::uint32_t kVtNameWindow = 0x00518994;
constexpr std::uint32_t kVtRackBase = 0x005189b4;
constexpr std::uint32_t kVtDeckRack = 0x005189c4;
constexpr std::uint32_t kVtTruckRack = 0x005189d8;

class EquipSelect final : public ScreenKit {
public:
    EquipSelect(RetailMemory& memory, RetailServices& services, FontTools& fonts, WindowBase& window, MenuToolkit& menu, skater::SkaterManager& skaters)
        : ScreenKit(memory, services, fonts, window, menu, skaters) {}

    // ---- the screen callbacks and the run loop ----
    void no_op();                                                                                // 0x00444550
    void init_callbacks(std::uint32_t record);                                                   // 0x00444560
    void exit_callback();                                                                        // 0x00444580
    void screen_call(std::uint32_t argument, std::uint32_t result);                              // 0x00444590
    std::int32_t run(std::int32_t mode, std::int32_t player);                                    // 0x004445b0

    // ---- decks: ownership, price, stats ----
    [[nodiscard]] bool colours_match(std::int32_t a, std::int32_t b);                            // 0x00444510
    [[nodiscard]] std::int32_t deck_price(std::int32_t set, std::int32_t index);                 // 0x00444810
    void buy_deck(std::int32_t set, std::int32_t index);                                         // 0x00444880
    [[nodiscard]] bool owns_deck(std::int32_t set, std::int32_t index);                          // 0x004448c0
    [[nodiscard]] bool is_current_character(std::int32_t set);                                   // 0x004448f0
    [[nodiscard]] bool can_afford(std::int32_t set, std::int32_t index);                         // 0x00444900
    std::uint32_t alloc_zeroed(std::uint32_t size);                                              // 0x00444940
    void free_block(std::uint32_t block);                                                        // 0x00444970
    [[nodiscard]] std::int32_t deck_stat(std::uint32_t kind, std::int32_t set, std::int32_t index);   // 0x00445810
    [[nodiscard]] std::int32_t count_owned_decks();                                              // 0x004458b0
    void character_short_name(std::int32_t character, std::uint32_t out);                       // 0x004468a0

    // ---- the window ----
    std::uint32_t window_construct(std::uint32_t self);                                          // 0x00444980
    std::uint32_t window_delete(std::uint32_t self, std::uint32_t flag);                         // 0x00444ff0
    void window_destroy(std::uint32_t self);                                                     // 0x00445010
    void window_draw(std::uint32_t self);                                                        // 0x004450a0
    void window_update(std::uint32_t self);                                                      // 0x004450e0
    void window_slot4();                                                                         // 0x00445540
    [[nodiscard]] bool window_step(std::uint32_t self, std::uint32_t result);                    // 0x00445550

    // ---- the stat dots ----
    std::uint32_t dots_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y);   // 0x004455c0
    std::uint32_t dots_delete(std::uint32_t self, std::uint32_t flag);                           // 0x004456a0
    void dots_destroy(std::uint32_t self);                                                       // 0x004456c0
    void dots_draw(std::uint32_t self, std::int32_t x, std::int32_t y);                          // 0x00445740
    void dots_update(std::uint32_t self);                                                        // 0x00445780

    // ---- the cash window ----
    std::uint32_t cash_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y);   // 0x00445920
    std::uint32_t cash_delete(std::uint32_t self, std::uint32_t flag);                           // 0x00445cf0
    void cash_destroy(std::uint32_t self);                                                       // 0x00445d10
    void cash_update(std::uint32_t self);                                                        // 0x00445d80
    void cash_set_visible(std::uint32_t self, std::uint32_t visible);                            // 0x00445e70

    // ---- the view: the page of racks ----
    std::uint32_t view_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y);   // 0x00445ea0
    std::uint32_t view_delete(std::uint32_t self, std::uint32_t flag);                           // 0x004460a0
    void view_destroy(std::uint32_t self);                                                       // 0x004460c0
    void view_update(std::uint32_t self);                                                        // 0x00446150
    void view_draw(std::uint32_t self, std::int32_t x, std::int32_t y);                          // 0x00446180
    void view_move(std::uint32_t self, std::uint32_t direction);                                 // 0x00446420
    void view_set_page(std::uint32_t self, std::uint32_t page);                                  // 0x00446440
    void view_clear_flag(std::uint32_t self, std::int32_t set, std::int32_t index);              // 0x00446470

    // ---- the name window ----
    std::uint32_t name_construct(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t start_x, std::int32_t start_y);   // 0x00446490
    std::uint32_t name_delete(std::uint32_t self, std::uint32_t flag);                           // 0x00446650
    void name_destroy(std::uint32_t self);                                                       // 0x00446670
    void name_update(std::uint32_t self, std::int32_t set, std::int32_t index);                  // 0x004466d0

    // ---- the racks ----
    void rack_init(std::uint32_t self);                                                          // 0x004468e0
    void rack_slot2();                                                                           // 0x00446900
    void rack_slot3();                                                                           // 0x00446910
    std::uint32_t rack_delete(std::uint32_t self, std::uint32_t flag);                           // 0x00446920
    void rack_destroy(std::uint32_t self);                                                       // 0x00446940
    void rack_set_active(std::uint32_t self, std::uint32_t active);                              // 0x00446950
    void rack_step(std::uint32_t self);                                                          // 0x00446990

    std::uint32_t deck_rack_construct(std::uint32_t self);                                       // 0x004469f0
    std::uint32_t deck_rack_delete(std::uint32_t self, std::uint32_t flag);                      // 0x00446c20
    void deck_rack_destroy(std::uint32_t self);                                                  // 0x00446c40
    void deck_rack_build_sets(std::uint32_t self);                                               // 0x00446d80
    void deck_rack_update(std::uint32_t self);                                                   // 0x00446fb0
    void deck_rack_draw(std::uint32_t self, std::uint32_t x, std::uint32_t y);                   // 0x00447340
    void deck_rack_move(std::uint32_t self, std::uint32_t direction);                            // 0x00447440
    void deck_rack_turn(std::uint32_t self, std::uint32_t direction);                            // 0x004474b0
    void deck_rack_scroll(std::uint32_t self, std::int32_t step);                                // 0x00447730
    void deck_rack_clear_flag(std::uint32_t self, std::int32_t set, std::int32_t index);         // 0x00447a90

    std::uint32_t truck_construct(std::uint32_t self);                                           // 0x00448230
    std::uint32_t truck_delete(std::uint32_t self, std::uint32_t flag);                          // 0x00448480
    void truck_destroy(std::uint32_t self);                                                      // 0x004484a0
    void truck_update(std::uint32_t self);                                                       // 0x00448540
    void truck_draw(std::uint32_t self, std::uint32_t x, std::uint32_t y);                       // 0x00448740
    void truck_move(std::uint32_t self, std::uint32_t direction);                                // 0x00448790

    // ---- a deck's picture and the spool queue ----
    std::uint32_t item_construct(std::uint32_t self, std::uint32_t name, std::uint32_t texture_set, std::uint32_t held);   // 0x00447ae0
    void item_free_thunk(std::uint32_t self);                                                    // 0x00447b70
    void item_set_data(std::uint32_t self, std::uint32_t data);                                  // 0x00447b80
    void item_free(std::uint32_t self);                                                          // 0x00447be0
    void item_set_wanted(std::uint32_t self, std::uint32_t wanted, std::int32_t texture_set);    // 0x00447c00
    void item_update(std::uint32_t self);                                                        // 0x00447c20
    [[nodiscard]] bool item_priority_le(std::uint32_t self, std::uint32_t other);                // 0x00447cf0
    std::uint32_t spool_construct(std::uint32_t self, std::uint32_t kept);                       // 0x00447d10
    void spool_reset(std::uint32_t self);                                                        // 0x00447d30
    void spool_clear_queue(std::uint32_t self);                                                  // 0x00447d70
    void spool_insert(std::uint32_t self, std::uint32_t item);                                   // 0x00447e00
    void spool_begin(std::uint32_t self);                                                        // 0x00447e80
    void spool_start_next(std::uint32_t self);                                                   // 0x00447f40
    [[nodiscard]] bool spool_poll(std::uint32_t self);                                           // 0x00448110
    void spool_verify(std::uint32_t self, std::uint32_t data);                                   // 0x004481c0

    // The virtual methods of the window-base classes and of this screen's own classes, run natively (see PlayerSelect::dispatch).
    bool dispatch(std::uint32_t object, std::uint32_t slot, const std::vector<std::uint32_t>& args);
    bool dispatch_named(const char* name, std::uint32_t object, const std::vector<std::uint32_t>& args);

private:
    // The line and file an assertion records are retail's globals (0x00568638, 0x0056881c): only a failure is reported, so the checks pass the message.
    void check_at(bool condition, std::uint32_t message) { check(condition, message); }
    // sprintf into the screens' stack buffer
    std::uint32_t sprite_object(std::uint32_t memory, std::uint32_t file);
    std::uint32_t text_pointer(std::uint32_t variable) { return guest::read_scalar<std::uint32_t>(m_, variable); }
    // A virtual call of slot 0 (the deleting destructor, flag 1) on an owned child
    void delete_child(std::uint32_t child) {
        if (child != 0) {
            delete_object(child);
        }
    }
    // Copy the font the engine finds by name into a window's Font object (the construct-and-copy every class of this screen starts with)
    void font_from(std::uint32_t dest, std::uint32_t name) { copy_font_named(dest, name); }
};

}  // namespace opentony::runtime::frontend
