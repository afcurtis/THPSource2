#include "trick_database.hpp"

#include <algorithm>
#include <cstring>
#include <format>

namespace opentony::runtime {
namespace {

std::uint16_t image_u16(std::span<const std::uint8_t> image, std::size_t offset) {
    if (offset + 2 > image.size()) {
        throw TrickDatabaseFault(std::format("TRICKS.BIN read at {:#x} leaves the image", offset));
    }
    return static_cast<std::uint16_t>(image[offset] | (image[offset + 1] << 8));
}

std::int16_t image_s16(std::span<const std::uint8_t> image, std::size_t offset) {
    return static_cast<std::int16_t>(image_u16(image, offset));
}

std::uint8_t image_u8(std::span<const std::uint8_t> image, std::size_t offset) {
    if (offset >= image.size()) {
        throw TrickDatabaseFault(std::format("TRICKS.BIN read at {:#x} leaves the image", offset));
    }
    return image[offset];
}

void note(std::vector<std::string>* sink, std::string message) {
    if (sink != nullptr) {
        sink->push_back(std::move(message));
    }
}

// A word of a combination table read as retail does, straight through the record boundary.
std::uint16_t combo_word(std::span<const ComboWords> table, std::size_t word) {
    const std::size_t record = word / 6;
    return record < table.size() ? table[record][word % 6] : std::uint16_t{0};
}

int find_combination(std::span<const ComboWords> table, std::span<const std::uint16_t> buttons,
                     const TrickDatabase& db) {
    const auto count = buttons.size();
    if (count == 0 || count > 4) {
        db.note(std::format("Unlikely NumButtons ({}) in trick database combination lookup", count));
    }
    for (std::size_t index = 0; index < table.size(); ++index) {
        if (table[index][0] != count) {
            continue;
        }
        bool match = true;
        for (std::size_t k = 0; k < count; ++k) {
            if (buttons[k] != combo_word(table, index * 6 + 1 + k)) {
                match = false;
            }
        }
        if (match) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

// One key-stream entry: count, buttons, source, descriptor.
struct KeyEntry final {
    std::array<std::uint16_t, 4> buttons{};
    std::size_t count{};
    std::uint16_t source{};
    std::uint16_t descriptor{};
};

// Reads the entry at `pos` (its count word is non-zero) and advances past it.
KeyEntry read_key_entry(std::span<const std::uint8_t> image, std::size_t& pos) {
    KeyEntry entry;
    const std::int16_t count = image_s16(image, pos);
    if (count < 1 || count > 4) {
        throw TrickDatabaseFault(std::format("key stream entry at {:#x} has {} buttons", pos, count));
    }
    entry.count = static_cast<std::size_t>(count);
    pos += 2;
    for (std::size_t i = 0; i < entry.count; ++i, pos += 2) {
        entry.buttons[i] = image_u16(image, pos);
    }
    entry.source = image_u16(image, pos);
    entry.descriptor = image_u16(image, pos + 2);
    pos += 4;
    return entry;
}

} // namespace

std::size_t trick_command_size(std::span<const std::uint8_t> image, std::size_t offset,
                               std::vector<std::string>* diagnostics) {
    const std::uint8_t op = image_u8(image, offset);
    switch (op) {
    case 0x01: case 0x09: case 0x0a: case 0x0d: case 0x0e: case 0x10: case 0x11: case 0x12:
    case 0x15: case 0x18: case 0x19: case 0x1b: case 0x1d: case 0x1f: case 0x21: case 0x23:
    case 0x24: case 0x25: case 0x26: case 0x27: case 0x28: case 0x29: case 0x2a: case 0x2d:
    case 0x2e: case 0x2f: case 0x34: case 0x36: case 0x3a: case 0x3d: case 0x3f: case 0x40:
    case 0x41: case 0x42: case 0x43: case 0x44: case 0x49: case 0x4b: case 0x4e: case 0x51:
    case 0x53: case 0x54: case 0x58:
        return offset + 3;
    case 0x0f: case 0x2b: case 0x38: case 0x45:
        return offset + 7;
    case 0x17: case 0x37: case 0x47: case 0x48:
        return offset + 5;
    case 0x39:
        return static_cast<std::size_t>(static_cast<std::ptrdiff_t>(offset + 3) +
                                        2 * static_cast<std::ptrdiff_t>(image_s16(image, offset + 1)));
    case 0x4c:
        return offset + 4;
    case 0x4f: case 0x0b: case 0x50: {
        std::size_t p = offset + (op == 0x4f ? 10 : 1);
        while (image_u8(image, p) != 0) {
            ++p;
        }
        return p + 1;
    }
    case 0x02: case 0x03: case 0x07: case 0x0c: case 0x13: case 0x14: case 0x16: case 0x1a:
    case 0x1c: case 0x1e: case 0x20: case 0x22: case 0x2c: case 0x30: case 0x31: case 0x32:
    case 0x33: case 0x35: case 0x3b: case 0x3c: case 0x3e: case 0x46: case 0x4a: case 0x4d:
    case 0x56: case 0x57:
        return offset + 1;
    case 0x52: case 0x55: case 0x59:
        return offset + 2;
    default:
        note(diagnostics, std::format("Script Error {}", static_cast<int>(static_cast<std::int8_t>(op))));
        return offset + 1;
    }
}

void trick_clean_name(std::string& name, bool strip_direction_prefix) {
    const std::size_t length = name.size();
    if (length == 0) {
        return;
    }
    auto at = [&](std::size_t i) -> char { return i < name.size() ? name[i] : '\0'; };
    std::size_t in = 0;
    std::size_t out = 0;
    for (;;) {
        const bool prefix = strip_direction_prefix && (at(in) == 'f' || at(in) == 'b') &&
                            at(in + 1) == 's' && at(in + 2) == ' ' && in <= 1;
        if (prefix) {
            in += 3;
        } else {
            const char c = at(in);
            if (c != '{' && c != '}') {
                name[out++] = c;
            }
            ++in;
        }
        if (length <= in) {
            name.resize(out);
            return;
        }
    }
}

const TrickRecord& TrickDatabase::record(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= records_.size()) {
        note(std::format("Out of range index {}", index));
        throw TrickDatabaseFault(std::format("trick record index {} out of range", index));
    }
    return records_[static_cast<std::size_t>(index)];
}

const TrickRecord* TrickDatabase::find_by_source(std::uint16_t source) const noexcept {
    for (const auto& r : records_) {
        if (r.source == source) {
            return &r;
        }
    }
    return nullptr;
}

int TrickDatabase::index_by_source(std::uint16_t source) const {
    for (std::size_t i = 0; i < records_.size(); ++i) {
        if (records_[i].source == source) {
            return static_cast<int>(i);
        }
    }
    note("Trick not found");
    return -1;
}

int TrickDatabase::find_button_combination(std::span<const std::uint16_t> buttons) const {
    return find_combination(builtin_combos(), buttons, *this);
}

int TrickDatabase::find_special_combination(std::span<const std::uint16_t> buttons) const {
    return find_combination(special_combos(), buttons, *this);
}

TrickRecord& TrickDatabase::add_or_merge(const std::string& name, std::uint16_t flags, std::uint16_t cost,
                                         std::uint16_t source, std::uint16_t descriptor) {
    auto* existing = const_cast<TrickRecord*>(find_by_source(source));
    if (existing == nullptr) {
        if (records_.size() >= kTrickDatabaseCapacity) {
            note("Too many tricks in database");
            throw TrickDatabaseFault("trick database is full");
        }
        if (name.size() >= kTrickNameCapacity) {
            note("Name too long");
            throw TrickDatabaseFault("trick name does not fit the record");
        }
        records_.push_back(TrickRecord{name, source, cost, flags, descriptor});
        existing = &records_.back();
    } else if ((descriptor & kTrickFlagSpecial) != 0 && (existing->flags & kTrickFlagSpecial) == 0) {
        note("Trick has been listed twice as both ordinary and special");
        existing->flags |= kTrickFlagSpecial;
        existing->descriptor |= kTrickFlagSpecial;
    }
    if ((existing->descriptor & 0x0800) != 0) {
        existing->flags |= 0x0800;
    }
    if ((existing->descriptor & 0x4000) != 0) {
        existing->flags = static_cast<std::uint16_t>((existing->flags & 0xf7ff) | 0x4000);
    }
    if ((existing->descriptor & 0x8000) != 0) {
        existing->flags |= 0x8000;
    }
    if ((existing->descriptor & 0x2000) != 0) {
        existing->flags |= 0x2000;
    }
    if ((existing->descriptor & 0x1000) != 0) {
        existing->flags |= 0x1000;
    }
    if (existing->name != name) {
        note(std::format("Bad name {} {} in duplicate trick", existing->name, name));
    }
    return *existing;
}

std::size_t trick_section_offset(std::span<const std::uint8_t> image, int section) {
    if (section < 0 || section > 6) {
        throw TrickDatabaseFault("TRICKS.BIN has seven sections");
    }
    return static_cast<std::size_t>(static_cast<std::ptrdiff_t>(image_s16(image, 2 * static_cast<std::size_t>(section))));
}

std::size_t trick_character_key_stream(std::span<const std::uint8_t> image, int character) {
    const std::size_t table = trick_section_offset(image, kSectionCharacterKeys);
    return static_cast<std::size_t>(
        static_cast<std::ptrdiff_t>(image_s16(image, table + 2 * static_cast<std::size_t>(character))));
}

namespace {

// TrickDatabase_ParseRecord (0x004bb7e0) over the command stream at `pos`.
void parse_record(TrickDatabase& db, std::vector<std::uint8_t>& image, std::size_t pos,
                  std::uint16_t source, std::uint16_t table_class, std::uint16_t descriptor) {
    std::string name = "Unknown trick";
    bool have_name = false;
    bool have_p = false;
    int cost_tokens = 0;
    std::uint16_t cost = kTrickDefaultCost;
    std::uint32_t flags = table_class;

    auto take_name = [&](std::size_t at) {
        std::size_t end = at;
        while (image_u8(image, end) != 0) {
            const std::uint8_t c = image[end];
            if (c >= 'A' && c <= 'Z') {
                image[end] = static_cast<std::uint8_t>(c + ' ');  // FUN_004b8340 lowercases in place
            }
            ++end;
        }
        if (end - at >= kTrickNameCapacity) {
            throw TrickDatabaseFault("trick name overflows the parse buffer");
        }
        name.assign(reinterpret_cast<const char*>(image.data() + at), end - at);
        trick_clean_name(name, (flags & kTrickClassLip) != 0);
    };

    std::uint8_t c = image_u8(image, pos);
    if (c != 0x07) {
        do {
            if (c != 0x0b || have_name) {
                if (c != 'P' || have_p) {
                    if (c == 'Q') {
                        cost = static_cast<std::uint16_t>(image_s16(image, pos + 1));
                        ++cost_tokens;
                    } else if (c == 'X') {
                        flags |= static_cast<std::uint32_t>(static_cast<std::int32_t>(image_s16(image, pos + 1)));
                    }
                } else {
                    take_name(pos + 1);
                    have_name = true;
                    have_p = true;
                }
            } else {
                take_name(pos + 1);
                have_name = true;
            }
            pos = trick_command_size(image, pos, db.diagnostics_sink());
            c = image_u8(image, pos);
        } while (c != 0x07);
        if (cost_tokens > 1) {
            db.note(std::format("Warning: Cost listed {} times for {}", cost_tokens, name));
        }
    }
    db.add_or_merge(name, static_cast<std::uint16_t>(flags), cost, source, descriptor);
}

} // namespace

TrickDatabase TrickDatabase::build(std::vector<std::uint8_t>& image) {
    TrickDatabase db;
    const std::size_t all_tricks = trick_section_offset(image, kSectionAllTricks);
    if (all_tricks == 0) {
        db.note("GAllTricks doesn't exist");
        throw TrickDatabaseFault("TRICKS.BIN has no GAllTricks section");
    }
    std::size_t pos = all_tricks;
    std::int16_t count = image_s16(image, pos);
    while (count != 0) {
        if (count < 1 || count > 4) {
            db.note(std::format("Unlikely NumButtons ({}) in All Tricks", count));
            throw TrickDatabaseFault("descriptor prefix count is outside 1..4");
        }
        const std::size_t n = static_cast<std::size_t>(count);
        const std::int16_t type = image_s16(image, pos + 2 * n);
        const std::size_t fields = pos + 2 * (n + 1);
        std::uint16_t table_class = 0;
        switch (type) {
        case 1: case 2: table_class = kTrickClassAir; break;
        case 9: table_class = kTrickClassGrind; break;
        case 10: table_class = kTrickClassManual; break;
        case 12: table_class = kTrickClassLip; break;
        default: db.note("Unrecognized trick type"); break;
        }
        const std::int16_t data_offset = image_s16(image, fields);
        if (data_offset < 0) {
            throw TrickDatabaseFault("trick script offset is negative");
        }
        parse_record(db, image, static_cast<std::size_t>(data_offset), image_u16(image, fields), table_class,
                     image_u16(image, fields + 2));
        pos = fields + 4;
        count = image_s16(image, pos);
    }
    return db;
}

bool trick_config_consume_points_and_set_bit(RetailImage& config, const TrickDatabase& db, int trick_index,
                                             int cost) {
    if (trick_index < 0 || trick_index > 0x7f) {
        db.note("Bad trick index");
    }
    const std::int32_t points = config.s32(kConfigPoints);
    if (points < cost) {
        return false;
    }
    config.set32(kConfigPoints, static_cast<std::uint32_t>(points - cost));
    // Retail rounds the index toward zero before scaling it to a word; every legal index is >= 0.
    const std::size_t word = kConfigUnlocks + 4 * static_cast<std::size_t>(trick_index >> 5);
    config.set32(word, config.u32(word) | (1u << (static_cast<unsigned>(trick_index) & 0x1f)));
    return true;
}

void trick_binding_assign_button(std::span<std::uint8_t> block, const TrickDatabase& db, int combo, int record) {
    if (combo < 0 || combo > 0x2a) {
        db.note("Assignable trick out of range");
    }
    if (((record < 0) || (record > 0x7f)) && record != 0xff) {
        db.note(std::format("Bad trick index {}", record));
    }
    if (combo < 0 || static_cast<std::size_t>(combo) >= block.size()) {
        throw TrickDatabaseFault("binding slot is outside the block");
    }
    block[static_cast<std::size_t>(combo)] = static_cast<std::uint8_t>(record);
}

bool trick_binding_assign_special(std::span<std::uint8_t> block, const TrickDatabase& db, int combo, int record) {
    auto special_record = [&](std::size_t slot) -> std::uint8_t& { return block[kBindingSpecialRecords + slot]; };
    auto special_combo = [&](std::size_t slot) -> std::uint8_t& { return block[kBindingSpecialCombos + slot]; };
    if (block.size() < kBindingBlockSize) {
        throw TrickDatabaseFault("binding block is too small");
    }
    if (record == -1) {
        for (std::size_t slot = 0; slot < 5; ++slot) {
            if (static_cast<std::int8_t>(special_combo(slot)) == combo) {
                special_record(slot) = kBindingEmpty;
                special_combo(slot) = kBindingEmpty;
                break;
            }
        }
        return true;
    }
    if (record < 0 || record > 0x7f) {
        db.note(std::format("Bad trick index {}", record));
    }
    for (std::size_t slot = 0; slot < 5; ++slot) {
        if (static_cast<std::int8_t>(special_combo(slot)) == combo) {
            special_record(slot) = kBindingEmpty;
            special_combo(slot) = kBindingEmpty;
        }
    }
    if (trick_binding_special_assigned(block, record).has_value()) {
        return false;
    }
    for (std::size_t slot = 0; slot < 5; ++slot) {
        if (special_record(slot) == kBindingEmpty) {
            special_record(slot) = static_cast<std::uint8_t>(record);
            special_combo(slot) = static_cast<std::uint8_t>(combo);
            return true;
        }
    }
    return false;
}

std::optional<std::uint8_t> trick_binding_special_assigned(std::span<const std::uint8_t> block, int record) {
    if (block.size() < kBindingBlockSize) {
        throw TrickDatabaseFault("binding block is too small");
    }
    for (std::size_t slot = 0; slot < 5; ++slot) {
        if (static_cast<std::int8_t>(block[kBindingSpecialRecords + slot]) == record) {
            return block[kBindingSpecialCombos + slot];
        }
    }
    return std::nullopt;
}

void trick_apply_unlock_state(const TrickDatabase& db, std::span<const std::uint8_t> image,
                              std::size_t key_stream, RetailImage& config) {
    std::size_t pos = key_stream;
    while (image_s16(image, pos) != 0) {
        const KeyEntry entry = read_key_entry(image, pos);
        if (db.find_by_source(entry.source) != nullptr) {
            const int index = db.index_by_source(entry.source);
            trick_config_consume_points_and_set_bit(config, db, index, 0);
        }
    }
}

void trick_apply_character_bindings(const TrickDatabase& db, std::span<const std::uint8_t> image,
                                    std::size_t key_stream, std::span<std::uint8_t> block, int character) {
    if (block.size() < kBindingBlockSize) {
        throw TrickDatabaseFault("binding block is too small");
    }
    std::fill_n(block.begin(), kBindingBlockSize, kBindingEmpty);

    std::size_t pos = key_stream;
    while (image_s16(image, pos) != 0) {
        const KeyEntry entry = read_key_entry(image, pos);
        if (db.find_by_source(entry.source) == nullptr) {
            db.note(std::format("Trick exists in skater {}'s trick list but not in the database", character));
        }
        if ((entry.descriptor & kTrickFlagSpecial) == 0) {
            const int combo = db.find_button_combination(std::span(entry.buttons.data(), entry.count));
            if (combo != -1) {
                trick_binding_assign_button(block, db, combo, db.index_by_source(entry.source));
            }
        }
    }

    pos = key_stream;
    while (image_s16(image, pos) != 0) {
        const KeyEntry entry = read_key_entry(image, pos);
        if (db.find_by_source(entry.source) == nullptr) {
            db.note(std::format("Trick exists in skater {}'s trick list but not in the database", character));
        }
        if ((entry.descriptor & kTrickFlagSpecial) != 0) {
            const int combo = db.find_special_combination(std::span(entry.buttons.data(), entry.count));
            const int record = db.index_by_source(entry.source);
            if (combo == -1) {
                db.note(std::format("Unrecognized special key combo for skater {}", character));
            } else if (!trick_binding_assign_special(block, db, combo, record)) {
                db.note(std::format("Skater {} has too many special tricks", character));
            }
        }
    }
}

void trick_config_initialize_character(const TrickDatabase& db, std::span<const std::uint8_t> image,
                                       RetailImage& config, int character,
                                       std::span<const std::uint8_t, 10> costs, bool full_class_mask) {
    if (config.size() < kTrickConfigSize) {
        throw TrickDatabaseFault("character record is too small");
    }
    std::fill(config.bytes().begin(), config.bytes().begin() + kTrickConfigSize, std::uint8_t{0});
    for (std::size_t i = 0; i < costs.size(); ++i) {
        config.set8(kConfigCosts + i, costs[i]);
    }
    config.set32(kConfigClassMask, full_class_mask ? 0x3ff : 1);
    const std::size_t stream = trick_character_key_stream(image, character);
    trick_apply_unlock_state(db, image, stream, config);
    trick_apply_character_bindings(
        db, image, stream, std::span(config.bytes().data() + kConfigBindings, kBindingBlockSize), character);
}

std::vector<std::array<std::uint16_t, 6>> trick_table_return_combos(
    const TrickDatabase& db, std::span<const std::uint8_t> image, std::size_t key_stream, std::size_t limit) {
    std::vector<std::array<std::uint16_t, 6>> entries;
    std::size_t pos = key_stream;
    if (image_u16(image, pos) == 0) {
        return entries;
    }
    do {
        if (entries.size() >= limit) {
            db.note("Too many player tricks");
            throw TrickDatabaseFault("player trick list overflows the return-combo buffer");
        }
        const std::int16_t count = image_s16(image, pos);
        pos += 2;
        if (count < 1 || count > 4) {
            db.note(std::format("Unlikely NumButtons ({}) in trick list", count));
            throw TrickDatabaseFault("player trick entry has a bad button count");
        }
        std::array<std::uint16_t, 6> entry{};
        entry[0] = static_cast<std::uint16_t>(count);
        for (std::int16_t i = 0; i < count; ++i, pos += 2) {
            if (count >= 4) {
                db.note("Too many buttons");
            }
            entry[static_cast<std::size_t>(i) + 1] = image_u16(image, pos);
        }
        const std::uint16_t source = image_u16(image, pos);
        entry[5] = source;
        const TrickRecord* record = db.find_by_source(source);
        if (record == nullptr) {
            throw TrickDatabaseFault("player trick names a record the database does not hold");
        }
        entry[4] = record->flags;
        pos += 4;
        entries.push_back(entry);
    } while (image_u16(image, pos) != 0);
    return entries;
}

namespace {

using Entry = std::array<std::uint16_t, 6>;

// TrickTable_AppendFilteredEntries (0x004bcb70)
void append_filtered(std::vector<std::uint16_t>& out, const TrickDatabase& db, const std::vector<Entry>& entries,
                     std::uint16_t mask) {
    for (const Entry& e : entries) {
        if ((e[4] & kTrickFlagSpecial) != 0) {
            continue;
        }
        if (db.find_button_combination(std::span(e.data() + 1, e[0])) != -1) {
            continue;
        }
        if ((e[4] & mask) == 0) {
            continue;
        }
        out.push_back(e[0]);
        for (std::size_t i = 0; i < e[0]; ++i) {
            out.push_back(e[1 + i]);
        }
        out.push_back(e[5]);
        const TrickRecord* record = db.find_by_source(e[5]);
        if (record == nullptr) {
            db.note("Generic trick not found");
            throw TrickDatabaseFault("generic trick is not in the database");
        }
        out.push_back(record->descriptor);
    }
}

// TrickTable_AppendBuiltInCombos (0x004bcc90)
void append_builtin(std::vector<std::uint16_t>& out, const TrickDatabase& db, std::span<const std::uint8_t> block,
                    std::uint16_t mask, std::uint16_t kind) {
    const auto table = builtin_combos();
    for (std::size_t i = 0; i < table.size(); ++i) {
        if ((table[i][4] & mask) != 0 && table[i][5] == kind && block[i] != kBindingEmpty) {
            const TrickRecord& record = db.record(static_cast<std::int8_t>(block[i]));
            out.push_back(table[i][0]);
            for (std::size_t k = 0; k < table[i][0]; ++k) {
                out.push_back(combo_word(table, i * 6 + 1 + k));
            }
            out.push_back(record.source);
            out.push_back(record.descriptor);
        }
    }
}

// TrickTable_AppendSpecialCombos (0x004bcdd0)
void append_special(std::vector<std::uint16_t>& out, const TrickDatabase& db, std::span<const std::uint8_t> block) {
    const auto table = special_combos();
    for (std::size_t slot = 0; slot < 5; ++slot) {
        if (block[kBindingSpecialRecords + slot] == kBindingEmpty) {
            continue;
        }
        const int combo = static_cast<std::int8_t>(block[kBindingSpecialCombos + slot]);
        if (combo >= 50) {
            db.note("Unlikely key combo index");
        }
        if (combo < 0 || static_cast<std::size_t>(combo) >= table.size()) {
            throw TrickDatabaseFault("special combination index is outside the table");
        }
        const auto& words = table[static_cast<std::size_t>(combo)];
        out.push_back(words[0]);
        for (std::size_t k = 0; k < words[0]; ++k) {
            out.push_back(combo_word(table, static_cast<std::size_t>(combo) * 6 + 1 + k));
        }
        const TrickRecord& record = db.record(static_cast<std::int8_t>(block[kBindingSpecialRecords + slot]));
        out.push_back(record.source);
        out.push_back(record.descriptor);
    }
}

} // namespace

std::vector<std::uint16_t> trick_table_build_for_player(
    const TrickDatabase& db, std::span<const std::uint8_t> image, std::size_t key_stream,
    std::span<const std::uint8_t> block) {
    if (block.size() < kBindingBlockSize) {
        throw TrickDatabaseFault("binding block is too small");
    }
    const auto entries = trick_table_return_combos(db, image, key_stream);
    std::vector<std::uint16_t> out;
    auto pass = [&](std::uint16_t mask) {
        append_filtered(out, db, entries, mask);
        for (std::uint16_t kind = 0; kind < 4; ++kind) {
            append_builtin(out, db, block, mask, kind);
        }
    };
    pass(0x1000);
    pass(0x0800);
    append_special(out, db, block);
    pass(0x2000);
    pass(0x4000);
    pass(0x0300);
    out.push_back(0);
    if (out.size() * 2 > kPlayerTableMaxBytes) {
        db.note("Trick table is too big, must be 512 shorts or less");
        throw TrickDatabaseFault("player trick table does not fit its 0x400-byte buffer");
    }
    return out;
}

} // namespace opentony::runtime
