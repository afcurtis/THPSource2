#include "auto_views.hpp"
#include "frame_pacing.hpp"
#include "pc_system.hpp"
#include "seam_addresses_generated.hpp"

#include <cmath>
#include <stdexcept>

namespace opentony::runtime::pcplatform {

using namespace opentony::runtime::autoviews;

using namespace winmain;

namespace {

constexpr std::uint32_t kFormatString = 0x005256dc;       // "%s"
constexpr std::uint32_t kFormatInt = 0x005486d0;          // "%i"
constexpr std::uint32_t kFormatPath = 0x00549fc8;         // "%s\%s"
constexpr std::uint32_t kOpenRead = 0x0054b348;           // the mode strings handed to fopen
constexpr std::uint32_t kOpenWrite = 0x0054b344;
constexpr std::uint32_t kOpenCreate = 0x00548a4c;

constexpr std::uint32_t kInstall = 0x0055bf14;            // "INSTALL" and its entries
constexpr std::uint32_t kDataPathKey = 0x0055bf1c;
constexpr std::uint32_t kMusicPathKey = 0x0055bf04;
constexpr std::uint32_t kMoviePathKey = 0x0055bedc;
constexpr std::uint32_t kVersionKey = 0x0055bf2c;         // "VERSION" is both section and key
constexpr std::uint32_t kVersion = 0x0055bf28;            // "6.0"
constexpr std::uint32_t kVersionDefault = 0x0055bf34;     // "0.0"
constexpr std::uint32_t kVideo = 0x005486e0;              // "VIDEO" and its entries
constexpr std::uint32_t kRendererKey = 0x005486e8;
constexpr std::uint32_t kResolutionKey = 0x005486d4;
constexpr std::uint32_t kGammaKey = 0x005313e8;
constexpr std::uint32_t kQualityKey = 0x005486c0;
constexpr std::uint32_t kFogKey = 0x005486b4;

// the widths and heights WINMAIN_BuildDisplayModeList tries, with the pointer variable that names each mode ("640x480", ...)
constexpr std::uint32_t kName320 = 0x0054859c;
constexpr std::uint32_t kName512 = 0x00548598;
constexpr std::uint32_t kName640 = 0x00548594;
constexpr std::uint32_t kName800 = 0x00548590;
constexpr std::uint32_t kName1024 = 0x0054858c;
constexpr std::uint32_t kName1280 = 0x00548588;

constexpr std::uint32_t kHardwareName = 0x00548564;       // the pointers to the "Hardware" and "Software" names the options file is compared with
constexpr std::uint32_t kSoftwareName = 0x00548568;
constexpr std::uint32_t kQualityNormal = 0x0056e7ac;      // and to the two quality names
constexpr std::uint32_t kQualityHigh = 0x0056e7b0;
constexpr std::uint32_t kFogFactor = 0x00519728;          // floats: the scale between VideoFogYonScale and the file's percentage
constexpr std::uint32_t kFogPercent = 0x00519934;

constexpr std::uint32_t kThstubPrefix = 0x0055bd2c;       // "!"
constexpr std::uint32_t kThstubExtension = 0x0055bd08;    // ".PRK"

float float_at(const RetailMemory& m, std::uint32_t address) {
    const std::uint32_t bits = guest::read_scalar<std::uint32_t>(m, address);
    float value;
    static_assert(sizeof value == sizeof bits);
    __builtin_memcpy(&value, &bits, sizeof value);
    return value;
}

std::uint32_t float_bits(float value) {
    std::uint32_t bits;
    __builtin_memcpy(&bits, &value, sizeof bits);
    return bits;
}

}  // namespace

// ---- pc\winmain.cpp ---------------------------------------------------------------------------------------------------------------------------------------------------

// 0x004f6660: can the card do this mode? It must be one DirectDraw listed, and the two buffers (2 bytes a pixel, 3 or 2 buffers' worth) must fit the video memory
bool PcSystem::can_do_mode(std::int32_t width, std::int32_t height, std::uint32_t half) {
    bool listed = false;
    const std::int32_t count = global_int(m_, kModeCount);
    if (count > 0) {
        std::uint32_t mode = kModes;
        for (std::int32_t k = count; k != 0; --k, mode += kModeStride) {
            if (width == int_at(m_, mode + 4) && height == int_at(m_, mode)) {
                listed = true;
            }
        }
        if (listed) {
            std::uint32_t need = static_cast<std::uint32_t>(width) * static_cast<std::uint32_t>(height) * 2u;
            need = (half & 0xff) != 0 ? need + need : need * 3u;
            if (need <= global_word(m_, kVideoMemory)) {
                return true;
            }
            log_line(0x0055bdd8);
            return false;
        }
    }
    log_line(0x0055be14);
    return false;
}

// 0x004f6d00
void PcSystem::screen_dimensions(std::uint32_t width_out, std::uint32_t height_out) {
    set_word_at(m_, width_out, global_word(m_, kScreenWidth));
    set_word_at(m_, height_out, global_word(m_, kScreenHeight));
}

// 0x004f86f0: the orderly exit: the net layer, the timer, the input, the movie and music, Direct3D
void PcSystem::shut_down() {
    log_line(0x0055c464);
    engine(0x004ddb70);
    engine(seam::hal::D3DTimer_Shutdown);
    engine(seam::hal::PCInput_Shutdown);
    engine(0x004e7140);
    engine(0x004e7370);
    engine(seam::hal::D3D_Shutdown, {1});
}

// 0x004f6850: is there a file `file` in the directory `directory`? (the directory text, the separator and the name are opened for reading and closed again)
bool PcSystem::probe_configured_file(std::uint32_t directory, std::uint32_t file) {
    const std::uint32_t handle = crt_fopen(path_in(directory) + m_.cstring(kSeparator) + path_in(file), kStackLocal, kOpenRead);
    if (handle == 0) {
        return false;
    }
    crt_fclose(handle);
    return true;
}

// 0x004f6920: find the data, music and movie folders: the options file's INSTALL entries, else the install folder, else (data and music) the disc's, written back to the file
void PcSystem::install_setup() {
    auto text = [&](std::uint32_t address) { return m_.cstring(address, 0x200); };
    set_string(kScratchDefault, crt::format(text(kFormatString), {text(kInstallDir)}));
    load_setting(kInstall, kDataPathKey, kScratchDefault, kDataPath, 0x100);
    if (!probe_configured_file(kDataPath, 0x0055bf0c)) {
        set_string(kDataPath, text(kInstallDir));
        if (!probe_configured_file(kDataPath, 0x0055bf0c)) {
            set_string(kDataPath, text(kDiscDir));
            probe_configured_file(kDataPath, 0x0055bf0c);
        }
    }
    save_setting(kInstall, kDataPathKey, kDataPath);

    set_string(kScratchDefault, crt::format(text(kFormatPath), {text(kInstallDir), text(0x00531f78)}));
    load_setting(kInstall, kMusicPathKey, kScratchDefault, kMusicPath, 0x100);
    if (!probe_configured_file(kMusicPath, 0x0055bef8)) {
        set_string(kMusicPath, crt::format(text(kFormatPath), {text(kInstallDir), text(0x00531f78)}));
        if (!probe_configured_file(kMusicPath, 0x0055bef8)) {
            set_string(kMusicPath, crt::format(text(kFormatPath), {text(kDiscDir), text(0x0055bee4)}));
            probe_configured_file(kMusicPath, 0x0055bef8);
        }
    }
    save_setting(kInstall, kMusicPathKey, kMusicPath);

    set_string(kScratchDefault, crt::format(text(kFormatPath), {text(kInstallDir), text(0x00531d10)}));
    load_setting(kInstall, kMoviePathKey, kScratchDefault, kMoviePath, 0x100);
    if (!probe_configured_file(kMoviePath, 0x0055bed0)) {
        set_string(kMoviePath, crt::format(text(kFormatPath), {text(kInstallDir), text(0x00531d10)}));
        if (!probe_configured_file(kMoviePath, 0x0055bed0)) {
            set_string(kMoviePath, crt::format(text(kFormatPath), {text(kDiscDir), text(0x0055bebc)}));
            probe_configured_file(kMoviePath, 0x0055bed0);
        }
    }
    save_setting(kInstall, kMoviePathKey, kMoviePath);
    log_line(0x0055beac);
    log_line(0x0055be9c);
    log_line(0x0055be8c);
}

// 0x004f6b90: read the options file: a file of another version is truncated first, then the version is written, the video options and the bindings are read
void PcSystem::load_options() {
    set_string(kScratchDefault, crt::format(m_.cstring(kFormatString), {m_.cstring(kVersionDefault)}));
    load_setting(kVersionKey, kVersionKey, kScratchDefault, kScratchOut, 0x100);
    if (m_.cstring(kScratchOut, 0x100) != m_.cstring(kVersion)) {
        crt_fclose(crt_fopen(m_.cstring(kIniFile, 0x100), kIniFile, kOpenCreate));
    }
    set_string(kScratchDefault, crt::format(m_.cstring(kFormatString), {m_.cstring(kVersion)}));
    save_setting(kVersionKey, kVersionKey, kScratchDefault);
    load_video_options();
    pad_.load_bindings();
}

// 0x004f6c60
void PcSystem::save_options() {
    set_string(kScratchDefault, crt::format(m_.cstring(kFormatString), {m_.cstring(kVersion)}));
    save_setting(kVersionKey, kVersionKey, kScratchDefault);
    save_video_options();
    pad_.save_bindings();
}

// 0x004cf420: once, the list of resolutions the card can do (hardware: from 320x240, software: from 640x480, up to 1280x1024); each name is the pointer variable that holds it
void PcSystem::build_display_mode_list() {
    if (global_byte(m_, kModesBuilt) != 0) {
        return;
    }
    set_global_byte(m_, kModesBuilt, 1);
    engine(seam::hal::WINMAIN_EnumDisplayModes);
    std::uint32_t count = 1;
    auto add = [&](std::uint32_t name_variable) { set_word_at(m_, kModeNames + 4 * count, word_at(m_, name_variable)); ++count; };
    if (global_word(m_, kRenderer) == 1) {
        set_global_word(m_, kModeNames, global_word(m_, kName320));
        if (can_do_mode(0x200, 0x180, 1)) {
            set_global_word(m_, kModeNames + 4, global_word(m_, kName512));
            count = 2;
        }
        if (can_do_mode(0x280, 0x1e0, 1)) {
            add(kName640);
        }
        if (can_do_mode(0x320, 0x258, 1)) {
            add(kName800);
        }
        if (can_do_mode(0x400, 0x300, 1)) {
            add(kName1024);
        }
        if (can_do_mode(0x500, 0x400, 1)) {
            add(kName1280);
        }
    } else {
        set_global_word(m_, kModeNames, global_word(m_, kName640));
        if (can_do_mode(0x320, 0x258, 0)) {
            set_global_word(m_, kModeNames + 4, global_word(m_, kName800));
            count = 2;
        }
        if (can_do_mode(0x400, 0x300, 0)) {
            add(kName1024);
        }
        if (can_do_mode(0x500, 0x400, 0)) {
            add(kName1280);
        }
    }
    set_word_at(m_, kModeNames + 4 * count, 0);
}

// 0x004cf030: read the VIDEO entries of the options file: the renderer, the resolution (by name), the gamma (-15..15), the texture quality and the fog scale
void PcSystem::load_video_options() {
    auto nocase_equal = [&](const std::string& a, std::uint32_t pointer) { return crt::equal_nocase(a, m_.cstring(pointer, 0x100)); };
    std::int32_t number = 0;
    set_string(kScratchDefault, crt::format(m_.cstring(kFormatString), {m_.cstring(global_word(m_, kRenderer) != 0 ? 0x00531428 : 0x00531434)}));
    load_setting(kVideo, kRendererKey, kScratchDefault, kScratchOut, 0x100);
    std::string loaded = m_.cstring(kScratchOut, 0x100);
    if (nocase_equal(loaded, global_word(m_, kHardwareName))) {
        set_global_word(m_, kRenderer, 0);
    } else {
        set_global_word(m_, kRenderer, nocase_equal(loaded, global_word(m_, kSoftwareName)) ? 1 : 0);
    }
    if (global_byte(m_, kModesBuilt) == 0) {
        build_display_mode_list();
    }
    set_string(kScratchDefault, crt::format(m_.cstring(kFormatString), {m_.cstring(word_at(m_, kModeNames + 4 * global_word(m_, kModeIndex)), 0x100)}));
    load_setting(kVideo, kResolutionKey, kScratchDefault, kScratchOut, 0x100);
    loaded = m_.cstring(kScratchOut, 0x100);
    for (std::int32_t index = 0;; ++index) {
        const std::uint32_t name = word_at(m_, kModeNames + 4 * static_cast<std::uint32_t>(index));
        if (name == 0) {
            break;
        }
        if (crt::equal_nocase(m_.cstring(name, 0x100), loaded)) {
            set_global_word(m_, kModeIndex, static_cast<std::uint32_t>(index));
            break;
        }
    }
    number = 0;
    set_string(kScratchDefault, crt::format(m_.cstring(kFormatInt), {}, {global_int(m_, kGamma)}));
    load_setting(kVideo, kGammaKey, kScratchDefault, kScratchOut, 0x100);
    crt::scan_int(m_.cstring(kScratchOut, 0x100), number);
    if (number > 15) {
        set_global_word(m_, kGamma, 0);
    } else {
        set_global_word(m_, kGamma, static_cast<std::uint32_t>(number));
        if (number < -15) {
            set_global_word(m_, kGamma, 0);
        }
    }
    set_string(kScratchDefault, crt::format(m_.cstring(kFormatString), {m_.cstring(global_word(m_, kQuality) != 0 ? 0x005486c8 : 0x00531420)}));
    load_setting(kVideo, kQualityKey, kScratchDefault, kScratchOut, 0x100);
    loaded = m_.cstring(kScratchOut, 0x100);
    if (nocase_equal(loaded, global_word(m_, kQualityNormal))) {
        set_global_word(m_, kQuality, 0);
    } else {
        set_global_word(m_, kQuality, nocase_equal(loaded, global_word(m_, kQualityHigh)) ? 1 : 0);
    }
    // the fog scale is a float the file keeps as a percentage: the x87 runs at 24 bits, so each step is a float operation
    const double scaled = static_cast<double>(float_at(m_, kFogScale)) * static_cast<double>(float_at(m_, kFogFactor));
    set_string(kScratchDefault, crt::format(m_.cstring(kFormatInt), {}, {static_cast<std::int32_t>(scaled)}));
    load_setting(kVideo, kFogKey, kScratchDefault, kScratchOut, 0x100);
    crt::scan_int(m_.cstring(kScratchOut, 0x100), number);
    if (number <= 0x1f4 && number >= 0x32) {
        set_global_word(m_, kFogScale, float_bits(static_cast<float>(number) * float_at(m_, kFogPercent)));
    } else {
        set_global_word(m_, kFogScale, 0x3fc00000);
    }
    save_video_options();
    if (global_word(m_, kRenderer) == 0) {
        set_global_word(m_, kSoftwareRenderer, 0);
        set_global_byte(m_, kTextureRootEnabled, 1);
    } else {
        set_global_word(m_, kSoftwareRenderer, global_word(m_, kQuality) + 1);
        set_global_byte(m_, kTextureRootEnabled, global_word(m_, kQuality) != 0 ? 1 : 0);
    }
}

// 0x004cf300
void PcSystem::save_video_options() {
    set_string(kScratchDefault, crt::format(m_.cstring(kFormatString), {m_.cstring(global_word(m_, kRenderer) != 0 ? 0x00531428 : 0x00531434)}));
    save_setting(kVideo, kRendererKey, kScratchDefault);
    set_string(kScratchDefault, crt::format(m_.cstring(kFormatString), {m_.cstring(word_at(m_, kModeNames + 4 * global_word(m_, kModeIndex)), 0x100)}));
    save_setting(kVideo, kResolutionKey, kScratchDefault);
    set_string(kScratchDefault, crt::format(m_.cstring(kFormatInt), {}, {global_int(m_, kGamma)}));
    save_setting(kVideo, kGammaKey, kScratchDefault);
    set_string(kScratchDefault, crt::format(m_.cstring(kFormatString), {m_.cstring(global_word(m_, kQuality) != 0 ? 0x005486c8 : 0x00531420)}));
    save_setting(kVideo, kQualityKey, kScratchDefault);
    const double scaled = static_cast<double>(float_at(m_, kFogScale)) * static_cast<double>(float_at(m_, kFogFactor));
    set_string(kScratchDefault, crt::format(m_.cstring(kFormatInt), {}, {static_cast<std::int32_t>(scaled)}));
    save_setting(kVideo, kFogKey, kScratchDefault);
    engine(0x004ce260);
}

// 0x004f77f0: the Direct3D device's description to the log (the values go into the text the stub does not record)
void PcSystem::dump_device_caps(std::uint32_t) {
    static constexpr std::uint32_t kLines[] = {0x0054c24c, 0x0055c1d8, 0x0055c1bc, 0x0055c19c, 0x0055c17c, 0x0055c15c, 0x0055c13c, 0x0055c11c, 0x0055c0fc, 0x0055c0dc,
                                            0x0055c0bc, 0x0055c0a4, 0x0055c08c, 0x0055c054, 0x0055c01c, 0x0055bfe4, 0x0054c24c};
    for (const std::uint32_t line : kLines) {
        log_line(line);
    }
}

// ---- pc\thstub.cpp and the PC file backends ------------------------------------------------------------------------------------------------------------------------

namespace {

// strchr(text, '.') cut and the extension appended: the helper at 0x004f6080 (4.14a ports the function; the custom park files only need the effect)
std::string with_extension(const std::string& name, const std::string& extension) {
    const std::size_t dot = name.find('.');
    return (dot == std::string::npos ? name : name.substr(0, dot)) + extension;
}

}  // namespace

// 0x004f6400: delete the custom park file `name` (under the "!" folder unless it already starts with one), as .PRK
void PcSystem::delete_custom_level(std::uint32_t name, std::uint32_t flag) {
    std::string path = path_in(kInstallDir) + m_.cstring(kSeparator);
    if ((flag & 0xff) != 0 && byte_at(m_, name) != '!') {
        path += m_.cstring(kThstubPrefix);
    }
    path += path_in(name);
    log_line(0x0055bd6c);
    crt_remove(with_extension(path, m_.cstring(kThstubExtension)), kStackLocal);
}

// 0x004f62c0: write a custom park: the old file is deleted, the new one created, the bytes written and the file closed
void PcSystem::save_custom_level(std::uint32_t name, std::uint32_t flag, std::uint32_t data, std::uint32_t size) {
    delete_custom_level(name, flag);
    std::string file = ((flag & 0xff) == 0 || byte_at(m_, name) == '!') ? path_in(name) : m_.cstring(kThstubPrefix) + path_in(name);
    set_string(kScratchDefault, with_extension(file, m_.cstring(kThstubExtension)));
    log_line(0x0055bd50);
    // (live, the file calls are the host backend's whole: its table is the one the seek and the close below use; a replay runs the retail table)
    const std::int32_t handle = s_.live() ? static_cast<std::int32_t>(engine_value(0x004e7900, {kScratchDefault})) : pc_create(kScratchDefault);
    if (handle >= 0) {
        engine(0x004e79f0, {static_cast<std::uint32_t>(handle), 0, 0});
        if (s_.live()) {
            (void)engine_value(0x004e7b60, {static_cast<std::uint32_t>(handle), data, size});
        } else {
            pc_write(handle, data, size);
        }
        engine(0x004e7bc0, {static_cast<std::uint32_t>(handle)});
        return;
    }
    check(false, 0x0055bd30);
}

// 0x004f6140: load a custom park: in the menu mode (0) through a new park menu object; otherwise the .PRK file (under the "!" folder unless the name already starts with one) is opened
// and its first 0x2000 bytes read into `buffer`, and the park named. 1 when loaded, 0 when the file is not there. (A read this large is scripted as its first 64 bytes.)
std::uint32_t PcSystem::load_custom_level(std::uint32_t name, std::uint32_t menu_mode, std::uint32_t flag, std::uint32_t buffer) {
    if (menu_mode == 0) {
        s_.note("operator_new", {0x144});
        const std::uint32_t object = static_cast<std::uint32_t>(s_.value("operator_new"));
        if (object != 0) {
            engine(0x004329d0, {object, 0, buffer, 0, 0});
        }
        return engine_value(0x00432f60, {object, name});
    }
    std::string file = ((flag & 0xff) == 0 || byte_at(m_, name) == '!') ? path_in(name) : m_.cstring(kThstubPrefix) + path_in(name);
    set_string(kScratchDefault, with_extension(file, m_.cstring(kThstubExtension)));
    log_line(0x0055bd10);
    s_.note(engine_name(0x004e75b0), {s_.live() ? kScratchDefault : kStackLocal, 2, 0}, m_.cstring(kScratchDefault, 0x100));   // (live the name is where it was written)
    const std::int32_t handle = static_cast<std::int32_t>(s_.value(engine_name(0x004e75b0)));
    if (handle < 0) {
        return static_cast<std::uint32_t>(handle) & 0xffffff00u;       // (retail clears only AL of the failed handle)
    }
    engine(0x004e7ad0, {static_cast<std::uint32_t>(handle), buffer, 0x2000});
    if (!s_.live()) {
        // (a live read has written the bytes itself; only a replay scripts them)
        std::vector<std::uint8_t> bytes;
        for (std::uint32_t k = 0; k < 16; ++k) {
            const auto word = static_cast<std::uint32_t>(s_.value(engine_name(0x004e7ad0)));
            for (std::uint32_t b = 0; b < 4; ++b) {
                bytes.push_back(static_cast<std::uint8_t>(word >> (8 * b)));
            }
        }
        m_.write(buffer, bytes);
    }
    engine(0x004e7bc0, {static_cast<std::uint32_t>(handle)});
    engine(0x00432980, {name, 1});
    return 1;
}

std::uint32_t PcSystem::slot_free() {
    for (std::uint32_t slot = 0; slot < kBackendCount; ++slot) {
        if (byte_at(m_, kBackendState + slot) == 0) {
            return slot;
        }
    }
    return 0xffffffff;
}

// 0x004e7900: a file slot for a new file `name` under the install folder (opened for writing): its number, or -1 when no slot is free or the file cannot be created
std::int32_t PcSystem::pc_create(std::uint32_t name) {
    const std::uint32_t slot = slot_free();
    if (static_cast<std::int32_t>(slot) < 0) {
        return -1;
    }
    const std::uint32_t file = crt_fopen(path_in(kInstallDir) + m_.cstring(kSeparator) + path_in(name), kStackLocal, kOpenWrite);
    set_word_at(m_, kBackendFile + 4 * slot, file);
    if (file == 0) {
        log_line(0x0054b34c);
        return -1;
    }
    set_byte_at(m_, kBackendState + slot, 2);
    return static_cast<std::int32_t>(slot);
}

// 0x004e7b60: write to a slot's file
std::int32_t PcSystem::pc_write(std::int32_t handle, std::uint32_t data, std::uint32_t size) {
    if (handle != -1 && byte_at(m_, kBackendState + static_cast<std::uint32_t>(handle)) != 0) {
        if (byte_at(m_, kBackendState + static_cast<std::uint32_t>(handle)) == 2) {
            return static_cast<std::int32_t>(crt_fwrite(data, 1, size, word_at(m_, kBackendFile + 4 * static_cast<std::uint32_t>(handle))));
        }
        log_line(0x0054b3b4);
        return 0;       // (retail returns a stack word here)
    }
    log_line(0x0054b398);
    return -1;
}

// 0x004e7500
void PcSystem::pc_remove(std::uint32_t name) {
    crt_remove(path_in(kInstallDir) + m_.cstring(kSeparator) + path_in(name), kStackLocal);
}

// 0x004e74f0
void PcSystem::game_shutdown() { engine(0x004e9b80, {0x006a72bc}); }

// ---- pc\d3d*.cpp -----------------------------------------------------------------------------------------------------------------------------------------------------------

// 0x004d3600: a viewport rectangle in the PlayStation's 512x240 space (x, y, width, height as 16-bit fields at +8..+0xe; y is first reduced to a byte, keeping its sign) scaled to the
// screen; a rectangle that does not fit the screen is dropped
void PcSystem::submit_viewport_rect(std::uint32_t rect) {
    std::uint32_t y_field = static_cast<std::uint32_t>(short_at(m_, rect + 10)) & 0x800000ffu;
    if (static_cast<std::int32_t>(y_field) < 0) {
        y_field = ((y_field - 1) | 0xffffff00u) + 1;
    }
    set_half_at(m_, rect + 10, y_field);
    const std::int32_t width = global_int(m_, kScreenWidth);
    const std::int32_t height = global_int(m_, kScreenHeight);
    const auto times = [](std::int32_t a, std::int32_t b) { return static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b); };
    std::int32_t x = static_cast<std::int32_t>(times(short_at(m_, rect + 8), width) >> 9);
    std::int32_t y = static_cast<std::int32_t>(times(static_cast<std::int16_t>(y_field), height) / 240u);
    std::int32_t w = static_cast<std::int32_t>(times(short_at(m_, rect + 12), width) >> 9);
    std::int32_t h = static_cast<std::int32_t>(times(short_at(m_, rect + 14), height) / 240u);
    auto clamp = [](std::int32_t value, std::int32_t limit) { return value < 0 ? 0 : (value > limit ? limit : value); };
    x = clamp(x, width);
    w = clamp(w, width);
    y = clamp(y, height);
    h = clamp(h, height);
    if (w + x <= width && h + y <= height) {
        engine(seam::hal::D3D_SetViewportRect, {static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h)});
    }
}

// 0x004d0bc0: begin a scene no sooner than two simulation ticks after the last one (sleeping until the clock moves), over the level's sky colour in the first level
void PcSystem::begin_scene(std::uint32_t first, std::uint32_t color) {
    constexpr std::uint32_t kTick = 0x0056e31c;
    constexpr std::uint32_t kLastTick = 0x0056e840;
    constexpr std::uint32_t kFrameAccumulator = 0x006a0590;       // the double the tick word is the truncation of
    const std::uint32_t limiter = framepacing::limiter_ticks();   // 2 in retail (30 fps)
    const bool sub_tick = framepacing::pacing().sub_tick;         // F2A (120 fps): the wait is half a tick, measured on the accumulator against the last scene's exact time (host-side)
    const auto waits = [&] {
        return sub_tick ? framepacing::scene_waits(guest::read_scalar<double>(m_, kFrameAccumulator)) : global_word(m_, kTick) < global_word(m_, kLastTick) + limiter;
    };
    for (std::uint32_t guard = 0; waits(); ++guard) {
        if (guard > 10000) {
            throw std::runtime_error("begin_scene: the clock never moved");
        }
        engine(seam::hal::WINMAIN_Sleep, {0});
        set_global_word(m_, kTick, static_cast<std::uint32_t>(s_.value(engine_name(seam::hal::WINMAIN_Sleep), static_cast<std::int32_t>(global_word(m_, kTick)))));
    }
    if (sub_tick) {
        framepacing::scene_begins(guest::read_scalar<double>(m_, kFrameAccumulator));
    }
    set_global_word(m_, kLastTick, global_word(m_, kTick));
    if (global_word(m_, pcinput::kControlMode) == 0 && global_word(m_, 0x0056a898) == 6) {
        color = 0xff7080a0;
    }
    engine(seam::hal::D3DPoly_StartScene, {first, color});
}

// 0x004d0a20: the number of bits set
std::uint32_t PcSystem::bit_count(std::uint32_t value) {
    std::uint32_t count = 0;
    for (; value != 0; value >>= 1) {
        count += value & 1u;
    }
    return count;
}

// 0x004d0a40: the index of the lowest bit set (0 for none)
std::uint32_t PcSystem::lowest_bit(std::uint32_t value) {
    if (value == 0) {
        return 0;
    }
    std::uint32_t index = 0;
    for (; (value & 1u) == 0; value >>= 1) {
        ++index;
    }
    return index;
}

// 0x004dab30
bool PcSystem::timer_active() { return global_word(m_, 0x006a05a0) != 0; }

// 0x004dae00: every 60 calls, the frame rate from the time of the last 60 (a double the debug display reads); outside the renderer the x87 runs at 53 bits, so this is double math
void PcSystem::frame_rate_meter() {
    constexpr std::uint32_t kCounter = 0x006a05c0;
    constexpr std::uint32_t kClock = 0x006a05ac;
    constexpr std::uint32_t kLastClock = 0x006a05b4;
    constexpr std::uint32_t kRate = 0x0054a218;
    const std::int32_t before = global_int(m_, kCounter);
    set_global_word(m_, kCounter, static_cast<std::uint32_t>(before - 1));
    if (before > 0) {
        return;
    }
    const std::uint32_t elapsed = global_word(m_, kClock) - global_word(m_, kLastClock);
    set_global_word(m_, kLastClock, global_word(m_, kClock));
    set_global_word(m_, kCounter, 0x3c);
    const double product = static_cast<double>(elapsed) * static_cast<double>(float_at(m_, 0x00519990));
    const double quotient = static_cast<double>(float_at(m_, 0x0051998c)) / product;
    const double rate = static_cast<double>(float_at(m_, 0x00519988)) / quotient;
    std::uint64_t bits;
    static_assert(sizeof bits == sizeof rate);
    __builtin_memcpy(&bits, &rate, sizeof bits);
    set_global_word(m_, kRate, static_cast<std::uint32_t>(bits));
    set_global_word(m_, kRate + 4, static_cast<std::uint32_t>(bits >> 32));
}

// 0x004ec930: the DirectInput mouse wrapper's bounds {+0x14 left, +0x18 top, +0x1c right, +0x20 bottom}; the position {+4, +8} is pulled inside them
void PcSystem::mouse_set_bounds(std::uint32_t self, std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom) {
    std::int32_t x = int_at(m_, self + 4);
    set_word_at(m_, self + 0x14, static_cast<std::uint32_t>(left));
    set_word_at(m_, self + 0x1c, static_cast<std::uint32_t>(right));
    set_word_at(m_, self + 0x18, static_cast<std::uint32_t>(top));
    set_word_at(m_, self + 0x20, static_cast<std::uint32_t>(bottom));
    std::int32_t clamped = left;
    if (left <= x) {
        clamped = x;
        if (right < x) {
            clamped = right;
        }
    }
    set_word_at(m_, self + 4, static_cast<std::uint32_t>(clamped));
    const std::int32_t y = int_at(m_, self + 8);
    if (top <= y) {
        set_word_at(m_, self + 8, static_cast<std::uint32_t>(y <= bottom ? y : bottom));
    } else {
        set_word_at(m_, self + 8, static_cast<std::uint32_t>(top));
    }
}

// ---- pc\d3dpoly.cpp ---------------------------------------------------------------------------------------------------------------------------------------------------

// 0x004d3a70: push a value on the reversed ordering-table list: an 8-byte cell {value, previous} from the heap (a heap overrun is logged and the cell dropped)
void PcSystem::reverse_push(std::uint32_t value) {
    constexpr std::uint32_t kNext = 0x006a0074;
    constexpr std::uint32_t kEnd = 0x006a0078;
    constexpr std::uint32_t kHead = 0x006a007c;
    const std::uint32_t cell = global_word(m_, kNext);
    set_global_word(m_, kNext, cell + 8);
    if (cell + 8 > global_word(m_, kEnd)) {
        log_line(0x00549ee4);
        return;
    }
    set_word_at(m_, cell + 4, global_word(m_, kHead));
    set_word_at(m_, cell, value);
    set_global_word(m_, kHead, cell);
}

// 0x004d3a40: the reversed copy of a singly linked list (each node's first word is the next node): the heap is emptied and the nodes pushed in order
void PcSystem::build_reverse_command_list(std::uint32_t first) {
    set_global_word(m_, 0x006a007c, 0);
    set_global_word(m_, 0x006a0074, global_word(m_, 0x006a0070));
    for (std::uint32_t node = first; node != 0; node = word_at(m_, node)) {
        reverse_push(node);
    }
}

// 0x004d3ab0: make `count` 8-byte cells at `base` into a free list (each cell's first word points at the next, the last at nothing)
void PcSystem::init_list_heap(std::uint32_t base, std::int32_t count) {
    const std::uint32_t bytes = static_cast<std::uint32_t>(count) * 8u;
    m_.fill(base, 0, bytes);
    std::uint32_t cell = base;
    for (std::int32_t k = count; k > 0; --k, cell += 8) {
        set_word_at(m_, cell, cell + 8);
    }
    set_word_at(m_, base + bytes - 8, 0);
}

// 0x004d40d0: close the scene: the floating-point control word is put to the 24-bit mode the renderer needs for the software back end, then restored
void PcSystem::end_scene() {
    constexpr std::uint32_t kSceneActive = 0x006a04c4;
    if (global_byte(m_, kSceneActive) == 0) {
        return;
    }
    set_global_byte(m_, kSceneActive, 0);
    s_.note("crt_controlfp", {0x20000, 0x30000});
    if (global_word(m_, kSoftwareRenderer) != 0) {
        engine(seam::hal::FUN_004f23d0);
    }
    s_.note("crt_controlfp", {0x9001f, 0xfffff});
}

// 0x004d6090 / 0x004d60c0 / 0x004d60f0: a solid rectangle from a 20-byte description with its size field set to 1, 8 or 16 (both halves of the fourth word)
void PcSystem::draw_rectangle(std::uint32_t record, std::uint32_t size) {
    std::vector<std::uint8_t> bytes = m_.bytes(record, 20);
    bytes[12] = static_cast<std::uint8_t>(size);
    bytes[13] = static_cast<std::uint8_t>(size >> 8);
    bytes[14] = static_cast<std::uint8_t>(size);
    bytes[15] = static_cast<std::uint8_t>(size >> 8);
    s_.note(engine_name(seam::hal::D3D_DrawSolidRectangle), {kStackLocal}, std::string(bytes.begin(), bytes.end()));
}

// 0x004d7430 / 0x004d7480: the polygon heap (0x2af80 bytes) and its end
void PcSystem::init_poly_buffer() {
    s_.note("crt_new", {0x2af80});
    const std::uint32_t block = static_cast<std::uint32_t>(s_.value("crt_new"));
    set_global_word(m_, 0x006a0070, block);
    set_global_word(m_, 0x006a0078, block + 0x2af80);
}

void PcSystem::free_poly_buffer() {
    if (global_word(m_, 0x006a0070) != 0) {
        s_.note("crt_delete", {global_word(m_, 0x006a0070)});
        set_global_word(m_, 0x006a0070, 0);
    }
}

// ---- pc\d3dtex.cpp --------------------------------------------------------------------------------------------------------------------------------------------------------

// 0x004da3e0: link a texture record and a scene material to each other
void PcSystem::attach_to_material(std::uint32_t material, std::uint32_t record) {
    if (material != 0) {
        set_word_at(m_, material + 0x14, record);
    }
    if (record != 0) {
        set_word_at(m_, record + 4, material);
    }
}

// 0x004da400: free the texture record of a material (a null record is reported)
void PcSystem::release_material_texture(std::uint32_t record) {
    check(record != 0, 0x0054a138);
    if (record != 0) {
        engine(seam::hal::D3DTex_DestroyRecord, {record});
        set_global_word(m_, 0x006a051c, static_cast<std::uint32_t>(s_.value(engine_name(seam::hal::D3DTex_DestroyRecord), global_int(m_, 0x006a051c))));          // (the destroyed record leaves the list: the head moves on when it was the head)
    }
}

// 0x004d8b20: destroy every texture record, the list head moving on as each is freed
void PcSystem::destroy_all_textures() {
    constexpr std::uint32_t kHead = 0x006a051c;
    if (global_word(m_, kHead) == 0) {
        return;
    }
    std::uint32_t guard = 0;
    do {
        if (++guard > 10000) {
            throw std::runtime_error("destroy_all_textures: the list never emptied");
        }
        engine(seam::hal::D3DTex_DestroyRecord, {global_word(m_, kHead)});
        set_global_word(m_, kHead, static_cast<std::uint32_t>(s_.value(engine_name(seam::hal::D3DTex_DestroyRecord), global_int(m_, kHead))));
    } while (global_word(m_, kHead) != 0);
    set_global_word(m_, 0x006a0520, global_word(m_, kHead));
    set_global_word(m_, 0x006a04d4, global_word(m_, kHead));
}

// 0x004dab20
void PcSystem::texture_reset() {
    destroy_all_textures();
    engine(0x004da550);
}

// 0x004d9f10: a 4-bit bitmap was asked for, which this loader does not do (logged, with the file when it is named)
std::uint32_t PcSystem::bmp4_unsupported(std::uint32_t name) {
    log_line(name != 0 ? 0x0054a10c : 0x0054a0e4);
    return 0;
}

// 0x004d8c60: a bitmap's pixels into a texture record (kind 1: indexed; kind 2: 4-bit, refused), then the record is attached to the material
void PcSystem::load_bitmap(std::uint32_t record, std::uint32_t image, std::uint32_t material, std::uint32_t extra, std::uint32_t kind, std::uint32_t keyed) {
    if ((keyed & 0xff) != 0) {
        set_word_at(m_, record + 0x10, word_at(m_, record + 0x10) | 0x80);
    }
    if ((kind & 0xff) == 1) {
        engine(seam::hal::D3DTex_LoadIndexedBitmap, {image, record, 0, extra});
        attach_to_material(material, record);
        return;
    }
    if ((kind & 0xff) == 2) {
        bmp4_unsupported(extra);
    }
    attach_to_material(material, record);
}

// 0x004d8cd0: the texture record of an image whose bitmap is a file: the file's 14-byte header and 40-byte info are read (only a 24-bit uncompressed bitmap is taken, its size
// replacing the record's), the pixels are uploaded and the record attached to the material
std::uint32_t PcSystem::prepare_texture(std::uint32_t image, std::uint32_t upload_arg, std::uint32_t material, std::uint32_t keyed) {
    const std::uint32_t record = engine_value(0x004d9880);
    set_word_at(m_, record + 8, image + 0x14);
    set_half_at(m_, record + 0x14, half_at(m_, image + 0x10));
    set_half_at(m_, record + 0x16, half_at(m_, image + 0x12));
    set_word_at(m_, record + 0xc, word_at(m_, material + 0x18));
    const std::uint32_t flags = word_at(m_, record + 0x10) | 0x12;
    set_word_at(m_, record + 0x10, flags);
    if ((keyed & 0xff) != 0) {
        set_word_at(m_, record + 0x10, flags | 0x80);
    }
    const std::uint32_t handle = engine_value(0x004d8df0, {word_at(m_, material + 0x18), 0});
    if (handle != 0xffffffffu) {
        // FileIO_BackendRead(handle, buffer, count): the probe scripts the bytes as dwords
        auto read = [&](std::uint32_t scratch, std::uint32_t count) {
            if (s_.live()) {
                engine(0x004e7ad0, {handle, scratch, count});      // a live read writes the bytes where they are wanted
                return;
            }
            engine(0x004e7ad0, {handle, kStackLocal, count});
            std::vector<std::uint8_t> bytes;
            for (std::uint32_t k = 0; k < (count + 3) / 4; ++k) {
                const auto word = static_cast<std::uint32_t>(s_.value(engine_name(0x004e7ad0)));
                for (std::uint32_t b = 0; b < 4; ++b) {
                    bytes.push_back(static_cast<std::uint8_t>(word >> (8 * b)));
                }
            }
            bytes.resize(count);
            m_.write(scratch, bytes);
        };
        read(kScratchOut, 0xe);
        read(kScratchOut + 0x10, 0x28);
        const std::uint32_t info = kScratchOut + 0x10;
        if (half_at(m_, info + 0xe) != 0x18 || word_at(m_, info + 0x10) != 0) {
            log_line(0x00549f90);
            attach_to_material(material, 0);
            return 0;
        }
        const std::uint16_t old_width = half_at(m_, record + 0x14);
        const std::uint16_t old_height = half_at(m_, record + 0x16);
        set_half_at(m_, record + 0x1c, old_width);
        set_half_at(m_, record + 0x16, half_at(m_, info + 8));
        set_half_at(m_, record + 0x1e, old_height);
        set_half_at(m_, record + 0x14, half_at(m_, info + 4));
        set_word_at(m_, record + 0x10, word_at(m_, record + 0x10) | 8);
        set_word_at(m_, record + 0x20, 1);
        upload_arg = 0;
        engine(0x004e7bc0, {handle});
    }
    engine(0x004d8f10, {record, upload_arg, 0, 0});
    attach_to_material(material, record);
    return record;
}

// 0x004d98d0: a texture record for pixels the caller supplies (width, height, the pixel block and its two flags): uploaded, attached, and its extra pointer cleared
std::uint32_t PcSystem::create_external_record(std::uint32_t pixels, std::uint32_t width, std::uint32_t height, std::uint32_t palette, std::uint32_t colours, std::uint32_t material) {
    const std::uint32_t record = engine_value(0x004d9880);
    set_half_at(m_, record + 0x16, height);
    set_word_at(m_, record + 8, pixels);
    set_half_at(m_, record + 0x14, width);
    set_word_at(m_, record + 0x10, word_at(m_, record + 0x10) | 0x12);
    set_word_at(m_, record + 0xc, 0);
    // retail's palette descriptor for the upload is a stack local {checksum -1, colour count, entries} (0x004d9916..0x004d9926); the upload reads its count and
    // entries (milestone 7c: with the words unwritten, every external texture, the font glyphs among them, uploaded blank). A replay scripts the upload and has
    // no scratch window, so only a live call writes them.
    if (s_.live()) {
        set_global_word(m_, kStackLocal, 0xffffffffu);
        set_global_word(m_, kStackLocal + 4, colours);
        set_global_word(m_, kStackLocal + 8, palette);
    }
    set_word_at(m_, record + 0x20, kStackLocal);
    engine(0x004d8f10, {record, 0xffffffff, 0, 1});
    attach_to_material(material, record);
    set_word_at(m_, record + 0x20, 0);
    return record;
}

// 0x004d7db0: the software renderer's frame (width x height 16-bit pixels at the object's +0x18) copied row by row to a surface whose rows are `pitch` bytes apart
void PcSystem::copy_screen(std::uint32_t self, std::uint32_t destination, std::uint32_t pitch) {
    std::uint32_t source = word_at(m_, self + 0x18);
    if (global_word(m_, kSoftwareRenderer) == 0) {
        return;
    }
    const std::uint32_t rows = global_word(m_, kScreenHeight);
    for (std::uint32_t row = 0; row < rows; ++row) {
        const std::uint32_t bytes = global_word(m_, kScreenWidth) * 2u;
        m_.copy(destination, source, bytes);
        destination += static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(pitch)));
        source += global_word(m_, kScreenWidth) * 2u;
    }
}

// ---- small wrappers ----------------------------------------------------------------------------------------------------------------------------------------------------

// 0x0040b5c0 / 0x0040b610: a camera object's method at 0x0040e000 with (0, 0, 0, 0x1000)
void PcSystem::camera_reset(std::uint32_t camera) { engine(0x0040e000, {camera, 0, 0, 0, 0x1000}); }

// 0x0044b1c0: the falling-text effect object at 0x005617a0 constructed
void PcSystem::falling_text_construct() { engine(0x004b9730, {0x005617a0}); }

// 0x004bb220: the version tag of a record (its text from +4 when it reads "V..." with a point after three characters), else none
std::uint32_t PcSystem::version_tag(std::uint32_t record) {
    const std::uint32_t tag = record + 4;
    return (byte_at(m_, tag) == 'V' && byte_at(m_, tag + 3) == '.') ? tag : 0;
}

// 0x004d4120: the gamma tables (red <<16, green <<8, blue) of 256 entries: level i becomes 255 * (i / 255) ^ (1 / gamma) rounded to the nearest, capped at 255. The power is the x87
// intrinsic; the tables are built outside the renderer (53-bit mode)
void PcSystem::build_gamma_table(std::uint32_t gamma_bits) {
    float gamma;
    __builtin_memcpy(&gamma, &gamma_bits, sizeof gamma);
    const float exponent = static_cast<float>(static_cast<double>(float_at(m_, 0x00518d70)) / static_cast<double>(gamma));
    for (std::uint32_t level = 0; level < 256; ++level) {
        const double base = static_cast<double>(level) * static_cast<double>(float_at(m_, 0x00518d74));
        const double power = static_cast<double>(powl(static_cast<long double>(base), static_cast<long double>(exponent)));
        const double scaled = power * static_cast<double>(float_at(m_, 0x00519938)) + static_cast<double>(float_at(m_, 0x00519948));
        std::uint32_t value = static_cast<std::uint32_t>(static_cast<std::int32_t>(scaled));
        if (value > 0xff) {
            value = 0xff;
        }
        set_word_at(m_, 0x006a00b4 + 4 * level, value << 16);
        set_word_at(m_, 0x005dd340 + 4 * level, value << 8);
        set_word_at(m_, 0x0069fc64 + 4 * level, value);
    }
}

// 0x004d44d0: do the screen points of a polygon (x, y floats at the start of each vertex, 0x1c bytes apart for the lit kind, 0x14 for the flat) spread wider or taller than the
// limits? (true: too large to draw as one piece)
bool PcSystem::polygon_too_large(std::uint32_t vertices, std::int32_t count, std::uint32_t lit) {
    const std::uint32_t stride = (lit & 0xff) != 0 ? 0x1c : 0x14;
    float min_x = float_at(m_, vertices);
    float max_x = min_x;
    float min_y = float_at(m_, vertices + 4);
    float max_y = min_y;
    std::uint32_t at = vertices + stride;
    for (std::int32_t left = count; left > 1; --left, at += stride) {
        const float x = float_at(m_, at);
        const float y = float_at(m_, at + 4);
        if (x < min_x) {
            min_x = x;
        }
        if (max_x < x) {
            max_x = x;
        }
        if (y < min_y) {
            min_y = y;
        }
        if (!(y <= max_y)) {
            max_y = y;
        }
    }
    if (static_cast<double>(max_x) - static_cast<double>(min_x) > static_cast<double>(float_at(m_, 0x00519960))) {
        return true;
    }
    return static_cast<double>(max_y) - static_cast<double>(min_y) > static_cast<double>(float_at(m_, 0x0051995c));
}

// ---- the video options window (vidmenu): the option rows and their pad -----------------------------------------------------------------------------------------------------

// 0x004cebb0: change option `option` (0 renderer, 1 resolution, 2 gamma) of the window: by `delta`, or to the value `absolute` (0xffff: keep), kept inside the option's list (or
// -15..15 for the gamma), and its text pushed to the row's element (a virtual call)
void PcSystem::video_set_option(std::uint32_t self, std::int32_t option, std::int32_t delta, std::uint32_t absolute) {
    constexpr std::uint32_t kValues = 0x0056e7c0;          // three options of 12 bytes: the value, then +8 the list of names
    constexpr std::uint32_t kGammaText = 0x0056e780;
    std::int32_t low;
    std::int32_t high;
    std::uint32_t list = 0;
    if (option == 2) {
        low = -0xf;
        high = 0xf;
    } else {
        low = 0;
        list = word_at(m_, kValues + 12u * static_cast<std::uint32_t>(option) + 8);
        std::int32_t last = 0;
        for (std::uint32_t entry = list; word_at(m_, entry) != 0; entry += 4) {
            ++last;
        }
        high = last - 1;
    }
    const std::uint32_t slot = kValues + 12u * static_cast<std::uint32_t>(option);
    std::int32_t value = int_at(m_, slot);
    if (delta == 0) {
        if (absolute != 0xffff) {
            value = static_cast<std::int32_t>(absolute);
        }
    } else {
        value += delta;
    }
    if (high < value) {
        value = high;
    }
    if (value < low) {
        value = low;
    }
    std::uint32_t text;
    if (option == 2) {
        text = kGammaText;
        set_string(kGammaText, crt::format(m_.cstring(0x00525764), {}, {value}));
    } else {
        text = word_at(m_, list + 4u * static_cast<std::uint32_t>(value));
    }
    set_word_at(m_, slot, static_cast<std::uint32_t>(value));
    const std::uint32_t element = word_at(m_, self + 0x38c + 4u * static_cast<std::uint32_t>(option));
    if (element != 0) {
        (void)virtual_call(element, 9, {text, 1, 0x104, pc_system_video_at(m_, self).field_3b0()}, m_.cstring(text, 0x100));
    }
}

// 0x004cec70: draw the video options window: its list, the pad poll, the list's two methods, the window and the sprite quads of its background
void PcSystem::video_draw(std::uint32_t self) {
    const std::uint32_t list = pc_system_video_at(m_, self).field_388();
    (void)virtual_call(list, 3);
    const std::uint32_t vtable = word_at(m_, list);
    const std::uint32_t poll = engine_value(0x0046f550, {0, 0, 6});
    engine(word_at(m_, vtable + 0x14), {list, poll});
    (void)virtual_call(list, 4);
    engine(0x004ac770, {self, 0, 0});
    engine(0x004d7990, {pc_system_video_at(m_, self).field_238(), word_at(m_, global_word(m_, 0x00560fd4) + 0x88), 0, 0});
}

// 0x004cefb0: a pad press on the window: left (or the first button) / right sets *out to -1 / +1, marks the window changed and sounds it; 0 when nothing was pressed
bool PcSystem::video_pad_choice(std::uint32_t self, std::uint32_t out) {
    const bool moved = global_byte(m_, 0x0056b029) != 0 || global_byte(m_, 0x0056b0d9) != 0;
    const bool back = global_byte(m_, 0x0056aff9) != 0;
    if (!moved && !back) {
        return false;
    }
    const bool minus = moved ? back : true;
    set_word_at(m_, out, minus ? 0xffffffffu : 1u);
    pc_system_video_at(m_, self).set_field_e4(1);
    s_.note("sfx", {minus ? 0x92u : 0x91u, 0x2000, 0});
    return true;
}

// 0x004cece0: one step of the window: the highlighted row's option moves by the left / right press, the lists are rebuilt when the renderer changed, the gamma applied
void PcSystem::video_step(std::uint32_t self) {
    const std::uint32_t id = engine_value(0x00470ab0, {pc_system_video_at(m_, self).field_388()});
    const std::uint32_t slot = 0x0056e7c0 + 12u * id;
    const std::uint32_t before = word_at(m_, slot);
    if (global_byte(m_, 0x0056b079) != 0) {
        video_set_option(self, static_cast<std::int32_t>(id), -1, 0xffff);
        set_global_byte(m_, 0x0056b079, 0);
    } else {
        if (global_byte(m_, 0x0056b089) == 0) {
            engine(0x004ac730, {self});
            return;
        }
        video_set_option(self, static_cast<std::int32_t>(id), 1, 0xffff);
        set_global_byte(m_, 0x0056b089, 0);
    }
    if (before != word_at(m_, slot)) {
        if (id == 0) {
            video_build_mode_names(self, 0);
            engine(0x004ac730, {self});
            return;
        }
        if (id == 2) {
            engine(0x004ce260);
        }
    }
    engine(0x004ac730, {self});
}

// 0x004ced80: the resolutions the window offers for the chosen renderer (software: 320x240 and 512x384; hardware: 640x480 up to 1280x1024, those the card can do), the current
// one picked again by name unless `keep`, and the row's flag set to say which
void PcSystem::video_build_mode_names(std::uint32_t self, std::uint32_t keep) {
    const bool software = global_word(m_, kRenderer) == 1;
    std::string current;
    if (keep == 0) {
        current = m_.cstring(word_at(m_, kModeNames + 4 * global_word(m_, kModeIndex)), 0x40);
    }
    std::uint32_t count = 1;
    auto add = [&](std::uint32_t name_variable) { set_word_at(m_, kModeNames + 4 * count, word_at(m_, name_variable)); ++count; };
    if (software) {
        set_global_word(m_, kModeNames, global_word(m_, 0x0054859c));
        if (can_do_mode(0x200, 0x180, 1)) {
            set_global_word(m_, kModeNames + 4, global_word(m_, 0x00548598));
            count = 2;
        }
    } else {
        set_global_word(m_, kModeNames, global_word(m_, 0x00548594));
        if (can_do_mode(0x320, 0x258, 0)) {
            set_global_word(m_, kModeNames + 4, global_word(m_, 0x00548590));
            count = 2;
        }
        if (can_do_mode(0x400, 0x300, 0)) {
            add(0x0054858c);
        }
        if (can_do_mode(0x500, 0x400, 0)) {
            add(0x00548588);
        }
    }
    set_word_at(m_, kModeNames + 4 * count, 0);
    if (keep == 0) {
        std::int32_t found = 0;
        for (std::uint32_t k = 0; k < count; ++k) {
            if (crt::equal_nocase(current, m_.cstring(word_at(m_, kModeNames + 4 * k), 0x40))) {
                found = static_cast<std::int32_t>(k);
                break;
            }
        }
        video_set_option(self, 1, 0, static_cast<std::uint32_t>(found));
    }
    if (pc_system_video_at(m_, self).field_398() != 0) {
        set_byte_at(m_, pc_system_video_at(m_, self).field_398() + 0xc, software ? 1 : 0);
    }
}

}  // namespace opentony::runtime::pcplatform
