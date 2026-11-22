#include "auto_views.hpp"
#include "level_editor.hpp"

namespace opentony::runtime::leveleditor {

using namespace opentony::runtime::autoviews;

// The editor's camera (fields: +0 the quarter turn the view is in, +4 the turn in progress, +8 the turn asked for, +0xc four yaw angles and +0x1c four pitch values, one per
// quarter, +0x2c..+0x34 the point it looks at, +0x3c the state (1 steady, 2 turning), +0x40 the turn button is held, +0x41 the angle was changed, +0x44 and +0x48 the last
// direction).

// 0x00437270
std::uint32_t LevelEditor::camera_init(std::uint32_t self) {
    level_editor_camera_camera_at(m_, self).set_field_c(0x154);
    level_editor_camera_camera_at(m_, self).set_field_10(0x154);
    level_editor_camera_camera_at(m_, self).set_field_0(1);
    level_editor_camera_camera_at(m_, self).set_field_4(0);
    level_editor_camera_camera_at(m_, self).set_field_8(0);
    level_editor_camera_camera_at(m_, self).set_field_44(0);
    level_editor_camera_camera_at(m_, self).set_field_48(0);
    level_editor_camera_camera_at(m_, self).set_field_3c(1);
    level_editor_camera_camera_at(m_, self).set_field_40(0);
    level_editor_camera_camera_at(m_, self).set_field_1c(0x238);
    level_editor_camera_camera_at(m_, self).set_field_20(0x1c7);
    level_editor_camera_camera_at(m_, self).set_field_14(0);
    level_editor_camera_camera_at(m_, self).set_field_24(0x1c7);
    level_editor_camera_camera_at(m_, self).set_field_18(0);
    level_editor_camera_camera_at(m_, self).set_field_28(0x400);
    return self;
}

// 0x004372c0
void LevelEditor::nothing_c() {}

// 0x004372d0
void LevelEditor::camera_set_target(std::uint32_t self, std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    level_editor_camera_camera_at(m_, self).set_field_2c(static_cast<Word>(x));
    level_editor_camera_camera_at(m_, self).set_field_30(static_cast<Word>(y));
    level_editor_camera_camera_at(m_, self).set_field_34(static_cast<Word>(z));
}

// 0x004372f0: the next quarter turn
void LevelEditor::camera_next_quarter(std::uint32_t self) {
    std::int32_t quarter = static_cast<std::int32_t>(level_editor_camera_camera_at(m_, self).field_0()) + 1;
    if (quarter > 3) {
        quarter = 0;
    }
    level_editor_camera_camera_at(m_, self).set_field_0(static_cast<Word>(static_cast<std::uint32_t>(quarter)));
    level_editor_camera_camera_at(m_, self).set_field_3c(1);
    level_editor_camera_camera_at(m_, self).set_field_40(0);
}

// 0x00437310: the pad turns, tilts and zooms the view; a quarter turn is a flick of the turn button and swings round in steps; the view is then worked out and the point
// the park is seen from kept clear of the edges of the park.
void LevelEditor::camera_update(std::uint32_t self) {
    const std::uint32_t state = level_editor_camera_camera_at(m_, self).field_3c();
    if (state == 1) {
        bool steady = true;
        if (level_editor_camera_camera_at(m_, self).field_40() == 0) {
            if (global_byte(m_, kPadTurn) != 0) {
                level_editor_camera_camera_at(m_, self).set_field_40(1);
                level_editor_camera_camera_at(m_, self).set_field_41(0);
            }
            if (global_byte(m_, kPadTurnEdge) != 0) {
                set_global_byte(m_, kPadTurnEdge, 0);
                level_editor_camera_camera_at(m_, self).set_field_8(static_cast<Word>(level_editor_camera_camera_at(m_, self).field_8() + 0x400));
                level_editor_camera_camera_at(m_, self).set_field_3c(2);
                engine(0x004ad620, {0x97, 0x2000, 0});
            }
            steady = level_editor_camera_camera_at(m_, self).field_40() != 0;
        }
        if (steady) {
            if (global_byte(m_, kPadLeft) != 0) {
                level_editor_camera_camera_at(m_, self).set_field_41(1);
                const std::uint32_t at = self + level_editor_camera_camera_at(m_, self).field_0() * 4 + 0xc;
                set_word_at(m_, at, word_at(m_, at) - 0x20);
                set_word_at(m_, self + level_editor_camera_camera_at(m_, self).field_0() * 4 + 0xc, word_at(m_, self + level_editor_camera_camera_at(m_, self).field_0() * 4 + 0xc) & 0xfff);
            }
            if (global_byte(m_, kPadRight) != 0) {
                level_editor_camera_camera_at(m_, self).set_field_41(1);
                const std::uint32_t at = self + level_editor_camera_camera_at(m_, self).field_0() * 4 + 0xc;
                set_word_at(m_, at, word_at(m_, at) + 0x20);
                set_word_at(m_, self + level_editor_camera_camera_at(m_, self).field_0() * 4 + 0xc, word_at(m_, self + level_editor_camera_camera_at(m_, self).field_0() * 4 + 0xc) & 0xfff);
            }
            if (global_byte(m_, kPadUp) != 0) {
                level_editor_camera_camera_at(m_, self).set_field_41(1);
                const std::uint32_t at = self + level_editor_camera_camera_at(m_, self).field_0() * 4 + 0x1c;
                set_word_at(m_, at, word_at(m_, at) + 0x20);
                if (int_at(m_, self + level_editor_camera_camera_at(m_, self).field_0() * 4 + 0x1c) > 0x400) {
                    set_word_at(m_, self + level_editor_camera_camera_at(m_, self).field_0() * 4 + 0x1c, 0x400);
                }
            }
            if (global_byte(m_, kPadDown) != 0) {
                level_editor_camera_camera_at(m_, self).set_field_41(1);
                const std::uint32_t at = self + level_editor_camera_camera_at(m_, self).field_0() * 4 + 0x1c;
                set_word_at(m_, at, word_at(m_, at) - 0x20);
                if (int_at(m_, self + level_editor_camera_camera_at(m_, self).field_0() * 4 + 0x1c) < 0x80) {
                    set_word_at(m_, self + level_editor_camera_camera_at(m_, self).field_0() * 4 + 0x1c, 0x80);
                }
            }
            if (global_byte(m_, kPadTurn) == 0) {
                level_editor_camera_camera_at(m_, self).set_field_40(0);
                set_global_byte(m_, kPadTurnEdge, 0);
                if (level_editor_camera_camera_at(m_, self).field_41() == 0) {
                    camera_next_quarter(self);
                }
            }
        }
    } else if (state == 2) {
        const std::int32_t before = static_cast<std::int32_t>(level_editor_camera_camera_at(m_, self).field_4());
        const std::int32_t after = before + 0x80;
        level_editor_camera_camera_at(m_, self).set_field_4(static_cast<Word>(static_cast<std::uint32_t>(after)));
        if ((after >> 10) != (before >> 10)) {
            level_editor_camera_camera_at(m_, self).set_field_4(static_cast<Word>(static_cast<std::uint32_t>(after) & 0xc00));        // (retail also takes 0x1000 off a result of 0x1000 or more, which the mask rules out)
            level_editor_camera_camera_at(m_, self).set_field_3c(1);
        }
    }
    camera_view(self, word_at(m_, self + level_editor_camera_camera_at(m_, self).field_0() * 4 + 0xc) + level_editor_camera_camera_at(m_, self).field_4(), kViewMatrix, kCameraX);
    const std::int32_t cell = global_int(m_, kCellSize);
    const std::int32_t half_x = (cell * global_int(m_, kGridW)) >> 1;
    const std::int32_t half_z = (global_int(m_, kGridD) * cell) >> 1;
    std::int32_t x = global_int(m_, kCameraX);
    std::int32_t z = global_int(m_, kCameraZ);
    for (const std::int32_t edge : {0, 0x3fc}) {
        std::int32_t d = edge - x + half_x;
        if (d >= 0 && d < 0x1e) {
            x = edge + half_x + 2;
        }
        d = edge + x + half_x;
        if (d >= 0 && d < 0x1e) {
            x = -2 - edge - half_x;
        }
        d = edge - z + half_z;
        if (d >= 0 && d < 0x1e) {
            z = edge + half_z + 2;
        }
        d = edge + z + half_z;
        if (d >= 0 && d < 0x1e) {
            z = -2 - edge - half_z;
        }
    }
    set_global_word(m_, kCameraX, static_cast<std::uint32_t>(x));
    set_global_word(m_, kCameraZ, static_cast<std::uint32_t>(z));
}

// 0x00437580: the view of the park from the quarter's pitch and an angle: the engine builds the rotation (its three angles are the negated pitch, the negated angle and
// nothing) and carries the camera's offset through it; the camera's target is then added and the sum scaled down by 4096.
void LevelEditor::camera_view(std::uint32_t self, std::uint32_t angle, std::uint32_t matrix, std::uint32_t position) {
    const std::uint32_t quarter = level_editor_camera_camera_at(m_, self).field_0();
    static constexpr std::uint32_t kBack[4] = {0xff9f7000, 0xff5ab000, 0xff1c3000, 0xfef34000};
    const std::uint32_t back = quarter < 4 ? kBack[quarter] : 0;
    const auto pitch = static_cast<std::uint16_t>(-static_cast<std::int16_t>(half_at(m_, self + quarter * 4 + 0x1c)));
    const auto turn = static_cast<std::uint16_t>(-static_cast<std::int32_t>(angle & 0xfff));
    set_global_half(m_, kScratchAngles, pitch);
    set_global_half(m_, kScratchAngles + 2, turn);
    set_global_half(m_, kScratchAngles + 4, 0);
    set_global_word(m_, kScratchVector, 0);
    set_global_word(m_, kScratchVector + 4, 0);
    set_global_word(m_, kScratchVector + 8, back);
    engine_fill(0x004e80e0, {kScratchAngles, matrix}, bytes_text(kScratchAngles, 6), matrix, 0x20);
    engine_fill(0x004e85a0, {matrix, kScratchVector, position}, bytes_text(kScratchVector, 12), position, 12);
    for (std::uint32_t k = 0; k < 3; ++k) {
        const std::int32_t sum = static_cast<std::int32_t>(word_at(m_, self + 0x2c + 4 * k) + word_at(m_, position + 4 * k));
        set_word_at(m_, position + 4 * k, static_cast<std::uint32_t>(sum >> 12));
    }
}

// 0x00437650: the pad's four direction bits (bits 0-1 sideways, 2-3 forward) turned to the camera's quarter; a sideways or forward step is a bit pair of 1 and 2 (or 4 and 8).
std::uint32_t LevelEditor::camera_direction(std::uint32_t self, std::uint32_t buttons) {
    std::int32_t x = static_cast<std::int32_t>(buttons & 3);
    if (x == 1) {
        x = -1;
    } else if (x == 2) {
        x = 1;
    }
    std::int32_t z = static_cast<std::int32_t>(buttons & 0xc);
    if (z == 4) {
        z = -1;
    } else if (z == 8) {
        z = 1;
    }
    const std::uint32_t angle = (word_at(m_, self + level_editor_camera_camera_at(m_, self).field_0() * 4 + 0xc) + level_editor_camera_camera_at(m_, self).field_4()) & 0xfff;
    if (angle >= 0x200) {
        if (angle < 0x600) {
            const std::int32_t swap = x;
            x = z;
            z = -swap;
        } else if (angle < 0xa00) {
            x = -x;
            z = -z;
        } else if (angle < 0xe00) {
            const std::int32_t swap = x;
            z = -z;
            x = z;
            z = swap;
            // (x takes the negated forward step, z the sideways one)
        }
    }
    if (x == -1) {
        x = 1;
    } else if (x == 1) {
        x = 2;
    }
    if (z == -1) {
        return 4u | static_cast<std::uint32_t>(x);
    }
    if (z == 1) {
        return 8u | static_cast<std::uint32_t>(x);
    }
    return static_cast<std::uint32_t>(z) | static_cast<std::uint32_t>(x);
}

// 0x00437720: the direction the view looks along (the third column of the view's rotation) into three shorts; true when it changed sign on either axis since the last call.
std::uint32_t LevelEditor::camera_look(std::uint32_t self, std::uint32_t out) {
    camera_view(self, word_at(m_, self + level_editor_camera_camera_at(m_, self).field_0() * 4 + 0xc) + level_editor_camera_camera_at(m_, self).field_8(), kScratchMatrix, kScratchPosition);
    const std::int16_t x = global_short(m_, kScratchMatrix + 4);
    const std::int16_t y = global_short(m_, kScratchMatrix + 0xa);
    const std::int16_t z = global_short(m_, kScratchMatrix + 0x10);
    set_half_at(m_, out + 2, static_cast<std::uint16_t>(y));
    set_half_at(m_, out, static_cast<std::uint16_t>(x));
    set_half_at(m_, out + 4, static_cast<std::uint16_t>(z));
    bool changed = false;
    const std::int32_t last_x = static_cast<std::int32_t>(level_editor_camera_camera_at(m_, self).field_44());
    const std::int32_t last_z = static_cast<std::int32_t>(level_editor_camera_camera_at(m_, self).field_48());
    if (x < 0 && last_x >= 0) {
        changed = true;
    }
    if (x > 0 && last_x <= 0) {
        changed = true;
    }
    if (x == 0 && last_x != 0) {
        changed = true;
    }
    if (z < 0 && last_z >= 0) {
        changed = true;
    }
    if (z > 0 && last_z <= 0) {
        changed = true;
    }
    if (z == 0 && last_z != 0) {
        changed = true;
    }
    level_editor_camera_camera_at(m_, self).set_field_44(static_cast<Word>(static_cast<std::uint32_t>(static_cast<std::int32_t>(x))));
    level_editor_camera_camera_at(m_, self).set_field_48(static_cast<Word>(static_cast<std::uint32_t>(static_cast<std::int32_t>(z))));
    return changed ? 1 : 0;
}

}  // namespace opentony::runtime::leveleditor
