#include "gen_views.hpp"

namespace opentony::runtime::genobjs {

using namespace views;

// editorpieces2.cpp: the piece id tables.  The piece records (0x528820, 0x25 bytes each) hold one id per theme in their first bytes and, at +0x24, the piece's kind;
// a record whose id for the theme is 0xff ends the list for that theme.
namespace {
constexpr std::uint32_t kPieceRecords = 0x00528820;
constexpr std::uint32_t kRecordBytes = 0x25;
constexpr std::uint32_t kKindOffset = 0x24;
constexpr std::uint32_t kSpecialA = 0x005614a8;         // five ids each: the pieces the editor handles specially
constexpr std::uint32_t kSpecialB = 0x00561490;
constexpr std::uint32_t kSpecialC = 0x00561174;
constexpr std::uint32_t kListBase = 0x0052a0c0;         // ids ended by 0xfe
constexpr std::uint32_t kRotationTable = 0x00529e18;    // five bytes each: an id and the four turned ids
constexpr std::uint32_t kStrideTable = 0x00529e38;      // 0x22 bytes each, ended by an id of 0xff
constexpr std::uint32_t kRecordCount = 0x0056118c;
constexpr std::uint32_t kClearedFlag = 0x00561170;
constexpr std::uint32_t kMsgNoPiece = 0x0052b584;
constexpr std::uint32_t kMsgNoPieceB = 0x0052b5f4;
constexpr std::uint32_t kMsgNotListed = 0x0052b5d8;
constexpr std::uint32_t kMsgNoPieceC = 0x0052b620;
constexpr std::uint32_t kMsgNoPieceD = 0x0052b678;
constexpr std::uint32_t kLogUnknown = 0x0052b64c;
}  // namespace

// 0x00444010
void GenObjs::pieces_clear_flag() { set_global_byte(m_, kClearedFlag, 0); }

// 0x004441c0: the index of the piece whose id for `theme` is `id` (-1 when the theme's list ends first).
std::int32_t GenObjs::piece_find(std::uint32_t id, std::uint32_t theme) {
    std::uint32_t at = kPieceRecords + theme;
    std::int32_t index = 0;
    std::uint32_t value = byte_at(m_, at);
    if (value == id) {
        return 0;
    }
    for (;;) {
        if (value == 0xff) {
            return -1;
        }
        value = byte_at(m_, at + kRecordBytes);
        at += kRecordBytes;
        ++index;
        if (value == id) {
            return index;
        }
    }
}

// 0x00444210: the id the piece with that theme-0 id has in `theme`.
std::uint32_t GenObjs::piece_theme_id(std::uint32_t id, std::uint32_t theme) {
    const std::int32_t index = piece_find(id, 0);
    s_.check(index != -1, kMsgNoPiece);
    return byte_at(m_, kPieceRecords + theme + kRecordBytes * static_cast<std::uint32_t>(index));
}

// 0x00444280: is this id one of the specially handled pieces?
bool GenObjs::piece_is_special(std::uint32_t id) {
    for (std::uint32_t offset = 0; offset < 0x14; offset += 4) {
        if (word_at(m_, kSpecialA + offset) == id || word_at(m_, kSpecialB + offset) == id || word_at(m_, kSpecialC + offset) == id) {
            return true;
        }
    }
    return false;
}

// 0x004442b0: the position of a piece (by its id in the current theme) in the list of listed pieces.
std::uint32_t GenObjs::piece_list_index(std::uint32_t id) {
    const std::int32_t index = piece_find(id, global_word(m_, kTheme));
    s_.check(index != -1, kMsgNoPieceB);
    const std::uint32_t wanted = byte_at(m_, kPieceRecords + kRecordBytes * static_cast<std::uint32_t>(index));
    std::uint32_t value = global_byte(m_, kListBase);
    std::uint32_t position = 0;
    if (value != 0xfe) {
        for (;;) {
            if (value == wanted) {
                return position;
            }
            value = byte_at(m_, kListBase + 1 + position);
            ++position;
            if (value == 0xfe) {
                break;
            }
        }
    }
    s_.check(false, kMsgNotListed);
    return 0;
}

// 0x00444380: the current theme's id of the piece at a position of the list (-1 for an empty position).
std::int32_t GenObjs::piece_translate(std::uint32_t position) {
    const std::uint32_t id = byte_at(m_, kListBase + position);
    if (id == 0xff) {
        return -1;
    }
    const std::int32_t index = piece_find(id, 0);
    s_.check(index != -1, kMsgNoPieceC);
    return byte_at(m_, kPieceRecords + global_word(m_, kTheme) + kRecordBytes * static_cast<std::uint32_t>(index));
}

// 0x00444410: the kind byte of the piece (3 and a log line for an unknown piece).
std::uint32_t GenObjs::piece_kind(std::uint32_t id) {
    const std::int32_t index = piece_find(id, global_word(m_, kTheme));
    if (index == -1) {
        log(kLogUnknown);
        return 3;
    }
    return byte_at(m_, kPieceRecords + kKindOffset + kRecordBytes * static_cast<std::uint32_t>(index));
}

// 0x00444450: the id the piece has turned by (a - b) quarter turns.
std::uint32_t GenObjs::piece_turned(std::uint32_t id, std::uint32_t a, std::uint32_t b) {
    const std::int32_t index = piece_find(id, global_word(m_, kTheme));
    s_.check(index != -1, kMsgNoPieceD);
    const std::uint32_t wanted = byte_at(m_, kPieceRecords + kRecordBytes * static_cast<std::uint32_t>(index));
    std::uint32_t value = global_byte(m_, kRotationTable);
    std::uint32_t row = 0;
    std::uint32_t at = kRotationTable;
    while ((value & 0xff) != wanted) {
        if (value == 0xff) {
            return 0;
        }
        value = byte_at(m_, at + 5);
        at += 5;
        ++row;
    }
    return byte_at(m_, kRotationTable + 1 + ((a - b) & 3) + row * 5);
}

// 0x00444020: take a theme: the ids of the specially handled pieces in it, a copy of each stride row's first byte at +0x21, and the number of pieces it lists.
void GenObjs::pieces_setup(std::uint32_t theme) {
    set_global_word(m_, kTheme, theme);
    // (the id asked for and the word it lands in, in the order the retail code asks)
    static constexpr std::uint32_t kSlots[24][2] = {
        {0x15, 0x005614a8}, {0x24, 0x005614ac}, {0x24, 0x005614b0}, {0x24, 0x005614b4}, {0x24, 0x005614b8}, {0x17, 0x00561490},
        {0x2b, 0x00561494}, {0x2b, 0x00561498}, {0x2b, 0x0056149c}, {0x2b, 0x005614a0}, {0x76, 0x00561174}, {0x2a, 0x00561178},
        {0x2a, 0x0056117c}, {0x2a, 0x00561180}, {0x2a, 0x00561184}, {0x22, 0x005611a0}, {0x72, 0x005611b4}, {0x72, 0x005613b0},
        {0x73, 0x005613b4}, {0x94, 0x0056116c}, {0x74, 0x005611a8}, {0x54, 0x005611ac}, {0x85, 0x005614a4}, {0x23, 0x00561158}};
    for (const auto& slot : kSlots) {
        set_word_at(m_, slot[1], piece_theme_id(slot[0], theme));
    }
    std::uint32_t first = global_byte(m_, kStrideTable);
    if (first != 0xff) {
        std::uint32_t at = kStrideTable;
        do {
            set_byte_at(m_, at + 0x21, first);
            first = byte_at(m_, at + 0x22);
            at += 0x22;
        } while (first != 0xff);
    }
    std::uint32_t count = 0;
    std::uint32_t at = kPieceRecords + theme;
    std::uint32_t value = byte_at(m_, at);
    set_global_word(m_, kRecordCount, 0);
    if (value != 0xff) {
        do {
            value = byte_at(m_, at + kRecordBytes);
            at += kRecordBytes;
            ++count;
        } while (value != 0xff);
        set_global_word(m_, kRecordCount, count);
    }
}

}  // namespace opentony::runtime::genobjs
