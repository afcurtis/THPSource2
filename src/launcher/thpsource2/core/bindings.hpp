// THPSource2 launcher: the game's own key and pad bindings, [KEYBOARD] and [JOYPAD] of THPSource2.ini (the one settings file: the game reads them there), with the rules of the game's reader (PCINPUT load_bindings,
// 0x004e4d10, src/runtime/pad_input.cpp): up to three key names per action, separated by commas and matched without regard to case against the retail table
// of 256 key names (0x0052eff0); a key used by two actions, an unknown name, a pad button outside -1..32 or one button on two actions makes the game put
// every binding back to its defaults. The launcher writes only what that reader accepts.
#pragma once

#include "ini.hpp"
#include "settings.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace thpsource2 {

constexpr std::size_t kActionCount = 14;
constexpr std::size_t kPadActionCount = 10;   // the first ten actions have a pad button too
constexpr std::size_t kKeysPerAction = 3;
constexpr int kPadButtonCount = 12;           // the buttons the standalone reports (standalone_platform.cpp write_joystick)

struct ActionInfo {
    const char* ini_key;       // the settings file's entry ([KEYBOARD] and, for the first ten, [JOYPAD])
    const char* label;         // the name the launcher shows
};
extern const std::array<ActionInfo, kActionCount> kActions;

// The retail tables the bindings need, read from the player's THawk2.exe or from the static-data image opentony.exe wrote (Data\Unpacked\thawk2_static.img).
// Nothing of the game is kept in this program.
struct RetailTables {
    std::array<std::string, 256> key_names;                                         // by DirectInput key code; 0 is "-?-" (no key)
    std::array<std::array<std::uint8_t, kKeysPerAction>, kActionCount> default_keys{};
    std::array<int, kPadActionCount> default_pad{};
    std::array<std::size_t, kActionCount> load_order{};   // kActions indices in the order the game reads [KEYBOARD]
};
std::optional<RetailTables> read_retail_tables(const std::vector<std::uint8_t>& file, std::string& error);   // a PE file or an OTSD image
// THawk2.exe of `game_dir`, else `static_image` (Data\Unpacked\thawk2_static.img)
std::optional<RetailTables> load_retail_tables(const std::filesystem::path& game_dir, const std::filesystem::path& static_image, std::string& error);

// A key the reader can find again by its name: the name has no surrounding space and no comma, and no lower code has the same name. Code 3 ("2 ", with a
// trailing space that GetPrivateProfileString trims away) is not one: retail itself cannot read that binding back.
bool key_bindable(const RetailTables& tables, std::uint8_t code);
std::optional<std::uint8_t> key_code_of(const RetailTables& tables, std::string_view name);

struct Bindings {
    std::array<std::array<std::uint8_t, kKeysPerAction>, kActionCount> keys{};   // 0 = empty slot
    std::array<int, kPadActionCount> pad{};                                       // -1 = unbound
};

Bindings default_bindings(const RetailTables& tables);
// the settings file as the game reads it; an entry that is missing or that the game would refuse leaves the default (and is reported in `problems`)
Bindings read_bindings(const IniFile& ini, const RetailTables& tables, std::vector<std::string>* problems = nullptr);
void write_bindings(IniFile& ini, const Bindings& bindings, const RetailTables& tables);

struct Conflict {
    std::string message;
    std::size_t action_a;
    std::size_t action_b;
};
// Everything that would make the game reset the bindings; empty when the settings file can be written as it is
std::vector<Conflict> find_conflicts(const Bindings& bindings, const RetailTables& tables);

// Binding helpers for the editor: put a key in a slot (the key leaves any other action that had it), clear a slot, set a pad button (it leaves the other action)
void assign_key(Bindings& bindings, std::size_t action, std::size_t slot, std::uint8_t code);
void assign_pad(Bindings& bindings, std::size_t action, int button);

// SDL scancodes (USB HID usages) to the game's DirectInput key codes (src/hal/keymap.cpp); 0 when the key has none
std::uint8_t key_code_from_scancode(int scancode);

// The pad buttons as the standalone numbers them and their names
const char* pad_button_name(int button);
// SDL_GamepadButton (SDL3) to the standalone's numbering; -1 for a button the game cannot be bound to (d-pad, guide, paddles, touchpad).
// The triggers are axes in SDL3 and are caught as axis motion by the app, not here.
int pad_button_from_sdl(int sdl_button);

}  // namespace thpsource2
