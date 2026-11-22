// Replays retail forced calls (Phase 4.15e) through the native renderer layer (render_*.cpp: the display buffers, the animation pose, the image classes, the palette slots and cache, the M3D
// scene renderer and the D3D start-up):
//
//   opentony_render_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the groups of render_specs*.py) holds the retail memory regions the ported code touches and, for every
// case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp for the format and the comparison.
#include "render_kit.hpp"
#include "retail_calls_replay.hpp"

#include <memory>
#include <string>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;
using namespace opentony::runtime::renderkit;
namespace x87 = opentony::runtime::mathkit::x87;
using opentony::runtime::pcplatform::kScratchBase;

namespace {

// the x87 precision control the fixture was captured under: the forced calls run at 53 bits, the twin captures (render-calls-p24-*.txt) set the render window's 24 bits first
x87::Precision g_precision = x87::Precision::k53;

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    RenderKit render;
    std::uint32_t self = 0;                                          // (the object of the call under replay: where the bytes that depend on a stack address are)
    explicit Ctx(RetailMemory memory) : mem(std::move(memory)), render(mem, svc) {
        render.math().set_precision(g_precision);
        if (!mem.contains(kScratchBase, 0x10)) {
            mem.add_region(kScratchBase, 0x3000);                    // (the stack locals of the retail functions)
        }
    }
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using RenderAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. `a` are the stack arguments, `self` is `this` (ecx).
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define G c.render
// AS(expr): like A, for a constructor whose clut word (+0x5a) comes from the address of a stack local in retail, which no replay can reproduce
#define AS(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; c.self = self; return U(expr); }
#define CLUT [](Ctx& c) -> Unspecified { return {{c.self + 0x5a, 2}}; }

const std::map<std::uint32_t, RenderAdapter>& adapters() {
    static const std::map<std::uint32_t, RenderAdapter> table = {
        // ---- the display buffers (render_display.cpp)
        {0x0042fc60, {"Db_OTSize", 0xffffffff, false, A(G.db_ot_size())}},
        {0x0042fc70, {"Db_UpdateSky", 0, false, V(G.db_update_sky())}},
        {0x0042fcd0, {"Db_CallBack", 0, false, V(G.db_callback())}},
        {0x0042fd20, {"Db_Vblank", 0, false, V(G.db_vblank())}},
        {0x0042fed0, {"Db_WaitRender", 0, false, V(G.db_wait_render())}},
        {0x0042ffc0, {"Db_Render", 0, false, V(G.db_render())}},
        {0x004301a0, {"Db_InstallCallback", 0, false, V(G.db_install_callback())}},
        {0x004301b0, {"Db_Reset", 0, false, V(G.db_reset())}},
        {0x004301f0, {"Db_Init", 0, false, V(G.db_init(a[0], a[1], a[2]))}},
        {0x004302c0, {"HiRes_ClearOTag", 0, false, V(G.hires_clear_otag(a[0], a[1], a[2], a[3]))}},
        {0x004304b0, {"Db_FlipClear", 0, false, V(G.db_flip_clear())}},
        {0x00430510, {"Db_DisplayDraw", 0, false, V(G.db_display_draw())}},
        {0x004305b0, {"Db_Flip", 0, false, V(G.db_flip())}},
        // ---- the animation pose (render_anim.cpp)
        {0x004305f0, {"Animation_DecompressStream", 0xffffffff, false, A(G.anim_decompress_stream(a[0], a[1], S(a[2]), S(a[3])))}},
        {0x00430920, {"Animation_GetDecompressedPose", 0xffffffff, false, A(G.anim_pose(a[0]))}},
        {0x00431190, {"ModelList_Construct", 0xffffffff, false, A(G.model_list_construct(self))}},
        {0x004311b0, {"ModelList_ScalarDelete", 0xffffffff, false, A(G.model_list_scalar_delete(self, a[0]))}},
        {0x004311d0, {"ModelList_Destruct", 0, false, V(G.model_list_destruct(self))}},
        // ---- the pose side of the M3D renderer (render_m3d_anim.cpp)
        {0x00464b90, {"M3D_ReadHooksPacket", 0, false, V(G.read_hooks_packet(a[0], a[1]))}},
        {0x00464bb0, {"M3D_ClearPoseStuff", 0, false, V(G.clear_pose_stuff(a[0]))}},
        {0x00464c00, {"M3D_ReadLinksPacket", 0, false, V(G.read_links_packet(a[0], a[1]))}},
        {0x00464d10, {"M3D_GetPartPosition", 0, false, V(G.part_position(a[1], a[2], a[0]))}},
        {0x00464e90, {"M3D_GetHookPosition", 0, false, V(G.hook_position(a[1], a[2], a[0]))}},
        {0x00465060, {"M3D_InterpolateAnimationPose", 0, false, V(G.interpolate_animation_pose(a[0]))}},
        {0x00465110, {"M3D_TweenAnimationBuffer", 0, false, V(G.tween_animation_buffer(a[0], a[1], a[2], a[3], a[4], a[5]))}},
        // ---- the image classes (render_image.cpp)
        {0x004564b0, {"Image_Construct", 0xffffffff, false, A(G.image_construct(self, a[0], a[1]))}},
        {0x00456550, {"Image_ScalarDelete", 0xffffffff, false, A(G.image_scalar_delete(self, a[0]))}},
        {0x00456570, {"Image_Destruct", 0, false, V(G.image_destruct(self))}},
        {0x00456580, {"Image_RenderObject", 0, false, V(G.render_single_object(self))}},
        {0x00456600, {"Image_FlatRectangle", 0, false, V(G.flat_rectangle(S(a[0]), S(a[1]), S(a[2]), S(a[3]), a[4], a[5], a[6], true))}},
        {0x00456790, {"Image_FlatRectangleNoTerminator", 0, false, V(G.flat_rectangle(S(a[0]), S(a[1]), S(a[2]), S(a[3]), a[4], a[5], a[6], false))}},
        {0x004568d0, {"Image_RectPrimitive", 0, false, V(G.rect_primitive(a[0], a[1], a[2], a[3], a[4]))}},
        {0x00456990, {"Image_Nothing456990", 0, false, V((void)0)}},
        {0x004569a0, {"Bmp_Depth", 0xffffffff, false, A(G.bmp_depth(a[0]))}},
        {0x00456a00, {"Bmp_Load4", 0xffffffff, false, A(G.bmp_load(a[0], a[1], a[2], a[3], a[4], a[5], false))}},
        {0x00456a70, {"Bmp_Load8", 0xffffffff, false, A(G.bmp_load(a[0], a[1], a[2], a[3], a[4], a[5], true))}},
        {0x00456ae0, {"MakeTransparentClut2", 0xffffffff, false, A(G.make_transparent_clut(a[0], a[1], a[2], a[3], a[4], a[5]))}},
        {0x00456c60, {"SlicedImage_BaseConstruct", 0xffffffff, false, A(G.sliced_base_construct(self))}},
        {0x00456c90, {"SlicedImage_PaletteSource", 0xffffffff, false, A(G.sliced_palette_source(self))}},
        {0x00456cb0, {"SlicedImage_SetPalette", 0, false, V(G.sliced_set_palette(self, a[0]))}},
        {0x00456cd0, {"SlicedImage_Construct", 0xffffffff, false, A(G.sliced_construct(self))}},
        {0x00456cf0, {"SlicedImage_ScalarDelete", 0xffffffff, false, A(G.sliced_scalar_delete(self, a[0]))}},
        {0x00456d10, {"FontGlyph_ConstructImage", 0xffffffff, false, A(G.font_glyph_construct(self, a[0], a[1], a[2], a[3], a[4], a[5], a[6]))}},
        {0x00456d90, {"SlicedImage2__removeFromMemory", 0, false, V(G.sliced_remove_from_memory(self))}},
        {0x00456e10, {"FontGlyph_SetImageFormat", 0, false, V(G.font_glyph_set_image_format(self, a[0], a[1], a[2], a[3], a[4], a[5], a[6]))}},
        {0x00457090, {"Image_BindPalette", 0, false, V(G.image_bind_palette(self, a[0]))}},
        {0x00457120, {"Image_Nothing457120", 0, false, V((void)0)}},
        {0x00457130, {"Image_Nothing457130", 0, false, V((void)0)}},
        {0x00457150, {"Image_Nothing457150", 0, false, V((void)0)}},
        {0x00457160, {"SlicedImage_Draw", 0, false, V(G.sliced_draw(self, S(a[0]), S(a[1]), a[2]))}},
        {0x004573e0, {"RawImage_Destruct", 0, false, V(G.raw_destruct(self))}},
        {0x00457400, {"RawImage_ScalarDelete", 0xffffffff, false, A(G.raw_scalar_delete(self, a[0]))}},
        {0x00457420, {"BmpImage_Construct", 0xffffffff, false, AS(G.bmp_image_construct(self, a[0], a[1], a[2], a[3])), CLUT}},
        {0x00457520, {"BmpImage_ScalarDelete", 0xffffffff, false, A(G.bmp_image_scalar_delete(self, a[0]))}},
        {0x00457540, {"BmpImage_ConstructFromData", 0xffffffff, false, AS(G.bmp_data_image_construct(self, a[0], a[1], a[2], a[3])), CLUT}},
        {0x00457660, {"VramSprite_Construct", 0xffffffff, false, A(G.vram_sprite_construct(self))}},
        {0x004576f0, {"BmpImage_Destruct", 0, false, V(G.bmp_image_destruct(self))}},
        {0x00457810, {"Image_TextureValue", 0xffffffff, false, A(G.image_texture_value(self))}},
        {0x00457820, {"SlicedImage2__screenWidth", 0xffffffff, false, A(G.image_scaled_width(self))}},
        {0x00457830, {"SlicedImage2__screenHeight", 0xffffffff, false, A(G.image_scaled_height(self))}},
        {0x00457870, {"Sprite_Construct", 0xffffffff, false, AS(G.sprite_construct(self, a[0], a[1], a[2], a[3])), CLUT}},
        {0x004578a0, {"Sprite_ScalarDelete", 0xffffffff, false, A(G.sprite_scalar_delete(self, a[0]))}},
        {0x004578c0, {"Sprite_Destruct", 0, false, V(G.sprite_destruct(self))}},
        {0x00457e10, {"Image_LoadBmpIntoMaterial", 0, false, V(G.load_bmp_into_material(a[0], a[1], a[2]))}},
        {0x00457f30, {"Image_DecrementRegionTextures", 0, false, V(G.decrement_region_textures(a[0]))}},
        {0x004581a0, {"Image_ClearMaterialPalette", 0, false, V(G.clear_material_palette(a[0], a[1], a[2], a[3]))}},
        // ---- the scene renderer's small functions (render_m3d.cpp)
        {0x00461a50, {"M3D_SubmitStateGeometry", 0, false, V(G.submit_state_geometry(a[0]))}},
        {0x00461a90, {"M3D_SubmitStateGeometryVariant", 0, false, V(G.submit_state_geometry(a[0]))}},
        {0x00461ad0, {"RenderBackgroundModel", 0, false, V(G.submit_background_model(a[0]))}},
        {0x00461b10, {"M3D_SetGeometryState", 0, false, V(G.set_geometry_state(a[0], a[1]))}},
        {0x00461b50, {"RenderModelInSuperFast", 0, false, V(G.render_model_super_fast(a[0], a[1]))}},
        {0x00461b70, {"RenderModelNonRotated", 0, false, V(G.submit_state_geometry(a[0]))}},
        {0x00461bb0, {"RenderBackgroundModelNonRotated", 0, false, V(G.submit_background_model(a[0]))}},
        {0x00461bf0, {"M3d_BuildTransform", 0, false, V(G.build_transform(a[0]))}},
        {0x00461c90, {"uWibble", 0xffffffff, false, A(G.wibble(a[0], 2))}},
        {0x00461cc0, {"vWibble", 0xffffffff, false, A(G.wibble(a[0], 3))}},
        {0x00461cf0, {"M3D_ApplyTextureWib", 0, false, V(G.apply_texture_wib(a[0]))}},
        {0x00462170, {"M3D_UpdateColourPulse", 0, false, V(G.update_colour_pulse(a[0]))}},
        {0x004645f0, {"M3dInit_InitAtStart", 0, false, V(G.m3d_init_at_start())}},
        {0x00464690, {"M3D_WindowRatio", 0, false, V(G.window_ratio(a[0], a[1]))}},
        {0x00464710, {"M3D_SetFogRange", 0, false, V(G.set_fog_range(a[0], a[1], a[2]))}},
        {0x00464a80, {"M3D_PrepareTextureWib", 0, false, V(G.prepare_texture_wib(a[0]))}},
        {0x004657c0, {"M3D_MatrixAngles", 0, false, V(G.matrix_angles(a[0], a[1]))}},
        {0x00466030, {"Blockmap_ClearRuntimeGrid", 0, false, V(G.blockmap_clear_grid(S(a[0])))}},
        {0x00466080, {"Blockmap_ClearGrid0", 0, false, V(G.blockmap_clear_grid(0))}},
        // ---- the scene renderer's frame functions (render_scene.cpp)
        {0x0045f3f0, {"M3d_RenderCleanup", 0, false, V(G.render_cleanup())}},
        {0x00460a90, {"Render_SubmitObjectList", 0, false, V(G.submit_object_list(a[0]))}},
        {0x004667e0, {"M3D_LoadZoneBlockmap", 0, false, V(G.load_zone_blockmap(a[0], a[1]))}},
        {0x00466a00, {"ReplayLoadingBanner", 0, false, V(G.loading_banner())}},
        // ---- the D3D start-up and sprite quad (render_d3d.cpp)
        {0x004d11d0, {"M3D_SubmitGeometryPackets", 0, false, V(G.submit_geometry_packets(a[0], S(a[1]), a[2], a[3]))}},
        {0x004d2da0, {"D3DMODEL_Startup", 0, false, V(G.d3dmodel_startup())}},
        {0x004d3f50, {"D3D_SetupFog", 0, false, V(G.d3d_setup_fog(a[0]))}},
        {0x004d7990, {"D3D_BuildSpriteQuad", 0, false, V(G.d3d_build_sprite_quad(self, a[0], a[1], a[2]))}},
        {0x00465300, {"M3D_BuildPose", 0, false, V(G.build_pose(a[0]))}},
        // ---- the start of a view (render_view.cpp)
        {0x0045e8e0, {"M3D_BeginRenderView", 0, false, V(G.begin_render_view(a[0], a[1], a[2]))}},
        {0x0045ef80, {"M3d_RenderSetupCamera", 0, false, V(G.setup_camera(a[0]))}},
        {0x00465830, {"M3D_SegmentDistance", 0xffffffff, false, A(G.segment_distance(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]))}},
        {0x00460ca0, {"M3d_RenderBackground", 0, false, V(G.render_background(a[0])), [](Ctx& c) -> Unspecified { return c.render.unspecified(); }}},
        {0x004604f0, {"RenderSuperItemShadow", 0, false, V(G.render_super_item(a[0])), [](Ctx& c) -> Unspecified { return c.render.unspecified(); }}},
        {0x004610f0, {"M3D_RenderModel", 0, false, V(G.render_model(a[0])), [](Ctx& c) -> Unspecified { return c.render.unspecified(); }}},
        {0x0045f530, {"M3D_RenderObjects", 0, false, V(G.render_objects(a[0])), [](Ctx& c) -> Unspecified { return c.render.unspecified(); }}},
        // ---- the palette slots and the palette cache (render_palette.cpp)
        {0x004305e0, {"Animation_EmptyHook", 0, false, V(G.animation_empty_hook())}},
        {0x0048aad0, {"Pal_CopyToMemory", 0, false, V(G.pal_copy_to_memory(a[0], a[1]))}},
        {0x0048ab00, {"Pal_StoreFromMemory", 0, false, V(G.pal_store_from_memory(a[0], a[1]))}},
        {0x0048a0b0, {"Pal_Init", 0, false, V(G.pal_init(a[0], a[1]))}},
        {0x0048a320, {"Pal_Cleanup", 0, false, V(G.pal_cleanup())}},
        {0x0048a360, {"Palette_AllocSourceLookupNode", 0xffffffff, false, A(G.lookup_alloc(a[0]))}},
        {0x0048a3b0, {"RemovePaletteEntry", 0, false, V(G.lookup_remove(a[0]))}},
        {0x0048a400, {"Palette_FindSourceLookupNode", 0xffffffff, false, A(G.lookup_find(a[0]))}},
        {0x0048a420, {"Palette_ClearSourceLookup", 0, false, V(G.lookup_clear())}},
        {0x0048a490, {"GetFree16Slot", 0xffffffff, false, A(G.get_free16_slot())}},
        {0x0048a500, {"Free16Slot", 0, false, V(G.free16_slot(a[0]))}},
        {0x0048a590, {"GetFree256Slot", 0xffffffff, false, A(G.get_free256_slot())}},
        {0x0048a6f0, {"Get256SlotNumber", 0xffffffff, false, A(0u)}},
        {0x0048a700, {"Get16SlotNumber", 0xffffffff, false, A(0u)}},
        {0x0048a7e0, {"Palette_CreateFromPsxColors", 0xffffffff, false, A(G.palette_create(a[0], a[1], a[2] & 0xff))}},
        {0x004da460, {"Palette_AllocCacheRecord", 0xffffffff, false, A(G.cache_alloc())}},
        {0x004da4b0, {"D3DTEX_FreePaletteEntry", 0, false, V(G.d3dtex_free_palette_entry(a[0], a[1]))}},
        {0x004da4d0, {"Palette_FreeCacheRecord", 0, false, V(G.cache_free(a[0], a[1]))}},
        {0x004da550, {"Palette_FreeAllCacheRecords", 0, false, V(G.cache_free_all())}},
        {0x004da580, {"Palette_CacheSource", 0xffffffff, false, A(G.cache_source(a[0]))}},
        {0x004da590, {"Palette_ReloadTexture", 0, false, V(G.texture_reload_palette(a[0], a[1], a[2]))}},
        {0x004da620, {"Palette_SetCacheColours", 0, false, V(G.cache_set_colours(a[0], a[1]))}},
        {0x004da680, {"Palette_ConvertColors", 0xffffffff, false, A(G.palette_convert(a[0], a[1], a[2]))}},
        {0x004da780, {"Palette_ConvertInPlace", 0, false, V(G.convert_in_place(a[0], a[1]))}},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    replay.on_file = [](const std::string& path) { g_precision = path.find("-p24-") != std::string::npos ? x87::Precision::k24 : x87::Precision::k53; };
    return replay.run_files(argc, argv, "renderer layer retail forced calls");
}
