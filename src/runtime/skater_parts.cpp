#include "skater_views.hpp"

// Phase 4.15a: the skater's model parts (physics.cpp, 0x0048fbe0, 0x0048fc90, 0x0048fd60): the lookup of a part by its number, the shift of the vertices of three parts, and the scaling ("fattening") of the parts of
// a skater's model. The model of a region is a table of model records (the count one word before the table, `PsxModelTablesByRegion`, 0x44 bytes a region); a record has its vertex count (short) at +2 and
// its vertices from +0x1c, eight bytes each: three shorts and a flag byte at +6 (bits 0x12 keep a vertex out of every change).

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

inline std::int32_t sext16(Word v) { return static_cast<std::int16_t>(static_cast<std::uint16_t>(v)); }

constexpr Word kModelTables = 0x0056d43c;       // PsxModelTablesByRegion: a pointer per region, 0x44 bytes a region
constexpr Word kRegionStride = 0x44;
constexpr Word kShiftByte = 0x0056d466;         // per region: the amount the vertices of three parts have been shifted by
constexpr Word kFattenByte = 0x0056d467;        // per region: the fattening in force (0 none, 1 and 2 the two kinds)
constexpr Word kPlayerOne = 0x0056a858;         // Player
constexpr Word kPlayerTwo = 0x0056a85c;
constexpr Word kSelectionRecords = 0x0053a514;  // SkaterSelectionRecords: 0x170 bytes a character; bit 8 of the first word keeps a skater from being changed
constexpr Word kFattenTableNone = 0x00536950;   // per part, a short: how much of the change a part takes
constexpr Word kFattenTableA = 0x00536978;
constexpr Word kFattenTableB = 0x005369a0;
constexpr Word kLogFatten = 0x00536b08;         // "%d: Fatten %d, %d, %d"

}  // namespace

// 0x0048fbe0: the index of a part number in the skater's order of parts (the bytes at +0x150, 19 of them); the order must be in use (+0x14c); a part that is not there is an assertion and index 0
Word SkaterKit::find_part_index(Word self, Word part) {
    constexpr Word kMsgOrder = 0x00536aec;
    constexpr Word kMsgPart = 0x00536ad8;
    check(skater_at(m_, self).field_14c() != 0, kMsgOrder);
    for (Word k = 0; k < 0x13; ++k) {
        if (byte_at(m_, self + 0x150 + k) == part) {
            return k;
        }
    }
    check(false, kMsgPart);
    return 0;
}

// 0x0048fc90: the vertices of the three parts the skater keeps in +0x2cd0, +0x2cd4 and +0x2cd8 are moved by the change of an amount (a byte kept per region); a vertex marked 0x12 stays
void SkaterKit::offset_part_vertices(Word self, Word amount) {
    const Word region = skater_at(m_, self).psx_region_slot() * kRegionStride;
    const Word old = byte_at(m_, kShiftByte + region);
    if (amount == old) {
        return;
    }
    set_byte_at(m_, kShiftByte + region, amount);
    const Word delta = amount - old;
    const Word parts[3] = {skater_at(m_, self).appearance_resource_10().address, skater_at(m_, self).appearance_resource_11().address, skater_at(m_, self).appearance_resource_12().address};
    const std::int32_t count = int_at(m_, word_at(m_, kModelTables + region) - 4);
    for (std::int32_t k = 0; k < count; ++k) {
        const Word model = word_at(m_, word_at(m_, kModelTables + region) + 4 * static_cast<Word>(k));
        Word vertices = half_at(m_, model + 2);
        if ((static_cast<Word>(k) == parts[0] || static_cast<Word>(k) == parts[2] || static_cast<Word>(k) == parts[1]) && vertices != 0) {
            Word vertex = model + 0x20;
            do {
                if ((byte_at(m_, vertex + 2) & 0x12) == 0) {
                    set_half_at(m_, vertex, half_at(m_, vertex) + delta);
                }
                vertex += 8;
            } while (--vertices != 0);
        }
    }
}

// 0x0048fd60: the parts of the skater's model scaled in x and z (and in y for part 15) by a percentage that depends on the part (`amount`, the kind in `kind`: 1 and 2 have their own tables and amounts of 0x78
// and 0xfa, the others take `amount` itself); `undo` divides instead of multiplying. Before a change the earlier kind is undone (a state of 2 is undone with the table of kind 1: a retail quirk). Not done for
// the skater of a player whose character record has bit 8 set. The line "%d: Fatten ..." goes to the log first
void SkaterKit::fatten(Word self, Word amount, Word undo, Word kind) {
    const auto locked = [this, self](Word player) {
        return self == player && (word_at(m_, kSelectionRecords + word_at(m_, player + 0x2cc0) * 0x170) & 0x100) != 0;
    };
    if (locked(global_word(m_, kPlayerOne)) || locked(global_word(m_, kPlayerTwo))) {
        return;
    }
    log_line(kLogFatten);
    const Word region = skater_at(m_, self).psx_region_slot() * kRegionStride;
    if (undo == 0) {
        if (byte_at(m_, kFattenByte + region) != 0) {
            if (byte_at(m_, kFattenByte + region) == 1) {
                fatten(self, 0x78, 1, 1);
            }
            if (byte_at(m_, kFattenByte + region) == 2) {
                fatten(self, 0xfa, 1, 1);
            }
        }
        set_byte_at(m_, kFattenByte + region, kind);
    } else {
        set_byte_at(m_, kFattenByte + region, 0);
    }
    Word table = kFattenTableNone;
    if (kind == 1) {
        table = kFattenTableA;
        amount = 0x78;
    } else if (kind == 2) {
        table = kFattenTableB;
        amount = 0xfa;
    }
    const std::int32_t distance = (s(amount) < 0 ? -s(amount) : s(amount)) - 100;
    const std::int32_t count = int_at(m_, word_at(m_, kModelTables + region) - 4);
    for (std::int32_t k = 0; k < count; ++k) {
        const Word part = byte_at(m_, self + 0x150 + static_cast<Word>(k));
        const Word model = word_at(m_, word_at(m_, kModelTables + region) + 4 * static_cast<Word>(k));
        Word vertex = model + 0x1c;
        for (Word remaining = half_at(m_, model + 2); remaining != 0; --remaining) {
            if ((byte_at(m_, vertex + 6) & 0x12) == 0) {
                std::int32_t percent = s(u(sext16(half_at(m_, table + part * 2))) * u(distance) * 10u) / 100 + 1000;
                if (undo != 0 && percent != 0) {        // (retail divides by zero here: the fault is not reproduced)
                    percent = 1000000 / percent;
                }
                const auto scale = [this, percent](Word at) { set_half_at(m_, at, u(s(u(sext16(half_at(m_, at))) * u(percent)) / 1000)); };
                scale(vertex);
                scale(vertex + 4);
                if (part == 0xf) {
                    scale(vertex + 2);
                }
            }
            vertex += 8;
        }
    }
}

}  // namespace opentony::runtime::skaterkit
