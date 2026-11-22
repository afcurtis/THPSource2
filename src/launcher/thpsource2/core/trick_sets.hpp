// THPSource2 launcher: the Quick Start's trick sets (docs/LAUNCHER.md, "Trick sets"). The game's Edit Tricks screen is not reachable from a quick start (it is a
// career and skater-select screen), so the launcher keeps named sets of its own: the trick on every button combination and the five specials, as the game's
// binding block holds them (re/evidence/functions/trick-database-runtime.md). A quick start with a set chosen hands the block to the game (OT_QUICK_TRICKS), which
// writes it into every skater's career record before the level builds its trick table; PLAY never passes it.
//
// The tricks, the combinations and each skater's own set come from the player's TRICKS.BIN through the runtime's trick database (src/runtime/trick_database.hpp),
// so the launcher and the game agree on every index.
#pragma once

#include "ini.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace thpsource2 {

// The pages, in the editor's order. The game's own menu has Flip tricks, Grab tricks, Lip tricks and Specials; Grinds (its page 8: Up, Up and Down, Down with Grind)
// is in the game's tables and its menu's labels but not in its category list, so every skater keeps the two it starts with there unless a set changes them.
enum class TrickPage { Flip = 0, Grab, Lip, Grind, Special };
constexpr std::size_t kTrickPageCount = 5;
extern const std::array<const char*, kTrickPageCount> kTrickPageNames;   // "Flip tricks", "Grab tricks", "Lip tricks", "Grinds", "Specials"

constexpr std::size_t kTrickCombos = 30;       // the ordinary combinations (the executable's table at 0x00540cb8)
constexpr std::size_t kSpecialCombos = 36;     // the special combinations (0x00540e30)
constexpr std::size_t kSpecialSlots = 5;       // a skater holds at most five specials
constexpr std::size_t kTrickBlockSize = 0x35;  // the binding block: 43 ordinary slots (30 used), 5 special records, 5 special combinations

// The kinds a trick is listed under (a special's kind is the kind of trick it is)
enum class TrickKind { Flip = 0, Grab, Lip, Grind, Manual };
constexpr std::size_t kTrickKindCount = 5;
extern const std::array<const char*, kTrickKindCount> kTrickKindNames;   // "Flip tricks", "Grab tricks", "Lip tricks", "Grinds", "Manuals"

struct TrickInfo {
    std::string name;      // the database's (lower case, as the game keeps it): the name a set stores
    std::string label;     // for the screen: "Kickflip to Indy", "The 900"
    std::uint16_t flags = 0;
    TrickKind kind = TrickKind::Flip;
    bool special = false;
};

struct TrickCombo {
    std::string key;       // the name a set stores: "Up+Flip", "Up,Up+Grind", "Right,Down+Grab"
    std::string label;     // "Up + Flip", "Up, Up + Grind", "Right, Down + Grab"
    TrickPage page = TrickPage::Flip;   // the ordinary combination's page (Special for a special combination)
    int button = 0;        // the last button: 9 Grab, 10 Flip, 12 Grind
};

// The combinations, from the executable's two tables (src/runtime/trick_combo_tables.cpp), in the tables' order: the index is the game's
const std::array<TrickCombo, kTrickCombos>& trick_combos();
const std::array<TrickCombo, kSpecialCombos>& special_trick_combos();
int find_trick_combo(std::string_view key);     // an ordinary combination, -1 if none (the key without regard to case)
int find_special_combo(std::string_view key);   // -1 if none

// One set: a trick name (or "") for each ordinary combination, in the table's order, and up to five specials, each a special combination and a trick
struct TrickSet {
    std::string name;
    std::array<std::string, kTrickCombos> combos;
    struct Special {
        std::string combo;   // TrickCombo::key of a special combination, "" an empty slot
        std::string trick;
        bool operator==(const Special&) const = default;
    };
    std::array<Special, kSpecialSlots> specials;
    bool operator==(const TrickSet&) const = default;
};

class TrickCatalog {
public:
    // TRICKS.BIN as the game reads it (Data\Unpacked\data\data\TRICKS.BIN once the game files are unpacked)
    static std::optional<TrickCatalog> load(const std::filesystem::path& tricks_bin, std::string& error);
    static std::optional<TrickCatalog> from_image(std::vector<std::uint8_t> image, std::string& error);

    const std::vector<TrickInfo>& tricks() const { return tricks_; }   // in the database's order: the index is the record's
    int find_trick(std::string_view name) const;          // -1 if none (the name without regard to case)
    // The tricks the game's menu lists for an ordinary combination: on the flip and grab pages every flip and grab trick, on the lip page the lip tricks, on the grind
    // page the grinds; never a special
    bool fits(int combo, int trick) const;
    // A special combination ending in Grind takes the grind and lip specials, one ending in Flip or Grab the others (TrickMenu_BuildPageList, 0x004c21a0)
    bool fits_special(int special_combo, int trick) const;

    // The skater's own set, as a new career starts it (TrickConfig_InitializeCharacter: the skater's key stream in TRICKS.BIN); named after the skater
    TrickSet skater_set(int skater) const;
    // The binding block the game takes (OT_QUICK_TRICKS, as 106 hex digits): what does not fit, an unknown name, a special used twice and a special combination
    // used twice are left out, each with a line in `problems`
    std::array<std::uint8_t, kTrickBlockSize> block(const TrickSet& set, std::vector<std::string>* problems = nullptr) const;

private:
    std::vector<TrickInfo> tricks_;
    std::vector<TrickSet> skater_sets_;   // 20, from the key streams
};

std::string trick_label(std::string_view name);   // "kickflip to indy" -> "Kickflip to Indy", "fs shove it" -> "FS Shove It"
std::string trick_block_hex(const std::array<std::uint8_t, kTrickBlockSize>& block);
// A name a set can have: not empty, at most 40 characters, no brackets, no ';' or '#' first, no space at either end
bool trick_set_name_valid(std::string_view name);

// Config\TrickSets.ini: one section a set; the ordinary combinations' keys name their tricks, Special1..Special5 are "<combination>: <trick>"
std::vector<TrickSet> read_trick_sets(const IniFile& ini);
IniFile write_trick_sets(const std::vector<TrickSet>& sets);

}  // namespace thpsource2
