// Where the player's own files go (docs/LAUNCHER.md, "The install folder"). Retail writes saves (*.SAV), custom parks (*.PRK) and its options file (TH2_OPT.CFG)
// beside THawk2.exe, in the folder it was started from. The game still builds those names from that folder; the host answers for them from another one:
//
//   OPENTONY_SAVES_DIR    where a *.SAV or *.PRK of the working directory is kept (and searched for: the memory card's scan of "<folder>\*.SAV")
//   OPENTONY_CONFIG_DIR   where TH2_OPT.CFG of the working directory is kept
//
// Unset (or empty), nothing moves. A file anywhere else (the package, a mod, an absolute path of the player's choosing) is never touched.
#pragma once

#ifdef _WIN32
#include <direct.h>
#else
#include "ot_os.hpp"
#include "win32_posix.h"   // _getcwd
#endif

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace opentony::kitdll {

// fopen of a host path the game named: on Linux a name that exists under another case is that file (the game spells its files as a case-insensitive file system
// lets it, "data/tricks.bin" for DATA/TRICKS.BIN)
inline std::FILE* host_fopen(const std::string& path, const char* mode) {
#ifdef _WIN32
    return std::fopen(path.c_str(), mode);
#else
    std::string host = path;   // a path built with the Windows separator (the mod overlay's) is the host's
    for (char& c : host) {
        if (c == '\\') {
            c = '/';
        }
    }
    return opentony::os::fopen_nocase(host, mode);
#endif
}

namespace userdata_detail {

inline std::string lower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

inline std::string slashed(std::string path) {
    for (char& c : path) {
        if (c == '\\') {
            c = '/';
        }
    }
    while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }
    return path;
}

inline bool ends_with(const std::string& text, const char* suffix) {
    const std::string tail = suffix;
    return text.size() >= tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
}

inline const char* environment(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? value : nullptr;
}

}  // namespace userdata_detail

// `path` as the host calls take it ('/' separators). The same path, or its place in the saves or the config folder.
inline std::string user_path(const std::string& path) {
    using namespace userdata_detail;
    const std::size_t slash = path.find_last_of('/');
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const std::string leaf = lower(name);
    const char* folder = nullptr;
    if (ends_with(leaf, ".sav") || ends_with(leaf, ".prk")) {
        folder = environment("OPENTONY_SAVES_DIR");
    } else if (leaf == "th2_opt.cfg") {
        folder = environment("OPENTONY_CONFIG_DIR");
    }
    if (folder == nullptr) {
        return path;
    }
    if (slash != std::string::npos) {   // only a file of the working directory, where retail keeps them
        char here[512];
        if (_getcwd(here, sizeof here) == nullptr || lower(slashed(path.substr(0, slash))) != lower(slashed(here))) {
            return path;
        }
    }
    return slashed(folder) + "/" + name;
}

// A quick start (OT_QUICK_START) never changes a save (*.SAV) or a custom park (*.PRK): its menus grey every way to one (host_options.hpp quick_play), and the file calls
// refuse to create, write, update or delete one as well, so nothing a quick start does reaches the career. Reading one is left alone. `path` is any spelling of the
// file; the first refusal is logged.
inline bool user_file_locked(const std::string& path) {
    using namespace userdata_detail;
    if (environment("OT_QUICK_START") == nullptr) {
        return false;
    }
    const std::string leaf = lower(path.substr(path.find_last_of("/\\") == std::string::npos ? 0 : path.find_last_of("/\\") + 1));
    if (!ends_with(leaf, ".sav") && !ends_with(leaf, ".prk")) {
        return false;
    }
    static bool logged = false;
    if (!logged) {
        logged = true;
        std::printf("files: quick start: %s left as it is (saves are read-only in a quick start)\n", path.c_str());
        std::fflush(stdout);
    }
    return true;
}

}  // namespace opentony::kitdll
