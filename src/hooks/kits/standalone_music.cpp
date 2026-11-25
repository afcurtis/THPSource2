// The custom soundtrack (standalone_music.hpp; docs/LAUNCHER.md, "Custom soundtrack").
//
// The player's switches, from the launcher's Sound tab:
//   OT_CUSTOM_MUSIC=1          on
//   OT_MUSIC_DIR=<dir>         the music root, <install>\Mods\Music (src/launcher/music_library.hpp: every folder under it with a .bik is a pack)
//   OT_MUSIC_FOLDERS_OFF=a|b   packs switched off, by their names relative to the root ("." the root's own files)
//   OT_MUSIC_SHUFFLE=1         the playlist in a random order, a new one each time round
//   OT_MUSIC_DEFAULT=1         the game's own level songs are in the playlist too
//   OT_MUSIC_NOW_PLAYING=1     the band and title come up when a song starts, the game's own songs too (off when unset, which is retail and every gate;
//                              the launcher's Display > HUD sets it whether or not the custom soundtrack is on)
//
// Which way the player went is read from the game's own walk (music::Playlist, src/launcher/music_library.hpp), so the pause menu's TRACK line skips through
// however many songs the playlist has.
#include "standalone_music.hpp"

#include "kit_bridge.hpp"
#include "standalone_options.hpp"
#include "guest_arena.hpp"
#include "../../launcher/music_library.hpp"
#include "../../runtime/host_options.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include "win32_posix.h"
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace opentony::kitdll {
namespace {

template <typename T>
volatile T& retail(std::uint32_t address) {
    return *arena::host<volatile T>(address);
}

constexpr std::uint32_t kTrackLengths = 0x0053770c;     // 32 dwords, -1: no such track (audio_game.hpp)
constexpr int kFirstSong = 0x10;                        // the level songs: groups 2 and 3 (redbook_next skips the first two groups)
constexpr int kTrackSlots = 32;
constexpr std::uint32_t kPauseFlag = 0x0056a880;        // non-zero while the game is paused
constexpr std::uint32_t kMessFontLoaded = 0x0056ab0c;   // byte: the message layer's font is loaded (frontend_mess.hpp)
constexpr std::uint32_t kMessDeleteByText = 0x004736c0; // Mess_DeleteByText(text)
constexpr std::uint32_t kMessAddMessage = 0x004736f0;   // Mess_AddMessage(text, program, stacked, x, y)

// The song's name in the message layer's own way: a program of five steps (frontend_mess.hpp has the step's layout), the game's own pop of its gap and
// letter messages (0x00534ec8: grow from nothing to one and a half, settle at one, hold, shrink away), at the bottom left, left-justified, smaller.
constexpr std::uint32_t kStepSize = 0x20;
constexpr std::uint32_t kSteps = 5;
constexpr std::int16_t kNameX = 16;
constexpr std::int16_t kNameY = 196;           // the first line, in the message layer's 512 x 240 space
constexpr std::int16_t kLineGap = 13;
constexpr std::uint16_t kNameScale = 0xa0;     // 256 is the gap messages' size
constexpr std::uint16_t kHoldTicks = 120;      // four seconds of message ticks at 30 frames a second
constexpr std::uint32_t kTextBytes = 64;
constexpr std::size_t kNameLength = 36;        // characters on a line, so the line stays left of the trick names
constexpr std::uint32_t kAnnounceDelayPolls = 45;   // after the seam opened the song: the level's fade-in and the loading screen are over

// An environment variable as UTF-8. The launcher sets OT_MUSIC_DIR through the wide API from its UTF-8 path; the narrow getenv hands back the ANSI code page's bytes
// (an é of C:\Users\José is 0xe9), which music::path_of would take for UTF-8, so the scan found no folder and the soundtrack never played.
std::string environment_utf8(const wchar_t* name) {
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (length <= 1) {
        return {};
    }
    std::wstring value(length, L'\0');
    const DWORD got = GetEnvironmentVariableW(name, value.data(), length);
    value.resize(got < length ? got : 0);
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string text(static_cast<std::size_t>(bytes > 0 ? bytes : 0), '\0');
    if (bytes > 0) {
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), text.data(), bytes, nullptr, nullptr);
    }
    return text;
}

bool flag(const char* name, bool unset) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return unset;
    }
    return std::strcmp(value, "0") != 0;
}

// The game's own level songs (LTIX16..LTIX30 of the PC release, the Activision Value install this runs). Their files carry no names; these are the soundtrack's
// published track list, matched by order: the PC files' lengths agree with the list's order (LTIX18 3:26, LTIX23 3:55, LTIX25 2:58, LTIX30 1:56 against the list's
// 3:26, 3:55, 2:58, 1:56; nine of the fifteen within three seconds, the rest within twenty-one), which no other order does. The band as the message layer's font can draw it:
// the guest of "The High & Mighty (feat. Mos Def & Mad Skillz)" is left out to fit a line.
struct GameSong {
    const char* band;
    const char* title;
};
constexpr int kLastNamedSong = 30;
constexpr GameSong kGameSongs[] = {
    {"Papa Roach", "Blood Brothers"},                          // LTIX16
    {"Anthrax & Public Enemy", "Bring the Noise"},             // 17
    {"Rage Against the Machine", "Guerrilla Radio"},           // 18
    {"Naughty by Nature", "Pin the Tail on the Donkey"},       // 19
    {"Bad Religion", "You"},                                   // 20
    {"Powerman 5000", "When Worlds Collide"},                  // 21
    {"Millencolin", "No Cigar"},                               // 22
    {"The High & Mighty", "B-Boy Document '99"},               // 23
    {"Dub Pistols", "Cyclone"},                                // 24
    {"Lagwagon", "May 16"},                                    // 25
    {"Styles of Beyond", "Subculture"},                        // 26
    {"Consumed", "Heavy Metal Winner"},                        // 27
    {"Fu Manchu", "Evil Eye"},                                 // 28
    {"Alley Life feat. Black Planet", "Out With the Old"},     // 29
    {"Swingin' Utters", "Five Lessons Learned"},               // 30
};
static_assert(sizeof kGameSongs / sizeof kGameSongs[0] == kLastNamedSong - 16 + 1, "one name for each of LTIX16..LTIX30");

const GameSong* game_song(int track) {
    return track >= 16 && track <= kLastNamedSong ? &kGameSongs[track - 16] : nullptr;
}

struct Item {
    int disc = -1;   // a game's level song by index (16..31), or -1 for a file of the player's
    std::string path;
    std::string band;
    std::string title;
    bool bad = false;   // the player's file could not be played (music_open_failed): passed over
};

struct State {
    bool read = false;
    bool on = false;
    bool shuffle = false;
    bool include_default = false;
    bool now_playing = true;
    std::string root;
    std::vector<std::string> off;
    bool built = false;
    std::vector<Item> items;
    std::unique_ptr<music::Playlist> playlist;
    std::mt19937 random{std::random_device{}()};
    bool in_level = false;
    bool pending = false;     // a song's name waits to be shown
    std::uint32_t pending_poll = 0;
    std::string pending_band;
    std::string pending_title;
    std::uint32_t text[2] = {0, 0};
    std::uint32_t program = 0;
    std::size_t last_entry = static_cast<std::size_t>(-1);   // the item music_choice gave last
    int last_game_track = -1;                                // the game's own song announced last this level (the custom soundtrack off or fallen back)
} g;

void read_switches() {
    g.read = true;
    g.now_playing = flag("OT_MUSIC_NOW_PLAYING", false);   // (the song's name does not need the custom soundtrack)
    g.on = flag("OT_CUSTOM_MUSIC", false);
    if (!g.on) {
        return;
    }
    g.root = environment_utf8(L"OT_MUSIC_DIR");
    g.off = music::split_list(environment_utf8(L"OT_MUSIC_FOLDERS_OFF"));   // (names of the scan's folders: UTF-8 too)
    g.shuffle = flag("OT_MUSIC_SHUFFLE", false);
    g.include_default = flag("OT_MUSIC_DEFAULT", false);
    if (g.root.empty()) {
        g.on = false;
        std::printf("music: OT_CUSTOM_MUSIC is set without OT_MUSIC_DIR; the game's own music plays\n");
        std::fflush(stdout);
    }
    runtime::host_options().custom_soundtrack = g.on;   // the pause menu's TRACK line shows "--": its number would name the disc track a song replaced
}

bool disc_track(int index) { return index >= 0 && index < kTrackSlots && retail<std::int32_t>(kTrackLengths + 4u * static_cast<std::uint32_t>(index)) != -1; }

// the game's own walk from `from` by one step: the next (or the one before) of the level songs that are on the disc
int game_step(int from, int direction) {
    int index = from;
    for (int k = 0; k < kTrackSlots; ++k) {
        index = (index + direction + kTrackSlots) % kTrackSlots;
        if (index >= kFirstSong && disc_track(index)) {
            return index;
        }
    }
    return -1;
}

void build_playlist() {
    g.built = true;
    std::size_t packs = 0;
    std::size_t own = 0;
    for (const music::Folder& folder : music::scan(music::path_of(g.root))) {
        if (music::is_off(g.off, folder.name)) {
            continue;
        }
        ++packs;
        for (const music::Track& track : folder.tracks) {
            g.items.push_back(Item{-1, music::utf8(track.file), track.band, track.title});
            ++own;
        }
    }
    if (own == 0) {
        g.items.clear();
        g.on = false;
        runtime::host_options().custom_soundtrack = false;   // the game's own music: TRACK shows its number again
        std::printf("music: no .bik files in the switched-on folders of %s; the game's own music plays\n", g.root.c_str());
        std::fflush(stdout);
        return;
    }
    std::size_t disc = 0;
    if (g.include_default) {
        for (int index = kFirstSong; index < kTrackSlots; ++index) {
            if (disc_track(index)) {
                g.items.push_back(Item{index, {}, {}, {}});
                ++disc;
            }
        }
    }
    g.playlist = std::make_unique<music::Playlist>(g.items.size(), g.shuffle, g.random);
    std::printf("music: custom soundtrack: %zu songs from %zu folders, %zu of the game's; %s\n", own, packs, disc, g.shuffle ? "shuffled" : "in order");
    std::fflush(stdout);
}

// "LTIX17.DAT" (any case, any folder before it) -> 17; -1 for any other name
int game_track_index(const std::string& name) {
    std::string base = name.substr(name.find_last_of("\\/") == std::string::npos ? 0 : name.find_last_of("\\/") + 1);
    base = music::lower(base);
    if (base.size() < 6 || base.compare(0, 4, "ltix") != 0 || !std::isdigit(static_cast<unsigned char>(base[4])) ||
        !std::isdigit(static_cast<unsigned char>(base[5]))) {
        return -1;
    }
    return (base[4] - '0') * 10 + (base[5] - '0');
}

// What the message layer's font can draw (FontTools::glyph_index): letters (lower case draws as upper), digits, a space and a few marks. An accented Latin letter
// becomes its plain one, '&' "AND", anything else is dropped; a line longer than fits ends in "...".
std::string font_text(const std::string& text) {
    static const char* const kLatin1 = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";   // U+00C0..U+00FF
    std::string out;
    for (std::size_t k = 0; k < text.size(); ++k) {
        const unsigned char c = static_cast<unsigned char>(text[k]);
        if (c == 0xc3 && k + 1 < text.size()) {
            const unsigned char next = static_cast<unsigned char>(text[k + 1]);
            if (next >= 0x80 && next <= 0xbf) {
                out += kLatin1[next - 0x80];
                ++k;
                continue;
            }
        }
        if (c >= 0x80) {
            continue;   // other non-ASCII bytes
        }
        if (std::isalnum(c) || std::strchr(" ?!:.,-%+'#/$_", c) != nullptr) {
            out += static_cast<char>(c);
        } else if (c == '&') {
            out += "AND";
        } else if (c == '"' || c == '`') {
            out += '\'';
        } else if (c == '\t') {
            out += ' ';
        }
    }
    std::string collapsed;
    for (char c : out) {
        if (c != ' ' || (!collapsed.empty() && collapsed.back() != ' ')) {
            collapsed += c;
        }
    }
    while (!collapsed.empty() && collapsed.back() == ' ') {
        collapsed.pop_back();
    }
    if (collapsed.size() > kNameLength) {
        collapsed = collapsed.substr(0, kNameLength - 3);
        while (!collapsed.empty() && collapsed.back() == ' ') {
            collapsed.pop_back();
        }
        collapsed += "...";
    }
    return collapsed;
}

void put16(volatile std::uint8_t* at, std::uint32_t offset, std::uint16_t value) {
    at[offset] = static_cast<std::uint8_t>(value & 0xff);
    at[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

void write_step(std::uint32_t address, std::uint16_t duration, std::uint16_t scale_from, std::uint16_t scale_to, std::int8_t next) {
    volatile std::uint8_t* step = arena::host<volatile std::uint8_t>(address);
    for (std::uint32_t k = 0; k < kStepSize; ++k) {
        step[k] = 0;
    }
    put16(step, 0x00, duration);
    step[0x02] = 1;   // (as in every retail program)
    step[0x03] = 1;   // left-justified (Mess_SetTextJustify 1)
    put16(step, 0x04, scale_from);
    put16(step, 0x06, scale_to);
    for (std::uint32_t k = 0x08; k < 0x0e; ++k) {
        step[k] = 0xff;   // white from start to end
    }
    put16(step, 0x12, static_cast<std::uint16_t>(kNameX));
    put16(step, 0x14, static_cast<std::uint16_t>(kNameY));
    put16(step, 0x16, static_cast<std::uint16_t>(kNameX));
    put16(step, 0x18, static_cast<std::uint16_t>(kNameY));
    step[0x1e] = static_cast<std::uint8_t>(next);
}

bool make_buffers() {
    if (g.program != 0) {
        return true;
    }
    const std::uint32_t program = arena::heap_allocate(kStepSize * kSteps);
    const std::uint32_t first = arena::heap_allocate(kTextBytes);
    const std::uint32_t second = arena::heap_allocate(kTextBytes);
    if (program == 0 || first == 0 || second == 0) {
        return false;   // the heap is full: no name (never at the size of these)
    }
    // the ticks are 30 Hz frames at every frame rate (CoreKit::game_gameplay_update runs the layer's update once per 30 Hz frame)
    const std::uint16_t hold = kHoldTicks;
    const std::uint16_t big = static_cast<std::uint16_t>(kNameScale * 3 / 2);
    write_step(program + 0 * kStepSize, 5, 0, big, 1);
    write_step(program + 1 * kStepSize, 1, big, big, 1);
    write_step(program + 2 * kStepSize, 4, big, kNameScale, 1);
    write_step(program + 3 * kStepSize, hold, kNameScale, kNameScale, 1);
    write_step(program + 4 * kStepSize, 15, kNameScale, 0, 0);
    g.program = program;
    g.text[0] = first;
    g.text[1] = second;
    // both lines are laid out with the in-level HUD (Mess::display, host_options.hpp hud_messages): on a wide picture with the HUD anchored they keep to the picture's
    // left edge, as the score does, and follow the HUD's size and margin; with no HUD layout they stay in the 4:3 middle
    runtime::host_options().hud_messages = {{first, kNameX, kNameY}, {second, kNameX, kNameY}};
    return true;
}

void write_text(std::uint32_t address, const std::string& text) {
    volatile char* at = arena::host<volatile char>(address);
    const std::size_t length = std::min<std::size_t>(text.size(), kTextBytes - 1);
    for (std::size_t k = 0; k < length; ++k) {
        at[k] = text[k];
    }
    at[length] = 0;
}

void show_name() {
    if (!make_buffers()) {
        g.pending = false;
        return;
    }
    // the last name, if it is still up, goes first (the layer finds a message by its text's address)
    call_native_or_seam(kMessDeleteByText, {g.text[0]});
    call_native_or_seam(kMessDeleteByText, {g.text[1]});
    const std::string band = font_text(g.pending_band);
    const std::string title = font_text(g.pending_title);
    g.pending = false;
    if (title.empty() && band.empty()) {
        return;
    }
    std::uint32_t line = 0;
    if (!band.empty()) {
        write_text(g.text[0], band);
        call_native_or_seam(kMessAddMessage, {g.text[0], g.program, 0, 0, 0});
        line = static_cast<std::uint32_t>(kLineGap);
    }
    if (!title.empty()) {
        write_text(g.text[1], title);
        call_native_or_seam(kMessAddMessage, {g.text[1], g.program, 0, 0, band.empty() ? static_cast<std::uint32_t>(kLineGap) : line});
    }
    std::printf("music: now playing '%s' / '%s' (poll %u)\n", band.c_str(), title.c_str(), platform_input_polls());
    std::fflush(stdout);
}

void announce(const std::string& band, const std::string& title) {
    g.pending = true;
    g.pending_poll = platform_input_polls();
    g.pending_band = band;
    g.pending_title = title;
}

// The game plays its own song `track` (the custom soundtrack is off, or has nothing to play): its name comes up when it is another song than the last of this level
// (the same one again is a song restarted after a cut scene)
MusicChoice game_plays(int track) {
    if (g.now_playing && track != g.last_game_track) {
        if (const GameSong* song = game_song(track)) {
            announce(song->band, song->title);
        }
    }
    g.last_game_track = track;
    return {};
}

}  // namespace

void music_level_running(bool running) {
    g.in_level = running;
    g.last_game_track = -1;   // each level announces its first song
    if (!running) {
        g.pending = false;   // a name not shown by the level's end is not shown in the menus
        return;
    }
    // OT_MUSIC_NAME_TEST=band|title (a test switch, docs/HOOKS.md): that name comes up at each level's start as a song's would, where the headless runs give no song
    if (const char* test = std::getenv("OT_MUSIC_NAME_TEST"); test != nullptr && *test != '\0') {
        const std::string text = test;
        const std::size_t bar = text.find('|');
        announce(text.substr(0, bar), bar == std::string::npos ? std::string() : text.substr(bar + 1));
    }
}

MusicChoice music_choice(const std::string& name) {
    if (!g.read) {
        read_switches();
    }
    if (!g.in_level) {
        return {};
    }
    const int index = game_track_index(name);
    if (index < kFirstSong) {
        return {};   // the level's own ambience (the CD music level at 0) and anything that is not a level song
    }
    if (!g.on) {
        return game_plays(index);
    }
    if (!g.built) {
        build_playlist();
        if (!g.on) {
            return game_plays(index);
        }
    }
    std::size_t entry = g.playlist->request(index, game_step, g.random);
    for (std::size_t k = 0; k < g.items.size() && g.items[entry].bad; ++k) {
        entry = g.playlist->advance(g.random);   // a song that could not be played: the next one
    }
    if (g.items[entry].bad) {
        g.on = false;   // (only the player's files can be bad, so there are no songs of the game's in the list)
        runtime::host_options().custom_soundtrack = false;
        std::printf("music: none of the custom soundtrack's songs can be played; the game's own music plays\n");
        std::fflush(stdout);
        return game_plays(index);
    }
    g.last_entry = entry;
    const Item& item = g.items[entry];
    std::printf("music: song %d of %zu (the game asked for track %d): %s\n", g.playlist->place() + 1, g.playlist->size(), index,
                item.disc >= 0 ? ("the game's track " + std::to_string(item.disc)).c_str() : item.path.c_str());
    std::fflush(stdout);
    if (g.playlist->moved() && g.now_playing) {
        // the game's own songs have no names in their files: theirs come from kGameSongs
        const GameSong* own = item.disc >= 0 ? game_song(item.disc) : nullptr;
        if (item.disc < 0) {
            announce(item.band, item.title);
        } else if (own != nullptr) {
            announce(own->band, own->title);
        }
    }
    MusicChoice choice;
    choice.replaced = true;
    if (item.disc >= 0) {
        char file[16];
        std::snprintf(file, sizeof file, "LTIX%02d.DAT", item.disc);
        choice.disc = true;
        choice.file = file;
    } else {
        choice.file = item.path;
    }
    return choice;
}

void music_open_failed(const std::string& file) {
    if (!g.on || g.last_entry >= g.items.size() || g.items[g.last_entry].disc >= 0 || g.items[g.last_entry].path != file) {
        return;
    }
    g.items[g.last_entry].bad = true;
    g.pending = false;   // its name is not shown
    std::printf("music: %s cannot be played; skipped from now on\n", file.c_str());
    std::fflush(stdout);
}

void music_each_poll() {
    if (!g.pending || !g.in_level) {
        return;
    }
    // (a level polls once a frame: the delay is as long in seconds at 60 and 120)
    const std::uint32_t rate = static_cast<std::uint32_t>(game_options().frame_rate);
    if (platform_input_polls() - g.pending_poll < kAnnounceDelayPolls * (rate >= 60 ? rate / 30 : 1u)) {
        return;
    }
    // shown when the level is on screen again: not under the pause menu (a skip there is announced as it closes), not before the HUD's font is loaded
    if (retail<std::uint32_t>(kPauseFlag) != 0 || retail<std::uint8_t>(kMessFontLoaded) == 0 || runtime::host_options().hide_hud) {
        return;
    }
    show_name();
}

}  // namespace opentony::kitdll
