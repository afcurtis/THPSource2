#pragma once

#include "trg_runtime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

namespace opentony::trg {

// Native model of the level-object script virtual machine (baddy.cpp).
//
// Retail runs one small bytecode program per scripted level object (TRG type-1
// nodes; the 0x00cb "script object" family is modelled here). The program
// lives in the TRG file, right after the node's fixed header. Each command is
// a little-endian u16 word: bit 0x4000 marks a command, bit 0x2000 (with
// 0x4000 clear) marks a field write, and a word with 0x2000 set and 0x8000
// clear used as an operand names a variable to read. A cursor into that
// stream is stored in the object itself (`+0x17c`), so a command that returns
// "stop" simply resumes at the same place on the next frame.
//
// Addresses: TRG_RunScriptStream 0x00401520, TRG_ExecuteBaddyScriptOpcode
// 0x004015e0, TRG_SkipConditionalBlock 0x004014a0, TRG_ReadScriptOperand
// 0x004027c0, TRG_ApplyScriptFieldWord 0x004027f0, TRG_GetScriptVariable
// 0x00402bb0, TRG_ApplyScriptCommand 0x00403410, CBaddy__Die 0x004013b0,
// CBaddy__SendDeathPulse 0x004013d0, per-frame update 0x004031a0.

// Script cursors are offsets into a virtual address space: the TRG file is at
// [0, file size) and a tiny scratch buffer (used by the read-modify-write
// forms of add/subtract) is at kScriptScratchBase.
inline constexpr std::uint32_t kScriptScratchBase = 0x80000000U;

inline constexpr std::size_t kScriptLabelCount = 8;
inline constexpr std::size_t kScriptRegisterCount = 6;
inline constexpr std::size_t kScriptAttributeCount = 6;

using ScriptVec3 = std::array<std::int32_t, 3>;

// The fields of a retail script object that the interpreter reads or writes.
// Member comments give the retail object offset. Everything else in the
// retail object (model, collision, animation, list links) is owned by other
// slices and is reached through ObjectScriptServices.
struct ScriptObject {
    std::uint16_t state{};                  // +0x04 bit0 0x01, bit3 0x08, bit6 0x40; high byte bit0 = timed-wait mode
    ScriptVec3 position{};                  // +0x08 fixed-point, 12 fractional bits
    ScriptVec3 velocity{};                  // +0x4c cleared by command 0x4226
    std::uint16_t event_word{};             // +0x68 bit0 = "signalled" (set by SendSignalToNode)
    std::uint16_t list_flags{};             // +0x6a bit0 suspended, bit6 dead
    std::array<std::uint16_t, 3> params{};  // +0x14/+0x16/+0x18 constructor parameters
    std::uint16_t model_index{};            // +0x1a
    std::uint8_t region{};                  // +0x1f PSX region byte used to resolve model checksums
    std::uint32_t distance{};               // +0xb8 nearest-player distance from the last object loop
    std::uint16_t node{0xffff};             // +0xb0 source TRG node
    std::uint16_t field_ae{};               // +0xae
    std::uint16_t field_b4{};               // +0xb4
    std::int32_t field_108{};               // +0x108 (sign-extended by 0x2121)
    std::uint8_t active{1};                 // +0x16c script still running
    std::uint8_t byte_16f{};                // +0x16f
    std::uint8_t byte_170{};                // +0x170 also counts signals in the frame update
    std::uint8_t death_pulse_sent{};        // +0x171
    std::uint8_t byte_172{};                // +0x172
    std::uint8_t byte_173{};                // +0x173
    std::uint16_t signal_latch{};           // +0x174 copy of event_word taken at frame start
    std::uint32_t flags{};                  // +0x178 bit1 set by 0x4227, bit8 by 0x4700
    std::uint32_t cursor{};                 // +0x17c
    std::array<std::uint32_t, kScriptLabelCount> labels{};  // +0x180
    std::array<std::uint16_t, kScriptAttributeCount> attributes{};  // +0x1a0
    std::uint16_t field_1ac{};              // +0x1ac
    std::uint16_t height_1ae{};             // +0x1ae subtracted (x 0x1000) for line-of-sight
    std::array<std::uint16_t, kScriptRegisterCount> registers{};  // +0x1b0
    std::uint16_t field_1bc{};              // +0x1bc
    std::uint16_t field_1be{};              // +0x1be
    std::uint32_t wait{};                   // +0x1c0 remaining wait
    // +0x1dc/+0x1e0: the base-class timed-wait fields (command 0x4280 with
    // state bit 0x0100 set). The 0xcb subclass reuses the same slots for its
    // attached-model count and array pointer, which the frame update reads.
    std::uint32_t field_1dc{};
    std::uint32_t field_1e0{};
    std::uint32_t context{};                // +0x1d8 owning skater/context id
    std::int32_t sound_handle{-1};          // +0x1e8
    std::int32_t sound_period{-1};          // +0x1ec
    std::uint32_t sound_scale{};            // +0x1f0

    // Fields only the platform (type-1 subtype 0x0192) object has. The 0xcb
    // layout reuses several of these offsets for other things (sound state at
    // +0x1e8..+0x1f0), so a platform keeps its own copy and ignores the
    // 0xcb-only members above.
    struct Platform {
        std::uint8_t byte_16e{};                             // +0x16e adds the world vector to the acceleration
        ScriptVec3 acceleration{};                           // +0x58 cleared at the top of every update
        std::array<std::uint8_t, 3> velocity_decay{};        // +0x6c shift counts (physics types 1 and 2)
        std::array<std::int16_t, 3> angular_velocity{};      // +0x70
        std::array<std::int16_t, 3> angular_acceleration{};  // +0x76
        std::array<std::uint8_t, 3> angular_decay{};         // +0x7c shift counts (physics types 1 and 2)
        std::array<std::int16_t, 3> scale{};                 // +0x28 model scale (Q12), state bit 0x200
        std::array<std::uint16_t, 3> scale_frames{};         // +0x1e4 frames left per axis
        std::array<std::int16_t, 3> scale_step{};            // +0x1ea per-frame change per axis
        ScriptVec3 target{};                                 // +0x1c4 move-to destination
        std::uint16_t bounce_count{};                        // +0x1f0
        std::uint16_t kind{0x0192};                          // +0x1f2 bounce kind (0x4309), 0x0192 at creation
        std::array<std::uint16_t, 3> extent{};               // +0x1f4 model extents used by the bounce probe
        std::uint16_t lifetime{};                            // +0x1fa frames until a bouncing object dies
        std::array<std::int16_t, 3> bounce_angles{};         // +0x1fc target orientation while bouncing
        std::int32_t sound_handle{};                         // +0x204
        std::int32_t sound_period{-1};                       // +0x208
        ScriptVec3 box{};                                    // +0x20c (0x4302)
    } platform;

    // Fields only a traffic car (type-1 subtypes 0xd5..0xdc) has.
    std::uint16_t subtype{};                // +0x3c
    struct Traffic {
        std::int32_t road_node{-1};         // +0x1dc node of the road segment the car is driving from
        std::int32_t sound_handle{-1};      // +0x1d0
        std::int32_t sound_period{-1};      // +0x1d4 (1 refreshes every frame, 0 for police)
        std::uint32_t sequence{};           // +0x1e0 index into the saved car positions
        std::int32_t latch{};               // +0x1e4 set while a player is beside the car
    } traffic;

    // Fields only a power-up (type-5 node, powerup.cpp) has. The shared offsets
    // are kept in other members: angular velocity +0x70 and its decay counts
    // +0x6c and the acceleration +0x58 live in `platform`.
    struct Powerup {
        std::uint32_t glow{};                 // +0xc8 the glow emitter (opaque handle, 0 when none)
        std::uint16_t glow_base{};            // +0xcc glow intensity base
        std::uint16_t glow_amplitude{};       // +0xce glow intensity swing
        std::uint8_t link_flag{};             // +0xd0 a node's link list runs when collected
        std::uint8_t visual{};                // +0xd1 0 until the model is resolved
        std::uint8_t motion{};                // +0xd2 1 while the power-up falls or is thrown
        std::uint8_t settled{};               // +0xd3 set once it has stopped
        std::uint8_t drop{};                  // +0xd4 resting height above the ground, in whole units
        std::uint16_t link_node{};            // +0xd6 node whose links run on pickup
        std::int32_t ground{};                // +0xd8 ground height under the power-up
        ScriptVec3 home{};                    // +0xdc where it hovers (and its physics position)
        std::uint16_t phase{};                // +0xe8 bob phase
        std::uint16_t amplitude_base{0x32};   // +0xea bob amplitude at full life
        std::uint16_t amplitude{0x32};        // +0xec bob amplitude now (shrinks while it expires)
        std::uint16_t rate{};                 // +0xee bob rate
        std::uint16_t timer{0xffff};          // +0xf0 frames left (0xffff: permanent)
        std::array<std::uint16_t, 2> follow{};// +0xf2/+0xf4
        std::uint16_t follow_scale{};         // +0xf6 non-zero: follows the camera
        std::int32_t age{};                   // +0xfc frames since creation
        ScriptVec3 shadow{};                  // +0x90 shadow position
        std::uint16_t shadow_height{};        // +0xaa
        std::uint16_t shadow_scale{};         // +0xa8
    } powerup;

    // TRG_ConstructSubtype0x00cb (0x00403000) state after construction.
    [[nodiscard]] static ScriptObject construct_cb(
        std::uint16_t node, std::uint32_t first_command, ScriptVec3 position,
        std::array<std::uint16_t, 3> params);
    // TRG_ConstructSubtype0x0192 (0x0049f250) state after construction.
    [[nodiscard]] static ScriptObject construct_platform(
        std::uint16_t node, std::uint32_t first_command, ScriptVec3 position,
        std::array<std::uint16_t, 3> params);
    // TRG_ConstructTrafficObject (0x00412640) state after construction. The
    // position is the road node's position (Y lowered by 0x6e000).
    [[nodiscard]] static ScriptObject construct_traffic(
        std::uint16_t node, std::uint16_t subtype, std::int32_t road_node, ScriptVec3 position,
        std::array<std::uint16_t, 3> params, std::uint32_t sequence);
};

struct ScriptRepeatedEffect {
    // FUN_00406070..00406200: the retail request always uses these constants;
    // they are recorded so a renderer can honour them.
    ScriptVec3 position{};
    std::array<std::uint16_t, 3> params{};
    std::uint16_t count{};
};

struct ScriptFlashEvent {
    // FUN_00449810: c0/c1/c2 become the blue/red/green channels.
    std::uint8_t c0{};
    std::uint8_t c1{};
    std::uint8_t c2{};
    std::uint16_t duration{};
    std::uint16_t sort{};
};

struct ScriptPowerupRequest {
    std::uint16_t kind{};
    ScriptVec3 position{};
    std::uint16_t word0{};
    std::uint16_t word1{};
    std::uint16_t lifetime{};
};

// What the platform scripts read from a model record (PsxModelTablesByRegion):
// the first byte's 0x10 bit and the six words at +0x0c..+0x16 (a bounding box
// as max/min pairs per axis).
struct ScriptModelInfo {
    bool flag_0x10{};
    std::array<std::int16_t, 6> box{};
};

// The part of an SLineInfo a bouncing platform reads back from a collision
// query (M3D_InitCollisionQuery 0x004624d0 + M3D_QueryZoneCollision 0x00466090).
struct ScriptCollisionHit {
    bool hit{};                            // hit_body != null
    ScriptVec3 position{};                 // +0x6c hit_position
    std::array<std::int16_t, 3> normal{};  // +0x78 hit_normal
};

// Everything the interpreter needs from the rest of the game. The defaults
// give an inert, deterministic world so unit tests only override what they
// exercise.
class ObjectScriptServices {
public:
    virtual ~ObjectScriptServices() = default;

    // Retail assertion (M3dUtils_InBetween with a false condition). The
    // retail build continues after it; so does the native interpreter.
    virtual void on_assertion(std::string_view) {}
    virtual void on_log(std::string_view) {}

    // World state read by variables and conditional commands.
    [[nodiscard]] virtual std::uint32_t game_mode_selector() const { return 0; }
    [[nodiscard]] virtual bool video_playing() const { return false; }
    [[nodiscard]] virtual std::uint32_t random(std::uint16_t) { return 0; }
    [[nodiscard]] virtual std::uint32_t simulation_time() const { return 0; }
    [[nodiscard]] virtual std::uint16_t global_2136() const { return 0; }
    // Retail DAT_0056bed8, stored beside a freshly started object sound.
    [[nodiscard]] virtual std::uint32_t sound_reference() const { return 0; }
    // The skater the script is currently targeting (retail DAT_0056a960).
    [[nodiscard]] virtual bool has_target() const { return false; }
    [[nodiscard]] virtual ScriptVec3 target_position() const { return {}; }
    [[nodiscard]] virtual std::uint32_t xz_distance(const ScriptVec3&, const ScriptVec3&) const {
        return 0;
    }
    [[nodiscard]] virtual std::uint32_t line_of_sight(const ScriptVec3&, const ScriptVec3&) const {
        return 0;
    }
    [[nodiscard]] virtual std::uint16_t find_model_index(std::uint32_t /*checksum*/, std::uint8_t /*region*/) {
        return 0;
    }

    // Career flags (per-level bits of the current skater profile). The test
    // includes the retail fallback byte used while a movie is playing.
    [[nodiscard]] virtual bool level_flag(std::uint16_t) const { return false; }
    virtual void set_level_flag(std::uint16_t) {}
    [[nodiscard]] virtual std::uint32_t level_goal_bits() const { return 0; }
    [[nodiscard]] virtual bool career_got_goal_type(std::uint32_t) const { return false; }
    virtual void on_goal_event(std::uint16_t) {}                     // 0x42c0

    // Presentation and audio.
    virtual void select_psx_region(std::string_view) {}              // 0x4200
    virtual void play_sound(std::int16_t) {}                         // 0x4290
    virtual void play_positional_sound(std::int16_t, const ScriptVec3&) {}  // 0x4291
    [[nodiscard]] virtual std::int32_t start_object_sound(std::int16_t /*id*/, bool /*positional*/) {
        return -1;
    }                                                                // 0x4507 / 0x4508
    virtual void stop_sound(std::int32_t) {}                         // 0x4507..0x4509
    [[nodiscard]] virtual bool effect_slots_available() const { return true; }
    virtual void on_repeated_effect(const ScriptRepeatedEffect&) {}  // 0x4292
    virtual void on_flash(const ScriptFlashEvent&) {}                // 0x429c
    virtual void apply_camera_shake(const ScriptVec3&, std::uint32_t /*type*/) {}  // 0x4298
    virtual void on_camera_height(std::uint32_t /*context*/, std::int32_t /*y*/) {}  // 0x429e
    virtual void on_global_word_42a2(std::uint16_t) {}               // 0x42a2
    virtual void animation_cycle(std::uint16_t, std::uint16_t) {}    // 0x4201
    virtual void animation_start(std::uint16_t) {}                   // 0x4202
    [[nodiscard]] virtual bool create_powerup(const ScriptPowerupRequest&) { return false; }  // 0x42a0

    // Power-ups (powerup.cpp). The level's state they read and the effects of a
    // pickup, each a separate call so a replay can compare them in order.
    [[nodiscard]] virtual std::uint32_t current_level() const { return 0; }                // CurrentLevel
    [[nodiscard]] virtual std::uint32_t powerup_draw_limit() const { return 0xffffffffU; } // _DAT_00563a3c
    [[nodiscard]] virtual std::uint32_t goal_progress_bits() const { return 0; }           // DAT_00568a24
    [[nodiscard]] virtual bool career_got_money(std::uint16_t /*node*/) const { return false; }
    // The model the level's pickup goal shows (Career_GoalIndex(3) and the level
    // table): checksum 0 when the goal has none yet.
    virtual void level_pickup_model(std::uint32_t& checksum, std::uint8_t& region) const {
        checksum = 0;
        region = 0;
    }
    [[nodiscard]] virtual std::int32_t ground_height(const ScriptVec3&) { return -1; }     // Utils_GetGroundHeight
    [[nodiscard]] virtual std::uint32_t create_powerup_glow(std::uint16_t /*subtype*/) { return 1; }
    virtual void update_powerup_glow(std::uint32_t, const ScriptVec3&, std::int32_t) {}
    virtual void destroy_powerup_glow(std::uint32_t) {}
    [[nodiscard]] virtual bool skater_on_ground(std::uint32_t /*context*/) const { return true; }      // skater +0x2f64 == 0
    [[nodiscard]] virtual bool skater_not_in_trick(std::uint32_t /*context*/) const { return true; }   // skater +0x30b8 == 0
    virtual void skater_trick_takeoff() {}                          // CBruce__Trick_TakeOff
    virtual void skater_trick_land(std::uint32_t /*context*/) {}    // CBruce__Trick_Land + the two trick words cleared
    virtual void career_give_money(std::uint16_t /*node*/, std::int32_t /*amount*/) {}
    virtual void career_give_goal_type(std::int32_t) {}
    virtual void career_level_pickup() {}
    [[nodiscard]] virtual bool goal_stat_taken(std::int32_t /*index*/) { return false; }  // FUN_0048d7c0
    virtual void show_message(std::uint16_t /*subtype*/) {}
    virtual void delete_messages() {}
    virtual void sound_pitch(std::int32_t /*handle*/, std::int32_t /*pitch*/) {}
    [[nodiscard]] virtual std::int32_t play_sound_handle(std::int16_t id) { play_sound(id); return 0; }

    // Traffic cars (0x00412960). Retail AnimationClock (0x005685f4) and
    // AnimationTimeScale (0x0056865c); the players the cars pass; the saved car
    // position table (0x004cde80 / 0x004cdef0) and the car-versus-skater
    // interaction (0x00412ef0).
    [[nodiscard]] virtual std::int32_t animation_clock() const { return 0; }
    [[nodiscard]] virtual std::int32_t animation_time_scale() const { return 0x100; }
    [[nodiscard]] virtual std::size_t player_count() const { return 1; }
    [[nodiscard]] virtual ScriptVec3 player_position(std::size_t) const { return {}; }
    virtual void save_car_position(std::uint32_t /*index*/, const ScriptVec3&, const std::array<std::uint16_t, 3>&) {}
    virtual void restore_car_position(std::uint32_t /*index*/, ScriptVec3&, std::array<std::uint16_t, 3>&) {}
    virtual void traffic_interaction(const ScriptObject&) {}

    // Platform (0x0192) objects.
    [[nodiscard]] virtual ScriptModelInfo model_info(std::uint8_t /*region*/, std::uint16_t /*index*/) { return {}; }
    // Retail DAT_0056a8d4: a frame counter the positional-sound refresh divides.
    [[nodiscard]] virtual std::int32_t tick_counter() const { return 0; }
    // Retail DAT_0056b770: added to the acceleration of objects with +0x16e set.
    [[nodiscard]] virtual ScriptVec3 platform_acceleration() const { return {}; }
    virtual void update_sound_position(std::int32_t /*handle*/, const ScriptVec3&) {}  // SFX_ModifyPos
    virtual void shatter_item(const ScriptObject&, int /*mode*/) {}                   // Shatter_Item
    virtual void item_flash(std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t) {} // CItem__Flash
    virtual void skater_grunt(std::uint32_t /*context*/) {}                           // SFX_Grunt
    virtual void vibrate(std::int32_t, std::int32_t, std::int32_t) {}                  // CBruce__Vibrate
    [[nodiscard]] virtual ScriptCollisionHit line_collision(const ScriptVec3& /*start*/, const ScriptVec3& /*end*/) {
        return {};
    }

    // Messaging to other nodes and objects.
    virtual void signal_node_links(std::uint16_t /*node*/) {}        // 0x42b1
    virtual void execute_node_links(std::uint16_t /*node*/) {}       // 0x42b2 and death pulse
    virtual void signal_objects_at_node(std::uint16_t) {}            // 0x42b3
    virtual void send_pulse(std::uint16_t) {}                        // 0x42b4
};

enum class ScriptClass {
    // Vtable 0x00518388: the base handler only.
    Base,
    // Vtable 0x005183b0 (type-1 subtype 0xcb): adds position-copy and sound
    // commands (0x4503..0x4509, 0x4700, 0x429b/0x429f/0x42a1).
    ScriptObjectCb,
    // Vtable 0x005194f8 (type-1 subtype 0x0192, platforms and bouncy props):
    // adds the move-to, scale, shatter and bounce commands and the physics
    // update (platform.cpp).
    Platform,
    // Vtable 0x005184e0 (type-1 subtypes 0xd5..0xdc): cars driving along roads
    // of type-1 nodes (car.cpp). They run no script.
    Traffic,
    // Vtable 0x00519684 (type-5 nodes): collectable power-ups.
    Powerup,
};

struct ScriptRunStats {
    std::size_t commands{};
    std::size_t assertions{};
    bool yielded{};
    bool finished{};
};

class ObjectScriptVm {
public:
    ObjectScriptVm(
        const TrgFile& file, ObjectScriptServices& services,
        ScriptClass script_class = ScriptClass::ScriptObjectCb);

    // TRG_RunScriptStream: resume the object's script from `cursor` and run
    // until a command yields or the 0x4100 terminator is reached.
    ScriptRunStats run(ScriptObject& object, std::uint32_t cursor);

    // The script-related part of the per-frame update (0x004031a0): latch and
    // clear the signal bit, count down a wait, run the script while it is
    // active, and kill the object once it is not. The retail update goes on to
    // refresh a looping positional sound and reposition attached models; those
    // presentation steps are not modelled here.
    ScriptRunStats update(ScriptObject& object, std::uint32_t frame_delta);

    // CBaddy__Die (0x004013b0) and CBaddy__SendDeathPulse (0x004013d0).
    void die(ScriptObject& object);

    // The platform (0x0192) update, CPlatform__Hit and CPlatform__Bounce
    // (platform_object.cpp). Only meaningful for ScriptClass::Platform.
    // 0x0049fef0: scale animation, signal latch, script (or the bounce
    // simulation when flag 0x10 is set), move-to, physics and sound refresh.
    ScriptRunStats update_platform(ScriptObject& object);
    // TRG_UpdateTrafficObject (0x00412960): drive along the road, turn to face
    // the road, and let nearby players trigger the car's sound.
    ScriptRunStats update_traffic(ScriptObject& object);
    // Power-ups. Powerup_Construct (0x004a7c50) as created from a type-5 node by
    // TRG_CreateObject: `flags` bit 2 drops the power-up to the ground, bit 3
    // throws it; `timer` is the frames it lives (0xffff: permanent).
    [[nodiscard]] ScriptObject create_powerup(
        std::uint16_t node, std::uint16_t subtype, const ScriptVec3& position, const ScriptVec3& velocity,
        std::uint8_t flags, std::uint16_t timer);
    // Powerup_Update (0x004a8ac0), CPowerUp__CheckAge (0x004a8620) and
    // CPowerUp__DoPhysics (0x004a8300).
    void update_powerup(ScriptObject& object);
    // CPowerUp__TakeEffect (0x004a86f0): a skater (context id) touched it.
    void take_powerup(ScriptObject& object, std::uint32_t skater, bool primary_skater);
    // Vtable slots +0x04 (hide: state bits, glow) and +0x08 (die).
    void hide_powerup(ScriptObject& object);
    void die_powerup(ScriptObject& object);
    // 0x004a0360 (virtual +0x18): damage an object that has hit points.
    void hit_platform(ScriptObject& object, std::int16_t damage);
    // 0x0049f4c0: start a bouncy platform flying from the skater's velocity.
    void bounce_platform(ScriptObject& object, std::uint32_t skater_context, const ScriptVec3& skater_velocity);

    // First command word of a type-1000 script point (TRG_DecodeNodePosition
    // returns the position record end; the runner adds 6 bytes of parameters).
    [[nodiscard]] std::uint32_t script_point_cursor(std::size_t node) const;

    [[nodiscard]] std::size_t assertion_count() const noexcept { return assertions_; }

    // Called with (address of the opcode word, opcode) before each command
    // executes. Used by the corpus tool and the retail trace comparison.
    using CommandTrace = std::function<void(std::uint32_t, std::uint16_t)>;
    void set_command_trace(CommandTrace trace) { trace_ = std::move(trace); }
    // Called with (variable id, result) after every script variable read.
    using VariableTrace = std::function<void(std::uint16_t, std::uint32_t)>;
    void set_variable_trace(VariableTrace trace) { variable_trace_ = std::move(trace); }

private:
    const TrgFile& file_;
    ObjectScriptServices& services_;
    ScriptClass script_class_;
    std::array<std::uint16_t, 8> scratch_{};
    std::size_t scratch_words_{};
    std::size_t assertions_{};
    CommandTrace trace_;
    VariableTrace variable_trace_;

    [[nodiscard]] std::uint16_t peek_word(std::uint32_t address) const;
    [[nodiscard]] std::uint16_t read_word(ScriptObject& object) const;
    [[nodiscard]] std::uint32_t read_u32_aligned(ScriptObject& object) const;
    void assert_that(bool condition, std::string_view message);

    [[nodiscard]] std::int32_t operand(ScriptObject& object, std::uint16_t word);
    [[nodiscard]] std::uint32_t read_operand_word(ScriptObject& object);
    [[nodiscard]] std::uint32_t variable(ScriptObject& object, std::uint16_t id);
    [[nodiscard]] std::uint32_t variable_value(ScriptObject& object, std::uint16_t id);
    void set_field(ScriptObject& object, std::uint16_t id);
    [[nodiscard]] bool execute(ScriptObject& object, std::uint16_t opcode);
    [[nodiscard]] bool execute_subclass(ScriptObject& object, std::uint16_t opcode);
    [[nodiscard]] bool execute_base(ScriptObject& object, std::uint16_t opcode);
    [[nodiscard]] bool execute_platform(ScriptObject& object, std::uint16_t opcode);
    // Field writes only platforms have (0x004a1060); false leaves `id` to the base handler.
    [[nodiscard]] bool platform_field(ScriptObject& object, std::uint16_t id);
    void set_platform_target(ScriptObject& object, const ScriptVec3& target);
    void platform_scale_animation(ScriptObject& object, std::size_t first_axis, std::size_t axis_count);
    void platform_physics(ScriptObject& object);
    void platform_move_to(ScriptObject& object);
    void bouncy_ai(ScriptObject& object);
    void traffic_proximity(ScriptObject& object, const ScriptVec3& direction);
    void powerup_check_age(ScriptObject& object);
    void powerup_physics(ScriptObject& object);
    void skip_conditional_block(ScriptObject& object);
    void stop_object_sound(ScriptObject& object);
    void unknown_command(std::string_view message);
};

} // namespace opentony::trg
