#include "trick_sets.hpp"

#include "runtime/trick_database.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

namespace thpsource2 {

namespace rt = opentony::runtime;

const std::array<const char*, kTrickPageCount> kTrickPageNames = {"Flip tricks", "Grab tricks", "Lip tricks", "Grinds", "Specials"};
const std::array<const char*, kTrickKindCount> kTrickKindNames = {"Flip tricks", "Grab tricks", "Lip tricks", "Grinds", "Manuals"};

namespace {

// The pad's codes in the combination tables (the recogniser's: re/evidence/functions/trick-database-runtime.md and TrickMenu_ButtonGlyphs, 0x004c1310): 1..8 the
// directions, the diagonals from the pattern table of trick_recognition.cpp (5 up and left, 6 up and right, 7 down and left, 8 down and right), then the four
// buttons in the PlayStation's order of the symbol font: 9 Circle (Grab), 10 Square (Flip), 11 Cross (Ollie), 12 Triangle (Grind). The names are the launcher's
// own for the actions (bindings.cpp kActions).
// the directions are arrows (U+2191 up, U+2193 down, U+2190 left, U+2192 right, U+2196 up-left, U+2197 up-right, U+2199 down-left, U+2198 down-right)
constexpr const char* kButtonLabels[] = {"", "\xE2\x86\x91", "\xE2\x86\x93", "\xE2\x86\x90", "\xE2\x86\x92", "\xE2\x86\x96", "\xE2\x86\x97", "\xE2\x86\x99", "\xE2\x86\x98", "Grab", "Flip", "Ollie", "Grind"};
constexpr const char* kButtonKeys[] = {"", "Up", "Down", "Left", "Right", "UpLeft", "UpRight", "DownLeft", "DownRight", "Grab", "Flip", "Ollie", "Grind"};
constexpr int kGrabButton = 9;
constexpr int kFlipButton = 10;
constexpr int kGrindButton = 12;

constexpr std::uint16_t kFlagGrab = 0x100;     // rt::kTrickClassGrind: the database's names are the descriptor types', the menu's are these
constexpr std::uint16_t kFlagFlip = 0x200;     // rt::kTrickClassManual
constexpr std::uint16_t kFlagGrind = 0x800;    // rt::kTrickClassLip
constexpr std::uint16_t kFlagManual = 0x2000;  // rt::kTrickClassAir
constexpr std::uint16_t kFlagLip = 0x4000;
constexpr std::uint16_t kFlagSpecial = rt::kTrickFlagSpecial;

TrickCombo make_combo(const rt::ComboWords& words, TrickPage page) {
    TrickCombo combo;
    const std::size_t count = std::min<std::size_t>(words[0], 3);
    for (std::size_t k = 0; k < count; ++k) {
        const std::uint16_t code = words[1 + k] <= 12 ? words[1 + k] : 0;
        const bool last = k + 1 == count;
        if (k > 0) {
            combo.key += last ? "+" : ",";
            combo.label += last ? " + " : ", ";
        }
        combo.key += kButtonKeys[code];
        combo.label += kButtonLabels[code];
        if (last) {
            combo.button = code;
        }
    }
    combo.page = page;
    return combo;
}

// TrickMenu_BuildNormalCombos (0x004c2c10): a combination's page from its last button and its class
TrickPage ordinary_page(const rt::ComboWords& words) {
    const int last = words[0] >= 1 && words[0] <= 3 ? words[words[0]] : 0;
    if (last == kFlipButton) {
        return TrickPage::Flip;
    }
    if (last == kGrabButton) {
        return TrickPage::Grab;
    }
    return (words[4] & kFlagLip) != 0 ? TrickPage::Lip : TrickPage::Grind;
}

TrickKind kind_of(std::uint16_t flags) {
    if ((flags & kFlagManual) != 0) {
        return TrickKind::Manual;
    }
    if ((flags & kFlagLip) != 0) {
        return TrickKind::Lip;
    }
    if ((flags & kFlagGrind) != 0) {
        return TrickKind::Grind;
    }
    if ((flags & kFlagFlip) != 0) {
        return TrickKind::Flip;
    }
    return TrickKind::Grab;
}

std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string special_key(int slot) { return "Special" + std::to_string(slot + 1); }

}  // namespace

const std::array<TrickCombo, kTrickCombos>& trick_combos() {
    static const std::array<TrickCombo, kTrickCombos> combos = [] {
        std::array<TrickCombo, kTrickCombos> out{};
        const auto table = rt::builtin_combos();
        for (std::size_t k = 0; k < out.size() && k < table.size(); ++k) {
            out[k] = make_combo(table[k], ordinary_page(table[k]));
        }
        return out;
    }();
    return combos;
}

const std::array<TrickCombo, kSpecialCombos>& special_trick_combos() {
    static const std::array<TrickCombo, kSpecialCombos> combos = [] {
        std::array<TrickCombo, kSpecialCombos> out{};
        const auto table = rt::special_combos();
        for (std::size_t k = 0; k < out.size() && k < table.size(); ++k) {
            out[k] = make_combo(table[k], TrickPage::Special);
        }
        return out;
    }();
    return combos;
}

int find_trick_combo(std::string_view key) {
    const auto& combos = trick_combos();
    for (std::size_t k = 0; k < combos.size(); ++k) {
        if (equal_nocase(combos[k].key, key)) {
            return static_cast<int>(k);
        }
    }
    return -1;
}

int find_special_combo(std::string_view key) {
    const auto& combos = special_trick_combos();
    for (std::size_t k = 0; k < combos.size(); ++k) {
        if (equal_nocase(combos[k].key, key)) {
            return static_cast<int>(k);
        }
    }
    return -1;
}

std::string trick_label(std::string_view name) {
    std::string out;
    std::size_t start = 0;
    bool first = true;
    while (start <= name.size()) {
        std::size_t end = name.find(' ', start);
        if (end == std::string_view::npos) {
            end = name.size();
        }
        std::string word = lower(name.substr(start, end - start));
        if (word == "fs" || word == "bs") {
            word = word == "fs" ? "FS" : "BS";
        } else if (!(word == "to" || word == "n") || first) {
            for (std::size_t k = 0; k < word.size(); ++k) {
                if (k == 0 || word[k - 1] == '-') {
                    word[k] = static_cast<char>(std::toupper(static_cast<unsigned char>(word[k])));
                }
            }
        }
        if (!out.empty() || !word.empty()) {
            if (!first) {
                out += ' ';
            }
            out += word;
            first = false;
        }
        start = end + 1;
    }
    return out;
}

std::optional<TrickCatalog> TrickCatalog::load(const std::filesystem::path& tricks_bin, std::string& error) {
    std::ifstream in(tricks_bin, std::ios::binary);
    if (!in) {
        error = "TRICKS.BIN is not there yet: the game's files are unpacked the first time the game starts";
        return std::nullopt;
    }
    std::vector<std::uint8_t> image{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return from_image(std::move(image), error);
}

std::optional<TrickCatalog> TrickCatalog::from_image(std::vector<std::uint8_t> image, std::string& error) {
    TrickCatalog catalog;
    try {
        const rt::TrickDatabase db = rt::TrickDatabase::build(image);   // lowercases the names in the image, as the game does before it reads the key streams
        for (const rt::TrickRecord& record : db.records()) {
            TrickInfo info;
            info.name = record.name;
            info.label = trick_label(record.name);
            info.flags = record.flags;
            info.kind = kind_of(record.flags);
            info.special = (record.flags & kFlagSpecial) != 0;
            catalog.tricks_.push_back(std::move(info));
        }
        for (int skater = 0; skater < 20; ++skater) {
            rt::RetailImage config(rt::kTrickConfigSize);
            const std::array<std::uint8_t, 10> costs{};
            rt::trick_config_initialize_character(db, image, config, skater, costs, false);
            const std::uint8_t* block = config.bytes().data() + rt::kConfigBindings;
            TrickSet set;
            for (std::size_t k = 0; k < kTrickCombos; ++k) {
                if (block[k] < catalog.tricks_.size()) {
                    set.combos[k] = catalog.tricks_[block[k]].name;
                }
            }
            std::size_t slot = 0;
            for (std::size_t k = 0; k < kSpecialSlots; ++k) {
                const std::uint8_t record = block[rt::kBindingSpecialRecords + k];
                const std::uint8_t combo = block[rt::kBindingSpecialCombos + k];
                if (record < catalog.tricks_.size() && combo < kSpecialCombos) {
                    set.specials[slot++] = {special_trick_combos()[combo].key, catalog.tricks_[record].name};
                }
            }
            catalog.skater_sets_.push_back(std::move(set));
        }
    } catch (const std::exception& e) {
        error = std::string("TRICKS.BIN cannot be read: ") + e.what();
        return std::nullopt;
    }
    if (catalog.tricks_.empty()) {
        error = "TRICKS.BIN has no tricks";
        return std::nullopt;
    }
    return catalog;
}

int TrickCatalog::find_trick(std::string_view name) const {
    for (std::size_t k = 0; k < tricks_.size(); ++k) {
        if (equal_nocase(tricks_[k].name, name)) {
            return static_cast<int>(k);
        }
    }
    return -1;
}

bool TrickCatalog::fits(int combo, int trick) const {
    if (combo < 0 || combo >= static_cast<int>(kTrickCombos) || trick < 0 || trick >= static_cast<int>(tricks_.size())) {
        return false;
    }
    const std::uint16_t flags = tricks_[static_cast<std::size_t>(trick)].flags;
    if ((flags & kFlagSpecial) != 0) {
        return false;
    }
    switch (trick_combos()[static_cast<std::size_t>(combo)].page) {
    case TrickPage::Flip:
    case TrickPage::Grab: return (flags & (kFlagFlip | kFlagGrab)) != 0;
    case TrickPage::Lip: return (flags & kFlagLip) != 0;
    case TrickPage::Grind: return (flags & kFlagGrind) != 0;
    default: return false;
    }
}

bool TrickCatalog::fits_special(int special_combo, int trick) const {
    if (special_combo < 0 || special_combo >= static_cast<int>(kSpecialCombos) || trick < 0 || trick >= static_cast<int>(tricks_.size())) {
        return false;
    }
    const std::uint16_t flags = tricks_[static_cast<std::size_t>(trick)].flags;
    const bool grind_button = special_trick_combos()[static_cast<std::size_t>(special_combo)].button == kGrindButton;
    return (flags & kFlagSpecial) != 0 && ((flags & (kFlagGrind | kFlagLip)) != 0) == grind_button;
}

TrickSet TrickCatalog::skater_set(int skater) const {
    TrickSet set;
    if (skater >= 0 && skater < static_cast<int>(skater_sets_.size())) {
        set = skater_sets_[static_cast<std::size_t>(skater)];
    }
    return set;
}

std::array<std::uint8_t, kTrickBlockSize> TrickCatalog::block(const TrickSet& set, std::vector<std::string>* problems) const {
    std::array<std::uint8_t, kTrickBlockSize> out;
    out.fill(rt::kBindingEmpty);
    const auto note = [problems](std::string text) {
        if (problems != nullptr) {
            problems->push_back(std::move(text));
        }
    };
    for (std::size_t k = 0; k < kTrickCombos; ++k) {
        if (set.combos[k].empty()) {
            continue;
        }
        const int trick = find_trick(set.combos[k]);
        if (trick < 0) {
            note(trick_combos()[k].label + ": no trick called \"" + set.combos[k] + "\"");
        } else if (!fits(static_cast<int>(k), trick)) {
            note(trick_combos()[k].label + ": " + tricks_[static_cast<std::size_t>(trick)].label + " cannot go there");
        } else {
            out[k] = static_cast<std::uint8_t>(trick);
        }
    }
    std::size_t slot = 0;
    for (const TrickSet::Special& special : set.specials) {
        if (special.combo.empty() && special.trick.empty()) {
            continue;
        }
        const int combo = find_special_combo(special.combo);
        const int trick = find_trick(special.trick);
        const auto used = [&](std::size_t base, int value) {
            for (std::size_t k = 0; k < slot; ++k) {
                if (out[base + k] == value) {
                    return true;
                }
            }
            return false;
        };
        if (combo < 0 || trick < 0) {
            note("Special \"" + special.combo + ": " + special.trick + "\" is not complete");
        } else if (!fits_special(combo, trick)) {
            note(tricks_[static_cast<std::size_t>(trick)].label + " cannot go on " + special_trick_combos()[static_cast<std::size_t>(combo)].label);
        } else if (used(rt::kBindingSpecialRecords, trick)) {
            note(tricks_[static_cast<std::size_t>(trick)].label + " is on two specials");
        } else if (used(rt::kBindingSpecialCombos, combo)) {
            note(special_trick_combos()[static_cast<std::size_t>(combo)].label + " has two specials");
        } else {
            out[rt::kBindingSpecialRecords + slot] = static_cast<std::uint8_t>(trick);
            out[rt::kBindingSpecialCombos + slot] = static_cast<std::uint8_t>(combo);
            ++slot;
        }
    }
    return out;
}

std::string trick_block_hex(const std::array<std::uint8_t, kTrickBlockSize>& block) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    for (std::uint8_t byte : block) {
        out += kDigits[byte >> 4];
        out += kDigits[byte & 15];
    }
    return out;
}

bool trick_set_name_valid(std::string_view name) {
    if (name.empty() || name.size() > 40 || trim(name) != name || name.front() == ';' || name.front() == '#') {
        return false;
    }
    return std::none_of(name.begin(), name.end(), [](char c) { return c == '[' || c == ']' || c == '\r' || c == '\n'; });
}

std::vector<TrickSet> read_trick_sets(const IniFile& ini) {
    std::vector<TrickSet> sets;
    for (const std::string& section : ini.sections()) {
        if (!trick_set_name_valid(section)) {
            continue;
        }
        TrickSet set;
        set.name = section;
        for (std::size_t k = 0; k < kTrickCombos; ++k) {
            set.combos[k] = lower(ini.get_string(section, trick_combos()[k].key));
        }
        for (std::size_t k = 0; k < kSpecialSlots; ++k) {
            const std::string value = ini.get_string(section, special_key(static_cast<int>(k)));
            const std::size_t colon = value.find(':');
            if (colon != std::string::npos) {
                const int combo = find_special_combo(trim(std::string_view(value).substr(0, colon)));
                set.specials[k].combo = combo >= 0 ? special_trick_combos()[static_cast<std::size_t>(combo)].key : trim(std::string_view(value).substr(0, colon));
                set.specials[k].trick = lower(trim(std::string_view(value).substr(colon + 1)));
            }
        }
        sets.push_back(std::move(set));
    }
    return sets;
}

IniFile write_trick_sets(const std::vector<TrickSet>& sets) {
    IniFile ini = IniFile::parse(
        "; THPSource2's trick sets for Quick Start (Quick Start > Edit Tricks). One section a set: each button combination names its trick (empty: none),\r\n"
        "; Special1 to Special5 a special's combination and trick. The game's own career and PLAY never read this file.\r\n");
    for (const TrickSet& set : sets) {
        if (!trick_set_name_valid(set.name)) {
            continue;
        }
        for (std::size_t k = 0; k < kTrickCombos; ++k) {
            ini.set(set.name, trick_combos()[k].key, set.combos[k]);
        }
        for (std::size_t k = 0; k < kSpecialSlots; ++k) {
            const TrickSet::Special& special = set.specials[k];
            ini.set(set.name, special_key(static_cast<int>(k)), special.combo.empty() && special.trick.empty() ? std::string() : special.combo + ": " + special.trick);
        }
    }
    return ini;
}

}  // namespace thpsource2
