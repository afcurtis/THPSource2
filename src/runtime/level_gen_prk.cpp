#include "auto_views.hpp"
#include "level_gen.hpp"

namespace opentony::runtime::levelgen {

using namespace opentony::runtime::autoviews;

namespace {
constexpr std::uint32_t kMagicOld = 0x4e24;
constexpr std::uint32_t kMagicCurrent = 0x4e25;
constexpr std::uint32_t kMsgBadFile = 0x0052ad38;
constexpr std::uint32_t kMsgCurrentVersion = 0x0052ad20;
constexpr std::uint32_t kMsgFileVersion = 0x0052ad04;
constexpr std::uint32_t kMsgOutdated = 0x0052acb8;
constexpr std::uint32_t kMsgMustSave = 0x0052ac7c;
constexpr std::uint32_t kTrailingEnd = 0x0056a23c;
constexpr std::uint32_t kAaa = 0x00414141;           // "AAA" and its terminator
}  // namespace

// 0x0043c050: a PRK buffer into the generator's cells and item records. The header is the magic (0x4e25 current, 0x4e24 older), the park version (which gives the size) and
// the theme; then eight bytes per cell, ten items of 0x24 bytes, and eight records of the trailing table. The older file keeps the item byte one earlier and its marker
// byte whole; its trailing table is taken as it is. A trailing record whose text is not three characters long is replaced by "AAA".
void LevelGen::parse_buffer(std::uint32_t buffer, std::uint32_t out) {
    const std::uint32_t base = word_at(m_, global_word(m_, kRows));
    const std::uint32_t magic = word_at(m_, buffer);
    if (magic != kMagicOld && magic != kMagicCurrent) {
        log(kMsgBadFile);
        log(kMsgCurrentVersion);
        log(kMsgFileVersion);
        set_word_at(m_, out + 4, global_word(m_, kTheme));
        s_.check(false, kMsgVersion);
        return;
    }
    const bool older = magic == kMagicOld;
    if (older) {
        log(kMsgOutdated);
        log(kMsgMustSave);
        log(kMsgCurrentVersion);
        log(kMsgFileVersion);
    }
    const std::uint32_t version = word_at(m_, buffer + 4);
    set_word_at(m_, out, version);
    set_word_at(m_, out + 4, word_at(m_, buffer + 8));
    std::uint32_t disk = buffer + 12;
    const std::int32_t cells = static_cast<std::int32_t>(word_at(m_, kVersionTable + version * 8 + 4) * word_at(m_, kVersionTable + version * 8));
    for (std::int32_t c = 0; c < cells; ++c) {
        const std::uint32_t cell = base + 0x10 * static_cast<std::uint32_t>(c);
        std::uint32_t packed = half_at(m_, disk + 6);
        set_byte_at(m_, cell + 0xf, packed & 7);
        packed >>= 3;
        for (std::uint32_t k = 0; k < 5; ++k) {
            const std::uint32_t reference = byte_at(m_, disk + k);
            if (reference == 0xff) {
                set_half_at(m_, cell + 2 * k, 0xffff);
            } else {
                set_half_at(m_, cell + 2 * k, static_cast<std::uint32_t>(g_.piece_translate(reference)));
            }
            set_byte_at(m_, cell + 0xa + k, packed & 3);
            packed = (packed & 0xffff) >> 2;
        }
        disk += 8;
    }
    for (std::uint32_t offset = 0; offset < kItemRecords * kItemBytes; offset += kItemBytes) {
        const std::uint32_t item = global_word(m_, kItems) + offset;
        std::uint32_t low = half_at(m_, disk + 6);
        std::uint32_t high = older ? byte_at(m_, disk + 8) : half_at(m_, disk + 8);
        for (std::uint32_t end = 0; end < 2; ++end) {
            set_byte_at(m_, item + end, byte_at(m_, disk + end));
            set_byte_at(m_, item + 2 + end, byte_at(m_, disk + 2 + end));
            set_byte_at(m_, item + 4 + end, byte_at(m_, disk + 4 + end));
            set_byte_at(m_, item + 8 + end, low & 0xf);
            low = (low & 0xffff) >> 4;
            set_byte_at(m_, item + 0xa + end, low & 0xf);
            low = (low & 0xffff) >> 4;
            set_byte_at(m_, item + 6 + end, high & 3);
            high = older ? (high & 0xff) >> 2 : (high & 0xffff) >> 2;
        }
        set_byte_at(m_, item + 0xc, high);
        set_word_at(m_, item + 0x28, byte_at(m_, disk + (older ? 9 : 10)));
        put_string(item + 0xd, m_.cstring(disk + (older ? 10 : 11)));
        disk += 0x24;
    }
    for (std::uint32_t at = kTrailing; at < kTrailingEnd; at += 8) {
        if (!older) {
            std::uint32_t length = 0;
            while (byte_at(m_, disk + length) != 0) {
                ++length;
            }
            if (length != 3) {
                set_word_at(m_, disk, kAaa);
                set_word_at(m_, disk + 4, 0);
            }
        }
        set_word_at(m_, at, word_at(m_, disk));
        set_word_at(m_, at + 4, word_at(m_, disk + 4));
        disk += 8;
    }
}

// 0x0043c480: the generator's cells and items into a new PRK buffer (current format, 0x4e25); the buffer's address is stored at `out`, its size is returned. A cell's
// pieces are written as their position in the list of listed pieces (0xff: none); an item still carrying the disabled marker 0x81 is written as free (0x80).
std::uint32_t LevelGen::serialize_buffer(std::uint32_t out) {
    std::uint32_t cells = static_cast<std::uint32_t>(global_int(m_, kGridD) * global_int(m_, kGridW));
    const std::uint32_t base = word_at(m_, global_word(m_, kRows));
    const std::uint32_t size = (cells * 8 + 0x22f) & 0xffffff80;
    s_.note("alloc", {size, 1, 1, 0});
    const std::uint32_t buffer = static_cast<std::uint32_t>(s_.value("alloc"));
    set_word_at(m_, out, buffer);
    set_word_at(m_, buffer, kMagicCurrent);
    set_word_at(m_, buffer + 4, global_word(m_, kVersion));
    set_word_at(m_, buffer + 8, global_word(m_, kTheme));
    std::uint32_t disk = buffer + 12;
    std::uint32_t cell = base;
    for (; static_cast<std::int32_t>(cells) > 0; --cells) {
        std::uint32_t packed = 0;
        for (std::uint32_t k = 0; k < 5; ++k) {
            const std::uint32_t reference = half_at(m_, cell + 2 * k);
            if (reference == 0xffff) {
                set_byte_at(m_, disk + k, 0xff);
            } else {
                set_byte_at(m_, disk + k, g_.piece_list_index(static_cast<std::uint32_t>(short_at(m_, cell + 2 * k))));
            }
            packed |= static_cast<std::uint32_t>(byte_at(m_, cell + 0xa + k)) << (2 * k);
        }
        set_half_at(m_, disk + 6, (packed << 3) | byte_at(m_, cell + 0xf));
        cell += 0x10;
        disk += 8;
    }
    for (std::uint32_t offset = 0; offset < kItemRecords * kItemBytes; offset += kItemBytes) {
        if (byte_at(m_, global_word(m_, kItems) + offset + 0xc) == 0x81) {
            set_byte_at(m_, global_word(m_, kItems) + offset + 0xc, 0x80);
        }
        const std::uint32_t item = global_word(m_, kItems) + offset;
        std::uint32_t low = 0;
        std::uint32_t high = 0;
        for (std::uint32_t end = 0; end < 2; ++end) {
            set_byte_at(m_, disk + end, byte_at(m_, item + end));
            set_byte_at(m_, disk + 2 + end, byte_at(m_, item + 2 + end));
            set_byte_at(m_, disk + 4 + end, byte_at(m_, item + 4 + end));
            const std::uint32_t pair = ((byte_at(m_, item + 0xa + end) & 0xfu) << 4) + (byte_at(m_, item + 8 + end) & 0xfu);
            low |= pair << (8 * end);
            high |= static_cast<std::uint32_t>(byte_at(m_, item + 6 + end) & 3) << (2 * end);
        }
        set_half_at(m_, disk + 6, low);
        set_half_at(m_, disk + 8, high | (static_cast<std::uint32_t>(byte_at(m_, item + 0xc)) << 4));
        set_byte_at(m_, disk + 0xa, byte_at(m_, item + 0x28));
        put_string(disk + 0xb, m_.cstring(item + 0xd));
        disk += 0x24;
    }
    for (std::uint32_t at = kTrailing; at < kTrailingEnd; at += 8) {
        set_word_at(m_, disk, word_at(m_, at));
        set_word_at(m_, disk + 4, word_at(m_, at + 4));
        disk += 8;
    }
    return size;
}

// 0x0043c6b0: after a park is loaded under a theme, every piece reference of every cell is turned from its position in the list of listed pieces into the library slot of
// that theme (the list index first, then the theme's piece tables are set up and the references are translated).
void LevelGen::translate_references(std::uint32_t theme) {
    const std::int32_t cells = global_int(m_, kGridD) * global_int(m_, kGridW);
    std::uint32_t cell = word_at(m_, global_word(m_, kRows));
    for (std::int32_t c = 0; c < cells; ++c, cell += 0x10) {
        for (std::uint32_t k = 0; k < byte_at(m_, cell + 0xf); ++k) {
            const std::uint32_t at = cell + 2 * k;
            if (half_at(m_, at) != 0xffff) {
                set_half_at(m_, at, g_.piece_list_index(static_cast<std::uint32_t>(short_at(m_, at))));
            }
        }
    }
    g_.pieces_setup(theme);
    cell = word_at(m_, global_word(m_, kRows));
    for (std::int32_t c = 0; c < cells; ++c, cell += 0x10) {
        for (std::uint32_t k = 0; k < byte_at(m_, cell + 0xf); ++k) {
            const std::uint32_t at = cell + 2 * k;
            if (half_at(m_, at) != 0xffff) {
                set_half_at(m_, at, static_cast<std::uint32_t>(g_.piece_translate(static_cast<std::uint32_t>(short_at(m_, at)))));
            }
        }
    }
}

}  // namespace opentony::runtime::levelgen
