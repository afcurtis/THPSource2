#include "auto_views.hpp"
#include "level_editor.hpp"
#include "seam_addresses_generated.hpp"

#include <cstring>

namespace opentony::runtime::leveleditor {

using namespace opentony::runtime::autoviews;

// The editor's cursor holds up to four pieces at once (a piece and the pieces carried along with it), each in an element of 0x94 bytes at +0 (+0x4c the piece's model, -1
// when the element is unused; +0x50 its turn; +0x54..+0x68 its place in the park; +0x6c..+0x74 its offset; +0x78..+0x84 its size); +0x250 the model, +0x254 the turn,
// +0x258 and +0x260 the cell, +0x25c the layer it floats at, +0x264 and +0x26c the footprint, +0x270..+0x278 where the cursor is, +0x27c..+0x288 the extent of the group,
// +0x28c..+0x2d8 the swing of the view and +0x2dc the park generator.  After the elements, +0x2f0, come 0x40 object records of 0x4c bytes for the base indicator.

namespace {
constexpr std::uint32_t kElement = 0x94;
constexpr std::uint32_t kRecord = 0x4c;
constexpr std::uint32_t kInModelBytes = 0x24;      // the park generator's source models: 0x24 bytes each, the flag byte of model n at generator + 0x21 + 0x24 * n
constexpr std::uint32_t kOutMap = 0x2348;
constexpr std::uint32_t kSpecialBase = 0x005614a8;  // the piece id of the base indicator
constexpr std::uint32_t kSpecialA = 0x005614b8;
constexpr std::uint32_t kSpecialB = 0x005614a0;
constexpr std::uint32_t kSpecialC = 0x00561184;
constexpr std::uint32_t kGapPiece = 0x0056116c;
}  // namespace

// ---- the element arrays --------------------------------------------------------------------------------------------------------

// 0x004377d0
std::uint32_t LevelEditor::cursor_construct(std::uint32_t self) {
    engine(seam::crt::FUN_00500400, {self, kElement, 4, 0x00437840, 0x00437850});
    engine(seam::crt::FUN_00500400, {self + 0x2f0, kRecord, 0x40, 0x0047fdb0, 0x0047fe20});
    return self;
}

// 0x00437840: an element's constructor
std::uint32_t LevelEditor::cursor_element_construct(std::uint32_t self) {
    engine(0x0047fdb0, {self});
    return self;
}

// 0x00437850: an element's destructor, a jump to the object record's
std::uint32_t LevelEditor::cursor_element_destruct(std::uint32_t self) { return engine_value(0x0047fe20, {self}); }

// 0x00437860
void LevelEditor::cursor_destruct(std::uint32_t self) {
    engine(seam::crt::FUN_0050030c, {self + 0x2f0, kRecord, 0x40, 0x0047fe20});
    engine(seam::crt::FUN_0050030c, {self, kElement, 4, 0x00437850});
}

// 0x004378d0: the cursor begins in the middle of the park, at the first quarter, with nothing in hand.
void LevelEditor::cursor_init(std::uint32_t self, std::uint32_t park) {
    level_editor_cursor_cursor_at(m_, self).set_field_2dc(static_cast<Word>(park));
    level_editor_cursor_cursor_at(m_, self).set_field_254(0);
    level_editor_cursor_cursor_at(m_, self).set_field_270(static_cast<Word>(static_cast<std::uint32_t>(global_int(m_, kGridW) >> 1)));
    level_editor_cursor_cursor_at(m_, self).set_field_274(0);
    level_editor_cursor_cursor_at(m_, self).set_field_258(0);
    level_editor_cursor_cursor_at(m_, self).set_field_278(static_cast<Word>(static_cast<std::uint32_t>(global_int(m_, kGridD) >> 1)));
    level_editor_cursor_cursor_at(m_, self).set_field_260(0);
    level_editor_cursor_cursor_at(m_, self).set_field_264(1);
    level_editor_cursor_cursor_at(m_, self).set_field_26c(1);
    level_editor_cursor_cursor_at(m_, self).set_field_2a4(4);
    level_editor_cursor_cursor_at(m_, self).set_field_2a8(1);
    level_editor_cursor_cursor_at(m_, self).set_field_4c(0xffffffff);
    level_editor_cursor_cursor_at(m_, self).set_field_2e8(0);
}

// 0x00439110
void LevelEditor::cursor_set_label(std::uint32_t self, std::uint32_t value, std::uint32_t small) {
    level_editor_cursor_cursor_at(m_, self).set_field_2e0(static_cast<Word>(value));
    level_editor_cursor_cursor_at(m_, self).set_field_2e4(static_cast<std::uint16_t>(small));
}

// 0x00438400: the integer at +0x8c times the constant at 0x518910, left on the x87 stack (rounded to the 24 bits the game runs the unit at)
float LevelEditor::cursor_scaled(std::uint32_t self) {
    const std::uint32_t bits = global_word(m_, 0x00518910);
    float scale;
    std::memcpy(&scale, &bits, sizeof scale);
    return static_cast<float>(static_cast<long double>(level_editor_cursor_cursor_at(m_, self).field_8c()) * static_cast<long double>(scale));
}

// ---- the piece in hand -------------------------------------------------------------------------------------------------------

// 0x00437950: take a model into the cursor: the elements are cleared, the model's size is read, a solid block also carries its base, and the extent of the group and the
// footprint follow.  Returns 1.
std::uint32_t LevelEditor::cursor_set_model(std::uint32_t self, std::uint32_t model) {
    flag_pieces(self, 0);
    const std::uint32_t park = level_editor_cursor_cursor_at(m_, self).field_2dc();
    for (std::uint32_t k = 0; k < 4; ++k) {
        const std::uint32_t e = self + k * kElement;
        set_word_at(m_, e + 0x4c, 0xffffffff);
        set_word_at(m_, e + 0x50, level_editor_cursor_cursor_at(m_, self).field_254());
        set_word_at(m_, e + 0x6c, 0);
        set_word_at(m_, e + 0x70, 0);
        set_word_at(m_, e + 0x74, 0);
        set_half_at(m_, e + 0x04, 0);
        set_half_at(m_, e + 0x06, 0);
        set_half_at(m_, e + 0x14, 0);
        set_half_at(m_, e + 0x16, 0);
        set_half_at(m_, e + 0x18, 0);
        set_byte_at(m_, e + 0x1c, 0);
        set_byte_at(m_, e + 0x1d, 0);
        set_byte_at(m_, e + 0x1e, 0);
        set_byte_at(m_, e + 0x1f, byte_at(m_, park + 0x14));
        set_word_at(m_, e + 0x24, 0xff00);
        set_word_at(m_, e + 0x20, 0);
    }
    level_editor_cursor_cursor_at(m_, self).set_field_250(static_cast<Word>(model));
    level_editor_cursor_cursor_at(m_, self).set_field_4c(static_cast<Word>(model));
    const std::uint32_t in = park + model * kInModelBytes;
    level_editor_cursor_cursor_at(m_, self).set_field_78(static_cast<Word>(static_cast<std::uint32_t>(sbyte_at(m_, in + 0x22))));
    set_word_at(m_, self + 0x7c, static_cast<std::uint32_t>(sbyte_at(m_, in + 0x23)));
    level_editor_cursor_cursor_at(m_, self).set_field_80(static_cast<Word>(static_cast<std::uint32_t>(sbyte_at(m_, park + (model * 9 + 9) * 4))));
    level_editor_cursor_cursor_at(m_, self).set_field_84(static_cast<Word>(static_cast<std::uint32_t>(sbyte_at(m_, in + 0x25))));
    if ((byte_at(m_, in + 0x21) & 0x10) != 0) {
        const std::int32_t a = static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_78());
        const std::int32_t c = static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_80());
        if (a == 1 && c == 1) {
            level_editor_cursor_cursor_at(m_, self).set_field_e0(static_cast<Word>(global_word(m_, kSpecialA)));
        } else if (a == 2) {
            if (c == 1) {
                level_editor_cursor_cursor_at(m_, self).set_field_e0(static_cast<Word>(global_word(m_, kSpecialB)));
            } else if (c == 2) {
                level_editor_cursor_cursor_at(m_, self).set_field_e0(static_cast<Word>(global_word(m_, kSpecialC)));
            }
        }
        const std::uint32_t base = level_editor_cursor_cursor_at(m_, self).field_e0();
        const std::uint32_t base_in = park + base * kInModelBytes;
        level_editor_cursor_cursor_at(m_, self).set_field_10c(static_cast<Word>(static_cast<std::uint32_t>(sbyte_at(m_, base_in + 0x22))));
        level_editor_cursor_cursor_at(m_, self).set_field_110(static_cast<Word>(static_cast<std::uint32_t>(sbyte_at(m_, base_in + 0x23))));
        level_editor_cursor_cursor_at(m_, self).set_field_114(static_cast<Word>(static_cast<std::uint32_t>(sbyte_at(m_, park + (base * 9 + 9) * 4))));
        level_editor_cursor_cursor_at(m_, self).set_field_118(static_cast<Word>(static_cast<std::uint32_t>(sbyte_at(m_, base_in + 0x25))));
        level_editor_cursor_cursor_at(m_, self).set_field_104(static_cast<Word>(word_at(m_, self + 0x7c)));
    }
    std::int32_t min_x = 0;
    std::int32_t min_y = 0;
    std::int32_t min_z = 0;
    std::int32_t max_x = -1;
    std::int32_t max_y = -1;
    std::int32_t max_z = -1;
    std::int32_t max_h = -1;
    const std::uint32_t objects = word_at(m_, kObjectTables + word_at(m_, park + 0x14) * 0x44);
    for (std::uint32_t k = 0; k < 4; ++k) {
        const std::uint32_t e = self + k * kElement;
        const std::uint32_t index = word_at(m_, e + 0x4c);
        if (index == 0xffffffff) {
            continue;
        }
        set_byte_at(m_, e + 0x1c, byte_at(m_, objects + 0x1c + index * kRecord));
        set_byte_at(m_, e + 0x1e, byte_at(m_, objects + 0x1e + index * kRecord));
        if ((byte_at(m_, level_editor_cursor_cursor_at(m_, self).field_2dc() + 0x21 + index * kInModelBytes) & 8) != 0) {
            set_byte_at(m_, e + 0x1c, 0x81);
            set_byte_at(m_, e + 0x1e, 0x81);
        }
        const std::int32_t x = int_at(m_, e + 0x6c);
        if (x < min_x) {
            min_x = x;
        }
        if (max_x < x + int_at(m_, e + 0x78)) {
            max_x = x + int_at(m_, e + 0x78);
        }
        const std::int32_t y = int_at(m_, e + 0x70);
        if (y < min_y) {
            min_y = y;
        }
        if (max_y < int_at(m_, e + 0x7c) + y) {
            max_y = int_at(m_, e + 0x7c) + y;
        }
        const std::int32_t z = int_at(m_, e + 0x74);
        if (z < min_z) {
            min_z = z;
        }
        if (max_z < z + int_at(m_, e + 0x80)) {
            max_z = z + int_at(m_, e + 0x80);
        }
        if (max_h < int_at(m_, e + 0x84) + y) {
            max_h = int_at(m_, e + 0x84) + y;
        }
    }
    const std::int32_t width = max_x - min_x;
    const std::int32_t depth = max_z - min_z;
    level_editor_cursor_cursor_at(m_, self).set_field_280(static_cast<Word>(static_cast<std::uint32_t>(max_y - min_y)));
    level_editor_cursor_cursor_at(m_, self).set_field_27c(static_cast<Word>(static_cast<std::uint32_t>(width)));
    level_editor_cursor_cursor_at(m_, self).set_field_284(static_cast<Word>(static_cast<std::uint32_t>(depth)));
    level_editor_cursor_cursor_at(m_, self).set_field_288(static_cast<Word>(static_cast<std::uint32_t>(max_h - min_y)));
    switch (level_editor_cursor_cursor_at(m_, self).field_254()) {
    case 0:
    case 2:
        level_editor_cursor_cursor_at(m_, self).set_field_264(static_cast<Word>(static_cast<std::uint32_t>(width)));
        level_editor_cursor_cursor_at(m_, self).set_field_26c(static_cast<Word>(static_cast<std::uint32_t>(depth)));
        break;
    case 1:
    case 3:
        level_editor_cursor_cursor_at(m_, self).set_field_264(static_cast<Word>(static_cast<std::uint32_t>(depth)));
        level_editor_cursor_cursor_at(m_, self).set_field_26c(static_cast<Word>(static_cast<std::uint32_t>(width)));
        break;
    default:
        break;
    }
    level_editor_cursor_cursor_at(m_, self).set_field_2a4(4);
    cursor_update(self);
    level_editor_cursor_cursor_at(m_, self).set_field_2a8(1);
    return 1;
}

// 0x00437c30: turn the group by quarters
void LevelEditor::cursor_rotate(std::uint32_t self, std::uint32_t quarters) {
    flag_pieces(self, 0);
    const std::uint32_t turned = level_editor_cursor_cursor_at(m_, self).field_254() + quarters;
    level_editor_cursor_cursor_at(m_, self).set_field_28c(static_cast<Word>((level_editor_cursor_cursor_at(m_, self).field_254() + 4) * 0x400));
    level_editor_cursor_cursor_at(m_, self).set_field_290(static_cast<Word>((turned + 4) * 0x400));
    level_editor_cursor_cursor_at(m_, self).set_field_254(static_cast<Word>(turned & 3));
    switch (turned & 3) {
    case 0:
    case 2:
        level_editor_cursor_cursor_at(m_, self).set_field_264(static_cast<Word>(level_editor_cursor_cursor_at(m_, self).field_27c()));
        level_editor_cursor_cursor_at(m_, self).set_field_26c(static_cast<Word>(level_editor_cursor_cursor_at(m_, self).field_284()));
        break;
    default:
        level_editor_cursor_cursor_at(m_, self).set_field_264(static_cast<Word>(level_editor_cursor_cursor_at(m_, self).field_284()));
        level_editor_cursor_cursor_at(m_, self).set_field_26c(static_cast<Word>(level_editor_cursor_cursor_at(m_, self).field_27c()));
        break;
    }
    level_editor_cursor_cursor_at(m_, self).set_field_2a4(0);
    cursor_update(self);
}

// 0x00437cd0: put the cursor in a cell and layer
std::uint32_t LevelEditor::cursor_move_to(std::uint32_t self, std::uint32_t x, std::uint32_t layer, std::uint32_t z) {
    flag_pieces(self, 0);
    level_editor_cursor_cursor_at(m_, self).set_field_270(static_cast<Word>(x));
    level_editor_cursor_cursor_at(m_, self).set_field_274(static_cast<Word>(layer));
    const std::uint32_t swing = (level_editor_cursor_cursor_at(m_, self).field_254() + 4) * 0x400;
    level_editor_cursor_cursor_at(m_, self).set_field_278(static_cast<Word>(z));
    level_editor_cursor_cursor_at(m_, self).set_field_290(static_cast<Word>(swing));
    level_editor_cursor_cursor_at(m_, self).set_field_28c(static_cast<Word>(swing));
    level_editor_cursor_cursor_at(m_, self).set_field_2a4(0);
    cursor_update(self);
    return 1;
}

// 0x00437d30: a step with the pad (bits 0-1 sideways, 2-3 forward); a transparent piece moves two cells
std::uint32_t LevelEditor::cursor_step(std::uint32_t self, std::uint32_t buttons) {
    std::uint32_t step = 1;
    if ((byte_at(m_, level_editor_cursor_cursor_at(m_, self).field_2dc() + 0x21 + level_editor_cursor_cursor_at(m_, self).field_4c() * kInModelBytes) & 8) != 0) {
        step = 2;
    }
    if ((buttons & 3) == 1) {
        level_editor_cursor_cursor_at(m_, self).set_field_270(static_cast<Word>(level_editor_cursor_cursor_at(m_, self).field_270() - step));
    } else if ((buttons & 3) == 2) {
        level_editor_cursor_cursor_at(m_, self).set_field_270(static_cast<Word>(level_editor_cursor_cursor_at(m_, self).field_270() + step));
    }
    if ((buttons & 0xc) == 4) {
        level_editor_cursor_cursor_at(m_, self).set_field_278(static_cast<Word>(level_editor_cursor_cursor_at(m_, self).field_278() - step));
    } else if ((buttons & 0xc) == 8) {
        level_editor_cursor_cursor_at(m_, self).set_field_278(static_cast<Word>(level_editor_cursor_cursor_at(m_, self).field_278() + step));
    }
    (void)cursor_move_to(self, level_editor_cursor_cursor_at(m_, self).field_270(), level_editor_cursor_cursor_at(m_, self).field_274(), level_editor_cursor_cursor_at(m_, self).field_278());
    return 1;
}

// 0x00437dc0: work out the cell the cursor's group covers (kept inside the park), the layer it floats at, the pose of every element in the park's units and the swing the
// view makes to get there.
void LevelEditor::cursor_update(std::uint32_t self) {
    const std::int32_t width = static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_264());
    const std::int32_t depth = static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_26c());
    const std::int32_t extent_y = static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_280());
    bool centre = true;
    std::uint32_t mode = 0;
    if (((static_cast<std::uint32_t>(width) ^ static_cast<std::uint32_t>(depth)) & 1) != 0) {
        mode = level_editor_cursor_cursor_at(m_, self).field_254();
        centre = mode <= 3;
    }
    if (centre) {
        std::int32_t w = width;
        std::int32_t d = depth;
        switch (mode) {
        case 1:
            d = depth + 1;
            break;
        case 2:
            w = width + 1;
            d = depth + 1;
            break;
        case 3:
            w = width + 1;
            break;
        default:
            break;
        }
        level_editor_cursor_cursor_at(m_, self).set_field_258(static_cast<Word>(static_cast<std::uint32_t>(static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_270()) - (w >> 1))));
        level_editor_cursor_cursor_at(m_, self).set_field_260(static_cast<Word>(static_cast<std::uint32_t>(static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_278()) - (d >> 1))));
    }
    level_editor_cursor_cursor_at(m_, self).set_field_268(static_cast<Word>(static_cast<std::uint32_t>(extent_y)));
    level_editor_cursor_cursor_at(m_, self).set_field_25c(static_cast<Word>(level_editor_cursor_cursor_at(m_, self).field_274()));
    while (static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_258()) < 0) {
        level_editor_cursor_cursor_at(m_, self).set_field_270(static_cast<Word>(level_editor_cursor_cursor_at(m_, self).field_270() + 1));
        level_editor_cursor_cursor_at(m_, self).set_field_258(static_cast<Word>(level_editor_cursor_cursor_at(m_, self).field_258() + 1));
    }
    while (global_int(m_, kGridW) < width + static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_258())) {
        level_editor_cursor_cursor_at(m_, self).set_field_270(static_cast<Word>(level_editor_cursor_cursor_at(m_, self).field_270() - 1));
        level_editor_cursor_cursor_at(m_, self).set_field_258(static_cast<Word>(level_editor_cursor_cursor_at(m_, self).field_258() - 1));
    }
    while (static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_260()) < 0) {
        level_editor_cursor_cursor_at(m_, self).set_field_278(static_cast<Word>(level_editor_cursor_cursor_at(m_, self).field_278() + 1));
        level_editor_cursor_cursor_at(m_, self).set_field_260(static_cast<Word>(level_editor_cursor_cursor_at(m_, self).field_260() + 1));
    }
    while (global_int(m_, kGridD) < depth + static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_260())) {
        level_editor_cursor_cursor_at(m_, self).set_field_278(static_cast<Word>(level_editor_cursor_cursor_at(m_, self).field_278() - 1));
        level_editor_cursor_cursor_at(m_, self).set_field_260(static_cast<Word>(level_editor_cursor_cursor_at(m_, self).field_260() - 1));
    }
    const std::uint32_t park = level_editor_cursor_cursor_at(m_, self).field_2dc();
    if ((byte_at(m_, park + 0x21 + level_editor_cursor_cursor_at(m_, self).field_4c() * kInModelBytes) & 8) != 0) {
        if ((level_editor_cursor_cursor_at(m_, self).field_258() & 1) != 0) {
            level_editor_cursor_cursor_at(m_, self).set_field_258(static_cast<Word>(level_editor_cursor_cursor_at(m_, self).field_258() - 1));
            level_editor_cursor_cursor_at(m_, self).set_field_270(static_cast<Word>(level_editor_cursor_cursor_at(m_, self).field_270() - 1));
        }
        if ((level_editor_cursor_cursor_at(m_, self).field_260() & 1) != 0) {
            level_editor_cursor_cursor_at(m_, self).set_field_260(static_cast<Word>(level_editor_cursor_cursor_at(m_, self).field_260() - 1));
            level_editor_cursor_cursor_at(m_, self).set_field_278(static_cast<Word>(level_editor_cursor_cursor_at(m_, self).field_278() - 1));
        }
    }
    level_editor_cursor_cursor_at(m_, self).set_field_294(static_cast<Word>(level_editor_cursor_cursor_at(m_, self).field_2d0()));
    level_editor_cursor_cursor_at(m_, self).set_field_298(static_cast<Word>(level_editor_cursor_cursor_at(m_, self).field_2d8()));
    const std::int32_t cell = global_int(m_, kCellSize);
    const std::int32_t gw = global_int(m_, kGridW);
    const std::int32_t gd = global_int(m_, kGridD);
    const std::int32_t px = (((-gw) >> 1) + static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_258())) * cell + ((width * cell) >> 1);
    const std::int32_t pz = ((gd >> 1) - static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_260())) * cell - ((depth * cell) >> 1);
    level_editor_cursor_cursor_at(m_, self).set_field_29c(static_cast<Word>(static_cast<std::uint32_t>(px) << 12));
    level_editor_cursor_cursor_at(m_, self).set_field_2a0(static_cast<Word>(static_cast<std::uint32_t>(pz) << 12));
    level_editor_cursor_cursor_at(m_, self).set_field_2b4(static_cast<Word>(level_editor_cursor_cursor_at(m_, self).field_2ac()));
    level_editor_cursor_cursor_at(m_, self).set_field_2b8(static_cast<Word>(level_editor_cursor_cursor_at(m_, self).field_2b0()));
    level_editor_cursor_cursor_at(m_, self).set_field_2bc(static_cast<Word>(static_cast<std::uint32_t>((((-gw) >> 1) + static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_270())) * cell) << 12));
    level_editor_cursor_cursor_at(m_, self).set_field_2c0(static_cast<Word>(static_cast<std::uint32_t>(((gd >> 1) - static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_278())) * cell) << 12));
    const std::int32_t lift = -(extent_y * global_int(m_, kCellDepth)) & ~1;
    level_editor_cursor_cursor_at(m_, self).set_field_2d4(static_cast<Word>(static_cast<std::uint32_t>(lift) << 11));
    const bool inside = static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_258()) >= 0 && static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_258()) + static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_264()) <= gw && static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_260()) >= 0 &&
                        static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_260()) + static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_26c()) <= gd;
    s_.check(inside, kMsgCursorOut);
    std::int32_t top = 0;
    for (std::int32_t j = 0; j < static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_26c()); ++j) {
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_264()); ++i) {
            const std::int32_t zz = static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_260()) + j;
            s_.check(zz >= 0 && zz < gd, kMsgGridZ);
            const std::uint32_t row = word_at(m_, global_word(m_, kRows) + static_cast<std::uint32_t>(zz) * 4);
            const std::int32_t layers = static_cast<std::int32_t>(byte_at(m_, row + (level_editor_cursor_cursor_at(m_, self).field_258() + static_cast<std::uint32_t>(i)) * 0x10 + 0xf)) - 1;
            if (layers > top) {
                top = layers;
            }
        }
    }
    level_editor_cursor_cursor_at(m_, self).set_field_25c(static_cast<Word>(static_cast<std::uint32_t>(top)));
    std::int32_t previous = -1;
    std::uint32_t previous_at = 0;
    for (std::uint32_t k = 0; k < 4; ++k) {
        const std::uint32_t e = self + k * kElement;
        if (word_at(m_, e + 0x4c) == 0xffffffff) {
            continue;
        }
        const std::uint32_t turn = level_editor_cursor_cursor_at(m_, self).field_254();
        set_word_at(m_, e + 0x50, turn);
        const std::int32_t ox = int_at(m_, e + 0x6c);
        const std::int32_t oy = int_at(m_, e + 0x70);
        const std::int32_t oz = int_at(m_, e + 0x74);
        const std::int32_t ow = int_at(m_, e + 0x78);
        const std::int32_t od = int_at(m_, e + 0x7c);
        const std::int32_t oh = int_at(m_, e + 0x80);
        const std::int32_t cx = static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_258());
        const std::int32_t cz = static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_260());
        const std::int32_t fw = static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_264());
        const std::int32_t fd = static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_26c());
        bool placed = true;
        switch (turn) {
        case 0:
            set_word_at(m_, e + 0x54, static_cast<std::uint32_t>(ox + cx));
            set_word_at(m_, e + 0x5c, static_cast<std::uint32_t>(oz + cz));
            set_word_at(m_, e + 0x60, static_cast<std::uint32_t>(ow));
            set_word_at(m_, e + 0x68, static_cast<std::uint32_t>(oh));
            break;
        case 1:
            set_word_at(m_, e + 0x54, static_cast<std::uint32_t>(oz + cx));
            set_word_at(m_, e + 0x5c, static_cast<std::uint32_t>(((cz - ox) - ow) + fd));
            set_word_at(m_, e + 0x60, static_cast<std::uint32_t>(oh));
            set_word_at(m_, e + 0x68, static_cast<std::uint32_t>(ow));
            break;
        case 2:
            set_word_at(m_, e + 0x54, static_cast<std::uint32_t>(((cx - ox) - ow) + fw));
            set_word_at(m_, e + 0x5c, static_cast<std::uint32_t>(((cz - oh) - oz) + fd));
            set_word_at(m_, e + 0x60, static_cast<std::uint32_t>(ow));
            set_word_at(m_, e + 0x68, static_cast<std::uint32_t>(oh));
            break;
        case 3:
            set_word_at(m_, e + 0x54, static_cast<std::uint32_t>(((cx - oh) - oz) + fw));
            set_word_at(m_, e + 0x5c, static_cast<std::uint32_t>(ox + cz));
            set_word_at(m_, e + 0x60, static_cast<std::uint32_t>(oh));
            set_word_at(m_, e + 0x68, static_cast<std::uint32_t>(ow));
            break;
        default:
            placed = false;
            break;
        }
        (void)placed;
        set_word_at(m_, e + 0x58, static_cast<std::uint32_t>(oy));
        set_word_at(m_, e + 0x64, static_cast<std::uint32_t>(od));
        set_word_at(m_, e + 0x8c, static_cast<std::uint32_t>(-((oy + top) * global_int(m_, kCellDepth))) << 12);
        set_half_at(m_, e + 0x1a, half_at(m_, e + 0x4c));
        if (previous >= 0) {
            set_word_at(m_, self + previous_at + 0x20, e);
        }
        previous = static_cast<std::int32_t>(k);
        previous_at = k * kElement;
    }
    level_editor_cursor_cursor_at(m_, self).set_field_2a8(0);
    indicator(self);
    mark_cursor_gaps(self);
}

// 0x00438320: the pieces with a gap are marked for the item under the cursor (a gap piece asks for the top piece of the cell, anything else clears the mark)
void LevelEditor::mark_cursor_gaps(std::uint32_t self) {
    const std::uint32_t park = word_at(m_, self + 0x2dc);
    if (word_at(m_, self + 0x4c) != global_word(m_, kGapPiece)) {
        engine(0x0043f5a0, {park, 0});
        return;
    }
    engine(0x0043f5a0, {park, top_piece(self)});
}

// 0x00438e00: the top piece of the cell the cursor is on (the highest layer that holds one), or 0
std::uint32_t LevelEditor::top_piece(std::uint32_t self) {
    const std::uint32_t z = word_at(m_, self + 0x260);
    const std::uint32_t x = word_at(m_, self + 0x258);
    const std::uint32_t row = word_at(m_, global_word(m_, kRows) + z * 4);
    std::int32_t layer = static_cast<std::int32_t>(byte_at(m_, row + x * 0x10 + 0xf)) - 1;
    if (layer < 0) {
        return 0;
    }
    std::uint32_t at = word_at(m_, word_at(m_, word_at(m_, self + 0x2dc) + kOutMap) + z * 4) + (static_cast<std::uint32_t>(layer) + x * 5) * 4;
    while (word_at(m_, at) == 0) {
        --layer;
        at -= 4;
        if (layer < 0) {
            return 0;
        }
    }
    return word_at(m_, at);
}

// 0x00438360: the item record that has an end at the cell of the top piece (not a free or disabled one); -1 when there is none, and the end it is at goes to `*end`.
std::int32_t LevelEditor::find_item(std::uint32_t self, std::uint32_t end) {
    const std::uint32_t piece = top_piece(self);
    if (piece == 0) {
        return -1;
    }
    const std::uint32_t x = byte_at(m_, piece + 5);
    const std::uint32_t layer = byte_at(m_, piece + 6);
    const std::uint32_t z = byte_at(m_, piece + 7);
    for (std::uint32_t i = 0; i < 10; ++i) {
        const std::uint32_t item = global_word(m_, kItems) + i * 0x2c;
        const std::uint8_t marker = byte_at(m_, item + 0xc);
        for (std::uint32_t k = 0; k < 2; ++k) {
            if (marker == 0x80 || marker == 0x81) {
                continue;
            }
            if (byte_at(m_, item + k) == x && byte_at(m_, item + 2 + k) == layer && byte_at(m_, item + 4 + k) == z) {
                if (end != 0) {
                    set_word_at(m_, end, k);
                }
                return static_cast<std::int32_t>(i);
            }
        }
    }
    return -1;
}

// 0x00438c50: add an item at the top piece's cell if the budget allows, the piece is not already an item's end and may take one; true when a record was taken.
bool LevelEditor::try_add_item(std::uint32_t self, std::uint32_t index, std::uint32_t result) {
    if (static_cast<std::int32_t>(engine_value(0x00435f60)) <= 0 && index == 0xffffffff) {
        return false;
    }
    const std::uint32_t piece = top_piece(self);
    if (piece == 0 || word_at(m_, piece + 0x234) != 0 || (byte_at(m_, word_at(m_, piece + 0x224) + 9) & 0x20) != 0) {
        return false;
    }
    const std::uint32_t item = engine_value(0x0043cc30, {byte_at(m_, piece + 5), byte_at(m_, piece + 6), byte_at(m_, piece + 7), index});
    set_word_at(m_, result, item);
    return item != 0xffffffff;
}

// 0x00438ce0: take the pieces under the cursor's footprint away (once without force, and when that took nothing once forcing solid blocks); true when something went.
bool LevelEditor::remove_selected(std::uint32_t self) {
    bool found = false;
    set_global_word(m_, kScratchLimit, 0);
    for (std::uint32_t pass = 0; pass < 2;) {
        const bool force = pass == 1;
        for (std::int32_t z = int_at(m_, self + 0x260); z < int_at(m_, self + 0x260) + int_at(m_, self + 0x26c); ++z) {
            for (std::int32_t x = int_at(m_, self + 0x258); x < int_at(m_, self + 0x258) + int_at(m_, self + 0x264); ++x) {
                if ((engine_value(0x0043cf20, {word_at(m_, self + 0x2dc), static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(z), kScratchLimit, force ? 1u : 0u}) & 0xff) != 0) {
                    found = true;
                }
            }
        }
        if (force || !found) {
            ++pass;
            continue;
        }
        break;
    }
    (void)cursor_move_to(self, word_at(m_, self + 0x270), word_at(m_, self + 0x274), word_at(m_, self + 0x278));
    return found;
}

// 0x00438dc0: take away the item that has the cell's top piece as an end
bool LevelEditor::remove_item_under(std::uint32_t self) {
    const std::uint32_t piece = top_piece(self);
    if (piece == 0 || word_at(m_, piece + 0x234) == 0) {
        return false;
    }
    engine(0x0043ce00, {byte_at(m_, piece + 5), byte_at(m_, piece + 6), byte_at(m_, piece + 7)});
    return true;
}

// 0x00438410: a step of the swing: the view and the cursor's pose are carried a quarter of the way along, the pieces flagged, and each element placed from the swing's sine
// and cosine
void LevelEditor::cursor_animate(std::uint32_t self) {
    const std::int32_t step = static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_2a4());
    auto lerp = [&](std::uint32_t to, std::uint32_t from) {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(int_at(m_, to) - int_at(m_, from)) * static_cast<std::uint32_t>(step)) >> 2;
    };
    set_word_at(m_, self + 0x2cc, static_cast<std::uint32_t>(lerp(self + 0x290, self + 0x28c) + static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_28c())) & 0xfff);
    level_editor_cursor_cursor_at(m_, self).set_field_2d0(static_cast<Word>(static_cast<std::uint32_t>(lerp(self + 0x29c, self + 0x294) + static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_294()))));
    level_editor_cursor_cursor_at(m_, self).set_field_2d8(static_cast<Word>(static_cast<std::uint32_t>(lerp(self + 0x2a0, self + 0x298) + static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_298()))));
    level_editor_cursor_cursor_at(m_, self).set_field_2ac(static_cast<Word>(static_cast<std::uint32_t>(lerp(self + 0x2bc, self + 0x2b4) + static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_2b4()))));
    level_editor_cursor_cursor_at(m_, self).set_field_2b0(static_cast<Word>(static_cast<std::uint32_t>(lerp(self + 0x2c0, self + 0x2b8) + static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_2b8()))));
    if (level_editor_cursor_cursor_at(m_, self).field_2a8() != 0) {
        flag_pieces(self, 1);
        level_editor_cursor_cursor_at(m_, self).set_field_2a8(0);
    }
    if (static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_2a4()) < 4) {
        flag_pieces(self, 1);
        level_editor_cursor_cursor_at(m_, self).set_field_2a4(static_cast<Word>(level_editor_cursor_cursor_at(m_, self).field_2a4() + 1));
        if (static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_2a4()) == 4) {
            level_editor_cursor_cursor_at(m_, self).set_field_2a8(1);
        }
    }
    for (std::uint32_t k = 0; k < 4; ++k) {
        const std::uint32_t e = self + k * kElement;
        if (word_at(m_, e + 0x4c) == 0xffffffff) {
            continue;
        }
        const std::uint32_t angle = word_at(m_, self + 0x2cc);
        const std::int32_t sine = static_cast<std::int32_t>(engine_value(0x004f39b0, {angle}));
        const std::int32_t first = sine * int_at(m_, e + 0x74);
        const std::int32_t cosine = static_cast<std::int32_t>(engine_value(0x004f3980, {angle}));
        const std::int32_t second = cosine * int_at(m_, e + 0x6c);
        const std::int32_t x = static_cast<std::int32_t>(static_cast<std::uint32_t>(second - first) * global_word(m_, kCellSize)) + static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_2d0());
        set_word_at(m_, e + 0x88, static_cast<std::uint32_t>(x));
        const std::int32_t cosine2 = static_cast<std::int32_t>(engine_value(0x004f3980, {angle}));
        const std::int32_t third = cosine2 * int_at(m_, e + 0x74);
        const std::int32_t sine2 = static_cast<std::int32_t>(engine_value(0x004f39b0, {angle}));
        const std::int32_t fourth = sine2 * int_at(m_, e + 0x6c);
        const std::int32_t z = static_cast<std::int32_t>(static_cast<std::uint32_t>(third + fourth) * global_word(m_, kCellSize)) + static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_2d8());
        set_word_at(m_, e + 0x90, static_cast<std::uint32_t>(z));
        set_word_at(m_, e + 0x08, word_at(m_, e + 0x88));
        set_word_at(m_, e + 0x0c, word_at(m_, e + 0x8c));
        set_word_at(m_, e + 0x10, word_at(m_, e + 0x90));
        set_half_at(m_, e + 0x16, static_cast<std::uint32_t>(0x1000 - half_at(m_, self + 0x2cc)));
    }
}

// 0x004385b0: draw the pieces in hand (a transparent model is drawn see-through), then the base indicator
void LevelEditor::cursor_draw(std::uint32_t self) {
    const std::uint32_t park = level_editor_cursor_cursor_at(m_, self).field_2dc();
    const bool see_through = ((byte_at(m_, park + level_editor_cursor_cursor_at(m_, self).field_4c() * kInModelBytes + 0x21) >> 3) & 1) != 0;
    if (see_through) {
        engine(0x0043fda0, {park + level_editor_cursor_cursor_at(m_, self).field_4c() * kInModelBytes + 0x18, 1});
    }
    engine(0x0045f530, {self});
    if (see_through) {
        engine(0x0043fda0, {level_editor_cursor_cursor_at(m_, self).field_2dc() + level_editor_cursor_cursor_at(m_, self).field_4c() * kInModelBytes + 0x18, 0});
    }
    draw_base(self);
}

// 0x00439540: the indicator of the base of a tall piece is drawn with its model's flag set for the draw
void LevelEditor::draw_base(std::uint32_t self) {
    if (byte_at(m_, self + 0x2ec) == 0) {
        return;
    }
    const std::uint32_t saved = global_word(m_, kPolygonFlag);
    const std::uint32_t slot = global_word(m_, kSpecialBase) * kInModelBytes;
    set_global_word(m_, kPolygonFlag, 0x80);
    const std::uint32_t flags = word_at(m_, word_at(m_, self + 0x2dc) + slot + 0x1c);
    set_byte_at(m_, flags + 1, byte_at(m_, flags + 1) | 4);
    engine(0x0045f530, {self + 0x2f0});
    const std::uint32_t after = word_at(m_, word_at(m_, self + 0x2dc) + slot + 0x1c);
    set_half_at(m_, after, half_at(m_, after) & 0xfbff);
    set_global_word(m_, kPolygonFlag, saved);
}

// 0x00438610: put the pieces in hand down at the cursor, when the budgets allow it, the footprint is level and free, and the colours of the neighbours match.
bool LevelEditor::cursor_place(std::uint32_t self) {
    const std::uint32_t park = level_editor_cursor_cursor_at(m_, self).field_2dc();
    if (static_cast<std::int32_t>(engine_value(0x00435f60)) <= 0 || static_cast<std::int32_t>(engine_value(0x00440170)) <= 0 ||
        static_cast<std::int32_t>(engine_value(0x0043f710)) <= 0) {
        return false;
    }
    if (level_editor_cursor_cursor_at(m_, self).field_4c() == global_word(m_, kStartPiece) && int_at(m_, park + 0x3698) >= 6) {
        return false;
    }
    const std::int32_t gw = global_int(m_, kGridW);
    const std::int32_t gd = global_int(m_, kGridD);
    s_.check(static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_260()) >= 0 && static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_260()) < gd, kMsgGridZ);
    const std::int32_t x0 = static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_258());
    const std::int32_t z0 = static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_260());
    const std::int32_t floor_layer = static_cast<std::int32_t>(byte_at(m_, word_at(m_, global_word(m_, kRows) + static_cast<std::uint32_t>(z0) * 4) + static_cast<std::uint32_t>(x0) * 0x10 + 0xf)) - 1;
    const std::uint32_t model = level_editor_cursor_cursor_at(m_, self).field_4c();
    const std::uint32_t flags = byte_at(m_, park + model * kInModelBytes + 0x21);
    const std::int32_t reach = (flags & 0x10) != 0 ? int_at(m_, self + 0x7c) + floor_layer : floor_layer;
    if (reach >= 5) {
        return false;
    }
    for (std::int32_t z = z0; z < z0 + static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_26c()); ++z) {
        for (std::int32_t x = static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_258()); x < static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_258()) + static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_264()); ++x) {
            s_.check(z >= 0 && z < gd, kMsgGridZ);
            const std::uint32_t row = word_at(m_, global_word(m_, kRows) + static_cast<std::uint32_t>(z) * 4);
            if (static_cast<std::int32_t>(byte_at(m_, row + static_cast<std::uint32_t>(x) * 0x10 + 0xf)) - 1 != floor_layer) {
                return false;
            }
            const std::uint32_t map = word_at(m_, word_at(m_, park + kOutMap) + static_cast<std::uint32_t>(z) * 4);
            if (word_at(m_, map + (static_cast<std::uint32_t>(floor_layer) + static_cast<std::uint32_t>(x) * 5) * 4) != 0) {
                return false;
            }
        }
    }
    s_.check(model != 0xffffffff, kMsgWhatModel);
    const std::int32_t layer = level_editor_cursor_cursor_at(m_, self).field_58() + floor_layer;
    if ((flags & 8) != 0 && layer > 0) {
        return false;
    }
    const std::uint32_t turn = level_editor_cursor_cursor_at(m_, self).field_254();
    const std::int32_t fw = static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_264());
    const std::int32_t fd = static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_26c());
    const std::uint32_t map = word_at(m_, park + kOutMap);
    auto neighbour = [&](std::uint32_t piece, std::uint32_t mine, std::uint32_t theirs) {
        const std::uint32_t a = engine_value(0x00444450, {model, mine, turn});
        const std::uint32_t b = engine_value(0x00444450, {byte_at(m_, word_at(m_, piece + 0x224) + 8), theirs, byte_at(m_, piece + 0xc)});
        return (engine_value(0x00444510, {a, b}) & 0xff) != 0;
    };
    const std::uint32_t slot = static_cast<std::uint32_t>(layer);
    if (x0 > 0) {
        const std::uint32_t piece = word_at(m_, word_at(m_, map + static_cast<std::uint32_t>(z0) * 4) + (slot + static_cast<std::uint32_t>(x0) * 5) * 4 - 0x14);
        if (piece != 0 && !neighbour(piece, 2, 0)) {
            return false;
        }
    }
    if (fw + x0 < gw) {
        const std::uint32_t piece = word_at(m_, word_at(m_, map + static_cast<std::uint32_t>(z0) * 4) + (slot + static_cast<std::uint32_t>(fw + x0) * 5) * 4);
        if (piece != 0 && !neighbour(piece, 0, 2)) {
            return false;
        }
    }
    if (z0 > 0) {
        const std::uint32_t piece = word_at(m_, word_at(m_, map + static_cast<std::uint32_t>(z0 - 1) * 4) + (slot + static_cast<std::uint32_t>(x0) * 5) * 4);
        if (piece != 0 && !neighbour(piece, 1, 3)) {
            return false;
        }
    }
    if (z0 + fd < gd) {
        const std::uint32_t piece = word_at(m_, word_at(m_, map + static_cast<std::uint32_t>(z0 + fd) * 4) + (slot + static_cast<std::uint32_t>(x0) * 5) * 4);
        if (piece != 0 && !neighbour(piece, 3, 1)) {
            return false;
        }
    }
    engine(0x0043c770, {park, static_cast<std::uint32_t>(x0), static_cast<std::uint32_t>(z0), slot, turn, model});
    s_.check(static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_260()) >= 0 && static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_260()) < gd, kMsgGridZ);
    const std::uint32_t cell = word_at(m_, global_word(m_, kRows) + level_editor_cursor_cursor_at(m_, self).field_260() * 4) + level_editor_cursor_cursor_at(m_, self).field_258() * 0x10 + 0xf;
    if (layer >= static_cast<std::int32_t>(byte_at(m_, cell))) {
        set_byte_at(m_, cell, byte_at(m_, self + 0x7c) + slot);
    }
    if ((byte_at(m_, level_editor_cursor_cursor_at(m_, self).field_2dc() + level_editor_cursor_cursor_at(m_, self).field_4c() * kInModelBytes + 0x21) & 0x10) != 0) {
        for (std::int32_t z = static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_260()); z < static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_260()) + static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_26c()); ++z) {
            for (std::int32_t x = static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_258()); x < static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_258()) + static_cast<std::int32_t>(level_editor_cursor_cursor_at(m_, self).field_264()); ++x) {
                const std::uint32_t row = word_at(m_, global_word(m_, kRows) + static_cast<std::uint32_t>(z) * 4);
                set_byte_at(m_, row + static_cast<std::uint32_t>(x) * 0x10 + 0xf, slot + byte_at(m_, self + 0x7c) + 1);
                // the layers above the piece in the cell are emptied (as many as the model is tall)
                for (std::int32_t k = 0; k < int_at(m_, self + 0x7c); ++k) {
                    set_half_at(m_, row + (slot + static_cast<std::uint32_t>(x) * 8) * 2 + 2 + 2 * static_cast<std::uint32_t>(k), 0xffff);
                }
            }
        }
    }
    return true;
}

// 0x00438e60: mark or unmark the edges and cell flags of the pieces under the cursor's footprint (every layer up to the tallest cell)
void LevelEditor::flag_pieces(std::uint32_t self, std::uint32_t on) {
    const std::int32_t gw = global_int(m_, kGridW);
    const std::int32_t gd = global_int(m_, kGridD);
    s_.check(int_at(m_, self + 0x260) + int_at(m_, self + 0x26c) > 0 && int_at(m_, self + 0x260) < gd && int_at(m_, self + 0x264) + int_at(m_, self + 0x258) > 0 && int_at(m_, self + 0x258) < gw,
             kMsgBadCursor);
    const std::int32_t z0 = std::max(int_at(m_, self + 0x260), 0);
    const std::int32_t z1 = std::min(int_at(m_, self + 0x260) + int_at(m_, self + 0x26c), gd);
    const std::int32_t x0 = std::max(int_at(m_, self + 0x258), 0);
    const std::int32_t x1 = std::min(int_at(m_, self + 0x258) + int_at(m_, self + 0x264), gw);
    std::int32_t tallest = 0;
    for (std::int32_t z = z0; z < z1; ++z) {
        for (std::int32_t x = x0; x < x1; ++x) {
            s_.check(z >= 0 && z < gd, kMsgGridZ);
            const std::int32_t layers = static_cast<std::int32_t>(byte_at(m_, word_at(m_, global_word(m_, kRows) + static_cast<std::uint32_t>(z) * 4) + static_cast<std::uint32_t>(x) * 0x10 + 0xf)) - 1;
            if (layers > tallest) {
                tallest = layers;
            }
        }
    }
    const std::uint32_t park = word_at(m_, self + 0x2dc);
    for (std::int32_t layer = 0; layer < tallest + 1; ++layer) {
        for (std::int32_t z = z0; z < z1; ++z) {
            for (std::int32_t x = x0; x < x1; ++x) {
                const std::uint32_t piece = word_at(m_, word_at(m_, word_at(m_, park + kOutMap) + static_cast<std::uint32_t>(z) * 4) + (static_cast<std::uint32_t>(layer) + static_cast<std::uint32_t>(x) * 5) * 4);
                if (piece != 0 && (byte_at(m_, word_at(m_, piece + 0x224) + 9) & 0x10) == 0) {
                    engine(0x0043f1b0, {park, piece, on});
                    const std::uint32_t fog = word_at(m_, piece + 0x228);
                    if ((on & 0xff) == 0) {
                        set_half_at(m_, fog, half_at(m_, fog) & 0xfbdf);
                    } else {
                        set_byte_at(m_, fog + 1, byte_at(m_, fog + 1) | 4);
                    }
                    engine(0x0043f400, {park, piece, on});
                }
            }
        }
    }
}

// 0x00439130: the indicator of the base of a tall piece: one record for every cell of the footprint on the floor, linked in rows, and the cells of the tallest layer
// marked.
void LevelEditor::indicator(std::uint32_t self) {
    const std::uint32_t park = word_at(m_, self + 0x2dc);
    if ((byte_at(m_, park + 0x21 + word_at(m_, self + 0x250) * kInModelBytes) & 0x40) == 0) {
        set_byte_at(m_, self + 0x2ec, 0);
        return;
    }
    set_byte_at(m_, self + 0x2ec, 1);
    s_.check(int_at(m_, self + 0x264) <= 8 && int_at(m_, self + 0x26c) <= 8, kMsgBaseGrid);
    const std::uint32_t base = global_word(m_, kSpecialBase);
    const std::uint32_t base_in = park + base * kInModelBytes;
    const std::int32_t offset_x = short_at(m_, base_in + 0x26);
    const std::int32_t offset_z = short_at(m_, base_in + 0x2a);
    const std::int32_t cell = global_int(m_, kCellSize);
    const std::int32_t left = (int_at(m_, self + 0x258) - (global_int(m_, kGridW) >> 1)) * cell;
    const std::int32_t front = ((global_int(m_, kGridD) >> 1) - int_at(m_, self + 0x260)) * cell;
    const std::int32_t width = int_at(m_, self + 0x264);
    const std::int32_t depth = int_at(m_, self + 0x26c);
    for (std::int32_t row = 0; row < depth; ++row) {
        std::uint32_t record = self + 0x2f0 + static_cast<std::uint32_t>(row) * 8 * kRecord;
        for (std::int32_t column = 0; column < width; ++column) {
            set_half_at(m_, record + 4, 0);
            set_half_at(m_, record + 6, 0);
            set_byte_at(m_, record + 0x1d, 0);
            set_word_at(m_, record + 0x24, 0);
            set_half_at(m_, record + 0x14, 0);
            set_half_at(m_, record + 0x16, 0);
            set_half_at(m_, record + 0x18, 0);
            set_half_at(m_, record + 0x1a, base);
            set_byte_at(m_, record + 0x1f, byte_at(m_, park + 0x14));
            set_byte_at(m_, record + 0x1c, 0);
            set_byte_at(m_, record + 0x1e, 0);
            set_word_at(m_, record + 8, static_cast<std::uint32_t>((cell * column + left + offset_x) * 0x1000));
            set_word_at(m_, record + 0x10, static_cast<std::uint32_t>(((front - offset_z) - cell * row) * 0x1000));
            set_word_at(m_, record + 0xc, static_cast<std::uint32_t>(int_at(m_, self + 0x25c) * global_int(m_, kCellDepth) * -0x1000));
            set_word_at(m_, record + 0x20, record + kRecord);
            record += kRecord;
        }
        set_word_at(m_, self + 0x2c4 + static_cast<std::uint32_t>(width + row * 8) * kRecord, self + 0x550 + static_cast<std::uint32_t>(row) * 0x260);
    }
    std::int32_t tallest = 0;
    set_word_at(m_, self + 0x64 + static_cast<std::uint32_t>(int_at(m_, self + 0x264) + int_at(m_, self + 0x26c) * 8) * kRecord, 0);
    for (std::int32_t z = int_at(m_, self + 0x260); z < int_at(m_, self + 0x26c) + int_at(m_, self + 0x260); ++z) {
        for (std::int32_t x = int_at(m_, self + 0x258); x < int_at(m_, self + 0x264) + int_at(m_, self + 0x258); ++x) {
            s_.check(z >= 0 && z < global_int(m_, kGridD), kMsgGridZ);
            const std::int32_t layers = static_cast<std::int32_t>(byte_at(m_, word_at(m_, global_word(m_, kRows) + static_cast<std::uint32_t>(z) * 4) + static_cast<std::uint32_t>(x) * 0x10 + 0xf)) - 1;
            if (layers > tallest) {
                tallest = layers;
            }
        }
    }
    for (std::int32_t z = int_at(m_, self + 0x260); z < int_at(m_, self + 0x26c) + int_at(m_, self + 0x260); ++z) {
        for (std::int32_t x = int_at(m_, self + 0x258); x < int_at(m_, self + 0x264) + int_at(m_, self + 0x258); ++x) {
            s_.check(z >= 0 && z < global_int(m_, kGridD), kMsgGridZ);
            if (static_cast<std::int32_t>(byte_at(m_, word_at(m_, global_word(m_, kRows) + static_cast<std::uint32_t>(z) * 4) + static_cast<std::uint32_t>(x) * 0x10 + 0xf)) - 1 == tallest) {
                const std::uint32_t record = self + 0x2f5 + static_cast<std::uint32_t>((z - int_at(m_, self + 0x260)) * 8 - int_at(m_, self + 0x258) + x) * kRecord;
                set_byte_at(m_, record, byte_at(m_, record) | 0x10);
            }
        }
    }
}

}  // namespace opentony::runtime::leveleditor
