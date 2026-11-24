// The standalone's session recorder (milestone 9): the hooked build's session probe (src/hooks/session_trace.cpp, Phase 5.1s) for a program with no
// retail code, so a session scenario of re/scenarios/sessions runs here and its trace is compared block by block with the retail recording's.
//
// The same variables, read once (docs/HOOKS.md, "Session probe"):
//   OPENTONY_SESSION_TRACE=<N>          stop after N clock deliveries, writing session.done
//   OPENTONY_SESSION_EVERY=<k>          hash every k-th delivery (default 2)
//   OPENTONY_SESSION_KEYS=<poll>:<dik hex>:<hold>[,...]   hold a key for <hold> input polls from <poll>
//   OPENTONY_SESSION_SKIP_MOVIES=1      the startup movies return at once
//   OPENTONY_SESSION_DUMP=<a>[,<b>]     write session_<a>.img (the image) at that delivery
//   OPENTONY_SESSION_MASK=<addr>:<len>,...  bytes hashed as zero
//   OPENTONY_SESSION_NORM=0             no pointer normalisation (default on)
//   OPENTONY_LOG_DIR=<directory>        where session.trace, the dumps and session.done go (default: the working directory)
//
// What is the same as the probe, and must stay so: a delivery is one run of the timer callback on the game thread (the clock of standalone_seams.cpp
// counts them as the lockstep clock of src/hooks/clock.cpp does); a poll is one keyboard device update of PCInput_Poll and the keys are written after
// it, before the bindings read them; the image is the canonical build's .data (one region, 0x00524000 + 0x024b7e3c), hashed in 64 KB blocks with
// session_hash.h, a word that points into the game's two memory zones hashed as an offset from the lowest zone.
#include "../../platform/path_separator.h"
#include "kit_bridge.hpp"

#include "../session_hash.h"

#ifdef _WIN32
#include <windows.h>
#else
#include "win32_posix.h"
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include "guest_arena.hpp"

namespace opentony::kitdll {
namespace {

constexpr std::uint32_t kDataBase = 0x00524000;
constexpr std::uint32_t kDataSize = 0x024b7e3c;      // .data's virtual size, rounded to a word, as the probe reads it from the section table
constexpr std::uint32_t kBlockSize = 64u * 1024u;
constexpr std::uint32_t kKeyboardState = 0x006a43e4;  // the 256 DirectInput key bytes (the keyboard wrapper +4)
constexpr std::uint32_t kZoneBounds = 0x0056a97c;     // begin, end of zone 0, then of zone 1 (Mem_Init)
const char kMagic[8] = {'O', 'T', 'S', 'E', 'S', 'S', '1', 0};

struct KeyEvent {
    std::uint32_t poll, dik, hold;
};
struct Range {
    std::uint32_t base, size;
};
// OT_POKE_AT_POLL=<poll>:<addr hex>=<value>[,...]: a test writes a word of the game's image once, after that input poll (a state the keys cannot reach, such as
// an unlocked level; the player's build never sets it)
struct PokeEvent {
    std::uint32_t poll, address, value;
};
std::vector<PokeEvent> g_pokes;

struct Recorder {
    bool enabled = false;
    bool skip_movies = false;
    std::uint32_t limit = 0;
    std::uint32_t every = 2;
    std::uint32_t dumps[2] = {0xffffffffu, 0xffffffffu};
    bool normalize = true;
    std::vector<KeyEvent> keys;
    std::vector<Range> masks;
    std::string directory;
    FILE* trace = nullptr;
    std::uint32_t deliveries = 0;
    std::uint32_t polls = 0;
    std::vector<std::uint32_t> record;
    std::vector<std::uint32_t> scratch;
} g_session;

std::string path_in(const std::string& name) { return g_session.directory.empty() ? name : g_session.directory + OT_PATH_SEPARATOR + name; }

std::uint32_t number(const char* name, std::uint32_t fallback) {
    const char* text = std::getenv(name);
    return text != nullptr && *text != '\0' ? static_cast<std::uint32_t>(std::strtoul(text, nullptr, 10)) : fallback;
}

void start() {
    static bool started = false;
    if (started) {
        return;
    }
    started = true;
    g_session.skip_movies = number("OPENTONY_SESSION_SKIP_MOVIES", 0) != 0;
    if (const char* text = std::getenv("OT_POKE_AT_POLL")) {
        for (const char* at = text; *at != '\0';) {
            char* next = nullptr;
            PokeEvent poke{};
            poke.poll = static_cast<std::uint32_t>(std::strtoul(at, &next, 10));
            if (*next != ':') break;
            poke.address = static_cast<std::uint32_t>(std::strtoul(next + 1, &next, 16));
            if (*next != '=') break;
            poke.value = static_cast<std::uint32_t>(std::strtoul(next + 1, &next, 0));
            g_pokes.push_back(poke);
            at = *next == ',' ? next + 1 : next;
        }
    }
    g_session.limit = number("OPENTONY_SESSION_TRACE", 0);
    if (g_session.limit == 0) {
        return;
    }
    g_session.every = std::max<std::uint32_t>(number("OPENTONY_SESSION_EVERY", 2), 1);
    g_session.normalize = number("OPENTONY_SESSION_NORM", 1) != 0;
    if (const char* text = std::getenv("OPENTONY_SESSION_DUMP")) {
        char* next = nullptr;
        g_session.dumps[0] = static_cast<std::uint32_t>(std::strtoul(text, &next, 10));
        if (*next == ',') {
            g_session.dumps[1] = static_cast<std::uint32_t>(std::strtoul(next + 1, nullptr, 10));
        }
    }
    if (const char* text = std::getenv("OPENTONY_SESSION_KEYS")) {   // <poll>:<dik hex>:<hold>[,...]
        for (const char* at = text; *at != '\0';) {
            char* next = nullptr;
            KeyEvent event{};
            event.poll = static_cast<std::uint32_t>(std::strtoul(at, &next, 10));
            if (*next != ':') break;
            event.dik = static_cast<std::uint32_t>(std::strtoul(next + 1, &next, 16));
            if (*next != ':') break;
            event.hold = static_cast<std::uint32_t>(std::strtoul(next + 1, &next, 10));
            g_session.keys.push_back(event);
            at = *next == ',' ? next + 1 : next;
        }
    }
    if (const char* text = std::getenv("OPENTONY_SESSION_MASK")) {   // <addr hex>:<len hex>[,...]
        for (const char* at = text; *at != '\0';) {
            char* next = nullptr;
            Range mask{};
            mask.base = static_cast<std::uint32_t>(std::strtoul(at, &next, 16));
            if (*next != ':') break;
            mask.size = static_cast<std::uint32_t>(std::strtoul(next + 1, &next, 16));
            g_session.masks.push_back(mask);
            at = *next == ',' ? next + 1 : next;
        }
    }
    if (const char* directory = std::getenv("OPENTONY_LOG_DIR")) {
        g_session.directory = directory;
    }
    g_session.trace = std::fopen(path_in("session.trace").c_str(), "wb");
    if (g_session.trace == nullptr) {
        std::fprintf(stderr, "session: cannot write %s\n", path_in("session.trace").c_str());
        return;
    }
    const std::uint32_t blocks = (kDataSize + kBlockSize - 1) / kBlockSize;
    const std::uint32_t header[4] = {kBlockSize, g_session.every, blocks, 1};
    const Range region{kDataBase, kDataSize};
    std::fwrite(kMagic, 1, sizeof kMagic, g_session.trace);
    std::fwrite(header, 4, 4, g_session.trace);
    std::fwrite(&region, 4, 2, g_session.trace);
    g_session.record.assign(2 + blocks, 0);
    g_session.scratch.assign(kBlockSize / 4, 0);
    g_session.enabled = true;
    std::printf("session: trace %u deliveries, every %u, %zu key events\n", static_cast<unsigned>(g_session.limit), static_cast<unsigned>(g_session.every),
                g_session.keys.size());
    std::fflush(stdout);
}

void dump(std::uint32_t delivery) {
    const std::string path = path_in("session_" + std::to_string(delivery) + ".img");
    if (FILE* file = std::fopen(path.c_str(), "wb")) {
        std::fwrite(arena::host<const void>(kDataBase), 1, kDataSize, file);
        std::fclose(file);
        std::printf("session: dumped the image at delivery %u to %s\n", static_cast<unsigned>(delivery), path.c_str());
        std::fflush(stdout);
    }
    // a cold boot's zone block is where retail's is (standalone_seams.cpp): its region, as the probe's session_N.heap records it (base, size, bytes)
    if (standalone_booting()) {
        const std::uint32_t kZoneRegion = standalone_zone_block() - 0x20;
        constexpr std::uint32_t kZoneRegionSize = 0x005e0000;
        if (FILE* file = std::fopen(path_in("session_" + std::to_string(delivery) + ".heap").c_str(), "wb")) {
            const std::uint32_t header[2] = {kZoneRegion, kZoneRegionSize};
            std::fwrite(header, 4, 2, file);
            std::fwrite(arena::host<const void>(kZoneRegion), 1, kZoneRegionSize, file);
            std::fclose(file);
        }
    }
}

void hash_image(std::uint32_t delivery) {
    std::uint32_t low = 0, high = 0;
    if (g_session.normalize) {
        const auto* zones = arena::host<const volatile std::uint32_t>(kZoneBounds);
        low = zones[0] < zones[2] ? zones[0] : zones[2];
        high = zones[1] > zones[3] ? zones[1] : zones[3];
        if (low == 0 || high <= low) {
            low = high = 0;
        }
    }
    g_session.record[0] = delivery;
    g_session.record[1] = g_session.polls;
    std::uint32_t block = 0;
    for (std::uint32_t offset = 0; offset < kDataSize; offset += kBlockSize) {
        const std::uint32_t address = kDataBase + offset;
        const std::uint32_t size = std::min(kBlockSize, kDataSize - offset);
        const auto* words = arena::host<const std::uint32_t>(address);
        bool touched = false;
        for (const Range& mask : g_session.masks) {
            if (mask.base < address + size && mask.base + mask.size > address) {
                if (!touched) {
                    std::memcpy(g_session.scratch.data(), words, size);
                    touched = true;
                }
                const std::uint32_t from = mask.base > address ? mask.base - address : 0;
                const std::uint32_t to = std::min(mask.base + mask.size - address, size);
                std::memset(reinterpret_cast<unsigned char*>(g_session.scratch.data()) + from, 0, to - from);
            }
        }
        if (touched) {
            words = g_session.scratch.data();
        }
        g_session.record[2 + block++] = low != high ? ot_session_hash_block_normalized(words, size / 4u, low, high) : ot_session_hash_block(words, size / 4u);
    }
    std::fwrite(g_session.record.data(), 4, g_session.record.size(), g_session.trace);
    std::fflush(g_session.trace);   // 10f: a run that stops on a FATAL (TerminateProcess) keeps the records it made
}

}  // namespace

// OPENTONY_SESSION_SKIP_MOVIES: the probe patches the two movie entries, 0x004e5ec0 (the blocking player IntroMovies calls) and 0x004e6590 (StartBinkMovie),
// to `xor eax, eax; ret`; here the resolver asks this before it calls either
// OPENTONY_PLAY_MUSIC=1 (the player's launcher): StartBinkMovie's music files (its second argument) are not skipped; only the movies are. The scenarios and gates leave it
// unset: retail's probe skips both, and their traces depend on it.
bool session_overrides(std::uint32_t address, bool music, std::uint32_t& result) {
    start();
    static const bool play_music = number("OPENTONY_PLAY_MUSIC", 0) != 0;
    if (address == 0x004e6590 && music && play_music) {
        return false;
    }
    // the player's launcher: the intro skip ends with the intro (the logo screen follows it), so Options > Movies > Cinematics plays whatever the intro setting is
    if (play_music && address == 0x0044b3a0) {
        g_session.skip_movies = false;
    }
    if (g_session.skip_movies && (address == 0x004e5ec0 || address == 0x004e6590)) {
        result = 0;
        return true;
    }
    return false;
}

void session_end_movie_skip() {
    start();
    g_session.skip_movies = false;
}

// after the keyboard device update of a PCInput_Poll: the scripted keys of this poll, then the poll is counted (the probe's ot_session_after_poll)
void session_after_poll() {
    start();
    const std::uint32_t poll = g_session.polls;
    auto* keys = arena::host<volatile std::uint8_t>(kKeyboardState);
    for (const KeyEvent& event : g_session.keys) {
        if (event.dik > 0xff) {
            continue;
        }
        if (poll >= event.poll && poll < event.poll + event.hold) {
            keys[event.dik] = 0x80;
        } else if (poll == event.poll + event.hold) {
            keys[event.dik] = 0;
        }
    }
    // OT_WATCH_WORDS=<addr hex>[,...]: print a word each time it changes (with the poll), so a test run shows the front end's status and the shell's screen
    static std::vector<std::pair<std::uint32_t, std::uint32_t>> watched;
    static bool watch_read = false;
    if (!watch_read) {
        watch_read = true;
        if (const char* text = std::getenv("OT_WATCH_WORDS")) {
            for (const char* at = text; *at != '\0';) {
                char* next = nullptr;
                watched.emplace_back(static_cast<std::uint32_t>(std::strtoul(at, &next, 16)), 0xdeadbeefu);
                at = *next == ',' ? next + 1 : next;
            }
        }
    }
    for (auto& word : watched) {
        const std::uint32_t now = *arena::host<volatile std::uint32_t>(word.first);
        if (now != word.second) {
            std::printf("watch poll %u: %08x %08x -> %08x\n", static_cast<unsigned>(poll), static_cast<unsigned>(word.first), static_cast<unsigned>(word.second), static_cast<unsigned>(now));
            word.second = now;
        }
    }
    for (const PokeEvent& poke : g_pokes) {
        if (poke.poll == poll) {
            *arena::host<volatile std::uint32_t>(poke.address) = poke.value;
            std::printf("poke at poll %u: %08x = %08x\n", static_cast<unsigned>(poll), static_cast<unsigned>(poke.address), static_cast<unsigned>(poke.value));
            std::fflush(stdout);
        }
    }
    g_session.polls = poll + 1;
}

// after each run of the timer callback on the game thread (the probe's ot_session_on_delivery)
void session_on_delivery() {
    start();
    if (!g_session.enabled) {
        return;
    }
    const std::uint32_t count = ++g_session.deliveries;
    if (count == g_session.dumps[0] || count == g_session.dumps[1]) {
        dump(count);
    }
    if (count % g_session.every == 0) {
        hash_image(count);
    }
    if (count >= g_session.limit) {
        session_stop();
    }
}

void session_stop() {
    start();
    if (g_session.trace != nullptr) {
        std::fclose(g_session.trace);
        g_session.trace = nullptr;
    }
    g_session.enabled = false;
    std::printf("session: %u deliveries, %u polls; stopping\n", static_cast<unsigned>(g_session.deliveries), static_cast<unsigned>(g_session.polls));
    if (FILE* done = std::fopen(path_in("session.done").c_str(), "wb")) {
        std::fclose(done);
    }
    std::fflush(stdout);
    ExitProcess(0);
}

}  // namespace opentony::kitdll
