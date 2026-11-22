// The player's own soundtrack (docs/LAUNCHER.md, "Custom soundtrack"): the Bink files under <install>\Mods\Music. Every folder that holds one is a pack the
// launcher's Sound tab can switch off, the folder itself ("." for loose files in Mods\Music) or any folder below it. A pack may carry a RETHAWED-style pack.ini:
// [PackInfo] Name=..., then one section per file stem with band= and track_title= (genre, album, album_art and game_logo are read past). A file with no section
// is named by its stem.
//
// Header-only and free of Windows calls: the launcher lists the packs from it, the game (src/hooks/kits/standalone_music.cpp) builds its playlist from the same
// scan, and the native tests (launcher_core_test.cpp) run it on Linux.
#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
#include <vector>

namespace opentony::music {

struct Track {
    std::filesystem::path file;
    std::string band;    // empty when neither pack.ini nor the name gives one
    std::string title;   // pack.ini's track_title, else the file's stem
};

struct Folder {
    std::string name;        // relative to the music root, '/' between folders; "." is the root itself
    std::string pack_name;   // pack.ini's [PackInfo] Name, or empty
    std::vector<Track> tracks;
};

inline std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

inline std::string trim(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

inline std::string utf8(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

inline std::filesystem::path path_of(const std::string& utf8_text) { return std::filesystem::path(std::u8string(utf8_text.begin(), utf8_text.end())); }

// pack.ini as section -> key -> value, the section names and keys lower case (a pack's stems and its sections need not agree on case)
inline std::map<std::string, std::map<std::string, std::string>> read_pack_ini(const std::filesystem::path& file) {
    std::map<std::string, std::map<std::string, std::string>> sections;
    std::ifstream in(file, std::ios::binary);
    std::string line;
    std::string section;
    bool first = true;
    while (std::getline(in, line)) {
        if (first && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xef && static_cast<unsigned char>(line[1]) == 0xbb &&
            static_cast<unsigned char>(line[2]) == 0xbf) {
            line.erase(0, 3);   // a UTF-8 byte order mark
        }
        first = false;
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = lower(trim(line.substr(1, line.size() - 2)));
            sections[section];
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals != std::string::npos && !section.empty()) {
            sections[section][lower(trim(line.substr(0, equals)))] = trim(line.substr(equals + 1));
        }
    }
    return sections;
}

inline bool is_track_file(const std::filesystem::path& file) { return lower(utf8(file.extension())) == ".bik"; }

// Every folder under `root` (and `root` itself) with at least one track, by name; the tracks of a folder by file name. Unreadable folders are passed over.
inline std::vector<Folder> scan(const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    std::map<std::string, std::vector<fs::path>> found;   // folder name -> its track files
    std::map<std::string, fs::path> folder_path;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        return {};
    }
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code file_ec;
        if (!it->is_regular_file(file_ec) || !is_track_file(it->path())) {
            continue;
        }
        const fs::path parent = it->path().parent_path();
        std::string name = utf8(parent.lexically_relative(root));
        if (name.empty()) {
            name = ".";
        }
        found[name].push_back(it->path());
        folder_path[name] = parent;
    }
    std::vector<Folder> folders;
    for (auto& [name, files] : found) {
        Folder folder;
        folder.name = name;
        std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) { return lower(utf8(a.filename())) < lower(utf8(b.filename())); });
        std::map<std::string, std::map<std::string, std::string>> ini;
        for (const fs::directory_entry& entry : fs::directory_iterator(folder_path[name], ec)) {
            if (lower(utf8(entry.path().filename())) == "pack.ini") {
                ini = read_pack_ini(entry.path());
                break;
            }
        }
        if (const auto info = ini.find("packinfo"); info != ini.end()) {
            if (const auto pack = info->second.find("name"); pack != info->second.end()) {
                folder.pack_name = pack->second;
            }
        }
        for (const fs::path& file : files) {
            Track track;
            track.file = file;
            track.title = utf8(file.stem());
            if (const auto section = ini.find(lower(utf8(file.stem()))); section != ini.end()) {
                if (const auto band = section->second.find("band"); band != section->second.end()) {
                    track.band = band->second;
                }
                if (const auto title = section->second.find("track_title"); title != section->second.end() && !title->second.empty()) {
                    track.title = title->second;
                }
            }
            folder.tracks.push_back(std::move(track));
        }
        folders.push_back(std::move(folder));
    }
    return folders;
}

// The launcher's list of switched-off packs, as the settings file and OT_MUSIC_FOLDERS_OFF carry it: folder names joined by '|'
inline std::vector<std::string> split_list(const std::string& text) {
    std::vector<std::string> list;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t bar = text.find('|', start);
        const std::string item = trim(text.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
        if (!item.empty()) {
            list.push_back(item);
        }
        if (bar == std::string::npos) {
            break;
        }
        start = bar + 1;
    }
    return list;
}

inline std::string join_list(const std::vector<std::string>& list) {
    std::string text;
    for (const std::string& item : list) {
        text += (text.empty() ? "" : "|") + item;
    }
    return text;
}

inline bool is_off(const std::vector<std::string>& off, const std::string& folder) { return std::find(off.begin(), off.end(), folder) != off.end(); }

// The playlist's place, moved by what the game asks for. The game walks its own level songs (redbook_next(step): the next one with a length at a song's end, a level's
// start and the pause menu's TRACK line, the one before on that line's left) and only the track it lands on reaches the music seam. `step(from, direction)` is that
// walk. The track before the last one asked for is a step back, the same one is the same song (restarted after a cut scene), anything else a step on.
class Playlist {
public:
    template <typename Random>
    Playlist(std::size_t size, bool shuffle, Random& random) : shuffle_(shuffle), size_(size) {
        new_order(random, static_cast<std::size_t>(-1));
    }

    // the entry to play for the game's track `index`
    template <typename Step, typename Random>
    std::size_t request(int index, Step step, Random& random) {
        int direction = 1;
        if (last_ == index) {
            direction = 0;
        } else if (last_ >= 0 && index == step(last_, -1) && index != step(last_, 1)) {
            direction = -1;
        }
        last_ = index;
        const int count = static_cast<int>(order_.size());
        if (cursor_ < 0) {
            cursor_ = 0;
        } else if (direction > 0 && cursor_ + 1 >= count) {
            new_order(random, order_[static_cast<std::size_t>(cursor_)]);   // round again: a new shuffle, or the same order from the top
            cursor_ = 0;
        } else if (direction > 0) {
            ++cursor_;
        } else if (direction < 0) {
            cursor_ = (cursor_ + count - 1) % count;
        }
        moved_ = direction != 0;
        return order_[static_cast<std::size_t>(cursor_)];
    }

    // one song on, as the game's next track would go (a song that cannot be played is passed over this way); the game's own place (`last_`) is kept
    template <typename Random>
    std::size_t advance(Random& random) {
        const int count = static_cast<int>(order_.size());
        if (cursor_ < 0) {
            cursor_ = 0;
        } else if (cursor_ + 1 >= count) {
            new_order(random, order_[static_cast<std::size_t>(cursor_)]);
            cursor_ = 0;
        } else {
            ++cursor_;
        }
        moved_ = true;
        return order_[static_cast<std::size_t>(cursor_)];
    }

    [[nodiscard]] bool moved() const { return moved_; }   // the last request started another song (not the same one again)
    [[nodiscard]] int place() const { return cursor_; }   // 0 based, in this round's order
    [[nodiscard]] std::size_t size() const { return order_.size(); }

private:
    // `previous`: the song that ended the last round (none for the first)
    template <typename Random>
    void new_order(Random& random, std::size_t previous) {
        const std::size_t count = size_;
        order_.resize(count);
        for (std::size_t k = 0; k < count; ++k) {
            order_[k] = k;
        }
        if (shuffle_) {
            std::shuffle(order_.begin(), order_.end(), random);
            if (count > 1 && order_.front() == previous) {
                std::swap(order_.front(), order_.back());   // a new round does not start with the song that ended the last one
            }
        }
    }

    bool shuffle_;
    std::size_t size_ = 0;
    std::vector<std::size_t> order_;
    int cursor_ = -1;
    int last_ = -1;
    bool moved_ = false;
};

}  // namespace opentony::music
