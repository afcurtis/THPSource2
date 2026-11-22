#include "auto_views.hpp"
#include "create_textures.hpp"

#include <algorithm>
#include <cstring>

namespace opentony::runtime::create {

using namespace opentony::runtime::autoviews;

namespace {

constexpr std::size_t kFindBuffer = 256;   // Find copies the name into a local of this size

std::string lowered(std::string text) {
    for (char& c : text) {
        // FUN_004b8340 (strlwr): a signed compare, so bytes above 0x7f are left alone
        if (static_cast<signed char>(c) >= 'A' && static_cast<signed char>(c) <= 'Z') {
            c = static_cast<char>(c + 0x20);
        }
    }
    return text;
}

}  // namespace

void TextureSets::Record::put32(std::uint32_t at, std::uint32_t v) {
    for (std::uint32_t i = 0; i < 4; ++i) {
        bytes[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
        defined[at + i] = true;
    }
}

void TextureSets::Record::put8(std::uint32_t at, std::uint8_t v) {
    bytes[at] = v;
    defined[at] = true;
}

void TextureSets::Record::put_string(std::uint32_t at, const std::string& s) {
    for (std::size_t i = 0; i <= s.size() && at + i < bytes.size(); ++i) {
        bytes[at + i] = i < s.size() ? static_cast<std::uint8_t>(s[i]) : 0;
        defined[at + i] = true;
    }
}

std::uint32_t TextureSets::alloc(std::uint32_t size) {
    s_.note(kAlloc, {size, 1, 1, 0});
    return static_cast<std::uint32_t>(s_.value(kAlloc));
}

std::int32_t TextureSets::strcmp_at(std::uint32_t a, std::uint32_t b) {
    for (;; ++a, ++b) {
        const std::uint8_t x = byte_at(m_, a);
        const std::uint8_t y = byte_at(m_, b);
        if (x != y) {
            return x < y ? -1 : 1;
        }
        if (x == 0) {
            return 0;
        }
    }
}

// ---- the texture database -----------------------------------------------------------------------

std::uint32_t TextureSets::find_texture(std::uint32_t database, std::uint32_t name) {
    return find_named(database, m_.cstring(name, kFindBuffer));
}

std::uint32_t TextureSets::find_named(std::uint32_t database, const std::string& name) {
    const std::string key = lowered(name);
    const std::int32_t count = int_at(m_, database + kDatabaseCountOffset);
    for (std::int32_t i = 0; i < count; ++i) {
        const std::uint32_t entry = word_at(m_, database + kDatabaseEntries + 4u * static_cast<std::uint32_t>(i));
        const std::string existing = m_.cstring(entry + 8, kFindBuffer);
        const int order = key < existing ? -1 : (key == existing ? 0 : 1);
        if (order == 0) {
            return entry;
        }
        if (order < 0) {
            break;   // the entries are sorted: the name would sit before this one
        }
    }
    s_.check(count < static_cast<std::int32_t>(kMaxTextures), kMsgTooManyTextures);
    std::array<std::uint8_t, kEntrySize> entry{};
    std::array<bool, kEntrySize> defined{};
    auto put = [&](std::uint32_t at, std::uint8_t v) {
        if (at < kEntrySize) {
            entry[at] = v;
            defined[at] = true;
        }
    };
    for (std::uint32_t i = 0; i < 4; ++i) {
        put(i, static_cast<std::uint8_t>(static_cast<std::uint32_t>(count) >> (8 * i)));
    }
    for (std::size_t i = 0; i <= key.size(); ++i) {
        put(8 + static_cast<std::uint32_t>(i), i < key.size() ? static_cast<std::uint8_t>(key[i]) : 0);
    }
    put(0x18, 0);
    return allocate_from(database, entry, defined);
}

std::uint32_t TextureSets::allocate_texture(std::uint32_t database, std::uint32_t record) {
    std::array<std::uint8_t, kEntrySize> entry{};
    std::array<bool, kEntrySize> defined{};
    const auto image = m_.bytes(record, kEntrySize);
    std::copy(image.begin(), image.end(), entry.begin());
    defined.fill(true);
    // the name is lower-cased in the caller's record first
    const std::int32_t count = int_at(m_, database + kDatabaseCountOffset);
    s_.check(count < static_cast<std::int32_t>(kMaxTextures), kMsgTooManyTextures);
    for (std::uint32_t i = 8; i < kEntrySize && entry[i] != 0; ++i) {
        if (static_cast<signed char>(entry[i]) >= 'A' && static_cast<signed char>(entry[i]) <= 'Z') {
            entry[i] = static_cast<std::uint8_t>(entry[i] + 0x20);
            set_byte_at(m_, record + i, entry[i]);
        }
    }
    // (the assertion above is the one Allocate makes; allocate_from must not repeat it)
    return allocate_from(database, entry, defined, /*asserted=*/true);
}

std::uint32_t TextureSets::allocate_from(std::uint32_t database, std::array<std::uint8_t, kEntrySize> entry,
                                         const std::array<bool, kEntrySize>& defined, bool asserted) {
    const std::int32_t count = int_at(m_, database + kDatabaseCountOffset);
    if (!asserted) {
        s_.check(count < static_cast<std::int32_t>(kMaxTextures), kMsgTooManyTextures);
    }
    // the first entry whose name is not below the new one, else the end
    std::string name;
    for (std::uint32_t i = 8; i < kEntrySize && entry[i] != 0; ++i) {
        name.push_back(static_cast<char>(entry[i]));
    }
    std::int32_t at = count;
    for (std::int32_t i = 0; i < count; ++i) {
        const std::uint32_t other = word_at(m_, database + kDatabaseEntries + 4u * static_cast<std::uint32_t>(i));
        if (name <= m_.cstring(other + 8, kFindBuffer)) {
            at = i;
            break;
        }
    }
    for (std::int32_t k = count; k > at; --k) {
        set_word_at(m_, database + kDatabaseEntries + 4u * static_cast<std::uint32_t>(k), word_at(m_, database + kDatabaseEntries + 4u * static_cast<std::uint32_t>(k - 1)));
    }
    const std::uint32_t block = alloc(kEntrySize);
    set_word_at(m_, database + kDatabaseEntries + 4u * static_cast<std::uint32_t>(at), block);
    m_.write(block, entry);
    for (std::uint32_t i = 0; i < kEntrySize; ++i) {
        if (!defined[i]) {
            unspecified_.emplace_back(block + i, 1);
        }
    }
    set_word_at(m_, database + kDatabaseCountOffset, word_at(m_, database + kDatabaseCountOffset) + 1);
    return block;
}

// ---- the set holder -----------------------------------------------------------------------------

std::uint32_t TextureSets::find_by_id(std::uint32_t holder, std::int32_t id) {
    const std::int32_t count = int_at(m_, holder + kSetCountOffset);
    for (std::int32_t i = 0; i < count; ++i) {
        const std::uint32_t set = holder + kSetsOffset + static_cast<std::uint32_t>(i) * kSetStride;
        if (int_at(m_, set) == id) {
            return set;
        }
    }
    return 0;
}

std::int32_t TextureSets::find_by_group(std::uint32_t holder, std::int32_t group) {
    const std::int32_t count = int_at(m_, holder + kSetCountOffset);
    for (std::int32_t i = 0; i < count; ++i) {
        const std::uint32_t set = holder + kSetsOffset + static_cast<std::uint32_t>(i) * kSetStride;
        const std::int32_t g = sbyte_at(m_, set + kSetGroup);
        if (g == group || (group == 2 && byte_at(m_, set + kSetGroup) == 1)) {
            return int_at(m_, set);
        }
    }
    return -1;
}

std::int32_t TextureSets::find_root(std::uint32_t holder, std::int32_t root, std::int32_t id) {
    const std::int32_t count = int_at(m_, holder + kSetCountOffset);
    std::int32_t index = -1;
    std::uint32_t found = 0;
    for (std::int32_t i = 0; i < count; ++i) {
        const std::uint32_t set = holder + kSetsOffset + static_cast<std::uint32_t>(i) * kSetStride;
        if (int_at(m_, set) == id) {
            index = i;
            found = set;
            break;
        }
    }
    s_.check(index != -1, kMsgNoCurrentId);
    if (index >= 0) {
        const std::uint8_t group = byte_at(m_, found + kSetGroup);
        for (std::int32_t i = index; i >= 0; --i) {
            const std::uint32_t set = holder + kSetsOffset + static_cast<std::uint32_t>(i) * kSetStride;
            if (byte_at(m_, set + kSetGroup) == group && int_at(m_, set + kSetRoot) == root) {
                return i;
            }
        }
    }
    s_.check(false, kMsgNoRoot);
    return index;
}

std::int32_t TextureSets::step(std::uint32_t holder, std::int32_t root, std::int32_t id, std::int32_t by) {
    const std::int32_t count = int_at(m_, holder + kSetCountOffset);
    std::int32_t index = -1;
    // retail keeps the pointer of the last set it looked at (or the id argument when there are none)
    std::uint32_t current = static_cast<std::uint32_t>(id);
    for (std::int32_t i = 0; i < count; ++i) {
        current = holder + kSetsOffset + static_cast<std::uint32_t>(i) * kSetStride;
        if (int_at(m_, current) == id) {
            index = i;
            break;
        }
    }
    s_.check(index != -1, kMsgNoCurrentId);
    std::int32_t candidate = index;
    std::int32_t wraps = 0;
    for (;;) {
        candidate = static_cast<std::int32_t>(static_cast<std::uint32_t>(candidate) + static_cast<std::uint32_t>(by));
        if (candidate < 0) {
            candidate = count - 1;
            ++wraps;
        }
        if (candidate >= count) {
            candidate = 0;
            ++wraps;
        }
        if (wraps > 1) {
            return index;
        }
        const std::uint32_t set = holder + kSetsOffset + static_cast<std::uint32_t>(candidate) * kSetStride;
        if (byte_at(m_, set + kSetGroup) != byte_at(m_, current + kSetGroup)) {
            continue;
        }
        if (int_at(m_, set + kSetRoot) != root && int_at(m_, set) != int_at(m_, current + kSetRoot)) {
            continue;
        }
        if (byte_at(m_, set + kSetFlag) != 0) {
            continue;
        }
        return candidate;
    }
}

void TextureSets::commit_record(std::uint32_t holder, const Record& record) {
    const std::int32_t count = int_at(m_, holder + kSetCountOffset);
    // (Commit's own assertion names "Too many textures"; the parser's names "Too many texture sets")
    s_.check(count < static_cast<std::int32_t>(kMaxSets), kMsgTooManyTextures);
    std::uint32_t id = 0;
    std::memcpy(&id, record.bytes.data(), 4);
    s_.check(find_by_id(holder, static_cast<std::int32_t>(id)) == 0, kMsgSetExists);
    s_.check(static_cast<std::int8_t>(record.bytes[kSetTextureCount]) < 5, kMsgTooManyTexparts);
    const std::uint32_t at = holder + kSetsOffset + static_cast<std::uint32_t>(count) * kSetStride;
    m_.write(at, record.bytes);
    for (std::uint32_t i = 0; i < kSetStride; ++i) {
        if (!record.defined[i]) {
            unspecified_.emplace_back(at + i, 1);
        }
    }
    // (retail reads the count again after the copy: a record written at 0x180 lands on the count itself)
    set_word_at(m_, holder + kSetCountOffset, word_at(m_, holder + kSetCountOffset) + 1);
}

void TextureSets::commit(std::uint32_t holder, std::uint32_t record) {
    Record image;
    const auto bytes = m_.bytes(record, kSetStride);
    std::copy(bytes.begin(), bytes.end(), image.bytes.begin());
    image.defined.fill(true);
    commit_record(holder, image);
}

void TextureSets::dump(std::uint32_t holder) {
    const std::int32_t count = int_at(m_, holder + kSetCountOffset);
    s_.note(kPrintf, {kFormatDump});
    for (std::int32_t i = 0; i < count; ++i) {
        s_.note(kPrintf, {kFormatSet});
    }
}

// ---- cretex.bin -----------------------------------------------------------------------------------

void TextureSets::load_cretex(std::uint32_t holder, std::uint32_t file_name) {
    const std::uint32_t database = holder + kDatabaseOffset;
    set_word_at(m_, holder + kSetCountOffset, 0);
    s_.note(kFileOpen, {file_name, 1});
    const auto file = static_cast<std::uint32_t>(s_.value(kFileOpen));
    s_.check(file != 0, kMsgNoFile);

    auto length = [&](std::uint32_t at) {
        std::uint32_t n = 0;
        while (byte_at(m_, at + n) != 0) {
            ++n;
        }
        return n;
    };
    auto next = [&](std::uint32_t at) { return at + length(at) + 1; };   // the string after the one at `at`
    auto count_now = [&] { return int_at(m_, holder + kSetCountOffset); };
    // A texture named by a string of the file: Find it, assert it exists, add it to the record.
    auto add_texture = [&](Record& r, std::uint32_t& textures, const std::string& name) {
        const std::uint32_t entry = find_named(database, name);
        r.put32(kSetTextures + 4 * textures, entry);
        ++textures;
        return entry;
    };
    auto assert_found = [&](std::uint32_t entry, std::uint32_t message) { s_.check(entry != 0, message); };

    // Locals the parser keeps across records (retail's stack): the digit of the previous group, the
    // first-of-group flag, the root id of the group being read, the root field of the last record,
    // and the root of the group with digit two that the extra sets at the end point to.
    std::int32_t digit_seen = -1;
    bool first_of_group = true;
    std::uint32_t group_root = 0;
    std::uint32_t root_field = 0;
    std::uint32_t digit_two_root = 0xffffffffu;

    // The record the parser builds each set in is one local of retail's frame, not a fresh one per set: a word a set does not write (the fourth texture of a clothing set
    // without a second texture, which the create-a-skater screens read all the same) keeps what the sets before it left there.
    Record r;

    // ---- section 1: the complete hair and face sets (group 0) ----
    std::uint32_t p = file;
    while (byte_at(m_, p) != 0) {
        const std::uint32_t s1 = p;
        const std::uint32_t s2 = next(s1);
        const std::uint32_t s3 = next(s2);
        const std::uint32_t s4 = next(s3);
        const std::uint32_t after = next(s4);
        const std::uint8_t flag = byte_at(m_, after);
        const std::int32_t digit = static_cast<std::int8_t>(byte_at(m_, s2 + 7)) - 0x30;
        if (digit_seen != digit) {
            first_of_group = true;
            digit_seen = digit;
        }
        s_.check(count_now() < static_cast<std::int32_t>(kMaxSets), kMsgTooManySets);
        const auto id = static_cast<std::uint32_t>(count_now());
        r.put32(0, id);
        root_field = 0xffffffffu;
        r.put8(kSetHasSecond, 0);
        r.put8(kSetGroup, 0);
        s_.check(length(s1) < 16, kMsgEmpty);
        r.put_string(kSetName, m_.cstring(s1, 64));
        r.put_string(kSetDisplay, m_.cstring(s4, 64));
        std::uint32_t textures = 0;
        assert_found(add_texture(r, textures, m_.cstring(s2, 64)), kMsgTextureMissing);
        if (byte_at(m_, s3) != 0) {
            const std::string second = m_.cstring(s3, 64);
            // the assertion reads the eighth character of the copy
            s_.check(second.size() > 7 && second[7] == '0', kMsgCapSkinTone);
            assert_found(add_texture(r, textures, second), kMsgTextureMissing);
            r.put8(kSetHasSecond, 1);
        }
        if (!first_of_group) {
            root_field = group_root;
        } else {
            first_of_group = false;
            group_root = id;
        }
        if (digit == 2) {
            digit_two_root = group_root;
        }
        r.put32(kSetRoot, root_field);
        r.put8(kSetTextureCount, static_cast<std::uint8_t>(textures));
        r.put8(kSetFlag, flag);
        commit_record(holder, r);
        p = after + 1;
    }

    // ---- section 2: the clothing sets (group 1) ----
    group_root = static_cast<std::uint32_t>(count_now());
    p = p + 1;
    while (byte_at(m_, p) != 0) {
        const std::uint32_t s1 = p;
        const std::uint32_t flag_at = s1 + length(s1) + 1;
        const std::uint32_t flag = static_cast<std::uint32_t>(static_cast<std::int8_t>(byte_at(m_, flag_at)));
        const std::uint32_t s2 = flag_at + 1;
        const std::uint32_t s3 = next(s2);
        const std::uint32_t s4 = next(s3);
        const std::uint32_t s5 = next(s4);        // skipped by the retail code
        const std::uint32_t s6 = next(s5);        // the display name
        p = next(s6);
        const bool has_s4 = byte_at(m_, s4) != 0;
        s_.check(count_now() < static_cast<std::int32_t>(kMaxSets), kMsgTooManySets);
        const auto id = static_cast<std::uint32_t>(count_now());
        r.put32(0, id);
        root_field = 0xffffffffu;
        r.put8(kSetHasSecond, 0);
        std::uint32_t next_root = id;
        if (flag != 0) {
            root_field = group_root;
            next_root = group_root;
        }
        group_root = next_root;
        r.put8(kSetGroup, 1);
        s_.check(length(s1) < 16, kMsgEmpty);
        r.put_string(kSetName, m_.cstring(s1, 64));
        r.put_string(kSetDisplay, m_.cstring(s6, 64));
        std::uint32_t textures = 0;
        {
            const std::uint32_t entry = add_texture(r, textures, m_.cstring(s2, 64));
            assert_found(entry, kMsgTextureMissing);
            if (flag != 0) {
                set_byte_at(m_, entry + 0x18, 1);   // the database entry is flagged as a clothing overlay
            }
        }
        assert_found(add_texture(r, textures, m_.cstring(s3, 64)), kMsgTextureMissing);
        if (has_s4) {
            assert_found(add_texture(r, textures, m_.cstring(s4, 64)), kMsgTextureMissing);
            r.put8(kSetHasSecond, 1);
        }
        assert_found(add_texture(r, textures, "handopn1.bmp"), kMsgTextureMissing);
        r.put8(kSetFlag, 0);
        r.put32(kSetRoot, root_field);
        r.put8(kSetTextureCount, static_cast<std::uint8_t>(textures));
        commit_record(holder, r);
    }

    // ---- section 3: names (lower-cased in the file) with a value byte each ----
    std::vector<std::uint32_t> names;
    std::vector<std::uint8_t> values;
    std::uint32_t q = p;   // points at the terminating empty string of section 2
    for (;;) {
        const std::uint32_t s = q + 1;
        // FUN_004b8340 lower-cases the string in place, then the empty string ends the list
        for (std::uint32_t at = s; byte_at(m_, at) != 0; ++at) {
            const std::uint8_t c = byte_at(m_, at);
            if (static_cast<std::int8_t>(c) >= 'A' && static_cast<std::int8_t>(c) <= 'Z') {
                set_byte_at(m_, at, static_cast<std::uint8_t>(c + 0x20));
            }
        }
        names.push_back(s);
        if (byte_at(m_, s) == 0) {
            names.pop_back();
            break;
        }
        q = s + length(s) + 1;
        values.push_back(byte_at(m_, q));
    }

    // ---- section 4: the legwear sets (group 4) ----
    std::uint32_t r4 = q + 2;
    while (byte_at(m_, r4) != 0) {
        const std::uint32_t s1 = r4;
        const std::uint32_t s2 = next(s1);
        const std::uint32_t s3 = next(s2);
        r4 = next(s3);
        s_.check(count_now() < static_cast<std::int32_t>(kMaxSets), kMsgTooManySets);
        const auto id = static_cast<std::uint32_t>(count_now());
        r.put32(0, id);
        r.put8(kSetHasSecond, 0);
        r.put8(kSetGroup, 4);
        s_.check(length(s1) < 16, kMsgEmpty);
        r.put_string(kSetName, m_.cstring(s1, 64));
        r.put_string(kSetDisplay, m_.cstring(s3, 64));
        std::uint32_t textures = 0;
        assert_found(add_texture(r, textures, m_.cstring(s2, 64)), kMsgTextureMissing);
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (strcmp_at(names[i], s1) == 0) {
                r.put8(kSetHasSecond, values[i]);
            }
        }
        r.put8(kSetFlag, 0);
        r.put32(kSetRoot, 0xffffffffu);
        r.put8(kSetTextureCount, static_cast<std::uint8_t>(textures));
        commit_record(holder, r);
    }

    // ---- sections 5 and 6: socks (group 5) and shoes (group 6), a texture and a display name each ----
    auto simple_section = [&](std::uint32_t start, std::uint8_t group) {
        std::uint32_t at = start + 1;
        while (byte_at(m_, at) != 0) {
            const std::uint32_t s1 = at;
            const std::uint32_t s2 = next(s1);
            at = next(s2);
            s_.check(count_now() < static_cast<std::int32_t>(kMaxSets), kMsgTooManySets);
            Record r;
            const auto id = static_cast<std::uint32_t>(count_now());
            r.put32(0, id);
            r.put32(kSetRoot, 0xffffffffu);
            r.put8(kSetHasSecond, 0);
            r.put8(kSetGroup, group);
            r.put_string(kSetName, "DUMMY");
            r.put_string(kSetDisplay, m_.cstring(s2, 64));
            std::uint32_t textures = 0;
            assert_found(add_texture(r, textures, m_.cstring(s1, 64)), kMsgTextureMissing);
            r.put8(kSetFlag, 0);
            r.put8(kSetTextureCount, static_cast<std::uint8_t>(textures));
            commit_record(holder, r);
        }
        return at;
    };
    const std::uint32_t end5 = simple_section(r4, 5);
    (void)simple_section(end5, 6);

    s_.note(kMemDelete, {file});

    // ---- the three sets the code adds itself (group 0, flag 1, rooted at the digit-two group) ----
    struct Extra {
        const char* name;
        const char* display;
        std::uint32_t first_line;
        const char* texture;
        const char* second;   // a texture that also sets the second-texture flag, or nullptr
        std::uint32_t second_line;
    };
    const Extra extras[3] = {
        {"slick", "connor", 0x245, "connor_2.bmp", nullptr, 0},
        {"caphelmet", "hudson", 0x24c, "hudson_2.bmp", "helmet_0.bmp", 0x24f},
        {"afrowhite", "earl", 0x257, "robert_2.bmp", nullptr, 0},
    };
    for (const Extra& extra : extras) {
        Record r;
        r.put32(0, static_cast<std::uint32_t>(count_now()));
        r.put8(kSetHasSecond, 0);
        r.put8(kSetGroup, 0);
        r.put_string(kSetName, extra.name);
        r.put_string(kSetDisplay, extra.display);
        std::uint32_t textures = 0;
        assert_found(add_texture(r, textures, extra.texture), kMsgEmpty);
        if (extra.second != nullptr) {
            assert_found(add_texture(r, textures, extra.second), kMsgEmpty);
            r.put8(kSetHasSecond, 1);
        }
        r.put32(kSetRoot, digit_two_root);
        r.put8(kSetFlag, 1);
        r.put8(kSetTextureCount, static_cast<std::uint8_t>(textures));
        s_.note(kPrintf, {kFormatSetCount});
        commit_record(holder, r);
    }
    s_.note(kPrintf, {kFormatTextureCount});
}

}  // namespace opentony::runtime::create
