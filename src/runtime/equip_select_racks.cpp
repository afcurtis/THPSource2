#include "equip_select.hpp"
#include "equip_views.hpp"
#include "seam_addresses_generated.hpp"

#include <cstring>

namespace opentony::runtime::frontend {

using namespace views;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }
std::int32_t s(std::uint32_t v) { return static_cast<std::int32_t>(v); }

// Retail globals
constexpr std::uint32_t kEqCareer = 0x00561c64;
constexpr std::uint32_t kEqCharacter = 0x00561c68;
constexpr std::uint32_t kEqPlayer = 0x00561c7c;
constexpr std::uint32_t kEqSet = 0x00561cc4;
constexpr std::uint32_t kEqIndex = 0x00561cc8;
constexpr std::uint32_t kEqTruck = 0x00561ccc;
constexpr std::uint32_t kTruckTexts = 0x00561cd0;       // three string pointers: the name of each truck style
constexpr std::uint32_t kSlotTable = 0x0052cb58;        // five pairs: the angle of each slot of the rack, then the picture priority of the deck standing there
constexpr std::uint32_t kPlayerTruck = 0x0056a2e0;
constexpr std::uint32_t kSavedTruck = 0x0055fb3a;
constexpr std::uint32_t kCharacterTruck = 0x0055fb22;
constexpr std::uint32_t kPadScrollUp = 0x0056b204;
constexpr std::uint32_t kPadScrollDown = 0x0056b214;
constexpr std::uint32_t kRegionNames = 0x0056d428;      // + region * 0x44: a byte per spool region
constexpr std::uint32_t kLoading = 0x005615d0;

// Assertion messages
constexpr std::uint32_t kMsgTooManyDecks = 0x0052b82c;     // "illegal number of decks"
constexpr std::uint32_t kMsgTargetSlot = 0x0052b844;       // "incorrect TargetSlot"
constexpr std::uint32_t kMsgDeckValue = 0x0052b85c;        // "deck value incorrect %d"
constexpr std::uint32_t kMsgSetDecks = 0x0052b874;         // "screwy number of decks in set %d, %d"
constexpr std::uint32_t kMsgDefaultImage = 0x0052b89c;     // "must set default image"
constexpr std::uint32_t kMsgDataHeld = 0x0052b8b4;         // "deck data already in memory"
constexpr std::uint32_t kMsgSpoolSomething = 0x0052b8d0;   // "should be spooling something"
constexpr std::uint32_t kMsgNothingInStack = 0x0052b8f0;   // "nothing in stack"
constexpr std::uint32_t kMsgNoMemory = 0x0052b904;         // "not enough memory for %s"
constexpr std::uint32_t kMsgSpoolBufNull = 0x0052b920;     // "mpSpoolBuf should be NULL at this point"
constexpr std::uint32_t kMsgNotFound = 0x0052b948;         // "file %s not found"
constexpr std::uint32_t kMsgNoItem = 0x0052b95c;           // "no spooling item pointed to"
constexpr std::uint32_t kMsgSpoolState = 0x0052b978;       // "invalid spool state"
constexpr std::uint32_t kMsgSpoolBufIsNull = 0x0052b98c;   // "mpSpoolBuf is NULL"
constexpr std::uint32_t kMsgFuck = 0x0052b9a0;

// the object records are RuntimePsxObjectRecord (0x4c bytes); the fields of the racks are the layouts' (re/types equip.yml), checked in equip_views.hpp
constexpr std::uint32_t kRecordSize = 0x4c;
static_assert(kRecordSize == guest::kRuntimePsxObjectRecordSize);

constexpr std::uint32_t kFontS2Font2 = 0x0052544c;

// the engine's per-record constructor and destructor (the vector constructor of the record array)
constexpr std::uint32_t kRecordInit = 0x0047fdb0;
constexpr std::uint32_t kRecordDestroy = 0x0047fe20;

}  // namespace

// ---- the rack base ----------------------------------------------------------------------------------------------------------------------------------------

// 0x004468e0: sliding in from off screen (600), nothing active.
void EquipSelect::rack_init(std::uint32_t self) {
    const RuntimeEquipRackBaseView rack(m_, self);
    rack.set_vtable(GuestPtr<void>{kVtRackBase});
    rack.set_state(3);
    rack.set_offset(600);
    rack.set_target(0);
    rack.set_active(0);
}

void EquipSelect::rack_slot2() {}

void EquipSelect::rack_slot3() {}

std::uint32_t EquipSelect::rack_delete(std::uint32_t self, std::uint32_t flag) {
    rack_destroy(self);
    if ((flag & 1) != 0) {
        free_block(self);
    }
    return self;
}

void EquipSelect::rack_destroy(std::uint32_t self) { RuntimeEquipRackBaseView(m_, self).set_vtable(GuestPtr<void>{kVtRackBase}); }

// 0x00446950: an active rack that was hidden slides in; one that is not active slides out (to -600).
void EquipSelect::rack_set_active(std::uint32_t self, std::uint32_t active) {
    const RuntimeEquipRackBaseView rack(m_, self);
    rack.set_active(static_cast<std::uint8_t>(active));
    const std::uint32_t state = u(rack.state());
    if ((active & 0xff) == 0) {
        if (state != 3) {
            rack.set_state(2);
            rack.set_target(s(0xfffffda8u));
        }
    } else if (state == 3) {
        rack.set_state(0);
        rack.set_target(0);
    }
}

// 0x00446990: the slide: 0x37 a frame towards the target (shown when it lands; a rack that slid out is hidden, or slides straight back in when it became active).
void EquipSelect::rack_step(std::uint32_t self) {
    const RuntimeEquipRackBaseView rack(m_, self);
    const std::uint32_t state = u(rack.state());
    if (state == 1 || state == 3) {
        return;
    }
    const std::int32_t offset = rack.offset() - 0x37;
    rack.set_offset(offset);
    if (offset > rack.target()) {
        return;
    }
    rack.set_offset(rack.target());
    if (state == 0) {
        rack.set_state(1);
    } else if (state == 2) {
        const bool active = rack.active() != 0;
        rack.set_state(3);
        rack.set_offset(600);
        rack.set_target(0);
        rack.set_active(0);
        rack.set_state(s(active ? 0 : 3));
    }
}

// ---- the deck rack ----------------------------------------------------------------------------------------------------------------------------------------

// 0x004469f0: the three markers (locked, affordable, too dear), the spool region, five object records, the placeholder picture, the sets and the spool queue.
std::uint32_t EquipSelect::deck_rack_construct(std::uint32_t self) {
    const RuntimeEquipDeckRackView rack(m_, self);
    rack_init(self);
    engine(seam::crt::FUN_00500400, {rack.records_address(), kRecordSize, 5, kRecordInit, kRecordDestroy});
    rack.set_vtable(GuestPtr<void>{kVtDeckRack});
    static const std::uint32_t kMarkerFiles[3] = {0x0052b7e4, 0x0052b7d4, 0x0052b7c4};
    for (std::uint32_t i = 0; i < 3; ++i) {
        const std::uint32_t memory = operator_new(0x5c);
        const std::uint32_t sprite = memory != 0 ? sprite_object(memory, kMarkerFiles[i]) : 0;
        rack.set_markers(i, GuestPtr<void>{sprite});
        set_byte_at(m_, sprite + 0xa, 1);
        set_byte_at(m_, sprite + 0xb, 1);
    }
    rack.set_region(engine_value(0x004b37a0, {0x0052b7bc, 0, 0, 1}, m_.cstring(0x0052b7bc)));
    engine(0x004b3df0);
    rack.set_step(0x333);
    for (std::uint32_t i = 0; i < 5; ++i) {
        const RuntimePsxObjectRecordView object = rack.records(i);
        object.set_object_flags(0);
        object.set_position_x(0);
        object.set_position_y(0);
        object.set_position_z(0xe6000);
        object.set_rotation_component_x(0);
        object.set_rotation_component_y(0);
        object.set_rotation_component_z(0);
        object.set_field_1c(0);
        object.set_field_1d(0);
        object.set_field_1e(0);
        object.set_model_index(static_cast<std::uint16_t>(i));
        object.set_psx_slot(static_cast<std::uint8_t>(rack.region()));
        object.set_source_rgbx_word(0xff00);
        object.set_next(GuestPtr<guest::RuntimePsxObjectRecord>{i < 4 ? rack.records_address(i + 1) : 0});
    }
    const std::uint32_t size = engine_value(0x00449030, {0x0052b7ac}, m_.cstring(0x0052b7ac));
    s_.note(kAllocBuffer, {size, 0, 1, 0});
    const std::uint32_t picture = static_cast<std::uint32_t>(s_.value(kAllocBuffer));
    rack.set_picture(GuestPtr<void>{picture});
    engine(0x00449230, {picture});
    engine(0x00449660);
    rack.set_front(0);
    rack.set_slot(0);
    rack.set_target_angle(s(global_word(m_, kSlotTable)));
    rack.set_angle(s(global_word(m_, kSlotTable)));
    rack.set_single(0);
    deck_rack_build_sets(self);
    const std::uint32_t spool = alloc_zeroed(0x18);
    rack.set_spool(GuestPtr<RuntimeEquipSpoolQueue>{spool != 0 ? spool_construct(spool, 0x18) : 0});
    rack.set_at_front(s(global_word(m_, kEqIndex)));
    rack.set_first_update(1);
    const std::uint32_t y = u(rack.set_now()) * 2000;
    rack.set_y(s(y));
    rack.set_target_y(s(y));
    return self;
}

std::uint32_t EquipSelect::deck_rack_delete(std::uint32_t self, std::uint32_t flag) {
    deck_rack_destroy(self);
    if ((flag & 1) != 0) {
        free_block(self);
    }
    return self;
}

// 0x00446c40: the spool, every deck's picture, the placeholder, the spool region, the markers, the records.
void EquipSelect::deck_rack_destroy(std::uint32_t self) {
    const RuntimeEquipDeckRackView rack(m_, self);
    rack.set_vtable(GuestPtr<void>{kVtDeckRack});
    const std::uint32_t spool = rack.spool().address;
    if (spool != 0) {
        spool_reset(spool);
        free_block(spool);
    }
    for (std::uint32_t set = 0; set < u(rack.set_count()); ++set) {
        const RuntimeEquipDeckSetView record = deck_set_at(m_, rack, static_cast<std::int32_t>(set));
        for (std::uint32_t i = 0; s(i) < record.deck_count(); ++i) {
            const std::uint32_t item = set_item(record, i);
            if (item != 0) {
                item_free_thunk(item);
                free_block(item);
            }
        }
    }
    free_block(rack.picture().address);
    const std::uint32_t region = rack.region();
    engine(0x004b32f0, {region, 1, 0});
    set_byte_at(m_, kRegionNames + region * 0x44, 0);
    for (std::uint32_t i = 0; i < 3; ++i) {
        delete_child(rack.markers(i).address);
    }
    engine(seam::crt::FUN_0050030c, {rack.records_address(), kRecordSize, 5, kRecordDestroy});
    rack_destroy(self);
}

// 0x00446d80: one set for each character whose first deck has the same logo as the character's (a skater with the custom flag has one set of its own and one
// for each of the first thirteen); a deck is a DeckItem, held back (no picture loaded) unless the character owns it.
void EquipSelect::deck_rack_build_sets(std::uint32_t self) {
    const RuntimeEquipDeckRackView rack(m_, self);
    const std::int32_t character = s(global_word(m_, kEqCharacter));
    const std::uint32_t flags = word_at(m_, skater::row(character) + 0x15c);
    const std::uint32_t entry = engine_value(0x004547f0, {u(character), 0});
    const std::uint32_t stem = word_at(m_, entry);
    const std::uint8_t first = byte_at(m_, stem + 3);
    const std::uint8_t second = byte_at(m_, stem + 4);
    auto record = [&](std::int32_t index) { return deck_set_at(m_, rack, index); };
    std::int32_t count = 0;
    if (((flags >> 1) & 1) == 0) {
        for (std::int32_t set = 0; set < 0x14; ++set) {
            const std::uint32_t other = word_at(m_, engine_value(0x004547f0, {u(set), 0}));
            if (first == byte_at(m_, other + 3) && second == byte_at(m_, other + 4)) {
                if (u(set) == global_word(m_, kEqSet)) {
                    rack.set_set_shown(count);
                    rack.set_set_now(count);
                }
                record(count).set_deck_count(8);
                record(count).set_saved_front(0);
                record(count).set_owner(set);
                ++count;
            }
        }
    } else {
        (void)engine_value(0x004547f0, {u(character), 0});
        deck_set_at(m_, rack, 0).set_owner(character);
        rack.set_set_shown(0);
        rack.set_set_now(0);
        deck_set_at(m_, rack, 0).set_deck_count(0x14);
        deck_set_at(m_, rack, 0).set_saved_front(0);
        count = 1;
        for (std::int32_t set = 0; set < 0xd; ++set) {
            (void)engine_value(0x004547f0, {u(set), 0});
            if (u(set) == global_word(m_, kEqSet)) {
                rack.set_set_shown(count);
                rack.set_set_now(count);
            }
            record(count).set_deck_count(8);
            record(count).set_saved_front(0);
            record(count).set_owner(set);
            ++count;
        }
    }
    rack.set_set_count(count);
    if (count == 1) {
        rack.set_single(1);
    }
    for (std::int32_t set = 0; set < count; ++set) {
        const std::int32_t owner = record(set).owner();
        for (std::int32_t i = 0; i < record(set).deck_count(); ++i) {
            const std::uint32_t held = owns_deck(owner, i) ? 0 : 1;
            std::uint32_t item = alloc_zeroed(0x28);
            if (item != 0) {
                const std::uint32_t texture = rack.picture().address;
                const std::uint32_t name = word_at(m_, engine_value(0x004547f0, {u(owner), u(i)}));
                item = item_construct(item, name, texture, held);
            }
            set_set_item(record(set), u(i), item);
        }
    }
    s_.note(kSvcPrintfLog, {0x0052b7f4});
}

// 0x00446fb0: the rack settles (the first update turns it to the character's own deck), the spool runs, every picture updates, the angle and the height move
// towards their targets, the five object records are placed, and the slot at the front decides which deck is chosen.
void EquipSelect::deck_rack_update(std::uint32_t self) {
    const RuntimeEquipDeckRackView rack(m_, self);
    if (rack.first_update() != 0) {
        deck_rack_turn(self, 0xffffffffu);
        deck_rack_scroll(self, 0);
        rack.set_first_update(0);
    }
    rack_step(self);
    (void)spool_poll(rack.spool().address);
    for (std::uint32_t set = 0; set < u(rack.set_count()); ++set) {
        const RuntimeEquipDeckSetView record = deck_set_at(m_, rack, static_cast<std::int32_t>(set));
        for (std::int32_t i = 0; i < record.deck_count(); ++i) {
            check_at(i >= 0 && i < 0x14, kMsgTooManyDecks);
            item_update(set_item(record, u(i)));
        }
    }
    // the angle moves a fixed step towards the target
    const std::int32_t target = rack.target_angle();
    std::int32_t angle = rack.angle();
    const std::int32_t gap = target - angle;
    if (gap < -0x400) {
        angle -= 0x80;
    } else if (gap > 0x400) {
        angle += 0x80;
    } else if (gap < -0x40) {
        angle -= 0x40;
    } else if (gap > 0x40) {
        angle += 0x40;
    } else {
        angle = target;
    }
    rack.set_angle(angle);
    if (angle >= 0x1000) {
        rack.set_angle(angle - 0x1000);
        rack.set_target_angle(target - 0x1000);
    }
    if (rack.angle() < 0) {
        rack.set_angle(s(u(rack.angle()) + 0x1000));
        rack.set_target_angle(s(u(rack.target_angle()) + 0x1000));
    }
    // the height towards its target, and the wrap round the sets
    const std::int32_t target_y = rack.target_y();
    std::int32_t y = rack.y();
    const std::int32_t rise = target_y - y;
    if (rise < -0x32) {
        y -= 100;
    } else if (rise > 0x32) {
        y += 100;
    } else {
        y = target_y;
    }
    rack.set_y(y);
    const std::int32_t span = rack.set_count() * 125 * 16;
    if (y < -1000) {
        rack.set_y(y + span);
        rack.set_target_y(target_y + span);
    } else if (y >= span - 0x3e8) {
        rack.set_y(y - span);
        rack.set_target_y(target_y - span);
    }
    std::int32_t extra = 0;
    if (global_byte(m_, kPadScrollUp) != 0) {
        extra = 0x5000;
    }
    if (global_byte(m_, kPadScrollDown) != 0) {
        extra = -0x5000;
    }
    const std::int32_t height = (rack.set_now() * 2000 - rack.y()) << 12;
    for (std::uint32_t i = 0; i < 5; ++i) {
        const RuntimePsxObjectRecordView object = rack.records(i);
        const std::uint32_t step = (u(rack.step()) & 0xffff) * i;
        const std::uint32_t turned = (u(rack.angle()) & 0xffff) - (step & 0xffff);
        object.set_rotation_component_y(static_cast<std::int16_t>(turned & 0xfff));
        object.set_position_y(height);
        object.set_position_x(s(u(rack.offset()) << 12));
    }
    if (extra != 0) {
        s_.note(kSvcPrintfLog, {0x0052b818});
    }
    // which slot is at the front now
    const std::int32_t old_slot = rack.front();
    const std::int32_t half = rack.step() >> 1;
    std::int32_t shown = rack.angle();
    if (shown >= half + s(word_at(m_, kSlotTable + 0x20))) {
        shown -= 0x1000;
    }
    std::int32_t slot = old_slot;
    for (std::int32_t i = 0; i < 5; ++i) {
        const std::int32_t centre = s(word_at(m_, kSlotTable + 8 * u(i)));
        if (shown >= centre - half && shown < centre + half) {
            slot = i;
            break;
        }
    }
    if (slot != old_slot) {
        std::int32_t direction = 0;
        if (slot == 4 && old_slot == 0) {
            direction = -1;
        } else if (slot == 0 && old_slot == 4) {
            direction = 1;
        } else if (slot < old_slot) {
            direction = -1;
        } else if (slot > old_slot) {
            direction = 1;
        }
        deck_rack_scroll(self, direction);
    }
    // the set whose band the height is in
    std::int32_t band = rack.set_now();
    const std::int32_t sets = rack.set_count();
    if (sets > 0) {
        const std::int32_t at = rack.y();
        std::int32_t upper = 0x3e8;
        for (std::int32_t i = 0; i < sets; ++i, upper += 0x7d0) {
            if (at > upper - 0x7d0 && at < upper) {
                band = i;
                break;
            }
        }
    }
    const std::int32_t now = rack.set_now();
    if (now != band) {
        deck_set_at(m_, rack, now).set_saved_front(rack.at_front());
        rack.set_set_now(band);
        rack.set_at_front(deck_set_at(m_, rack, band).saved_front());
        deck_rack_scroll(self, 0);
    }
}

// 0x00447340: a settled rack marks the deck at the front: nothing for a deck the character owns; the first marker for another character's deck (or any
// unowned deck outside the career); the second when the character can pay for it, the third when it cannot; the five records draw last.
void EquipSelect::deck_rack_draw(std::uint32_t self, std::uint32_t x, std::uint32_t y) {
    const RuntimeEquipDeckRackView rack(m_, self);
    auto mark = [&](std::uint32_t marker) { s_.note(kSpriteDrawService, {rack.markers(marker).address, x, y, 0}); };
    if (u(rack.state()) == 1 && u(rack.angle()) == u(rack.target_angle()) && u(rack.y()) == u(rack.target_y())) {
        const std::int32_t set = deck_set_at(m_, rack, rack.set_now()).owner();
        const std::int32_t deck = rack.at_front();
        if (global_byte(m_, kEqCareer) == 0) {
            if (!owns_deck(set, deck)) {
                mark(0);
            }
        } else if (!owns_deck(set, deck) && !is_current_character(set)) {
            mark(0);
        } else if (!owns_deck(set, deck)) {
            mark(can_afford(set, deck) ? 1 : 2);
        }
    }
    engine(0x0045f530, {rack.records_address()});
}

// 0x00447440: up and down are refused on a rack of one set; each move has its own sound (0x97 sideways, 0x90 between sets).
void EquipSelect::deck_rack_move(std::uint32_t self, std::uint32_t direction) {
    const RuntimeEquipDeckRackView rack(m_, self);
    if (rack.single() == 0 || (direction != 1 && direction != 3)) {
        if (direction == 0 || direction == 2) {
            sfx(0x97);
        }
        if (direction == 1 || direction == 3) {
            sfx(0x90);
        }
        deck_rack_turn(self, direction);
    }
}

// 0x004474b0: 0 and 2 turn the rack one slot (the target angle is a full turn away when the slot wraps), 1 and 3 move to the previous or next set.
void EquipSelect::deck_rack_turn(std::uint32_t self, std::uint32_t direction) {
    const RuntimeEquipDeckRackView rack(m_, self);
    auto slot_angle = [&](std::int32_t slot) { return word_at(m_, kSlotTable + 8 * u(slot)); };
    auto check_slot = [&]() { check_at(rack.slot() >= 0 && rack.slot() < 5, kMsgTargetSlot); };
    switch (direction) {
    case 0: {
        std::int32_t slot = rack.slot() + 1;
        rack.set_slot(slot);
        if (slot > 4) {
            rack.set_slot(0);
        }
        check_slot();
        const std::int32_t angle = s(slot_angle(rack.slot()));
        rack.set_target_angle(angle);
        if (angle < rack.angle()) {
            rack.set_target_angle(angle + 0x1000);
        }
        break;
    }
    case 1: {
        rack.set_slot(rack.front());
        check_slot();
        const std::uint32_t angle = slot_angle(rack.slot());
        rack.set_target_angle(s(angle));
        rack.set_angle(s(angle));
        std::int32_t shown = rack.set_shown() - 1;
        rack.set_set_shown(shown);
        if (shown < 0) {
            rack.set_set_shown(s(u(rack.set_count()) - 1));
        }
        rack.set_target_y(s(u(rack.target_y()) - 2000));
        break;
    }
    case 2: {
        std::int32_t slot = rack.slot() - 1;
        rack.set_slot(slot);
        if (slot < 0) {
            rack.set_slot(4);
        }
        check_slot();
        const std::int32_t angle = s(slot_angle(rack.slot()));
        rack.set_target_angle(angle);
        if (rack.angle() < angle) {
            rack.set_target_angle(angle - 0x1000);
        }
        break;
    }
    case 3: {
        rack.set_slot(rack.front());
        check_slot();
        const std::int32_t shown = rack.set_shown() + 1;
        const std::uint32_t angle = slot_angle(rack.slot());
        rack.set_target_angle(s(angle));
        rack.set_angle(s(angle));
        rack.set_set_shown(shown);
        if (shown >= rack.set_count()) {
            rack.set_set_shown(0);
        }
        rack.set_target_y(s(u(rack.target_y()) + 2000));
        break;
    }
    default:
        break;
    }
}

// 0x00447730: the front slot and the deck at the front move on by `step`; no picture is wanted, the five round the front are (each with the priority of its
// slot), and the spool queue is rebuilt with every picture's distance from the front in decks and in sets.
void EquipSelect::deck_rack_scroll(std::uint32_t self, std::int32_t step) {
    const RuntimeEquipDeckRackView rack(m_, self);
    std::int32_t front = rack.front() + step;
    rack.set_front(front);
    if (front < 0) {
        rack.set_front(4);
    } else if (front >= 5) {
        rack.set_front(0);
    }
    auto record = [&](std::int32_t index) { return deck_set_at(m_, rack, index); };
    std::int32_t at_front = rack.at_front() + step;
    rack.set_at_front(at_front);
    if (at_front < 0) {
        rack.set_at_front(record(rack.set_now()).deck_count() - 1);
    } else if (record(rack.set_now()).deck_count() <= at_front) {
        rack.set_at_front(0);
    }
    const std::int32_t sets = rack.set_count();
    for (std::int32_t set = 0; set < sets; ++set) {
        const std::int32_t decks = record(set).deck_count();
        check_at(decks >= 0 && decks <= 0x14, kMsgSetDecks);
        for (std::int32_t i = 0; i < decks; ++i) {
            item_set_wanted(set_item(record(set), u(i)), 0, -1);
        }
    }
    for (std::int32_t k = rack.front() - 2; k < rack.front() + 3; ++k) {
        std::uint32_t offset = u(k * 8);
        std::int32_t slot = k;
        if (k > 4) {
            offset -= 0x28;
            slot = k - 5;
        }
        if (slot < 0) {
            offset += 0x28;
        }
        std::int32_t deck = rack.at_front() - rack.front() + k;
        const std::int32_t count = record(rack.set_now()).deck_count();
        if (deck >= count) {
            deck -= count;
        }
        if (deck < 0) {
            deck += count;
        }
        check_at(deck >= 0 && deck < record(rack.set_now()).deck_count(), kMsgDeckValue);
        item_set_wanted(set_item(record(rack.set_now()), u(deck)), 1, int_at(m_, kSlotTable + 4 + offset));
    }
    spool_clear_queue(rack.spool().address);
    for (std::int32_t set = 0; set < sets; ++set) {
        const std::int32_t now = rack.set_now();
        const std::int32_t forward = set >= now ? sets - set + now : now - set;
        const std::int32_t backward = set >= now ? set - now : sets - now + set;
        const std::int32_t decks = record(set).deck_count();
        for (std::int32_t i = 0; i < decks; ++i) {
            const std::int32_t at = set == now ? rack.at_front() : record(set).saved_front();
            const std::int32_t near = i >= at ? decks - i + at : at - i;
            const std::int32_t far = i >= at ? i - at : decks - at + i;
            const std::uint32_t item = set_item(record(set), u(i));
            item_at(m_, item).set_near_priority(static_cast<std::int8_t>(near < far ? near : far));
            item_at(m_, item).set_set_priority(static_cast<std::int8_t>(forward < backward ? forward : backward));
            spool_insert(rack.spool().address, item);
        }
    }
    spool_begin(rack.spool().address);
    set_global_word(m_, kEqIndex, u(rack.at_front()));
    set_global_word(m_, kEqSet, u(record(rack.set_now()).owner()));
}

// 0x00447a90: the picture of a deck (in every set that is the character's) is no longer held back: it may load.
void EquipSelect::deck_rack_clear_flag(std::uint32_t self, std::int32_t set, std::int32_t index) {
    const RuntimeEquipDeckRackView rack(m_, self);
    for (std::uint32_t i = 0; s(i) < rack.set_count(); ++i) {
        const RuntimeEquipDeckSetView record = deck_set_at(m_, rack, static_cast<std::int32_t>(i));
        if (record.owner() == set) {
            item_at(m_, set_item(record, u(index))).set_held_back(0);
        }
    }
}

// ---- a deck's picture ---------------------------------------------------------------------------------------------------------------------------------------

// 0x00447ae0: the file name of the deck's picture, and the placeholder shown until it loads.
std::uint32_t EquipSelect::item_construct(std::uint32_t self, std::uint32_t name, std::uint32_t texture_set, std::uint32_t held) {
    const RuntimeEquipDeckItemView item = item_at(m_, self);
    const std::string line = retail_format("%s", {m_.cstring(name, 0x40)}, {});
    for (std::size_t i = 0; i <= line.size(); ++i) {
        set_byte_at(m_, item.file_name_address(i), i < line.size() ? static_cast<std::uint8_t>(line[i]) : 0);
    }
    item.set_data(GuestPtr<void>{0});
    check_at(texture_set != 0, kMsgDefaultImage);
    item.set_placeholder_set(GuestPtr<void>{texture_set});
    item.set_settled(0);
    item.set_picture_shown(0);
    item.set_wanted(0);
    item.set_not_wanted(0);
    item.set_data_held(0);
    item.set_held_back(static_cast<std::uint8_t>(held));
    return self;
}

void EquipSelect::item_free_thunk(std::uint32_t self) { item_free(self); }

// 0x00447b80: the spooled-in picture data.
void EquipSelect::item_set_data(std::uint32_t self, std::uint32_t data) {
    const RuntimeEquipDeckItemView item = item_at(m_, self);
    check_at(item.data_held() == 0, kMsgDataHeld);
    item.set_data(GuestPtr<void>{data});
    item.set_data_held(1);
}

void EquipSelect::item_free(std::uint32_t self) {
    const RuntimeEquipDeckItemView item = item_at(m_, self);
    if (item.data().address != 0) {
        free_block(item.data().address);
    }
    item.set_data(GuestPtr<void>{0});
    item.set_data_held(0);
}

// 0x00447c00: whether the picture is wanted (and the texture set it replaces, unless -1).
void EquipSelect::item_set_wanted(std::uint32_t self, std::uint32_t wanted, std::int32_t texture_set) {
    const RuntimeEquipDeckItemView item = item_at(m_, self);
    if (texture_set != -1) {
        item.set_texture_set(texture_set);
    }
    item.set_wanted(static_cast<std::uint8_t>(wanted));
    item.set_not_wanted(static_cast<std::uint8_t>((wanted & 0xff) == 0 ? 1 : 0));
}

// 0x00447c20: swap the texture set's picture in (the loaded data, unless the picture is held back) or out (the placeholder) as the item became wanted or not.
void EquipSelect::item_update(std::uint32_t self) {
    const RuntimeEquipDeckItemView item = item_at(m_, self);
    auto swap = [&](std::uint32_t data, std::uint32_t name) { engine(0x00457e10, {item.texture_set(), data, name}); };
    if (item.settled() != 0) {
        const std::uint32_t data = item.data().address;
        if (data != 0) {
            if (item.picture_shown() == 0 && item.held_back() == 0) {
                swap(data, item.file_name_address());
                item.set_picture_shown(1);
            }
        } else if (item.picture_shown() != 0) {
            swap(item.placeholder_set().address, 0x0052b7ac);
            item.set_picture_shown(0);
        }
    }
    if (item.settled() == 0) {
        if (item.wanted() != 0) {
            const std::uint32_t data = item.data().address;
            if (data != 0 && item.held_back() == 0) {
                swap(data, item.file_name_address());
                item.set_picture_shown(1);
            } else {
                swap(item.placeholder_set().address, 0x0052b7ac);
                item.set_picture_shown(0);
            }
            item.set_settled(1);
            item.set_wanted(0);
            item.set_not_wanted(0);
            return;
        }
    } else if (item.not_wanted() != 0) {
        item.set_settled(0);
        item.set_picture_shown(0);
    }
    item.set_wanted(0);
    item.set_not_wanted(0);
}

// 0x00447cf0: the sum of the two signed bytes at the front of an item (the second counts twice): this item is no further than the other.
bool EquipSelect::item_priority_le(std::uint32_t self, std::uint32_t other) {
    const RuntimeEquipDeckItemView theirs_item = item_at(m_, other);
    const RuntimeEquipDeckItemView mine_item = item_at(m_, self);
    const std::int32_t theirs = theirs_item.near_priority() + theirs_item.set_priority() * 2;
    const std::int32_t mine = mine_item.near_priority() + mine_item.set_priority() * 2;
    return mine <= theirs;
}

// ---- the spool queue ---------------------------------------------------------------------------------------------------------------------------------------

std::uint32_t EquipSelect::spool_construct(std::uint32_t self, std::uint32_t kept) {
    const RuntimeEquipSpoolQueueView queue = spool_at(m_, self);
    queue.set_kept(static_cast<std::int32_t>(kept));
    queue.set_head(node_ptr(0));
    queue.set_current(node_ptr(0));
    queue.set_buffer(GuestPtr<void>{0});
    queue.set_state(0);
    return self;
}

// 0x00447d30: finish what is loading, drop the buffer and the queue.
void EquipSelect::spool_reset(std::uint32_t self) {
    const RuntimeEquipSpoolQueueView queue = spool_at(m_, self);
    engine(0x00449660);
    if (queue.buffer().address != 0) {
        free_block(queue.buffer().address);
    }
    queue.set_buffer(GuestPtr<void>{0});
    queue.set_state(0);
    queue.set_current(node_ptr(0));
    spool_clear_queue(self);
}

// 0x00447d70: free the queue's nodes (the one being loaded stays); a running spool is marked for a restart.
void EquipSelect::spool_clear_queue(std::uint32_t self) {
    const RuntimeEquipSpoolQueueView queue = spool_at(m_, self);
    std::uint32_t node = queue.head().address;
    while (node != 0) {
        const std::uint32_t next = node_at(m_, node).next().address;
        if (node != queue.current().address) {
            free_block(node);
        }
        node = next;
    }
    queue.set_head(node_ptr(0));
    if (queue.state() != 0) {
        check_at(queue.current().address != 0, kMsgSpoolSomething);
        queue.set_state(2);
    }
}

// 0x00447e00: a node for the item, put in order of priority (not at all when it is the one being loaded).
void EquipSelect::spool_insert(std::uint32_t self, std::uint32_t item) {
    const RuntimeEquipSpoolQueueView queue = spool_at(m_, self);
    s_.note(kAllocBuffer, {8, 0, 1, 0});
    const std::uint32_t node = static_cast<std::uint32_t>(s_.value(kAllocBuffer));
    const RuntimeEquipSpoolNodeView entry = node_at(m_, node);
    entry.set_item(item_ptr(item));
    entry.set_next(node_ptr(0));
    if (queue.current().address != 0 && item == node_at(m_, queue.current().address).item().address) {
        free_block(node);
        return;
    }
    std::uint32_t previous = 0;
    std::uint32_t at = queue.head().address;
    while (at != 0 && item_priority_le(node_at(m_, at).item().address, item)) {
        previous = at;
        at = node_at(m_, at).next().address;
    }
    entry.set_next(node_ptr(at));
    if (previous != 0) {
        node_at(m_, previous).set_next(node_ptr(node));
    } else {
        queue.set_head(node_ptr(node));
    }
}

// 0x00447e80: the pictures past the kept ones are freed, and loading starts (or the queue restarts behind the one being loaded).
void EquipSelect::spool_begin(std::uint32_t self) {
    const RuntimeEquipSpoolQueueView queue = spool_at(m_, self);
    check_at(queue.head().address != 0, kMsgNothingInStack);
    queue.set_first_dropped(node_ptr(0));
    std::int32_t i = 0;
    for (std::uint32_t node = queue.head().address; node != 0; node = node_at(m_, node).next().address, ++i) {
        if (queue.kept() <= i) {
            item_free(node_at(m_, node).item().address);
            if (queue.first_dropped().address == 0) {
                queue.set_first_dropped(node_ptr(node));
            }
        }
    }
    if (queue.state() != 0) {
        if (queue.state() == 2) {
            node_at(m_, queue.current().address).set_next(queue.head());
            queue.set_head(queue.current());
            queue.set_state(1);
        }
        return;
    }
    queue.set_current(queue.head());
    queue.set_state(1);
    spool_start_next(self);
}

// 0x00447f40: start reading the next picture that is not in memory yet.
void EquipSelect::spool_start_next(std::uint32_t self) {
    const RuntimeEquipSpoolQueueView queue = spool_at(m_, self);
    check_at(queue.state() == 1, kMsgSpoolState);
    check_at(queue.current().address != 0, kMsgNoItem);
    if (item_at(m_, node_at(m_, queue.current().address).item().address).data_held() != 0) {
        for (;;) {
            const std::uint32_t node = node_at(m_, queue.current().address).next().address;
            queue.set_current(node_ptr(node));
            if (node == queue.first_dropped().address || node == 0) {
                queue.set_current(node_ptr(0));
                queue.set_state(0);
                return;
            }
            if (item_at(m_, node_at(m_, node).item().address).data_held() == 0) {
                break;
            }
        }
    }
    const std::uint32_t name = item_at(m_, node_at(m_, queue.current().address).item().address).file_name_address();
    const std::int32_t size = s(engine_value(0x00449030, {name}, m_.cstring(name, 0x40)));
    check_at(size > 0, kMsgNotFound);
    check_at(queue.buffer().address == 0, kMsgSpoolBufNull);
    s_.note(kAllocBuffer, {u(size), 0, 1, 0});
    queue.set_buffer(GuestPtr<void>{static_cast<std::uint32_t>(s_.value(kAllocBuffer))});
    check_at(queue.buffer().address != 0, kMsgNoMemory);
    engine(0x00449230, {queue.buffer().address});
}

// 0x00448110: when the read finishes the buffer goes to the item and the next one starts.
bool EquipSelect::spool_poll(std::uint32_t self) {
    const RuntimeEquipSpoolQueueView queue = spool_at(m_, self);
    if (queue.state() != 0) {
        engine(0x004494b0);
        if (global_word(m_, kLoading) != 0) {
            return false;
        }
        check_at(queue.buffer().address != 0, kMsgSpoolBufIsNull);
        item_set_data(node_at(m_, queue.current().address).item().address, queue.buffer().address);
        queue.set_buffer(GuestPtr<void>{0});
        const std::uint32_t node = node_at(m_, queue.current().address).next().address;
        queue.set_current(node_ptr(node));
        if (node != queue.first_dropped().address && node != 0) {
            spool_start_next(self);
            return true;
        }
        queue.set_current(node_ptr(0));
        queue.set_state(0);
    }
    return true;
}

// 0x004481c0: a check nothing calls: no node of the queue holds the data.
void EquipSelect::spool_verify(std::uint32_t self, std::uint32_t data) {
    for (std::uint32_t node = spool_at(m_, self).head().address; node != 0; node = node_at(m_, node).next().address) {
        check_at(item_at(m_, node_at(m_, node).item().address).data().address != data, kMsgFuck);
    }
}

// ---- the truck rack ----------------------------------------------------------------------------------------------------------------------------------------

// 0x00448230: the truck model's spool region, two object records, its caption font, the truck style the player has.
std::uint32_t EquipSelect::truck_construct(std::uint32_t self) {
    const RuntimeEquipTruckRackView rack(m_, self);
    rack_init(self);
    font_construct(rack.font_address());
    engine(seam::crt::FUN_00500400, {rack.records_address(), kRecordSize, 5, kRecordInit, kRecordDestroy});
    rack.set_vtable(GuestPtr<void>{kVtTruckRack});
    rack.set_region(engine_value(0x004b37a0, {0x0052b9ac, 0, 0, 1}, m_.cstring(0x0052b9ac)));
    engine(0x004b3df0);
    for (std::uint32_t i = 0; i < 2; ++i) {
        const RuntimePsxObjectRecordView object = rack.records(i);
        object.set_object_flags(0);
        object.set_position_x(0);
        object.set_position_z(s(0xfff88000u));
        object.set_rotation_component_x(0);
        object.set_rotation_component_y(0);
        object.set_rotation_component_z(0);
        object.set_field_1c(0);
        object.set_field_1d(0);
        object.set_field_1e(0);
        object.set_model_index(static_cast<std::uint16_t>(i));
        object.set_psx_slot(static_cast<std::uint8_t>(rack.region()));
        object.set_source_rgbx_word(0xff00);
        if (i == 0) {
            object.set_position_y(s(0xfffb5000u));
            object.set_next(GuestPtr<guest::RuntimePsxObjectRecord>{rack.records_address(1)});
        } else {
            object.set_position_y(0);
            object.set_next(GuestPtr<guest::RuntimePsxObjectRecord>{0});
        }
    }
    font_from(rack.font_address(), kFontS2Font2);
    const RuntimeFontView font = rack.font();
    font.set_horizontal_align(1);
    font.set_vertical_align(1);
    f_.set_rgb(rack.font_address(), 0xff, 0xff, 0xff);
    font.set_shadow(0);
    font.set_spacing(1);
    font.set_field_10(0);
    font.set_scale(0x1000);
    rack.set_style(s(global_word(m_, kEqTruck)));
    rack.set_previous_style(s(global_word(m_, kEqTruck)));
    rack.set_motion(0);
    rack.set_height(s(0xffffffb5u));
    rack.set_spin(0);
    rack.set_turn(0);
    rack.set_single(1);
    return self;
}

std::uint32_t EquipSelect::truck_delete(std::uint32_t self, std::uint32_t flag) {
    truck_destroy(self);
    if ((flag & 1) != 0) {
        free_block(self);
    }
    return self;
}

void EquipSelect::truck_destroy(std::uint32_t self) {
    const RuntimeEquipTruckRackView rack(m_, self);
    rack.set_vtable(GuestPtr<void>{kVtTruckRack});
    const std::uint32_t region = rack.region();
    engine(0x004b32f0, {region, 1, 0});
    set_byte_at(m_, kRegionNames + region * 0x44, 0);
    engine(seam::crt::FUN_0050030c, {rack.records_address(), kRecordSize, 5, kRecordDestroy});
    font_destroy(rack.font_address());
    rack_destroy(self);
}

// 0x00448540: the spin and the height go to both records, the truck style is kept in the player's and the character's tables, and the state machine runs:
// 0 spin up, 1 slow the spin, 2 rise, 3 turn to the new style (a style two away turns twice as far), 4 sink back.
void EquipSelect::truck_update(std::uint32_t self) {
    const RuntimeEquipTruckRackView rack(m_, self);
    rack_step(self);
    for (std::uint32_t i = 0; i < 2; ++i) {
        const RuntimePsxObjectRecordView object = rack.records(i);
        object.set_position_x(s(u(rack.offset()) << 12));
        object.set_rotation_component_y(static_cast<std::int16_t>(u(rack.spin()) & 0xfff));
    }
    rack.records(0).set_position_y(s(u(rack.height()) << 12));
    const RuntimePsxObjectRecordView first = rack.records(0);
    first.set_rotation_component_y(static_cast<std::int16_t>((static_cast<std::uint16_t>(first.rotation_component_y()) + (u(rack.turn()) & 0xffff)) & 0xfff));
    const std::uint32_t player = global_word(m_, kEqPlayer);
    const auto style = static_cast<std::uint8_t>(rack.style());
    set_byte_at(m_, kPlayerTruck + player, style);
    set_byte_at(m_, kSavedTruck + player, style);
    set_byte_at(m_, kCharacterTruck + global_word(m_, kEqCharacter), style);
    switch (u(rack.motion())) {
    case 0: {
        const std::int32_t spin = rack.spin();
        rack.set_spin(spin + 0x10);
        if (spin + 0x10 >= 0x800) {
            rack.set_spin(spin + 0x10 - 0x1000);
        }
        break;
    }
    case 1: {
        const std::int32_t spin = rack.spin() >> 1;
        rack.set_spin(spin);
        if (spin == 0 || spin == -1) {
            rack.set_motion(2);
        }
        break;
    }
    case 2: {
        const std::int32_t height = rack.height() + 0xf;
        rack.set_height(height);
        if (height > 4) {
            rack.set_height(5);
            rack.set_motion(3);
        }
        break;
    }
    case 3: {
        const std::int32_t now = rack.style();
        const std::int32_t before = rack.previous_style();
        std::uint32_t shift = 1;
        if (now == before) {
            rack.set_motion(4);
            rack.set_turn(0);
            break;
        }
        std::int32_t turn;
        if (now > before) {
            if (now - before == 2) {
                shift = 2;
            }
            turn = rack.turn() + 0xa0;
        } else {
            if (before - now == 2) {
                shift = 2;
            }
            turn = rack.turn() - 0xa0;
        }
        rack.set_turn(turn);
        if (turn >= (0x800 << shift) || turn <= (-0x800 * (1 << shift))) {
            rack.set_motion(4);
            rack.set_turn(0);
            rack.set_previous_style(now);
        }
        break;
    }
    case 4: {
        const std::int32_t height = rack.height() - 0xf;
        rack.set_height(height);
        if (height < -0x4a) {
            rack.set_height(s(0xffffffb5u));
            rack.set_motion(0);
        }
        break;
    }
    default:
        break;
    }
}

// 0x00448740: the truck's name under it (when the rack is shown), then the two records.
void EquipSelect::truck_draw(std::uint32_t self, std::uint32_t x, std::uint32_t y) {
    const RuntimeEquipTruckRackView rack(m_, self);
    if (u(rack.state()) == 1) {
        const std::uint32_t text = word_at(m_, kTruckTexts + 4 * u(rack.style()));
        s_.note(kFontDrawService, {rack.font_address(), x, y + 0x30, text, 0}, m_.cstring(text, 0x40));
    }
    engine(0x0045f530, {rack.records_address()});
}

// 0x00448790: left and right (0 and 2) change the truck style (a sound each, wrapping through the three); the rack starts its spin when it was idle.
void EquipSelect::truck_move(std::uint32_t self, std::uint32_t direction) {
    const RuntimeEquipTruckRackView rack(m_, self);
    if (direction == 0) {
        sfx(0x11f);
        const std::int32_t style = rack.style() + 1;
        rack.set_style(style);
        if (style == 3) {
            rack.set_style(0);
        }
        if (u(rack.motion()) == 0) {
            rack.set_motion(1);
        }
        return;
    }
    if (direction == 2) {
        sfx(0x11f);
        const std::int32_t style = rack.style() - 1;
        rack.set_style(style);
        if (style < 0) {
            rack.set_style(2);
        }
        if (u(rack.motion()) == 0) {
            rack.set_motion(1);
        }
    }
}

// ---- virtual dispatch --------------------------------------------------------------------------------------------------------------------------------------

// The slots of the classes below: 0 deleting destructor, 1 update, 2 draw (x, y), 3 the racks' move, 5 and 6 the snaps. The per-frame hook (slot 7) is an engine
// function and is never dispatched here.
bool EquipSelect::dispatch(std::uint32_t object, std::uint32_t slot, const std::vector<std::uint32_t>& args) {
    const std::uint32_t vt = word_at(m_, object);
    const std::uint32_t a0 = args.empty() ? 0 : args[0];
    const std::uint32_t a1 = args.size() < 2 ? 0 : args[1];
    if (slot == 7) {
        return false;
    }
    if (vt == kVtText) {
        switch (slot) {
        case 0: w_.text_delete(object, a0); return true;
        case 1: w_.element_update(object); return true;
        case 2: w_.text_draw(object, s(a0), s(a1)); return true;
        case 5: w_.element_snap_x(object); return true;
        case 6: w_.element_snap_y(object); return true;
        default: return false;
        }
    }
    if (vt == kVtSprite) {
        switch (slot) {
        case 0: w_.sprite_delete(object, a0); return true;
        case 1: w_.element_update(object); return true;
        case 2: w_.sprite_draw(object, s(a0), s(a1)); return true;
        case 5: w_.element_snap_x(object); return true;
        case 6: w_.element_snap_y(object); return true;
        default: return false;
        }
    }
    if (vt == kVtWindow) {
        switch (slot) {
        case 0: w_.window_delete(object, a0); return true;
        case 1: w_.window_update(object); return true;
        case 2: w_.window_draw(object, s(a0), s(a1)); return true;
        case 5: w_.window_snap_x(object); return true;
        case 6: w_.window_snap_y(object); return true;
        default: return false;
        }
    }
    if (vt == kVtElement) {
        switch (slot) {
        case 0: w_.element_delete(object, a0); return true;
        case 1: w_.element_update(object); return true;
        case 5: w_.element_snap_x(object); return true;
        case 6: w_.element_snap_y(object); return true;
        default: return false;
        }
    }
    // the screen's own classes: windows inherit the window base's snaps, elements the element's
    auto window_snap = [&]() {
        if (slot == 5) {
            w_.window_snap_x(object);
            return true;
        }
        if (slot == 6) {
            w_.window_snap_y(object);
            return true;
        }
        return false;
    };
    auto element_snap = [&]() {
        if (slot == 5) {
            w_.element_snap_x(object);
            return true;
        }
        if (slot == 6) {
            w_.element_snap_y(object);
            return true;
        }
        return false;
    };
    if (vt == kVtEquipWindow) {
        switch (slot) {
        case 0: window_delete(object, a0); return true;
        case 1: window_update(object); return true;
        case 2: w_.window_draw(object, s(a0), s(a1)); return true;
        case 4: window_slot4(); return true;
        default: return window_snap();
        }
    }
    if (vt == kVtStatDots) {
        switch (slot) {
        case 0: dots_delete(object, a0); return true;
        case 1: dots_update(object); return true;
        case 2: dots_draw(object, s(a0), s(a1)); return true;
        default: return element_snap();
        }
    }
    if (vt == kVtCashWindow) {
        switch (slot) {
        case 0: cash_delete(object, a0); return true;
        case 1: cash_update(object); return true;
        case 2: w_.window_draw(object, s(a0), s(a1)); return true;
        default: return window_snap();
        }
    }
    if (vt == kVtEquipView) {
        switch (slot) {
        case 0: view_delete(object, a0); return true;
        case 1: view_update(object); return true;
        case 2: view_draw(object, s(a0), s(a1)); return true;
        default: return element_snap();
        }
    }
    if (vt == kVtNameWindow) {
        switch (slot) {
        case 0: name_delete(object, a0); return true;
        case 1: w_.window_update(object); return true;
        case 2: w_.window_draw(object, s(a0), s(a1)); return true;
        default: return window_snap();
        }
    }
    if (vt == kVtRackBase) {
        switch (slot) {
        case 0: rack_delete(object, a0); return true;
        case 1: rack_step(object); return true;
        case 2: rack_slot2(); return true;
        case 3: rack_slot3(); return true;
        default: return false;
        }
    }
    if (vt == kVtDeckRack) {
        switch (slot) {
        case 0: deck_rack_delete(object, a0); return true;
        case 1: deck_rack_update(object); return true;
        case 2: deck_rack_draw(object, a0, a1); return true;
        case 3: deck_rack_move(object, a0); return true;
        default: return false;
        }
    }
    if (vt == kVtTruckRack) {
        switch (slot) {
        case 0: truck_delete(object, a0); return true;
        case 1: truck_update(object); return true;
        case 2: truck_draw(object, a0, a1); return true;
        case 3: truck_move(object, a0); return true;
        default: return false;
        }
    }
    return false;
}

// The window base reports a child's virtual call by service name; these are the names of slots 0, 1, 2, 3, 5 and 6.
bool EquipSelect::dispatch_named(const char* name, std::uint32_t object, const std::vector<std::uint32_t>& args) {
    std::uint32_t slot = 0xff;
    if (std::strcmp(name, kVirtualDelete) == 0) {
        slot = 0;
    } else if (std::strcmp(name, kElementUpdate) == 0) {
        slot = 1;
    } else if (std::strcmp(name, kElementDraw) == 0) {
        slot = 2;
    } else if (std::strcmp(name, "rack_v3") == 0) {
        slot = 3;
    } else if (std::strcmp(name, kElementSnapX) == 0) {
        slot = 5;
    } else if (std::strcmp(name, kElementSnapY) == 0) {
        slot = 6;
    } else {
        return false;
    }
    return dispatch(object, slot, args);
}

}  // namespace opentony::runtime::frontend
