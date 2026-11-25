// The install folder's layout, shared by opentony.exe and THPSource2.exe (docs/LAUNCHER.md, "The install folder"). The root holds the launcher and
// README.md; everything else is in a folder of its own:
//
//   Build\        opentony.exe, the standalone's programs and DLLs, SDL3.dll, the FFmpeg DLLs
//   Config\       THPSource2.ini (the launcher's settings and the game's own [VIDEO] [JOYPAD] [KEYBOARD] ...), TH2_OPT.CFG
//   Data\Game\    the player's own files: THawk2.exe, ALL.PKR, Music\, med\Movies\ (or an installed game elsewhere, [Launcher] GameDir)
//   Data\Unpacked\  made on the first run, safe to delete: thawk2_static.img, data\ (ALL.PKR unpacked), stamps
//   Saves\        *.SAV and *.PRK, highscores.tsv (every finished run, scores.hpp)
//   Mods\  Screenshots\  Logs\ (notes.txt, sessions.txt, crashes.txt, runs\)  Licenses\ (THIRD_PARTY_NOTICES.md and the licence texts)
#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace opentony::layout {

struct Layout {
    std::filesystem::path root;

    std::filesystem::path build() const { return root / "Build"; }
    std::filesystem::path config() const { return root / "Config"; }
    std::filesystem::path ini() const { return config() / "THPSource2.ini"; }
    std::filesystem::path data() const { return root / "Data"; }
    std::filesystem::path game() const { return data() / "Game"; }
    std::filesystem::path unpacked() const { return data() / "Unpacked"; }
    std::filesystem::path static_image() const { return unpacked() / "thawk2_static.img"; }
    std::filesystem::path package_data() const { return unpacked() / "data"; }
    // the texture upscaler (docs/LAUNCHER.md, "Upscaled textures"): the game's own textures dumped, the upscaled set the game draws, the tool and models
    std::filesystem::path texture_dump() const { return data() / "TextureDump"; }
    std::filesystem::path upscaled() const { return data() / "Upscaled"; }
    std::filesystem::path upscaler() const { return data() / "Upscaler"; }
    // the PC port's 24-bit bitmaps in all 8 bits a channel, named by the key of the game's 15-bit upload (docs/LAUNCHER.md, "True-colour textures"); safe to delete
    std::filesystem::path true_colour() const { return data() / "TrueColour"; }
    std::filesystem::path saves() const { return root / "Saves"; }
    std::filesystem::path scores() const { return saves() / "highscores.tsv"; }   // every finished run (scores.hpp); the memory card's scan of *.SAV passes it by
    std::filesystem::path mods() const { return root / "Mods"; }
    std::filesystem::path screenshots() const { return root / "Screenshots"; }
    std::filesystem::path logs() const { return root / "Logs"; }
    std::filesystem::path runs() const { return logs() / "runs"; }
    std::filesystem::path sessions() const { return logs() / "sessions.txt"; }
    std::filesystem::path crashes() const { return logs() / "crashes.txt"; }
    std::filesystem::path notes() const { return logs() / "notes.txt"; }
    std::filesystem::path licenses() const { return root / "Licenses"; }
};

// The root for a program that lives in `directory`: the folder above Build\ when it is called Build, else the folder itself (a program run from a build tree).
// The file names of the programs in Build: opentony.exe and opentony_standalone.dll on Windows, opentony and opentony_standalone.so on Linux (docs/LINUX.md)
#ifdef _WIN32
inline constexpr const char* kProgramSuffix = ".exe";
inline constexpr const char* kModuleSuffix = ".dll";
#else
inline constexpr const char* kProgramSuffix = "";
inline constexpr const char* kModuleSuffix = ".so";
#endif
inline std::string program_file(const char* name) { return std::string(name) + kProgramSuffix; }
inline std::string module_file(const char* name) { return std::string(name) + kModuleSuffix; }

inline Layout from_program_directory(const std::filesystem::path& directory) {
    std::string name = directory.filename().string();
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (name.empty() && directory.parent_path() != directory) {   // "C:\x\Build\" keeps its separator
        return from_program_directory(directory.parent_path());
    }
    return Layout{name == "build" ? directory.parent_path() : directory};
}

}  // namespace opentony::layout
