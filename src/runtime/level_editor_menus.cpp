#include "auto_views.hpp"
#include "level_editor.hpp"

namespace opentony::runtime::leveleditor {

using namespace opentony::runtime::autoviews;

// The editor's pad and the pieces and items it puts down and takes away (the modes and the states of the menus are listed in level_editor_states.cpp).

namespace {
constexpr std::uint32_t kPadPlace = 0x0056b029;
constexpr std::uint32_t kPadRemove = 0x0056aff9;
constexpr std::uint32_t kPadRotate = 0x0056b019;
constexpr std::uint32_t kPadCategoryDown = 0x0056b049;
constexpr std::uint32_t kPadCategoryUp = 0x0056b039;
constexpr std::uint32_t kPadPieceDown = 0x0056b069;
constexpr std::uint32_t kPadPieceUp = 0x0056b059;
constexpr std::uint32_t kMsgNoCell = 0x0052a574;
constexpr std::uint32_t kMsgFaultyState = 0x0052a620;
constexpr std::uint32_t kMsgNoItemList = 0x0052a4f0;
}  // namespace

// ---- the pad ----------------------------------------------------------------------------------------------------------------------

// 0x00434090: the pad of the editor: turn the piece, place, remove, change the category or the piece, and walk the cursor (a held direction repeats every four frames).
void LevelEditor::handle_input(std::uint32_t self) {
    (void)virtual_call(self, 0);
    const std::uint32_t cursor = word_at(m_, self + 0x1b8);
    const std::uint32_t ui = word_at(m_, self + 0x1a8);
    auto table = [&]() {
        set_word_at(m_, self + 0x1b0, word_at(m_, kObjectTables + word_at(m_, self + 0x1d4) * 0x44));
        s_.check(word_at(m_, self + 0x1b0) != 0, kMsgNoItemList);
    };
    auto piece_into_hand = [&]() { engine(0x00437950, {cursor, engine_value(0x004371d0, {ui})}); };
    if (global_byte(m_, kPadRotate) != 0) {
        set_global_byte(m_, kPadRotate, 0);
        engine(0x00437c30, {cursor, 1});
        engine(0x004ad620, {0x97, 0x2000, 0});
    }
    if (global_byte(m_, kPadPlace) != 0) {
        set_global_byte(m_, kPadPlace, 0);
        place_item(self);
        table();
    }
    if (global_byte(m_, kPadRemove) != 0) {
        set_global_byte(m_, kPadRemove, 0);
        remove_item(self);
        table();
    }
    if (global_byte(m_, kPadCategoryDown) != 0) {
        set_global_byte(m_, kPadCategoryDown, 0);
        engine(0x00436fa0, {ui, 0xffffffff});
        piece_into_hand();
        engine(0x004ad620, {0x90, 0x2000, 0});
    }
    if (global_byte(m_, kPadCategoryUp) != 0) {
        set_global_byte(m_, kPadCategoryUp, 0);
        engine(0x00436fa0, {ui, 1});
        piece_into_hand();
        engine(0x004ad620, {0x90, 0x2000, 0});
    }
    if (global_byte(m_, kPadPieceDown) != 0) {
        set_global_byte(m_, kPadPieceDown, 0);
        engine(0x00436da0, {ui, 0xffffffff});
        piece_into_hand();
        engine(0x004ad620, {0x90, 0x2000, 0});
    }
    if (global_byte(m_, kPadPieceUp) != 0) {
        set_global_byte(m_, kPadPieceUp, 0);
        engine(0x00436da0, {ui, 1});
        piece_into_hand();
        engine(0x004ad620, {0x90, 0x2000, 0});
    }
    // each direction: held and the delay is over, or its edge byte is up (which is then put down).  Left and right share one register, up and down the other: the second of a
    // pair wins.
    const bool idle = word_at(m_, self + 0x1d8) == 0;
    auto pressed = [&](std::uint32_t pad) {
        const bool now = (byte_at(m_, pad) != 0 && idle) || byte_at(m_, pad + 1) != 0;
        if (now) {
            set_byte_at(m_, pad + 1, 0);
        }
        return now;
    };
    std::uint32_t across = 0;
    std::uint32_t along = 0;
    if (pressed(kPadLeft)) {
        across = 1;
    }
    if (pressed(kPadRight)) {
        across = 2;
    }
    if (pressed(kPadUp)) {
        along = 4;
    }
    if (pressed(kPadDown)) {
        along = 8;
    }
    const std::uint32_t buttons = across | along;
    if (buttons != 0 && global_byte(m_, kPadTurn) == 0) {
        set_word_at(m_, self + 0x1d8, 4);
        const std::uint32_t direction = engine_value(0x00437650, {word_at(m_, self + 0x1b4), buttons});
        engine(0x00437d30, {cursor, direction});
        const std::uint32_t kind = word_at(m_, self + 0x1a8) + 0x434;
        if (word_at(m_, kind) == 2) {
            set_word_at(m_, kind, 1);
        }
    }
}

// 0x004343e0: adjust the gap under the cursor: the pad changes how far the end reaches (up and down along one axis, left and right along the other, with the turn of the
// end deciding which axis is which), the rotate button turns the end, the category buttons swap ends.  The reach is kept inside the park's grid; the gaps are then made again.
void LevelEditor::adjust_gap(std::uint32_t self) {
    const std::uint32_t item = global_word(m_, kItems) + word_at(m_, self + 0x1a0) * 0x2c;
    const std::uint32_t end = word_at(m_, self + 0x1a4);
    const std::uint32_t record = item + end;
    std::int32_t turn = byte_at(m_, record + 6);
    const std::int32_t z = byte_at(m_, record + 4);
    std::int32_t ex = byte_at(m_, record + 8);
    std::int32_t ey = byte_at(m_, record + 10);
    const std::int32_t x = byte_at(m_, record);
    const std::uint32_t park = word_at(m_, self + 0x1ac);
    const std::uint32_t cell = word_at(m_, word_at(m_, word_at(m_, park + 0x2348) + static_cast<std::uint32_t>(z) * 4) + (byte_at(m_, record + 2) + static_cast<std::uint32_t>(x) * 5) * 4);
    s_.check(cell != 0, kMsgNoCell);
    const std::int32_t cell_x = static_cast<std::int8_t>(byte_at(m_, cell + 8));
    const std::int32_t cell_z = static_cast<std::int8_t>(byte_at(m_, cell + 10));
    bool changed = false;
    if (global_byte(m_, kPadCategoryUp) != 0 || global_byte(m_, kPadPieceUp) != 0) {
        set_global_byte(m_, kPadCategoryUp, 0);
        set_global_byte(m_, kPadPieceUp, 0);
        engine(0x004ad620, {0x90, 0x2000, 0});
        const std::uint32_t other = 1 - end;
        set_word_at(m_, self + 0x1a4, other);
        const std::uint32_t at = item + other;
        engine(0x00437cd0, {word_at(m_, self + 0x1b8), byte_at(m_, at), byte_at(m_, at + 2), byte_at(m_, at + 4)});
        return;
    }
    if (global_byte(m_, kPadRotate) != 0) {
        set_global_byte(m_, kPadRotate, 0);
        turn = turn + 1;
        if (turn == 4) {
            turn = 0;
        }
        ey = 0;
        ex = 0;
        changed = true;
        set_byte_at(m_, record + 6, static_cast<std::uint8_t>(turn));
    }
    const std::uint32_t answer = engine_value(0x0046f550, {0, 0, 6});
    if (global_byte(m_, kPadTurn) == 0) {
        switch (answer) {
        case 0:
            ++ex;
            changed = true;
            break;
        case 1:
            --ex;
            changed = true;
            break;
        case 2:
            ++ey;
            changed = true;
            break;
        case 3:
            --ey;
            changed = true;
            break;
        default:
            break;
        }
    }
    const std::int32_t grid_x = global_int(m_, kGridW);
    const std::int32_t grid_z = global_int(m_, kGridD);
    switch (turn) {
    case 0:
        if (cell_z + z + ex > grid_z) {
            ex = -1;
        }
        if (z - ey < 0) {
            ey = -1;
        }
        break;
    case 1:
        if (cell_x + x + ex > grid_x) {
            ex = -1;
        }
        if (x - ey < 0) {
            ey = -1;
        }
        break;
    case 2:
        if (z - ex < 0) {
            ex = -1;
        }
        if (cell_z + z + ey > grid_z) {
            ey = -1;
        }
        break;
    case 3:
        if (x - ex < 0) {
            ex = -1;
        }
        if (cell_x + x + ey > grid_x) {
            ey = -1;
        }
        break;
    default:
        break;
    }
    if (!changed) {
        return;
    }
    engine(0x004ad620, {0x11f, 0x2000, 0});
    if (ex >= 0 && ex <= 3) {
        set_byte_at(m_, record + 8, static_cast<std::uint8_t>(ex));
    }
    if (ey >= 0 && ey <= 3) {
        set_byte_at(m_, record + 10, static_cast<std::uint8_t>(ey));
    }
    engine(0x0043cd90, {park, word_at(m_, self + 0x1a0), word_at(m_, self + 0x1a4)});
}

// ---- placing and removing ----------------------------------------------------------------------------------------------------------

// 0x00435be0: the place button.  With a piece in hand the piece is put down (an item the cursor was making is given up); with an item the first end is put down, then the
// second (and the gap menu opens); a refusal sounds the error.  The budget flag is looked at afterwards in every case.
void LevelEditor::place_item(std::uint32_t self) {
    const std::uint32_t mode = word_at(m_, self + 0x194);
    const std::uint32_t cursor = word_at(m_, self + 0x1b8);
    const std::uint32_t park = word_at(m_, self + 0x1ac);
    const std::uint32_t ui = word_at(m_, self + 0x1a8);
    auto finalize = [&]() { (void)engine_out(0x0043d400, {park, self + 0x1d4, 0}, self + 0x1d4, false); };
    auto fail = [&]() {
        engine(0x004ad620, {0x92, 0x2000, 0});
        refresh_budget(self);
    };
    auto open_gap_menu = [&]() {
        engine(0x0043f5e0, {park, 1});
        engine(0x00466de0);
        set_word_at(m_, self + 0x194, 3);
        set_word_at(m_, self + 0x188, 0xd);
    };
    if (mode == 0) {
        if ((engine_value(0x00438610, {cursor}) & 0xff) == 0) {
            fail();
            return;
        }
        if (word_at(m_, self + 0x19c) != 0xffffffff) {
            set_byte_at(m_, global_word(m_, kItems) + word_at(m_, self + 0x19c) * 0x2c + 0xc, 0x80);
            set_word_at(m_, self + 0x19c, 0xffffffff);
            set_word_at(m_, ui + 0x434, 0);
        }
        engine(0x004ad620, {0xea, 0x2000, 0});
        finalize();
        engine(0x00437d30, {cursor, 0xffffffff});
        refresh_budget(self);
        return;
    }
    if (mode != 1) {
        s_.check(false, kMsgFaultyState);
        refresh_budget(self);
        return;
    }
    // an item: which item and end the cursor is on (the end is written through the pointer), then the item is added (the index it got is written through the second pointer)
    set_word_at(m_, self + 0x1a0, engine_out(0x00438360, {cursor, self + 0x1a4}, self + 0x1a4, true));
    const bool added = (engine_out(0x00438c50, {cursor, word_at(m_, self + 0x19c), kScratchAdded}, kScratchAdded, true) & 0xff) != 0;
    if (added) {
        engine(0x004ad620, {0xea, 0x2000, 0});
        finalize();
        engine(0x00438320, {cursor});
        if (word_at(m_, self + 0x19c) == 0xffffffff) {
            set_word_at(m_, ui + 0x434, 1);
            set_word_at(m_, self + 0x19c, global_word(m_, kScratchAdded));
            refresh_budget(self);
            return;
        }
        set_word_at(m_, ui + 0x434, 0);
        set_word_at(m_, self + 0x1a0, word_at(m_, self + 0x19c));
        set_word_at(m_, self + 0x1a4, 1);
        engine(0x0043f5e0, {park, 1});
        engine(0x00466de0);
        set_word_at(m_, self + 0x19c, 0xffffffff);
        set_word_at(m_, self + 0x194, 3);
        set_word_at(m_, self + 0x188, 0xd);
        refresh_budget(self);
        return;
    }
    if (word_at(m_, self + 0x1a0) != 0xffffffff) {
        if (word_at(m_, self + 0x19c) == 0xffffffff) {
            open_gap_menu();
            refresh_budget(self);
            return;
        }
        set_word_at(m_, ui + 0x434, 2);
    }
    fail();
}

// 0x00435e50: the remove button: a piece (mode 0) or the item under the cursor (mode 1).
void LevelEditor::remove_item(std::uint32_t self) {
    const std::uint32_t mode = word_at(m_, self + 0x194);
    const std::uint32_t cursor = word_at(m_, self + 0x1b8);
    const std::uint32_t park = word_at(m_, self + 0x1ac);
    if (mode == 0) {
        if ((engine_value(0x00438ce0, {cursor}) & 0xff) != 0) {
            engine(0x004ad620, {0x18c, 0x2000, 0});
            (void)engine_out(0x0043d400, {park, self + 0x1d4, 0}, self + 0x1d4, false);
        } else {
            engine(0x004ad620, {0x92, 0x2000, 0});
        }
    } else if (mode == 1) {
        if ((engine_value(0x00438dc0, {cursor}) & 0xff) != 0) {
            engine(0x004ad620, {0x18c, 0x2000, 0});
            (void)engine_out(0x0043d400, {park, self + 0x1d4, 0}, self + 0x1d4, false);
            set_word_at(m_, self + 0x19c, 0xffffffff);
            set_word_at(m_, word_at(m_, self + 0x1a8) + 0x434, 0);
        } else {
            engine(0x004ad620, {0x92, 0x2000, 0});
        }
    }
    refresh_budget(self);
}

}  // namespace opentony::runtime::leveleditor
