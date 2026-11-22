#pragma once

#include "collision_kit.hpp"
#include "guest_view.hpp"
#include "core_kit.hpp"
#include "m3d_kit.hpp"
#include "math_kit.hpp"

#include <bit>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace opentony::runtime::renderkit {

// Phase 4.15e: the renderer's remainder: the display buffers (`Db_*`, the double-buffered draw/display environments and their ordering tables), the animation pose decompressor, the image
// classes (`image.cpp`: the bitmap, sliced and sprite images and the quads they queue), the palette slots and the palette cache, the M3D scene renderer (`m3d.cpp`: views, the object lists, the
// models, the wibble and colour pulse, the hook and part positions, the zone blockmap) and the D3D start-up and sprite quad. Like the rest of Phase 4 the functions work on the retail memory at the
// retail addresses (`self` is the object of a method); everything outside them (the allocators, the GPU stubs, the texture upload, the assertion routine) is a service reported by address
// (`engine_<address>`).

constexpr std::uint32_t kScratch = pcplatform::kScratchBase + 0xe00;

class RenderKit final : public collision::CollisionKit {
public:
    RenderKit(RetailMemory& memory, RetailServices& services) : CollisionKit(memory, services), core_(memory, services), m3d_(memory, services) { core_.use_native_square_root(); }

    // ---- the display buffers (render_display.cpp, 0x0042fc60..0x004305e0) ----
    std::uint32_t db_ot_size();                                                                 // 0x0042fc60 Db_OTSize
    void db_update_sky();                                                                       // 0x0042fc70 Db_UpdateSky
    void db_callback();                                                                         // 0x0042fcd0 Db_CallBack
    void db_vblank();                                                                           // 0x0042fd20 Db_Vblank
    void db_wait_render();                                                                      // 0x0042fed0 Db_WaitRender
    void db_render();                                                                           // 0x0042ffc0 Db_Render
    void db_install_callback();                                                                 // 0x004301a0
    void db_reset();                                                                            // 0x004301b0 Db_Reset
    void db_init(std::uint32_t arg0, std::uint32_t arg1, std::uint32_t flag);                   // 0x004301f0 Db_Init (cdecl: the third argument's low byte)
    void hires_clear_otag(std::uint32_t arg0, std::uint32_t arg1, std::uint32_t arg2, std::uint32_t arg3);   // 0x004302c0 HiRes_ClearOTag (four arguments: every caller pushes four)
    void db_flip_clear();                                                                       // 0x004304b0 Db_FlipClear
    void db_display_draw();                                                                     // 0x00430510 Db_DisplayDraw
    void db_flip();                                                                             // 0x004305b0

    // ---- the animation pose (render_anim.cpp, 0x004305f0..0x0043125f) ----
    std::uint32_t anim_decompress_stream(std::uint32_t stream, std::uint32_t dest, std::int32_t stride, std::int32_t count);   // 0x004305f0 Animation_DecompressStream (cdecl)
    std::uint32_t anim_pose(std::uint32_t model);                                               // 0x00430920 Animation_GetDecompressedPose (cdecl)
    std::uint32_t model_list_construct(std::uint32_t self);                                     // 0x00431190 (fastcall)
    std::uint32_t model_list_scalar_delete(std::uint32_t self, std::uint32_t flags);            // 0x004311b0 (thiscall, one argument)
    void model_list_destruct(std::uint32_t self);                                               // 0x004311d0 (fastcall)

    // ---- the pose side of the M3D renderer (render_m3d_anim.cpp, 0x00464b90..0x00465300) ----
    void read_hooks_packet(std::uint32_t model, std::uint32_t packet);                          // 0x00464b90 M3D_ReadHooksPacket (cdecl)
    void clear_pose_stuff(std::uint32_t model);                                                 // 0x00464bb0 M3D_ClearPoseStuff (cdecl)
    void read_links_packet(std::uint32_t model, std::uint32_t packet);                          // 0x00464c00 M3D_ReadLinksPacket (cdecl)
    void part_position(std::uint32_t model, std::uint32_t part, std::uint32_t out);             // 0x00464d10 M3D_GetPartPosition (cdecl)
    void hook_position(std::uint32_t model, std::uint32_t hook, std::uint32_t out);             // 0x00464e90 M3D_GetHookPosition (cdecl)
    void interpolate_animation_pose(std::uint32_t model);                                       // 0x00465060 M3D_InterpolateAnimationPose (cdecl)
    void tween_animation_buffer(std::uint32_t vectors, std::uint32_t segments, std::uint32_t animations, std::uint32_t model, std::uint32_t first_bone, std::uint32_t bones);   // 0x00465110 M3D_TweenAnimationBuffer (cdecl)

    // ---- the palette slots and the palette cache (render_palette.cpp, 0x0048a0b0..0x0048ab05, 0x004da460..0x004da840) ----
    void pal_init(std::uint32_t sixteen, std::uint32_t big);                                    // 0x0048a0b0 Pal_Init (cdecl)
    void pal_cleanup();                                                                         // 0x0048a320 Pal_Cleanup
    std::uint32_t lookup_alloc(std::uint32_t key);                                              // 0x0048a360 Palette_AllocSourceLookupNode (cdecl)
    void lookup_remove(std::uint32_t node);                                                     // 0x0048a3b0 RemovePaletteEntry (cdecl)
    std::uint32_t lookup_find(std::uint32_t key);                                               // 0x0048a400 Palette_FindSourceLookupNode (cdecl)
    void lookup_clear();                                                                        // 0x0048a420 Palette_ClearSourceLookup
    std::uint32_t get_free16_slot();                                                            // 0x0048a490 GetFree16Slot
    void free16_slot(std::uint32_t palette);                                                    // 0x0048a500 Free16Slot (cdecl)
    std::uint32_t get_free256_slot();                                                           // 0x0048a590 GetFree256Slot
    std::uint32_t palette_create(std::uint32_t key, std::uint32_t colours, std::uint32_t flags);   // 0x0048a7e0 Palette_CreateFromPsxColors (cdecl)
    std::uint32_t cache_alloc();                                                                // 0x004da460 Palette_AllocCacheRecord
    void d3dtex_free_palette_entry(std::uint32_t record, std::uint32_t free_colours);           // 0x004da4b0 D3DTEX_FreePaletteEntry (cdecl)
    void cache_free(std::uint32_t record, std::uint32_t free_colours);                          // 0x004da4d0 Palette_FreeCacheRecord (cdecl)
    void cache_free_all();                                                                      // 0x004da550
    std::uint32_t cache_source(std::uint32_t record);                                           // 0x004da580 (cdecl)
    void texture_reload_palette(std::uint32_t texture, std::uint32_t record, std::uint32_t colours);   // 0x004da590 (cdecl)
    void pal_copy_to_memory(std::uint32_t palette, std::uint32_t destination);                  // 0x0048aad0 Pal_CopyToMemory (cdecl)
    void pal_store_from_memory(std::uint32_t palette, std::uint32_t colours);                   // 0x0048ab00 Pal_StoreFromMemory (cdecl)
    void animation_empty_hook();                                                                // 0x004305e0 (cdecl, empty)
    void cache_set_colours(std::uint32_t record, std::uint32_t colours);                        // 0x004da620 (cdecl)
    std::uint32_t palette_convert(std::uint32_t tag, std::uint32_t colours, std::uint32_t count);   // 0x004da680 Palette_ConvertColors (cdecl)
    void convert_in_place(std::uint32_t colours, std::uint32_t count);                          // 0x004da780 (cdecl)

    // ---- the image classes (render_image.cpp, 0x004564b0..0x004581a0) ----
    std::uint32_t image_construct(std::uint32_t self, std::uint32_t source, std::uint32_t extent);   // 0x004564b0 (thiscall, two arguments)
    std::uint32_t image_scalar_delete(std::uint32_t self, std::uint32_t flags);                 // 0x00456550 (thiscall, one argument)
    void image_destruct(std::uint32_t self);                                                    // 0x00456570 (fastcall)
    void render_single_object(std::uint32_t object);                                            // 0x00456580 (thiscall)
    void flat_rectangle(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height, std::uint32_t red, std::uint32_t green, std::uint32_t blue, bool with_terminator);   // 0x00456600 (with), 0x00456790 (without) (cdecl)
    void rect_primitive(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height, std::uint32_t far_end);   // 0x004568d0 (cdecl)
    std::uint32_t bmp_depth(std::uint32_t header);                                              // 0x004569a0 (cdecl)
    std::uint32_t bmp_load(std::uint32_t header, std::uint32_t clear_this, std::uint32_t width_out, std::uint32_t height_out, std::uint32_t clut_out, std::uint32_t name, bool eight_bit);   // 0x00456a00 (4-bit), 0x00456a70 (8-bit) (cdecl)
    std::uint32_t make_transparent_clut(std::uint32_t name, std::uint32_t out_a, std::uint32_t out_width, std::uint32_t out_height, std::uint32_t out_clut, std::uint32_t out_depth);   // 0x00456ae0 (cdecl)
    std::uint32_t sliced_base_construct(std::uint32_t self);                                    // 0x00456c60 (fastcall)
    std::uint32_t sliced_palette_source(std::uint32_t self);                                    // 0x00456c90 (fastcall)
    void sliced_set_palette(std::uint32_t self, std::uint32_t colours);                         // 0x00456cb0 (ecx, one stack argument)
    std::uint32_t sliced_construct(std::uint32_t self);                                         // 0x00456cd0 (fastcall)
    std::uint32_t sliced_scalar_delete(std::uint32_t self, std::uint32_t flags);                // 0x00456cf0 (thiscall, one argument)
    std::uint32_t font_glyph_construct(std::uint32_t self, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6);   // 0x00456d10 FontGlyph_ConstructImage (thiscall, seven arguments)
    void sliced_remove_from_memory(std::uint32_t self);                                         // 0x00456d90 (fastcall)
    void font_glyph_set_image_format(std::uint32_t self, std::uint32_t a0, std::uint32_t width, std::uint32_t height, std::uint32_t x, std::uint32_t y, std::uint32_t depth, std::uint32_t clut);   // 0x00456e10 (thiscall, seven arguments)
    void image_bind_palette(std::uint32_t self, std::uint32_t unused);                          // 0x00457090 (thiscall, one argument)
    void sliced_draw(std::uint32_t self, std::int32_t x, std::int32_t y, std::uint32_t far_end);   // 0x00457160 (thiscall, three arguments)
    void raw_destruct(std::uint32_t self);                                                      // 0x004573e0 (fastcall)
    std::uint32_t raw_scalar_delete(std::uint32_t self, std::uint32_t flags);                   // 0x00457400 (thiscall, one argument)
    std::uint32_t bmp_image_construct(std::uint32_t self, std::uint32_t name, std::uint32_t vram, std::uint32_t x, std::uint32_t y);   // 0x00457420 (thiscall, four arguments)
    std::uint32_t bmp_image_scalar_delete(std::uint32_t self, std::uint32_t flags);             // 0x00457520 (thiscall, one argument)
    std::uint32_t bmp_data_image_construct(std::uint32_t self, std::uint32_t data, std::uint32_t vram, std::uint32_t x, std::uint32_t y);   // 0x00457540 (thiscall, four arguments)
    std::uint32_t vram_sprite_construct(std::uint32_t self);                                    // 0x00457660 (fastcall)
    void bmp_image_destruct(std::uint32_t self);                                                // 0x004576f0 (fastcall)
    void bmp_image_reload(std::uint32_t self, std::uint32_t data);                              // 0x00457700 (thiscall, one argument; slot 2 of the bitmap image)
    std::uint32_t image_texture_value(std::uint32_t self);                                      // 0x00457810 (fastcall)
    std::uint32_t image_scaled_width(std::uint32_t self);                                       // 0x00457820 (fastcall)
    std::uint32_t image_scaled_height(std::uint32_t self);                                      // 0x00457830 (fastcall)
    std::uint32_t sprite_construct(std::uint32_t self, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3);   // 0x00457870 (thiscall, four arguments)
    std::uint32_t sprite_scalar_delete(std::uint32_t self, std::uint32_t flags);                // 0x004578a0 (thiscall, one argument)
    void sprite_destruct(std::uint32_t self);                                                   // 0x004578c0 (fastcall)
    void sprite_draw_rotated(std::uint32_t self, std::int32_t x, std::int32_t y, std::int32_t angle);   // 0x00457940 (thiscall, three arguments; slot 3 of the sprite class)
    void load_bmp_into_material(std::uint32_t checksum, std::uint32_t data, std::uint32_t name);   // 0x00457e10 (cdecl)
    void decrement_region_textures(std::uint32_t region);                                       // 0x00457f30 (cdecl)
    void clear_material_palette(std::uint32_t checksum, std::uint32_t red, std::uint32_t green, std::uint32_t blue);   // 0x004581a0 (cdecl)

    // ---- the scene renderer's small functions (render_m3d.cpp) ----
    void submit_state_geometry(std::uint32_t model);                                            // 0x00461a50 M3D_SubmitStateGeometry, 0x00461a90, 0x00461b70 (cdecl)
    void submit_background_model(std::uint32_t model);                                          // 0x00461ad0 RenderBackgroundModel, 0x00461bb0 (cdecl)
    void set_geometry_state(std::uint32_t model, std::uint32_t flags);                          // 0x00461b10 M3D_SetGeometryState (cdecl)
    void render_model_super_fast(std::uint32_t model, std::uint32_t flags);                     // 0x00461b50 RenderModelInSuperFast (cdecl)
    void build_transform(std::uint32_t model);                                                  // 0x00461bf0 M3d_BuildTransform (cdecl)
    std::uint32_t wibble(std::uint32_t record, std::uint32_t byte_offset);                      // 0x00461c90 uWibble (byte +2), 0x00461cc0 vWibble (byte +3) (cdecl)
    void apply_texture_wib(std::uint32_t region);                                               // 0x00461cf0 M3D_ApplyTextureWib (cdecl)
    void update_colour_pulse(std::uint32_t region);                                             // 0x00462170 M3D_UpdateColourPulse (cdecl)
    void m3d_init_at_start();                                                                   // 0x004645f0 M3dInit_InitAtStart
    void window_ratio(std::uint32_t width, std::uint32_t height);                               // 0x00464690 (cdecl)
    void set_fog_range(std::uint32_t unused, std::uint32_t near, std::uint32_t range);          // 0x00464710 M3D_SetFogRange (cdecl)
    void prepare_texture_wib(std::uint32_t list);                                               // 0x00464a80 M3D_PrepareTextureWib (cdecl)
    void matrix_angles(std::uint32_t out, std::uint32_t matrix);                                // 0x004657c0 (cdecl)
    void blockmap_clear_grid(std::int32_t index);                                               // 0x00466030 Blockmap_ClearRuntimeGrid (cdecl); 0x00466080 clears grid 0

    // ---- the scene renderer's frame functions (render_scene.cpp) ----
    void render_cleanup();                                                                      // 0x0045f3f0 M3d_RenderCleanup
    void submit_object_list(std::uint32_t list);                                                // 0x00460a90 Render_SubmitObjectList (cdecl)
    void load_zone_blockmap(std::uint32_t index, std::uint32_t record);                         // 0x004667e0 M3D_LoadZoneBlockmap (cdecl)
    void loading_banner();                                                                      // 0x00466a00

    // ---- the D3D start-up and sprite quad (render_d3d.cpp) ----
    void d3dmodel_startup();                                                                    // 0x004d2da0 D3DMODEL_Startup
    void submit_geometry_packets(std::uint32_t packets, std::int32_t count, std::uint32_t light, std::uint32_t reset);   // 0x004d11d0 M3D_SubmitGeometryPackets (cdecl)
    void d3d_setup_fog(std::uint32_t colour);                                                   // 0x004d3f50 D3D_SetupFog (cdecl)
    void d3d_build_sprite_quad(std::uint32_t self, std::uint32_t table, std::uint32_t x_offset_bits, std::uint32_t y_offset_bits);   // 0x004d7990 D3D_BuildSpriteQuad (thiscall, three arguments)

    // ---- the model submission chain (render_model.cpp, 0x004d14d0..0x004d2d9e; milestone 10) ----
    void model_submit_state(std::uint32_t model, std::uint32_t flags);                          // 0x004d14d0 M3D_SubmitModelState (cdecl)
    void model_transform_vertices(std::uint32_t vertices, std::int32_t count);                  // 0x004d29e0 M3D_TransformVertices (cdecl; its third argument, the flags' bit 0, is not read)
    void model_submit_faces();                                                                  // 0x004d18b0 M3D_SubmitFacePolygons
    void model_face_colour(std::uint32_t face);                                                 // 0x004d1960 M3D_ResolveFaceColor (cdecl)
    void model_build_polygon(std::uint32_t face);                                               // 0x004d1d40 M3D_BuildD3DPolygon (cdecl)
    void model_clip_and_bucket(std::uint32_t polygon, std::uint32_t face, std::uint32_t clip);  // 0x004d20f0 M3D_ClipAndBucketPolygon (cdecl; the clip flags' low byte is read)
    std::uint32_t model_clip_near_plane(std::uint32_t polygon);                                 // 0x004d2310 M3D_ClipPolygonNearPlane (cdecl)
    std::uint32_t model_classify_depth(std::uint32_t polygon, std::uint32_t face, std::uint32_t offset_bits, std::uint32_t depth_bits);   // 0x004d26b0 M3D_ClassifyPolygonDepth (cdecl; two floats)

    void build_pose(std::uint32_t model);                                                       // 0x00465300 M3D_BuildPose (cdecl)

    // ---- the start of a view (render_view.cpp) ----
    void begin_render_view(std::uint32_t view, std::uint32_t edges, std::uint32_t buckets);     // 0x0045e8e0 M3D_BeginRenderView (cdecl)
    void setup_camera(std::uint32_t view);                                                      // 0x0045ef80 M3d_RenderSetupCamera (cdecl)

    std::uint32_t segment_distance(std::uint32_t p1, std::uint32_t p2, std::uint32_t p3, std::uint32_t p4, std::uint32_t p5, std::uint32_t p6, std::uint32_t p7, std::uint32_t p8);   // 0x00465830 (cdecl, eight arguments)

    void render_objects(std::uint32_t list);                                                    // 0x0045f530 M3D_RenderObjects (cdecl)
    void render_model(std::uint32_t object);                                                    // 0x004610f0 M3D_RenderModel (cdecl)
    void render_super_item(std::uint32_t object);                                               // 0x004604f0 RenderSuperItemShadow (cdecl)
    void render_background(std::uint32_t list);                                                 // 0x00460ca0 M3d_RenderBackground (cdecl)
    // the bytes of the last call's writes that are stack leftovers in retail (the replay does not compare them)
    [[nodiscard]] const std::vector<std::pair<std::uint32_t, std::uint32_t>>& unspecified() const { return unspecified_; }

    mathkit::MathKit& math() { return m3d_.math(); }
    core::CoreKit& core() { return core_; }
    m3d::M3dKit& m3d() { return m3d_; }

private:
    using Word = std::uint32_t;
    static Word u(std::int32_t v) { return static_cast<Word>(v); }
    static std::int32_t s(Word v) { return static_cast<std::int32_t>(v); }
    // FileIO_AllocLoadedBuffer(size, 1, 1, 0)
    Word alloc(Word size, Word heap = 1) {
        s_.note("alloc", {size, heap, 1, 0});
        return static_cast<Word>(s_.value("alloc"));
    }
    void mem_free(Word block) { s_.note("mem_delete", {block}); }
    Word display_colour();
    void view_planes(Word edges);
    void view_matrices(Word view, Word edges);
    void scale_view_matrix(Word view);
    static float float_from(Word bits) { return std::bit_cast<float>(bits); }
    static Word float_bits(float value) { return std::bit_cast<Word>(value); }
    float float_at(Word address) { return float_from(guest::read_scalar<std::uint32_t>(m_, address)); }
    void submit_with_flags(Word model, Word extra, bool pair_matters);
    Word screen_x(std::int32_t value);
    Word screen_y(std::int32_t value);
    void link_primitive(Word prim, bool far_end);
    // a thiscall to a service: the object is the call's first argument
    void thiscall_service(Word target, Word self, std::vector<Word> args = {}) {
        args.insert(args.begin(), self);
        engine(target, std::move(args));
    }
    void slot_allocator(Word width, Word slots, Word x_address, Word y_address);
    Word get_free_slot(Word count_address, Word flags, Word message);
    void free_slot(Word palette, Word count_address, Word x_address, Word y_address, Word flags, Word message);
    Word device_colour(Word colour);
    Word model_allocate_polygon();
    Word bone_pose(Word model, Word anims, Word anim, Word bone, Word bones);
    void hook_to_world(Word model, Word out);
    // the bytes at an address as the text of a reported call (a record handed to a service)
    std::string bytes_text(Word at, Word count) {
        const auto bytes = m_.bytes(at, count);
        return std::string(bytes.begin(), bytes.end());
    }

    core::CoreKit core_;
    m3d::M3dKit m3d_;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> unspecified_;
};

}  // namespace opentony::runtime::renderkit
