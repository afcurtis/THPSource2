#include "bindings.hpp"

#include "hal/input.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

namespace thpsource2 {

// The first ten have a pad button ([JOYPAD]); all fourteen have keys ([KEYBOARD]).
const std::array<ActionInfo, kActionCount> kActions = {{
    {"JUMP", "Ollie"},
    {"GRIND", "Grind"},
    {"GRAB", "Grab"},
    {"KICK", "Flip"},
    {"SPINLEFT", "Spin Left"},
    {"NOLLIE", "Nollie"},
    {"SPINRIGHT", "Spin Right"},
    {"SWITCH", "Switch"},
    {"PAUSE", "Pause"},
    {"CAMERA", "Camera"},
    {"UP", "Up"},
    {"DOWN", "Down"},
    {"LEFT", "Left"},
    {"RIGHT", "Right"},
}};

namespace {

// retail addresses (Activision Value re-release THawk2.exe; src/runtime/pad_input.cpp). A PE file is read only when it is that build (its timestamp,
// re/config/native_hooks.yml builds.canonical); the static-data image has them at the same addresses whichever release it was made from.
constexpr std::uint32_t kValueTimestamp = 0x3a54c1f6u;
constexpr std::uint32_t kNameTable = 0x0052eff0;   // 256 pointers to the key names
constexpr std::uint32_t kPairs = 0x0054ae28;       // {action name, chord (4 key codes)} pairs in the order the game reads them, ended by a null name
struct JoystickEntry {
    const char* name;
    std::uint32_t variable;
};
constexpr JoystickEntry kJoystickDefaults[kPadActionCount] = {
    {"JUMP", 0x0054ae04}, {"GRIND", 0x0054ae08},  {"GRAB", 0x0054ae0c},     {"KICK", 0x006a6984},  {"SPINLEFT", 0x0054ae10},
    {"NOLLIE", 0x0054ae18}, {"SPINRIGHT", 0x0054ae14}, {"SWITCH", 0x0054ae1c}, {"PAUSE", 0x0054ae20}, {"CAMERA", 0x0054ae24},
};
// opentony.exe binds Start to Pause and Back to the camera when the game left them unbound (bind_pad_pause): the defaults here say the same
constexpr int kOpenTonyPause = 9;
constexpr int kOpenTonyCamera = 8;

struct Segment {
    std::uint32_t va;
    const std::uint8_t* data;
    std::uint32_t size;
};

class Image {
public:
    std::vector<Segment> segments;
    bool read(std::uint32_t va, void* out, std::size_t size) const {
        for (const Segment& s : segments) {
            if (va >= s.va && static_cast<std::uint64_t>(va) + size <= static_cast<std::uint64_t>(s.va) + s.size) {
                std::memcpy(out, s.data + (va - s.va), size);
                return true;
            }
        }
        return false;
    }
    std::optional<std::uint32_t> word(std::uint32_t va) const {
        std::uint8_t b[4];
        if (!read(va, b, 4)) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(b[0] | (b[1] << 8) | (b[2] << 16) | (static_cast<std::uint32_t>(b[3]) << 24));
    }
    std::optional<std::string> cstring(std::uint32_t va, std::size_t max = 64) const {
        std::string text;
        for (std::size_t k = 0; k < max; ++k) {
            std::uint8_t c = 0;
            if (!read(va + static_cast<std::uint32_t>(k), &c, 1)) {
                return std::nullopt;
            }
            if (c == 0) {
                return text;
            }
            text.push_back(static_cast<char>(c));
        }
        return std::nullopt;
    }
};

std::uint32_t le32(const std::vector<std::uint8_t>& f, std::size_t at) {
    return at + 4 <= f.size() ? static_cast<std::uint32_t>(f[at] | (f[at + 1] << 8) | (f[at + 2] << 16) | (static_cast<std::uint32_t>(f[at + 3]) << 24)) : 0;
}

bool image_of(const std::vector<std::uint8_t>& f, Image& image, std::string& error) {
    if (f.size() >= 24 && std::memcmp(f.data(), "OTSD", 4) == 0) {   // opentony\thawk2_static.img (tony/static_data.py)
        const std::uint32_t count = le32(f, 20);
        std::size_t at = 24 + 8 * static_cast<std::size_t>(count);
        if (count > 64 || at > f.size()) {
            error = "the static-data image is damaged";
            return false;
        }
        for (std::uint32_t k = 0; k < count; ++k) {
            const std::uint32_t va = le32(f, 24 + 8 * k);
            const std::uint32_t size = le32(f, 28 + 8 * k);
            if (at + size > f.size()) {
                error = "the static-data image is damaged";
                return false;
            }
            image.segments.push_back({va, f.data() + at, size});
            at += size;
        }
        return true;
    }
    if (f.size() < 0x40 || f[0] != 'M' || f[1] != 'Z') {
        error = "not an executable";
        return false;
    }
    const std::uint32_t pe = le32(f, 0x3c);
    if (pe + 24 > f.size() || std::memcmp(f.data() + pe, "PE\0\0", 4) != 0) {
        error = "not a PE executable";
        return false;
    }
    if (le32(f, pe + 8) != kValueTimestamp) {
        // the original 2000 release keeps the tables elsewhere: they are read from the static-data image opentony.exe rebuilds from it
        error = "THawk2.exe is not the Activision Value re-release; its tables are read from the unpacked game data once it is prepared";
        return false;
    }
    const std::uint32_t sections = f[pe + 6] | (f[pe + 7] << 8);
    const std::uint32_t optional_size = f[pe + 20] | (f[pe + 21] << 8);
    const std::uint32_t base = le32(f, pe + 24 + 28);
    for (std::uint32_t k = 0; k < sections; ++k) {
        const std::size_t header = pe + 24 + optional_size + 40 * static_cast<std::size_t>(k);
        if (header + 40 > f.size()) {
            break;
        }
        const std::uint32_t virtual_size = le32(f, header + 8);
        const std::uint32_t va = le32(f, header + 12);
        const std::uint32_t raw_size = le32(f, header + 16);
        const std::uint32_t raw = le32(f, header + 20);
        const std::uint32_t size = std::min(virtual_size != 0 ? virtual_size : raw_size, raw_size);
        if (static_cast<std::size_t>(raw) + size <= f.size()) {
            image.segments.push_back({base + va, f.data() + raw, size});
        }
    }
    return true;
}

std::size_t action_index(std::string_view ini_key) {
    for (std::size_t k = 0; k < kActionCount; ++k) {
        if (equal_nocase(kActions[k].ini_key, ini_key)) {
            return k;
        }
    }
    return kActionCount;
}

std::vector<std::string> split_commas(const std::string& text) {
    // strtok(text, ","): empty fields are skipped and the tokens are not trimmed
    std::vector<std::string> tokens;
    std::string current;
    for (char c : text) {
        if (c == ',') {
            if (!current.empty()) {
                tokens.push_back(current);
            }
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

std::vector<std::uint8_t> distinct_keys(const std::array<std::uint8_t, kKeysPerAction>& slots) {
    std::vector<std::uint8_t> keys;
    for (std::uint8_t code : slots) {
        if (code != 0 && std::find(keys.begin(), keys.end(), code) == keys.end()) {
            keys.push_back(code);
        }
    }
    return keys;
}

std::array<std::uint8_t, kKeysPerAction> padded(const std::vector<std::uint8_t>& keys) {
    std::array<std::uint8_t, kKeysPerAction> slots{};
    for (std::size_t k = 0; k < kKeysPerAction; ++k) {
        slots[k] = keys.empty() ? 0 : keys[std::min(k, keys.size() - 1)];
    }
    return slots;
}

std::string chord_text(const RetailTables& tables, const std::array<std::uint8_t, kKeysPerAction>& slots);

// The game's load of [KEYBOARD] (pad_input.cpp load_bindings) on the default chords: the chords it ends with, or nothing when it would reset them all.
// `values` holds each action's text as GetPrivateProfileString returns it (nullopt: the entry is missing and the current chord is the default text).
std::optional<std::array<std::array<std::uint8_t, 4>, kActionCount>> simulate_keyboard_load(
    const RetailTables& tables, const std::array<std::optional<std::string>, kActionCount>& values, std::string* why) {
    std::array<std::array<std::uint8_t, 4>, kActionCount> chords{};
    for (std::size_t a = 0; a < kActionCount; ++a) {
        for (std::size_t k = 0; k < kKeysPerAction; ++k) {
            chords[a][k] = tables.default_keys[a][k];
        }
    }
    const std::string& none = tables.key_names[0];
    for (std::size_t a : tables.load_order) {
        // a missing entry reads as the default text the game passes: the current chord's names, checked like any other
        const std::string text = values[a] ? *values[a] : chord_text(tables, {chords[a][0], chords[a][1], chords[a][2]});
        std::array<std::string, kKeysPerAction> parts = {none, none, none};
        const std::vector<std::string> tokens = split_commas(text);
        for (std::size_t k = 0; k < kKeysPerAction && k < tokens.size(); ++k) {
            parts[k] = tokens[k];
        }
        chords[a][3] = 0;
        for (std::size_t index = 0; index < kKeysPerAction; ++index) {
            if (equal_nocase(parts[index], none)) {
                continue;
            }
            for (std::size_t other : tables.load_order) {
                if (other == a) {
                    continue;
                }
                for (std::size_t k = 0; k < 4; ++k) {
                    if (index != k && equal_nocase(tables.key_names[chords[other][k]], parts[index])) {
                        if (why != nullptr) {
                            *why = "the key " + parts[index] + " of " + kActions[a].ini_key + " is also a key of " + kActions[other].ini_key;
                        }
                        return std::nullopt;
                    }
                }
            }
            const auto code = key_code_of(tables, parts[index]);
            if (!code) {
                if (why != nullptr) {
                    *why = "the key name \"" + parts[index] + "\" of " + kActions[a].ini_key + " is not one the game knows";
                }
                return std::nullopt;
            }
            chords[a][index] = *code;
        }
    }
    return chords;
}

std::string chord_text(const RetailTables& tables, const std::array<std::uint8_t, kKeysPerAction>& slots) {
    std::string text;
    for (std::size_t k = 0; k < kKeysPerAction; ++k) {
        if (slots[k] == 0) {
            break;
        }
        if (!text.empty()) {
            text += ",";
        }
        text += tables.key_names[slots[k]];
    }
    return text.empty() ? tables.key_names[0] : text;
}

// The key codes a key of the keyboard can produce (src/hal/keymap.cpp). The others are never down: a binding to one is inert.
bool producible(std::uint8_t code) {
    static const std::array<bool, 256> table = [] {
        std::array<bool, 256> t{};
        for (std::uint32_t usage = 0; usage < 256; ++usage) {
            if (const std::uint8_t c = ot_hal_key_from_usb(usage); c != 0) {
                t[c] = true;
            }
        }
        return t;
    }();
    return table[code];
}

// What the writer puts in one slot of an action's entry: a key, "-?-" (the game then keeps the slot's default key), or an inert filler key.
constexpr int kKeepDefault = -1;
constexpr int kFiller = -2;
using SlotPlan = std::array<int, kKeysPerAction>;

std::size_t load_position(const RetailTables& tables, std::size_t action) {
    std::size_t position = 0;
    while (position < kActionCount && tables.load_order[position] != action) {
        ++position;
    }
    return position;
}

// The game reads a key as taken when an action read later still has it in its default chord in another slot (the other actions' final chords are disjoint
// from these keys: find_conflicts).
bool slot_allowed(const RetailTables& tables, std::size_t position, std::uint8_t code, std::size_t slot) {
    for (std::size_t later = position + 1; later < kActionCount; ++later) {
        const std::size_t other = tables.load_order[later];
        for (std::size_t k = 0; k < kKeysPerAction; ++k) {
            const std::uint8_t theirs = tables.default_keys[other][k];
            if (k != slot && theirs != 0 && equal_nocase(tables.key_names[theirs], tables.key_names[code])) {
                return false;
            }
        }
    }
    return true;
}

// The slots that make the game end with exactly `keys` for this action: the chord is read up to its first empty slot; a spare slot repeats a key, keeps
// a default that is empty or one of the keys, or (only when neither passes the game's check) holds a filler no key produces. Fewest fillers win.
std::optional<SlotPlan> plan_slots(const RetailTables& tables, std::size_t action, const std::vector<std::uint8_t>& keys) {
    if (keys.empty()) {
        return std::nullopt;
    }
    const std::size_t position = load_position(tables, action);
    std::array<std::vector<int>, kKeysPerAction> options;
    for (std::size_t s = 0; s < kKeysPerAction; ++s) {
        for (std::uint8_t code : keys) {
            if (slot_allowed(tables, position, code, s)) {
                options[s].push_back(code);
            }
        }
        const std::uint8_t kept = tables.default_keys[action][s];
        if (kept == 0 || std::find(keys.begin(), keys.end(), kept) != keys.end()) {
            options[s].push_back(kKeepDefault);
        }
        options[s].push_back(kFiller);
    }
    // the layout the game itself writes ("PAD8,V": the keys in order, nothing after the last) is preferred among the ones without fillers
    SlotPlan natural{};
    for (std::size_t s = 0; s < kKeysPerAction; ++s) {
        natural[s] = s < keys.size() ? keys[s] : tables.default_keys[action][s] == 0 ? kKeepDefault : keys.back();
    }
    std::optional<SlotPlan> best;
    int best_score = 1 << 20;
    for (int a : options[0]) {
        for (int b : options[1]) {
            for (int c : options[2]) {
                const SlotPlan plan = {a, b, c};
                std::vector<std::uint8_t> got;
                int fillers = 0;
                for (std::size_t s = 0; s < kKeysPerAction; ++s) {
                    const int value = plan[s] == kKeepDefault ? tables.default_keys[action][s] : plan[s];
                    if (value == 0) {
                        break;   // the game reads a chord up to its first empty slot
                    }
                    if (value == kFiller) {
                        ++fillers;
                    } else if (std::find(got.begin(), got.end(), static_cast<std::uint8_t>(value)) == got.end()) {
                        got.push_back(static_cast<std::uint8_t>(value));
                    }
                }
                int score = 100 * fillers;
                for (std::size_t s = 0; s < kKeysPerAction; ++s) {
                    score += plan[s] != natural[s] ? 1 : 0;
                }
                if (got.size() == keys.size() && std::is_permutation(got.begin(), got.end(), keys.begin()) && score < best_score) {
                    best = plan;
                    best_score = score;
                }
            }
        }
    }
    return best;
}

// Filler keys: names the game knows, that no key produces, that no default uses
std::vector<std::uint8_t> filler_pool(const RetailTables& tables) {
    std::array<bool, 256> in_defaults{};
    for (const auto& chord : tables.default_keys) {
        for (std::uint8_t code : chord) {
            in_defaults[code] = true;
        }
    }
    std::vector<std::uint8_t> pool;
    for (std::uint32_t code = 1; code < 256; ++code) {
        if (key_bindable(tables, static_cast<std::uint8_t>(code)) && !producible(static_cast<std::uint8_t>(code)) && !in_defaults[code]) {
            pool.push_back(static_cast<std::uint8_t>(code));
        }
    }
    return pool;
}

}  // namespace

std::optional<RetailTables> read_retail_tables(const std::vector<std::uint8_t>& file, std::string& error) {
    Image image;
    if (!image_of(file, image, error)) {
        return std::nullopt;
    }
    RetailTables tables;
    for (std::uint32_t code = 0; code < 256; ++code) {
        const auto pointer = image.word(kNameTable + 4 * code);
        const auto name = pointer ? image.cstring(*pointer) : std::nullopt;
        if (!name) {
            error = "the key-name table is not where the Activision Value re-release keeps it";
            return std::nullopt;
        }
        tables.key_names[code] = *name;
    }
    std::array<bool, kActionCount> seen{};
    std::size_t order = 0;
    for (std::uint32_t pair = 0; pair < 32; ++pair) {
        const auto name_pointer = image.word(kPairs + 8 * pair);
        if (!name_pointer || *name_pointer == 0) {
            break;
        }
        const auto name = image.cstring(*name_pointer);
        const auto chord = image.word(kPairs + 8 * pair + 4);
        const std::size_t a = name ? action_index(*name) : kActionCount;
        if (!chord || a == kActionCount || seen[a] || order == kActionCount) {
            error = "the keyboard bindings table is not the one of the Activision Value re-release";
            return std::nullopt;
        }
        seen[a] = true;
        tables.load_order[order++] = a;
        for (std::size_t k = 0; k < kKeysPerAction; ++k) {
            std::uint8_t code = 0;
            image.read(*chord + static_cast<std::uint32_t>(k), &code, 1);
            tables.default_keys[a][k] = code;
        }
    }
    if (order != kActionCount) {
        error = "the keyboard bindings table has " + std::to_string(order) + " actions, not 14";
        return std::nullopt;
    }
    for (std::size_t k = 0; k < kPadActionCount; ++k) {
        const auto value = image.word(kJoystickDefaults[k].variable);   // KICK's variable is in .bss: zero
        tables.default_pad[action_index(kJoystickDefaults[k].name)] = value ? static_cast<int>(static_cast<std::int32_t>(*value)) : 0;
    }
    if (tables.default_pad[8] == -1) {
        tables.default_pad[8] = kOpenTonyPause;
    }
    if (tables.default_pad[9] == -1) {
        tables.default_pad[9] = kOpenTonyCamera;
    }
    return tables;
}

std::optional<RetailTables> load_retail_tables(const std::filesystem::path& game_dir, const std::filesystem::path& static_image, std::string& error) {
    std::error_code ec;
    std::vector<std::filesystem::path> candidates;
    for (const auto& entry : std::filesystem::directory_iterator(game_dir, ec)) {
        if (equal_nocase(entry.path().filename().string(), "THawk2.exe")) {
            candidates.push_back(entry.path());
        }
    }
    candidates.push_back(static_image);
    error = "neither THawk2.exe in " + game_dir.string() + " nor " + static_image.string() + " can be read";
    for (const auto& path : candidates) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            continue;
        }
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        if (auto tables = read_retail_tables(bytes, error)) {
            return tables;
        }
    }
    return std::nullopt;
}

std::optional<std::uint8_t> key_code_of(const RetailTables& tables, std::string_view name) {
    for (std::uint32_t code = 0; code < 256; ++code) {
        if (equal_nocase(tables.key_names[code], name)) {
            return static_cast<std::uint8_t>(code);
        }
    }
    return std::nullopt;
}

bool key_bindable(const RetailTables& tables, std::uint8_t code) {
    if (code == 0) {
        return false;
    }
    const std::string& name = tables.key_names[code];
    if (name.empty() || name != trim(name) || name.find(',') != std::string::npos || name.find(';') != std::string::npos) {
        return false;
    }
    const auto first = key_code_of(tables, name);
    return first && *first == code;
}

Bindings default_bindings(const RetailTables& tables) {
    Bindings b;
    b.keys = tables.default_keys;
    b.pad = tables.default_pad;
    return b;
}

Bindings read_bindings(const IniFile& ini, const RetailTables& tables, std::vector<std::string>* problems) {
    Bindings b = default_bindings(tables);
    std::array<std::optional<std::string>, kActionCount> values;
    for (std::size_t a = 0; a < kActionCount; ++a) {
        values[a] = ini.get("KEYBOARD", kActions[a].ini_key);
    }
    std::string why;
    if (const auto chords = simulate_keyboard_load(tables, values, &why)) {
        for (std::size_t a = 0; a < kActionCount; ++a) {
            // the chord as the game uses it: up to its first empty slot, without the inert fillers the writer may have put in
            std::array<std::uint8_t, kKeysPerAction> live{};
            for (std::size_t k = 0; k < kKeysPerAction && (*chords)[a][k] != 0; ++k) {
                live[k] = producible((*chords)[a][k]) ? (*chords)[a][k] : 0;
            }
            const auto keys = distinct_keys(live);
            b.keys[a] = {};
            for (std::size_t k = 0; k < keys.size(); ++k) {
                b.keys[a][k] = keys[k];
            }
        }
    } else if (problems != nullptr) {
        problems->push_back("THPSource2.ini [KEYBOARD]: " + why + "; the game resets every binding to its defaults when it reads this.");
    }
    std::array<int, kPadActionCount> pad = b.pad;
    for (std::size_t a = 0; a < kPadActionCount; ++a) {
        pad[a] = ini.get_int("JOYPAD", kActions[a].ini_key, b.pad[a]);
    }
    bool reset = false;
    for (std::size_t i = 0; i < kPadActionCount; ++i) {
        reset = reset || pad[i] < -1 || pad[i] > 32;
        for (std::size_t j = 0; j < kPadActionCount; ++j) {
            reset = reset || (i != j && pad[i] != -1 && pad[i] == pad[j]);
        }
    }
    if (reset) {
        if (problems != nullptr) {
            problems->push_back("THPSource2.ini [JOYPAD]:a button is used twice or out of range; the game resets every binding to its defaults when it reads this.");
        }
        return default_bindings(tables);
    }
    b.pad = pad;
    return b;
}

std::vector<Conflict> find_conflicts(const Bindings& b, const RetailTables& tables) {
    std::vector<Conflict> conflicts;
    for (std::size_t a = 0; a < kActionCount; ++a) {
        const auto keys = distinct_keys(b.keys[a]);
        if (keys.empty()) {
            conflicts.push_back({std::string(kActions[a].label) + " has no key (the game keeps its default key in an empty binding)", a, a});
        }
        for (std::uint8_t code : keys) {
            if (!key_bindable(tables, code)) {
                conflicts.push_back({std::string("the game cannot read back the key ") + tables.key_names[code] + " of " + kActions[a].label, a, a});
            }
            for (std::size_t other = a + 1; other < kActionCount; ++other) {
                const auto theirs = distinct_keys(b.keys[other]);
                if (std::find(theirs.begin(), theirs.end(), code) != theirs.end()) {
                    conflicts.push_back({tables.key_names[code] + " is bound to both " + kActions[a].label + " and " + kActions[other].label, a, other});
                }
            }
        }
    }
    if (conflicts.empty()) {
        std::size_t fillers_needed = 0;
        for (std::size_t a = 0; a < kActionCount; ++a) {
            const auto plan = plan_slots(tables, a, distinct_keys(b.keys[a]));
            if (!plan) {
                conflicts.push_back({std::string("the game's reader cannot take these keys for ") + kActions[a].label +
                                         " (one of them is a default key of an action it reads later); move it to that action or pick another",
                                     a, a});
                continue;
            }
            fillers_needed += static_cast<std::size_t>(std::count(plan->begin(), plan->end(), kFiller));
        }
        if (fillers_needed > filler_pool(tables).size()) {
            conflicts.push_back({"too many bindings need a filler key; bind fewer keys", 0, 0});
        }
    }
    for (std::size_t i = 0; i < kPadActionCount; ++i) {
        if (b.pad[i] < -1 || b.pad[i] >= kPadButtonCount) {
            conflicts.push_back({std::string(kActions[i].label) + " has a pad button the standalone does not report", i, i});
        }
        for (std::size_t j = i + 1; j < kPadActionCount; ++j) {
            if (b.pad[i] != -1 && b.pad[i] == b.pad[j]) {
                conflicts.push_back({std::string(pad_button_name(b.pad[i])) + " is bound to both " + kActions[i].label + " and " + kActions[j].label, i, j});
            }
        }
    }
    return conflicts;
}

void write_bindings(IniFile& ini, const Bindings& b, const RetailTables& tables) {
    const std::vector<std::uint8_t> pool = filler_pool(tables);
    std::size_t next_filler = 0;
    for (std::size_t a = 0; a < kActionCount; ++a) {
        const auto keys = distinct_keys(b.keys[a]);
        const auto plan = plan_slots(tables, a, keys);
        if (!plan) {
            ini.set("KEYBOARD", kActions[a].ini_key, chord_text(tables, padded(keys)));   // find_conflicts reports it; the caller does not save it
            continue;
        }
        // trailing slots that keep an empty default are left out, as the game writes its own entries
        std::size_t used = kKeysPerAction;
        while (used > 1 && (*plan)[used - 1] == kKeepDefault && tables.default_keys[a][used - 1] == 0) {
            --used;
        }
        std::string text;
        for (std::size_t s = 0; s < used; ++s) {
            int value = (*plan)[s];
            if (value == kFiller) {
                value = next_filler < pool.size() ? pool[next_filler++] : keys[0];
            }
            if (s != 0) {
                text += ",";
            }
            text += value == kKeepDefault ? tables.key_names[0] : tables.key_names[static_cast<std::uint8_t>(value)];
        }
        ini.set("KEYBOARD", kActions[a].ini_key, text);
    }
    for (std::size_t a = 0; a < kPadActionCount; ++a) {
        ini.set_int("JOYPAD", kActions[a].ini_key, b.pad[a]);
    }
}

void assign_key(Bindings& b, std::size_t action, std::size_t slot, std::uint8_t code) {
    if (action >= kActionCount || slot >= kKeysPerAction) {
        return;
    }
    if (code != 0) {
        for (std::size_t other = 0; other < kActionCount; ++other) {
            for (std::size_t k = 0; k < kKeysPerAction; ++k) {
                if (b.keys[other][k] == code && !(other == action && k == slot)) {
                    b.keys[other][k] = 0;
                }
            }
        }
    }
    b.keys[action][slot] = code;
    // keep the filled slots first in every action (the game reads a chord up to its first empty slot)
    for (auto& slots : b.keys) {
        const auto keys = distinct_keys(slots);
        slots = {};
        for (std::size_t k = 0; k < keys.size(); ++k) {
            slots[k] = keys[k];
        }
    }
}

void assign_pad(Bindings& b, std::size_t action, int button) {
    if (action >= kPadActionCount) {
        return;
    }
    if (button != -1) {
        for (std::size_t other = 0; other < kPadActionCount; ++other) {
            if (other != action && b.pad[other] == button) {
                b.pad[other] = -1;
            }
        }
    }
    b.pad[action] = button;
}

std::uint8_t key_code_from_scancode(int scancode) {
    return scancode <= 0 ? 0 : ot_hal_key_from_usb(static_cast<std::uint32_t>(scancode));
}

const char* pad_button_name(int button) {
    static const char* const kNames[kPadButtonCount] = {"X / Square", "A / Cross", "B / Circle", "LB / L1", "Y / Triangle", "RB / R1",
                                                        "LT / L2",    "RT / R2",   "Back / Select", "Start", "L3", "R3"};
    if (button == -1) {
        return "None";
    }
    return button >= 0 && button < kPadButtonCount ? kNames[button] : "(unknown)";
}

int pad_button_from_sdl(int sdl_button) {
    switch (sdl_button) {
    case 0: return 1;     // south (A / cross)
    case 1: return 2;     // east (B / circle)
    case 2: return 0;     // west (X / square)
    case 3: return 4;     // north (Y / triangle)
    case 4: return 8;     // back
    case 6: return 9;     // start
    case 7: return 10;    // left stick
    case 8: return 11;    // right stick
    case 9: return 3;     // left shoulder
    case 10: return 5;    // right shoulder
    default: return -1;   // guide, d-pad, misc, paddles, touchpad
    }
}

}  // namespace thpsource2
