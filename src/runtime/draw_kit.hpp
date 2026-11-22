#pragma once

#include "../hal/video.h"
#include "host_options.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <cstdint>
#include <vector>

namespace opentony::runtime::drawkit {

// Milestone 7c: the draw path of a program with no retail image, on the retail memory model. Retail's D3D_DrawPolygonList walks the frame's ordering table and
// dispatches each packet through D3DPrimitiveDispatchTable (0x0065f82c) to the D3D_Draw* functions, which drive Direct3D 7. The hooked build replaces that walk with an
// adapter over the live process (src/hooks/hal/video_shadow.cpp, the same draws as retail on 1,306 Warehouse frames); this Kit is that adapter again on RetailMemory, drawing
// into the HAL backend the program sets, with the textures of the surface store (the service `ddraw_hal_texture_for`). The scene bracket around it (D3DPoly_StartScene,
// D3D_PresentFrame) and Db_ClearOrderingTable are here too.
//
// Besides the draws, the Kit writes the retail words the walk and the scene functions write, as their disassembly shows (the scene flags and count, the fog threshold and
// yon scale, the device-state caches 0x00549da0..0x00549db8, D3DPolyAlpha, TexpageBlendMode, the packet words 0x0065fc5c / 0x006a0064, the primitive count 0x006a0068,
// the texture descriptor word 0x006a0080, and the handlers' tails: the bound texture 0x006a04c0, the bindings 0x006a0084, the address-mode byte 0x006a04b4).
// tony/standalone_frame.py checks them: after 60 ticks of Options the memory image equals retail's but for stated masks. Milestone 8d added the D3DTLVERTEX staging buffers of
// the PSX handlers (0x00549dc0 textured, 0x00549e30 untextured), which hold the last primitive drawn (7c could not see their absence: the last primitive of the Options frame is
// the mouse cursor, the same as in the start image); tony/draw_oracle.py compares every word of the draw path with retail's functions on one-packet lists. Not written: the
// render and texture-stage states retail sends to the device (the HAL draw state is explicit per draw). Only the hardware renderer exists: a set D3DSoftwareRenderer stops
// the program.

// ---- retail's words ---------------------------------------------------------------------------------------------------------
constexpr std::uint32_t kSceneActive = 0x006a04c4;        // byte: D3DSceneActive
constexpr std::uint32_t kSceneListDrawn = 0x006a04c5;     // byte: D3DSceneListDrawn
constexpr std::uint32_t kSceneCount = 0x006a04b8;         // scenes begun
constexpr std::uint32_t kSceneWord = 0x006a04c0;          // cleared when a scene begins
constexpr std::uint32_t kStagesSet = 0x006a04b4;          // byte: set when a scene begins on the hardware renderer
constexpr std::uint32_t kTexpageBlendMode = 0x006a04bc;
constexpr std::uint32_t kCacheA = 0x00549da0;             // the device-state caches D3DPoly_StartScene resets (-1, then 0 on the hardware renderer)
constexpr std::uint32_t kColorBlendCache = 0x00549da4;    // D3D_SetColorBlendMode's mode
constexpr std::uint32_t kCacheC = 0x00549da8;
constexpr std::uint32_t kDepthCache = 0x00549dac;         // D3D_SetTextureBlendMode's mode: 0 no depth test, 1 test, 2 test and write; -1 after a scene begins
constexpr std::uint32_t kAlphaStageCache = 0x00549db0;    // 1: the alpha stage modulates (a textured PSX packet), 0: it selects
constexpr std::uint32_t kCacheF = 0x00549db4;
constexpr std::uint32_t kCacheG = 0x00549db8;
constexpr std::uint32_t kBoundTexture = 0x006a04c0;       // the surface holder the device has bound (D3D_Draw* handlers; cleared when a scene begins)
constexpr std::uint32_t kBindings = 0x006a0084;           // texture bindings made
constexpr std::uint32_t kPolyAlpha = 0x0069fc60;          // D3DPolyAlpha: the alpha of the packet being drawn
constexpr std::uint32_t kPacketFlags = 0x0065fc5c;        // 0 for a PSX packet, then 8 | the semi-transparency mode << 4
constexpr std::uint32_t kPacketRaw = 0x006a0064;          // byte: the packet's "raw texture" bit (command & 1)
constexpr std::uint32_t kPrimitiveCount = 0x006a0068;     // polygons the D3D_Draw* handlers were given
constexpr std::uint32_t kTextureDescriptor = 0x006a0080;  // the descriptor (+0x14) of the last texture record a packet named
constexpr std::uint32_t kColorLevelRed = 0x006a00b4;      // three 256-entry tables: a colour channel at the device's level, in place
constexpr std::uint32_t kColorLevelGreen = 0x005dd340;
constexpr std::uint32_t kColorLevelBlue = 0x0069fc64;
constexpr std::uint32_t kRedFog = 0x0061d82c;             // four 256 x 256 tables (channel x fog level) that D3D_SetupFog fills
constexpr std::uint32_t kGreenFog = 0x0065fc60;
constexpr std::uint32_t kAlphaFog = 0x0059d340;
constexpr std::uint32_t kBlueFog = 0x005dd740;
constexpr std::uint32_t kFogThreshold = 0x00549d9c;       // float
constexpr std::uint32_t kFogYonScale = 0x00549d98;        // float
constexpr std::uint32_t kDpqMin = 0x00563a44;
constexpr std::uint32_t kViewportEdges = 0x00563a38;      // RenderViewportEdges (a pointer)
constexpr std::uint32_t kMovieRecord = 0x006a69f8;        // the movie's texture record (Bink's surface, set by CreateBinkSurface; audio_movie.cpp's draw_movie_window queues a quad with it)
constexpr std::uint32_t kUnitScale = 0x00518d70;         // RenderProjectionUnitScale (float, 1.0)
constexpr std::uint32_t kVideoFogYonScale = 0x005485a0;   // float
constexpr std::uint32_t kTextureMode = 0x006a6964;        // D3DTextureMode
constexpr std::uint32_t kSoftwareRenderer = 0x029da3a0;   // D3DSoftwareRenderer
constexpr std::uint32_t kStagingTextured = 0x00549dc0;    // the D3DTLVERTEX staging buffers of the PSX handlers: four 28-byte entries, then four 20-byte ones
constexpr std::uint32_t kStagingPlain = 0x00549e30;
constexpr std::uint32_t kScreenWidth = 0x029da394;
constexpr std::uint32_t kScreenHeight = 0x029da398;

// ---- the program's side ----------------------------------------------------------------------------------------------------
// Where the draws go and what happens at a present. Set by the program (the standalone DLL's exports); the default draws nothing and records nothing, which is what a
// replay or the hooked build gets (the memory effects remain).
struct Output {
    const OtHalVideoBackend* video = nullptr;
    bool record = false;                                       // keep each frame's primitive stream (stream())
    void (*on_present)(std::uint32_t frame, void* user) = nullptr;   // after the HAL presented frame `frame` (0 first)
    void* user = nullptr;
    std::uint32_t (*movie_texture)() = nullptr;                // the HAL texture of the open in-game clip, current, or 0: what the quad of the movie's record (0x006a69f8) shows
};
void set_output(const Output& output);

// What the program changes of the game's picture on its way to the video (the launcher's options, standalone_options.cpp). The default changes nothing: the draws are
// retail's. None of it touches the game's memory.
struct Presentation {
    // A screen wider than 4:3 (the game's screen words say `screen_width`): the 2D layer, which the game lays out by scaling 512 PlayStation columns to the screen's
    // width, keeps the shape it has on a screen `hud_width` wide and sits in the middle. Both 0: off. Equal: the 2D layer is stretched with the screen.
    std::int32_t screen_width = 0;
    std::int32_t hud_width = 0;
    // Each texture drawn with the colour it was made for. The PC port sends every vertex colour through its gamma tables (ColorLevelRed, 0x006a00b4) and multiplies
    // the texel by it once; its replacement bitmaps (NEWTEX, NEWBMP: record flags 4 and 8) were drawn for that. A texture that is still the PlayStation's own (a
    // level's without a replacement, the skaters, the fonts, everything with the "PSX textures" option) was drawn for the PlayStation's blend, texel x colour x 2
    // with 0x80 leaving the texel as it is, and comes out dark and flat through the PC's. With this, such a texture is drawn the PlayStation's way.
    bool psx_colour = false;
};
void set_presentation(const Presentation& presentation);

// The primitive stream of the frame being drawn, as the records of an .otvh file (src/hooks/hal/video_shadow.cpp's format: type 1 draw = OtHalDrawState, topology,
// count, vertices; 2 viewport = x, y, w, h; 3 unsupported = command, reason). A draw's texture is the game's surface word (the texture record's word 0), as the shadow
// records it, not the HAL handle. Cleared when a scene begins.
[[nodiscard]] const std::vector<std::uint8_t>& stream();
[[nodiscard]] std::uint32_t stream_records();
// the .otvh file of the stream: header {"OTVH", 1, frame, records} and the records
[[nodiscard]] std::vector<std::uint8_t> stream_file(std::uint32_t frame);

struct Counters {
    std::uint32_t draws = 0;
    std::uint32_t unsupported = 0;
    std::uint32_t presents = 0;
    std::uint32_t packets = 0;           // packets walked (word 4 set)
    std::uint32_t no_texture = 0;        // textured PSX packets without a record, a descriptor or a surface: not drawn, as in retail
    std::uint32_t oversized = 0;         // dropped by the size guard
    std::uint32_t faded = 0;             // general polygons whose every vertex faded out
};
[[nodiscard]] const Counters& counters();

class DrawKit final {
public:
    DrawKit(RetailMemory& memory, RetailServices& services) : m_(memory), s_(services) {}

    void clear_ordering_table(std::uint32_t table, std::uint32_t count);   // 0x004d3b00 Db_ClearOrderingTable
    void start_scene(std::uint32_t fog_colour, std::uint32_t clear_colour);   // 0x004d3b40 D3DPoly_StartScene
    void draw_polygon_list(std::uint32_t list);                         // 0x004d3160 D3D_DrawPolygonList
    void present_frame();                                               // 0x004d0c30 D3D_PresentFrame

    // Milestone 8c: the seams of the draw path as natives. The D3D_Draw* handlers (0x004d42f0..0x004d68b0) are what the retail walk calls for one packet with the packet's
    // state already set (kPacketFlags, kPolyAlpha, the caches): `kind` is the PSX command (0x20 F3 ... 0x50 LINE_G2, 0x60 the rectangle, 0xb0 the general polygon). A program
    // that calls one directly gets the draw `walk` would have made after setting that state, from the words as they are.
    void draw_handler(std::uint32_t kind, std::uint32_t packet);
    void set_viewport_rect(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);   // 0x004d36c0 D3D_SetViewportRect
    void clear_buffers();                                                                               // 0x004d0ec0 D3D_ClearBuffers: the back buffer black

private:
    void walk(std::uint32_t list);
    void general_polygon(std::uint32_t packet);
    void psx_polygon(std::uint32_t layout, std::uint32_t packet, bool raw, std::uint32_t alpha, int colour_mode, int depth_mode);
    void psx_tile(std::uint32_t packet, std::int32_t size, std::uint32_t alpha, int colour_mode, int depth_mode);
    void viewport(std::uint32_t packet);
    void stage(bool textured, std::uint32_t count, bool closes);         // the handlers' D3DTLVERTEX staging buffers (8d)
    void handler_tail(std::uint32_t descriptor);                        // the caches and the texture binding of a drawn PSX primitive
    void bind_texture(std::uint32_t descriptor);
    void general_texture_tail(std::uint32_t flags, std::uint32_t descriptor);
    void emit(OtHalDrawState state, std::uint32_t topology, std::uint32_t count);
    void fit_flat(std::uint32_t count);                                  // Presentation: a 2D primitive's corners from the wide screen to the middle
    void place_hud_group(HostOptions::HudGroup& group, float screen, float hud, float height);   // the point an element of the in-level HUD is placed about (host_options.hpp)
    void fit_viewport(OtHalRect& area);                                // Presentation: a part-width view's rectangle to its place in the squeezed 2D layout
    void unsupported(std::uint32_t command, std::uint32_t reason);
    void colour_blend_mode(std::uint32_t mode);                         // D3D_SetColorBlendMode's cache
    void depth_mode(std::uint32_t mode);                                // D3D_SetTextureBlendMode's cache
    [[nodiscard]] std::uint32_t level(std::uint32_t colour) const;
    [[nodiscard]] std::uint32_t packet_colour(std::uint32_t packet, std::uint32_t offset, std::uint32_t alpha) const;
    [[nodiscard]] int fog_level(float rhw) const;
    [[nodiscard]] std::uint32_t fogged(std::uint32_t colour, std::uint32_t alpha, int fog) const;
    [[nodiscard]] float f32(std::uint32_t address) const;
    [[nodiscard]] std::uint32_t hal_texture(std::uint32_t surface);
    void software_renderer_check() const;

    RetailMemory& m_;
    RetailServices& s_;
    OtHalVertex fan_[64]{};
    float view_shift_ = 0.0f;      // Presentation: the horizontal shift of the draw about to be emitted (a part-width view's polygon, render_presentation.hpp)
    bool texture_is_hal_ = false;   // the draw about to be emitted has a HAL texture handle in its state, not a game surface word
    std::int32_t hud_group_ = -1;  // the HUD element (host_options().hud_groups) of the packet being walked, or -1
    bool flat_next_ = false;      // fit_flat ran for the draw about to be emitted: it is a 2D layer primitive
    bool scene_projected_ = false;  // the scene being drawn has had a draw that is not of the 2D layer (a level, a model): the next scene's clear keeps the game's colour across a wide screen
    bool double_colour_ = false;  // Presentation::psx_colour: the draw being emitted is a PlayStation texture, texel x colour x 2
    std::uint32_t plain_[64]{};    // its vertex colours before the PC port's colour levels
    [[nodiscard]] bool psx_texture(std::uint32_t descriptor) const;   // Presentation::psx_colour and the texture record is not one of the PC port's bitmaps
};

}  // namespace opentony::runtime::drawkit
