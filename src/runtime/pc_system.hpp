#pragma once

#include "pad_input.hpp"
#include "guest_view.hpp"
#include "pc_platform.hpp"

#include <cstdint>
#include <string>

namespace opentony::runtime::pcplatform {

// Phase 4.13a: the game's side of the PC platform layer on the game's globals: pc\winmain.cpp (the options file, the install paths, the display mode list and the video
// options, the mode check and the shutdown), pc\thstub.cpp (the custom park files), the PC file backends (create, write, remove) and the small helpers of pc\d3d*.cpp that are
// game logic (the viewport rectangle, the begin-scene frame limiter, the frame-rate meter, the bit counters). The C runtime (fopen, fclose, fwrite, remove), the options file, the
// DirectDraw calls and the HAL are services (`crt_*`, `engine_<address>`).
namespace winmain {

constexpr std::uint32_t kModeCount = 0x029d8478;            // the display modes DirectDraw listed: {+0 height, +4 width, ...} every 0x7c bytes from 0x029d8484
constexpr std::uint32_t kModes = 0x029d8484;
constexpr std::uint32_t kModeStride = 0x7c;
constexpr std::uint32_t kVideoMemory = 0x029da380;
constexpr std::uint32_t kScreenWidth = 0x029da394;
constexpr std::uint32_t kScreenHeight = 0x029da398;
constexpr std::uint32_t kSoftwareRenderer = 0x029da3a0;
constexpr std::uint32_t kDataPath = 0x029da3c4;              // the three install paths (0x100 bytes each)
constexpr std::uint32_t kMusicPath = 0x029da4c4;
constexpr std::uint32_t kMoviePath = 0x029da5c4;
constexpr std::uint32_t kIniFile = 0x029d8378;
constexpr std::uint32_t kInstallDir = 0x006a71bc;            // the directory the game was installed in (0x200 bytes), then the disc's (0x006a73bc)
constexpr std::uint32_t kDiscDir = 0x006a73bc;
constexpr std::uint32_t kSeparator = 0x00540c34;             // "\\"
constexpr std::uint32_t kBackendState = 0x006a6d10;          // ten file slots: state byte (0 free, 1 package, 2 file), then the FILE pointers at +0xc dwords
constexpr std::uint32_t kBackendFile = 0x006a6d1c;
constexpr std::uint32_t kBackendCount = 10;

// the video options
constexpr std::uint32_t kRenderer = 0x0056e7c0;               // 0 hardware, 1 software
constexpr std::uint32_t kModeIndex = 0x0056e7cc;
constexpr std::uint32_t kGamma = 0x0056e7d8;
constexpr std::uint32_t kQuality = 0x0056e7e4;                // 0 normal, 1 high
constexpr std::uint32_t kModesBuilt = 0x0056e7b8;
constexpr std::uint32_t kModeNames = 0x0056e788;
constexpr std::uint32_t kFogScale = 0x005485a0;
constexpr std::uint32_t kTextureRootEnabled = 0x00549f24;

}  // namespace winmain

class PcSystem final : public PcKit {
public:
    PcSystem(RetailMemory& memory, RetailServices& services, PadInput& pad) : PcKit(memory, services), pad_(pad) {}

    // ---- pc\winmain.cpp ----
    bool can_do_mode(std::int32_t width, std::int32_t height, std::uint32_t half);            // 0x004f6660
    void screen_dimensions(std::uint32_t width_out, std::uint32_t height_out);               // 0x004f6d00
    void nothing() {}                                                                        // 0x004f7d80
    void shut_down();                                                                        // 0x004f86f0
    bool probe_configured_file(std::uint32_t directory, std::uint32_t file);                 // 0x004f6850
    void install_setup();                                                                    // 0x004f6920
    void load_options();                                                                     // 0x004f6b90
    void save_options();                                                                     // 0x004f6c60
    void build_display_mode_list();                                                          // 0x004cf420
    void load_video_options();                                                               // 0x004cf030
    void save_video_options();                                                               // 0x004cf300
    void dump_device_caps(std::uint32_t caps);                                               // 0x004f77f0

    // ---- pc\thstub.cpp and the PC file backends ----
    void save_custom_level(std::uint32_t name, std::uint32_t flag, std::uint32_t data, std::uint32_t size);   // 0x004f62c0
    void delete_custom_level(std::uint32_t name, std::uint32_t flag);                        // 0x004f6400
    std::uint32_t load_custom_level(std::uint32_t name, std::uint32_t menu_mode, std::uint32_t flag, std::uint32_t buffer);   // 0x004f6140
    std::int32_t pc_create(std::uint32_t name);                                              // 0x004e7900
    std::int32_t pc_write(std::int32_t handle, std::uint32_t data, std::uint32_t size);      // 0x004e7b60
    void pc_remove(std::uint32_t name);                                                      // 0x004e7500
    void game_shutdown();                                                                    // 0x004e74f0

    // ---- pc\d3d*.cpp: the helpers that are game logic ----
    void submit_viewport_rect(std::uint32_t rect);                                           // 0x004d3600
    void begin_scene(std::uint32_t first, std::uint32_t color);                              // 0x004d0bc0
    std::uint32_t bit_count(std::uint32_t value);                                            // 0x004d0a20
    std::uint32_t lowest_bit(std::uint32_t value);                                           // 0x004d0a40
    bool timer_active();                                                                     // 0x004dab30
    void frame_rate_meter();                                                                 // 0x004dae00
    void mouse_set_bounds(std::uint32_t self, std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom);   // 0x004ec930
    void build_reverse_command_list(std::uint32_t first);                                    // 0x004d3a40
    void reverse_push(std::uint32_t value);                                                  // 0x004d3a70
    void init_list_heap(std::uint32_t base, std::int32_t count);                             // 0x004d3ab0
    void end_scene();                                                                        // 0x004d40d0
    void draw_rectangle(std::uint32_t record, std::uint32_t size);                           // 0x004d6090 (1), 0x004d60c0 (8), 0x004d60f0 (16)
    void init_poly_buffer();                                                                 // 0x004d7430
    void free_poly_buffer();                                                                 // 0x004d7480
    void destroy_all_textures();                                                             // 0x004d8b20
    void texture_reset();                                                                    // 0x004dab20
    void load_bitmap(std::uint32_t record, std::uint32_t image, std::uint32_t material, std::uint32_t extra, std::uint32_t kind, std::uint32_t keyed);   // 0x004d8c60
    std::uint32_t prepare_texture(std::uint32_t image, std::uint32_t upload_arg, std::uint32_t material, std::uint32_t keyed);   // 0x004d8cd0
    std::uint32_t create_external_record(std::uint32_t pixels, std::uint32_t width, std::uint32_t height, std::uint32_t palette, std::uint32_t colours, std::uint32_t material);   // 0x004d98d0
    std::uint32_t bmp4_unsupported(std::uint32_t name);                                      // 0x004d9f10 (the fourth argument names the file)
    std::uint32_t record_field_20(std::uint32_t record) { return guest::read_scalar<std::uint32_t>(m_, record + 0x20); }    // 0x004d9f40
    void attach_to_material(std::uint32_t material, std::uint32_t record);                   // 0x004da3e0
    void release_material_texture(std::uint32_t record);                                     // 0x004da400
    void copy_screen(std::uint32_t self, std::uint32_t destination, std::uint32_t pitch);    // 0x004d7db0
    void build_gamma_table(std::uint32_t gamma_bits);                                        // 0x004d4120
    bool polygon_too_large(std::uint32_t vertices, std::int32_t count, std::uint32_t lit);   // 0x004d44d0 (fastcall: ecx the vertices, edx the count)

    // ---- the video options window (vidmenu): the option rows and their pad ----
    void video_set_option(std::uint32_t self, std::int32_t option, std::int32_t delta, std::uint32_t absolute);   // 0x004cebb0
    void video_draw(std::uint32_t self);                                                     // 0x004cec70
    void video_step(std::uint32_t self);                                                     // 0x004cece0
    void video_build_mode_names(std::uint32_t self, std::uint32_t keep);                     // 0x004ced80
    bool video_pad_choice(std::uint32_t self, std::uint32_t out);                            // 0x004cefb0

    // ---- small wrappers the platform range holds ----
    void camera_reset(std::uint32_t camera);                                                 // 0x0040b5c0 (camera 0x0055f930) and 0x0040b610 (camera 0x0055f918)
    void falling_text_construct();                                                           // 0x0044b1c0
    std::uint32_t version_tag(std::uint32_t record);                                         // 0x004bb220

private:
    std::string path_in(std::uint32_t address) { return m_.cstring(address, 0x200); }
    // the options file's loader and saver with the file's text taken from / put into a retail buffer
    std::uint32_t slot_free();
    PadInput& pad_;
};

}  // namespace opentony::runtime::pcplatform
