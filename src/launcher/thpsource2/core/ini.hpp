// THPSource2 launcher: a small INI editor with the reading rules of GetPrivateProfileString (sections and keys match without regard to case,
// values are trimmed). Editing keeps every other line of the file as it was, so the game's own THawk2.ini and a player's comments survive a save.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace thpsource2 {

class IniFile {
public:
    static IniFile parse(std::string_view text);
    static IniFile load(const std::filesystem::path& path);   // an empty file when it cannot be read

    std::optional<std::string> get(std::string_view section, std::string_view key) const;
    std::string get_string(std::string_view section, std::string_view key, std::string_view fallback = {}) const;
    // GetPrivateProfileInt: the leading integer of the value ("0\t; a comment" is 0), else the fallback
    int get_int(std::string_view section, std::string_view key, int fallback) const;
    bool get_bool(std::string_view section, std::string_view key, bool fallback) const { return get_int(section, key, fallback ? 1 : 0) != 0; }

    void set(std::string_view section, std::string_view key, std::string_view value);
    void set_int(std::string_view section, std::string_view key, int value) { set(section, key, std::to_string(value)); }
    void set_bool(std::string_view section, std::string_view key, bool value) { set(section, key, value ? "1" : "0"); }

    // every section's name, in the file's order, each once (the first spelling of a name that is repeated)
    std::vector<std::string> sections() const;

    std::string text() const;
    // written next to the target and renamed over it, so a failed save leaves the old file
    bool save(const std::filesystem::path& path) const;

private:
    struct Line {
        std::string raw;       // the line as read (or as written by set)
        std::string section;   // the section the line belongs to
        std::string key;       // the key when the line is "key=value"
        bool is_section = false;
    };
    std::vector<Line> lines_;
    std::string newline_ = "\r\n";
};

bool equal_nocase(std::string_view a, std::string_view b);
std::string trim(std::string_view text);

}  // namespace thpsource2
