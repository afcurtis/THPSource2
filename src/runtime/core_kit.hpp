#pragma once

#include "math_kit.hpp"
#include "pc_platform.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace opentony::runtime::core {

// Phase 4.14c: the game's core utilities and start-up code: `utils.cpp` (the vector helpers, the distance and angle math, the object-list sweeps), `init.cpp` (the clean-up and restart sequences),
// `main.cpp` (the display, the main loop, the level launch), the relocatable-module loader, the PKR2 package wrappers (the RLE and zlib compression of the resource files) and the
// small `exp.cpp` / `fileio` pieces between them. Like the rest of Phase 4 the functions work on the retail memory at the retail addresses; everything outside them (the allocators, the file
// and package streams, the audio, the renderer, the CRT) is a service reported by its address (`engine_<address>`), so a captured forced call replays byte for byte.

// object and list layout shared by the sweeps (every object of the game's lists)
namespace object {

constexpr std::uint32_t kVtable = 0x00;          // the object's vtable (slot 0 deletes with a flag, slot 2 kills)
constexpr std::uint32_t kFlags = 0x04;           // visibility word
constexpr std::uint32_t kPosition = 0x08;        // three dwords: x, y, z (Q12 world units)
constexpr std::uint32_t kNext = 0x20;            // the next object of the list
constexpr std::uint32_t kRegion = 0x1f;          // byte: the region the object's model lives in

}  // namespace object

// the globals the utility functions read
namespace globals {

constexpr std::uint32_t kRandA = 0x0056e28c;               // the generator of Rnd (three words)
constexpr std::uint32_t kRandB = 0x0056e290;
constexpr std::uint32_t kRandState = 0x0056e2ac;
constexpr std::uint32_t kExplosionStack = 0x0056e30c;      // dword: the explosion stack depth
constexpr std::uint32_t kNumberOfPlayers = 0x00533f3c;     // GNumberOfPlayers
constexpr std::uint32_t kGameMode = 0x00533f38;            // LevelEventGameModeSelector
constexpr std::uint32_t kPlayer0 = 0x0056a858;             // Player[0] (Player[1] follows)
constexpr std::uint32_t kPlayerRecord = 0x0056a960;        // the second-camera / owner record (an object)
constexpr std::uint32_t kVisibilityOwner = 0x0056a954;     // dword: the owner the visibility sweeps compare with kPlayerRecord
constexpr std::uint32_t kEnvironmentList = 0x0056db28;     // PsxEnvironmentObjectListHead
constexpr std::uint32_t kRegionValid = 0x0056d432;         // byte per region, stride 0x44: the region is loaded
constexpr std::uint32_t kPowerUpList = 0x0056b830;
constexpr std::uint32_t kActiveList = 0x0056af44;
constexpr std::uint32_t kGameList = 0x0056af40;
constexpr std::uint32_t kBaddyList = 0x0055f6bc;
constexpr std::uint32_t kRailList = 0x0055f704;            // the list of type-2 objects the visibility sweep ends with
constexpr std::uint32_t kViewIndex = 0x0055fa3c;           // dword: the viewport being drawn
constexpr std::uint32_t kViews = 0x0055f7c8;               // the viewport records (0xa4 bytes each): +4 camera position, +0x74 rotation matrix
constexpr std::uint32_t kViewStride = 0xa4;
constexpr std::uint32_t kCollisionBusy = 0x00567c84;       // dword: set while a line-of-sight query runs

}  // namespace globals

// the query record of the collision routines (0xa4 bytes; the port builds it in the scratch area)
namespace line {

constexpr std::uint32_t kStart = 0x00;           // three dwords
constexpr std::uint32_t kEnd = 0x0c;             // three dwords
constexpr std::uint32_t kHitY = 0x70;            // dword: hit position, y
constexpr std::uint32_t kSlope = 0x7a;           // short: the normal's y component (a flat enough floor is < -0xbff)
constexpr std::uint32_t kHitBody = 0x68;         // dword: the object hit, 0 for none
constexpr std::uint32_t kFlag = 0x88;            // byte
constexpr std::uint32_t kBytes = 0xa4;

}  // namespace line

constexpr std::uint32_t kInitCollisionQuery = 0x004624d0;   // M3D_InitCollisionQuery(info)
constexpr std::uint32_t kZoneCollision = 0x00466090;        // M3D_QueryZoneCollision(info, 1)
constexpr std::uint32_t kIntegerSqrt = 0x004f53b0;          // FUN_004f53b0: truncated square root of an unsigned sum (x87 fsqrt)

class CoreKit final : public pcplatform::PcKit {
public:
    CoreKit(RetailMemory& memory, RetailServices& services) : PcKit(memory, services), math_(memory, services) {}

    // ---- utils.cpp: random numbers, vectors and angles ----
    void pause() {}                                                                                           // 0x004c9300 Pause
    void initial_rand(std::uint32_t seed);                                                                    // 0x004c9320 Utils_InitialRand
    std::int32_t random(std::int32_t range);                                                                  // 0x004c9340 Rnd (trg::RetailRandom on the three words)
    void rotate_y(std::uint32_t out, std::uint32_t in, std::int32_t angle);                                   // 0x004c9390 Utils_RotateY (a Q12 vector turned about y, result >> 9 of >> 3 inputs)
    void rotate_y_q12(std::uint32_t out, std::uint32_t in, std::int32_t angle);                               // 0x004c9400 Utils_RotateYQ12 (the products of the Q12 vector)
    void unit_vector(std::uint32_t out, std::uint32_t in);                                                    // 0x004c9470 Utils_CalcUnit
    void direction_from_angles(std::uint32_t out, std::int32_t scale, std::uint32_t angles);                  // 0x004c9500 Camera_DirectionFromAnglesRaw
    std::uint32_t distance(std::uint32_t a, std::uint32_t b);                                                 // 0x004c9590 Utils_Dist
    std::uint32_t distance_xz(std::uint32_t a, std::uint32_t b);                                              // 0x004c95e0 Utils_XZDist
    std::uint32_t cosine_between(std::uint32_t a, std::uint32_t b, std::uint32_t c);                          // 0x004c96d0 Utils_VectorCosineQ12
    std::uint32_t look_angles(std::uint32_t angles, std::uint32_t from, std::uint32_t to);                    // 0x004c9770 Camera_BuildLookAngles
    void turn_towards(std::uint32_t from_xy, std::uint32_t from_z, std::uint32_t zero_out, std::uint32_t out, std::uint32_t to_xy, std::uint32_t to_z, std::int32_t scale);   // 0x004c9880 Utils_TurnTowards
    void clear_explosion_stack();                                                                             // 0x004c9cb0 Utils_ClearExplosionStack
    std::uint32_t name_equal(std::uint32_t a, std::uint32_t b, std::uint32_t count);                          // 0x004c9f50 CaseInsensitiveBoundedNameEqual
    std::uint32_t platform_ai(std::uint32_t object, std::uint32_t target, std::int32_t pitch_limit, std::int32_t yaw_limit, std::int32_t distance_limit);   // 0x004c9fd0 CPlatform__AI
    std::uint32_t acos_ratio_to_angle(std::int32_t value);                                                    // 0x004ca0a0 Camera_AcosRatioToAngle12
    std::uint32_t line_of_sight(std::uint32_t a, std::uint32_t b);                                            // 0x004ca0c0 Utils_LineOfSight
    std::uint32_t ground_height(std::uint32_t position, std::int32_t up, std::int32_t down);                  // 0x004ca140 Utils_GetGroundHeight
    std::uint32_t kill_objects_in_box(std::uint32_t low, std::uint32_t high, std::uint32_t list, std::uint32_t destroy);   // 0x004ca1e0 Utils_KillObjectsInBox
    std::uint32_t apply_bounds_to_lists(std::uint32_t low, std::uint32_t high);                               // 0x004ca270 RuntimeObject_ApplyBoundsToLists
    void update_visibility_bounds(std::uint32_t low, std::uint32_t high, std::uint32_t show, std::uint32_t inside);   // 0x004ca2d0 RuntimeObject_UpdateVisibilityBounds
    std::uint32_t spatial_attenuation(std::uint32_t position, std::int32_t near_distance, std::int32_t far_distance);   // 0x004ca690 Utils_CalculateSpatialAttenuation
    std::uint32_t pulse(std::int32_t value, std::int32_t period, std::int32_t scale);                         // 0x004ca890 Utils_Pulse
    void limit_range(std::uint32_t value, std::int32_t low, std::int32_t high);                               // 0x004ca8d0 Utils_LimitRange
    std::uint32_t vector_length(std::uint32_t vector);                                                        // 0x004ca8f0 Math_Vector3Length (mapped in the object-script port; the same code here for the normalise)
    std::uint32_t vector_squared_length(std::uint32_t vector);                                                // 0x004ca940 Math_Vector3SquaredLength (no listed caller)
    void vector_normalize(std::uint32_t vector);                                                              // 0x004ca980 Math_Vector3Normalize
    void vector_dead_zone(std::uint32_t vector);                                                              // 0x004ca9b0 Math_Vector3DeadZone
    std::uint32_t vector_add_out(std::uint32_t out, std::uint32_t a, std::uint32_t b);                        // 0x004cabb0 Math_Vector3AddOut
    std::uint32_t vector_subtract_out(std::uint32_t out, std::uint32_t a, std::uint32_t b);                   // 0x004cabf0 Vector3_Subtract
    std::uint32_t vector_mul_out(std::uint32_t out, std::uint32_t vector, std::uint32_t scalar);              // 0x004cac30 Math_Vector3MulScalarOut
    std::uint32_t scalar_mul_out(std::uint32_t out, std::uint32_t scalar, std::uint32_t vector);              // 0x004cac60 Math_ScalarMulVector3Out
    std::uint32_t vector_div_out(std::uint32_t out, std::uint32_t vector, std::uint32_t scalar);              // 0x004cac90 Math_Vector3DivScalarOut
    std::uint32_t vector_shift_right_out(std::uint32_t out, std::uint32_t vector, std::uint32_t count);       // 0x004cacd0 Math_Vector3ShiftRightOut
    std::uint32_t vector_shift_left_out(std::uint32_t out, std::uint32_t vector, std::uint32_t count);        // 0x004cad00 Math_Vector3ShiftLeftOut
    std::uint32_t vector_negate_out(std::uint32_t vector, std::uint32_t out);                                 // 0x004cad30 Math_Vector3NegateOut
    std::uint32_t vector_mask12(std::uint32_t vector);                                                        // 0x004cad60 Math_Vector3sMask12
    // unlisted functions of the same files (no Ghidra function; found in the gaps between listed ones)
    std::uint32_t is_environment_object(std::uint32_t object);                                                // 0x004ca620 Utils_IsEnvironmentObject
    void shuffle_shorts(std::uint32_t array, std::int32_t count);                                             // 0x004ca650 Utils_ShuffleShorts
    void flag_list_objects(std::uint32_t head);                                                               // 0x004c9c90 Utils_FlagListObjects
    void copy_c_string(std::uint32_t source, std::uint32_t destination);                                      // 0x004c9fb0 Utils_StringCopy

    // ---- the relocatable-module loader (0x004ac020..0x004ac330) ----
    std::uint32_t file_name_hash(std::uint32_t name);                                                         // 0x00448cb0 FileIO_HashName (the same bitwise CRC over the lower-cased name)
    void module_destroy();                                                                                    // 0x004ac060 RelocatableModule_Destroy (asserts: nothing frees a module)
    std::int32_t compressed_size(std::uint32_t source, std::int32_t length, std::int32_t method);             // 0x004e9630 PKR2_CompressedSize
    std::int32_t best_method(std::uint32_t source, std::int32_t length, std::uint32_t size_out);              // 0x004e9670 PKR2_BestMethod
    std::uint32_t relocation_hash(std::uint32_t name);                                                        // 0x004ac020 RelocatableModule_HashName (a bitwise CRC of the name, not inverted)
    void nothing() {}                                                                                         // 0x004ac0b0, 0x004e9930 (empty)
    void load_module_pair(std::uint32_t name, std::uint32_t argument);                                        // 0x004ac0c0 RelocatableModule_LoadPair
    void apply_relocations(std::uint32_t table, std::uint32_t base);                                          // 0x004ac2e0 RelocatableModule_ApplyRelocations

    // ---- the PKR2 package wrappers (0x004e9150..0x004e9b80, 0x004ebf10..) ----
    std::uint32_t compress_bibd(std::uint32_t source, std::int32_t length, std::uint32_t out_length);         // 0x004e9150 PKR2_CompressBIBD (byte counts)
    std::uint32_t compress_wibd(std::uint32_t source, std::int32_t length, std::uint32_t out_length);         // 0x004e9200 PKR2_CompressWIBD (16-bit counts)
    std::uint32_t compress_zlib(std::uint32_t source, std::int32_t length, std::uint32_t out_length);         // 0x004e92c0 PKR2_CompressZlib
    std::uint32_t decompress_bibd(std::uint32_t source, std::int32_t length, std::uint32_t size, std::uint32_t name);   // 0x004e9390 PKR2_DecompressBIBD
    std::uint32_t decompress_wibd(std::uint32_t source, std::int32_t length, std::uint32_t size, std::uint32_t name);   // 0x004e9480 PKR2_DecompressWIBD
    std::uint32_t decompress_zlib(std::uint32_t source, std::uint32_t length, std::uint32_t size, std::uint32_t name);  // 0x004e9570 PKR2_DecompressZlib
    std::uint32_t compress_resource(std::uint32_t entry, std::uint32_t data, std::uint32_t release);          // 0x004e96b0 PKR2_CompressResource (the entry's method picks the wrapper)
    std::uint32_t pkr_preload(std::uint32_t name);                                                            // 0x004e9780 PKR_PreLoad
    void pkr_unload(std::uint32_t name);                                                                      // 0x004e9b80 PKR_UnLoad
    std::uint32_t stream_construct(std::uint32_t self);                                                       // 0x004ebf10 (fastcall)
    std::uint32_t stream_scalar_delete(std::uint32_t self, std::uint32_t flags);                              // 0x004ebf50
    void stream_destruct(std::uint32_t self);                                                                 // 0x004ebf70 (fastcall)

    // ---- exp.cpp and the head file ----
    void exp_hit_env_item(std::uint32_t object, std::uint32_t face, std::uint32_t whole);                     // 0x00448860 Exp_HitEnvItem
    std::uint32_t exp_construct(std::uint32_t self, std::uint32_t position, std::uint32_t start, std::uint32_t red, std::uint32_t green, std::uint32_t blue, std::uint32_t base_argument, std::uint32_t extra);   // 0x00448990
    std::uint32_t exp_scalar_delete(std::uint32_t self, std::uint32_t flags);                                 // 0x00448aa0
    void exp_destruct(std::uint32_t self);                                                                    // 0x00448ac0
    void exp_move(std::uint32_t self);                                                                        // 0x00448ad0 Exp_Move (thiscall: the burst ages and spreads)
    void load_head();                                                                                         // 0x00448c60 FileIO_LoadHed
    void unload_head();                                                                                       // 0x00448c90 FileIO_UnloadHed

    // ---- main.cpp (0x00466b60..0x0046ae8f) ----
    void reset_demo_timer() {}                                                                                // 0x00466b60 ResetMainDemoTimer
    std::uint32_t class_new(std::uint32_t size);                                                              // 0x00466b70 CClass_OpNew
    void class_delete(std::uint32_t block);                                                                   // 0x00466bb0 CClass_OpDelete
    void calc_poly_buffer_end();                                                                              // 0x00466be0 CalcPolyBufferEnd
    void poll_host() {}                                                                                       // 0x00466c00 PollHost
    void draw_dim_overlay();                                                                                  // 0x00466de0 Display_DrawDimOverlay
    void load_skater_bitmap(std::uint32_t key, std::uint32_t name);                                           // 0x004674d0 Skater_LoadBitmap
    void flip_primitives();                                                                                   // 0x00467610 FlipPrimitives
    void render_stage(std::uint32_t viewport, std::uint32_t index);                                           // 0x00467c90 M3D_RenderStage
    void add_quad(std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1, std::uint32_t x2, std::uint32_t y2, std::uint32_t x3, std::uint32_t y3, std::uint32_t colour);   // 0x00468350 Display_AddQuad
    void add_rect(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height);               // 0x004684f0 Display_AddRect
    void display();                                                                                           // 0x00468520 Display
    void fps_init();                                                                                          // 0x00468b00 FPS_Init
    void animation_clock_update();                                                                            // 0x00468b30 AnimationClock_Update
    void play_away_init();                                                                                    // 0x00468cf0 PlayAway_Init
    void setup_appearance(std::uint32_t slot, std::uint32_t skater, std::uint32_t name);                      // 0x00468e90 Skater_SetupAppearance
    void create_skater_objects();                                                                             // 0x004691e0 SkaterObjects_Create
    std::uint32_t replay_apply_loaded_header();                                                               // 0x00469400 Replay_ApplyLoadedHeaderAndRestart
    void prepare_video_masks();                                                                               // 0x004695a0 Game_PrepareVideoMasks
    void configure_replay_streams();                                                                          // 0x00469700 Game_ConfigureReplayStreams
    std::uint32_t prepare_video_masks_wrapper();                                                              // 0x00469900 Game_PrepareVideoMasksWrapper
    void game_init();                                                                                         // 0x00469910 Game_Init
    std::uint32_t game_input_update();                                                                        // 0x004699f0 Game_InputUpdate
    void check_skaters_stop();                                                                                // 0x00469d30 CheckSkatersStop
    void skater_update_animation_objects();                                                                   // 0x00469a30 Skater_UpdateAnimationObjects (the level-start countdown)
    void game_gameplay_update();                                                                              // 0x00469de0 Game_GameplayUpdate
    void game_display();                                                                                      // 0x0046a0f0 Game_Display
    void main_loop_end();                                                                                     // 0x0046a1a0 MainLoop_End
    void level_teardown();                                                                                    // 0x0046a250 Level_Teardown
    void game_level_loop();                                                                                   // 0x0046a3a0 Game_LevelLoop
    void intro_movies();                                                                                      // 0x0046a500 IntroMovies
    void logo_screen();                                                                                       // 0x0046a580 LogoScreen
    void message_screen(std::uint32_t font_copy, std::uint32_t text);                                         // 0x0046a600 Game_MessageScreen
    std::uint32_t main_entry();                                                                               // 0x0046a770 main
    void launch_game(std::uint32_t level);                                                                    // 0x0046a8d0 LaunchGame
    // the bytes the replay does not compare (a 32-byte copy of an 18-byte local in M3D_RenderStage)
    [[nodiscard]] const std::vector<std::pair<std::uint32_t, std::uint32_t>>& scrap() const { return scrap_; }

    // ---- init.cpp (0x00458270..0x004588b7) ----
    void delete_list(std::uint32_t head);                                                                     // 0x004584d0 DeleteList
    void init_kill_all();                                                                                     // 0x00458270 Init_KillAll
    void init_cleanup(std::uint32_t mode);                                                                    // 0x00458500 Init_Cleanup
    void init_reset_memory(std::uint32_t bytes);                                                              // 0x00458690 Init_ResetMemory
    void init_restart();                                                                                      // 0x004586f0 Init_Restart
    void init_for_game();                                                                                     // 0x00458730 Init_ForGame
    void init_at_start();                                                                                     // 0x00458760 Init_AtStart
    void init_at_end();                                                                                       // 0x00458840 Init_AtEnd

    mathkit::MathKit& math() { return math_; }
    std::uint32_t crap_distance_of(std::uint32_t a, std::uint32_t b) { return crap_distance(a, b); }   // 0x004c9620 Utils_CrapDist
    // the integer square root (0x004f53b0) computed here instead of reported as a service
    void use_native_square_root() { native_square_root_ = true; }
    bool set_native_square_root(bool on) { const bool before = native_square_root_; native_square_root_ = on; return before; }   // (a function that owns the root for its duration)

private:
    using Word = std::uint32_t;
    static Word u(std::int32_t v) { return static_cast<Word>(v); }
    static std::int32_t s(Word v) { return static_cast<std::int32_t>(v); }
    std::uint32_t crap_distance(Word a, Word b);
    std::int32_t wrap_half_turn(std::int32_t angle) const;
    void fail_division(const char* where) const;
    std::uint32_t square_root(std::uint32_t value);
    void query_result(std::uint32_t info);
    // the services most of the layer shares: the loader's allocator (size, heap, 1, 0), Mem_Delete, Rnd(limit)
    std::uint32_t alloc(std::uint32_t size, std::uint32_t heap, std::uint32_t one, std::uint32_t zero) {
        s_.note("alloc", {size, heap, one, zero});
        return static_cast<std::uint32_t>(s_.value("alloc"));
    }
    void mem_delete(std::uint32_t block) { s_.note("mem_delete", {block}); }
    std::int32_t rnd(std::uint32_t limit) {
        s_.note("rnd", {limit});
        return s_.value("rnd");
    }
    // the unsigned-byte comparison of two C strings in retail memory (the inlined strcmp)
    int compare_strings(std::uint32_t a, std::uint32_t b) const;
    void copy_string(std::uint32_t to, std::uint32_t from);
    // a service that filled a buffer: the probe scripts the bytes as dwords (the last one padded)
    void take_filled(std::uint32_t destination, std::uint32_t bytes, const std::string& service);
    std::uint32_t rle_compress(std::uint32_t source, std::int32_t length, std::uint32_t out_length, unsigned width);
    std::uint32_t rle_decompress(std::uint32_t source, std::int32_t length, std::uint32_t size, std::uint32_t name, unsigned width);
    void fill_bytes(std::uint32_t destination, std::uint32_t count, std::uint8_t value);
    std::uint32_t call_wrapper(std::uint32_t target, std::uint32_t a, std::uint32_t b, std::uint32_t c);
    void read_resource(std::uint32_t destination, std::uint32_t size, std::uint32_t count, std::uint32_t stream);
    [[nodiscard]] std::string string_at(std::uint32_t address) const;
    void skip_flags_clear();
    void copy_system_font(std::uint32_t font);
    void draw_player_in_front();
    void versus_pointer();

    mathkit::MathKit math_;
    bool native_square_root_ = false;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> scrap_;
};

}  // namespace opentony::runtime::core
