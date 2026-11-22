#pragma once

#include "collision_kit.hpp"
#include "math_kit.hpp"
#include "rail_kit.hpp"
#include "seam_addresses_generated.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace opentony::runtime::skaterkit {

// Phase 4.15a: physics.cpp, the skater (CBruce) functions. A skater is an object of 0x3538 bytes; the functions work on it at the retail addresses (`self` is its address), on the game's globals and,
// through the 4.14a math layer, on small vectors in a scratch area. Everything outside them (the animation player, the trick score, the panel, the sound, the network layer) is a service reported by its
// address (`engine_<address>`), so a captured forced call replays byte for byte.

namespace skater {

constexpr std::uint32_t kPosition = 0x08;                  // three dwords: x, y, z (Q12 world units)
constexpr std::uint32_t kFlags = 0xd8;                     // dword: bit 1 = the skater faces the other way (fakie)
constexpr std::uint32_t kAnimationFrame = 0xf4;            // short: the frame of the animation being played
constexpr std::uint32_t kAnimationId = 0xf6;               // short: the animation being played
constexpr std::uint32_t kAnimationScale = 0x108;           // dword: the playing speed (0x10000 = 1.0)
constexpr std::uint32_t kFlipRequestA = 0x2c78;            // dword: a flip of the facing is wanted
constexpr std::uint32_t kFlipRequestB = 0x2c7c;            // dword: a flip of the orientation is wanted
constexpr std::uint32_t kFlipTurn = 0x2c08;                // dword: the turn that goes with a flip
constexpr std::uint32_t kFlipCount = 0x2c88;               // dword: flips left to count
constexpr std::uint32_t kFlipPhase = 0x2c8c;               // dword: the phase of the flip (bit 12 changes once per turn)
constexpr std::uint32_t kFlipSpeed = 0x2c90;               // dword: the phase step
constexpr std::uint32_t kTimeLeft = 0x2cc8;                // dword: the time left of the run
constexpr std::uint32_t kSpinReset = 0x2dd0;               // dword: cleared when a half-turn animation flips the facing
constexpr std::uint32_t kTrickBuffer = 0x2e80;             // dword: the trick buffer's length
constexpr std::uint32_t kOrientation = 0x2e58;             // nine shorts: the orientation matrix, as published to the renderer
constexpr std::uint32_t kStateCounter = 0x3044;            // dword: how many times the skater changed between ground and air
constexpr std::uint32_t kState = 0x30b8;                   // dword: 0 ground-less, 1 air, 2 ground, 5 rail, ...
constexpr std::uint32_t kPreviousState = 0x30c0;           // dword
constexpr std::uint32_t kSavedOrientation = 0x30c8;        // 32 bytes: a copy of the orientation
constexpr std::uint32_t kSavedPosition = 0x30e8;           // three dwords: a copy of the position
constexpr std::uint32_t kBasis = 0x30f4;                   // nine dwords: three vectors (0x30f4 forward, 0x3100 right, 0x310c up), as integers
constexpr std::uint32_t kLandingVelocity = 0x3118;         // three dwords: the forward vector scaled when the skater leaves the ground

}  // namespace skater

namespace globals {

constexpr std::uint32_t kAnimationClock = 0x005685f4;      // AnimationClock
constexpr std::uint32_t kAnimationTimeScale = 0x0056865c;  // AnimationTimeScale
constexpr std::uint32_t kGameMode = 0x00533f38;            // LevelEventGameModeSelector
constexpr std::uint32_t kCurrentLevel = 0x0056a898;        // CurrentLevel
constexpr std::uint32_t kTimeless = 0x0056a89c;            // dword: no time limit
constexpr std::uint32_t kTimeCountsUp = 0x029da3a4;        // byte
constexpr std::uint32_t kNetworkFlag = 0x006a3b21;         // byte: the netgame surface is live
constexpr std::uint32_t kNetworkStatus = 0x006a18bc;       // dword: bit 19 = the host ended the run
constexpr std::uint32_t kRunLocked = 0x00568658;           // dword
constexpr std::uint32_t kEndCause = 0x0056a8d0;            // dword
constexpr std::uint32_t kPlayerOne = 0x0056db5c;           // pointer: the skater a versus mode follows
constexpr std::uint32_t kLevelRecords = 0x0053c240;        // the level records, 0x1ac bytes each; the first byte says whether the end is announced
constexpr std::uint32_t kSecondPlayerMask = 0x0056a854;    // dword: xored into a player index in the mode 7
constexpr std::uint32_t kVibrationSettings = 0x0056a3e0;   // dword per player: the pad may rumble
constexpr std::uint32_t kBailLocked = 0x0056a890;          // dword
constexpr std::uint32_t kTricksBinary = 0x0056a894;        // TricksBinaryBase: the loaded tricks.bin
constexpr std::uint32_t kGrindScriptOffsets = 0x0056a840;  // GrindTrickScriptOffsets: shorts, offsets into the binary
constexpr std::uint32_t kStateScriptOffsets = 0x005685f0;  // TrickStateScriptOffsets
constexpr std::uint32_t kBinaryHeader0 = 0x00568688;       // the tables tricks.bin starts with: seven shorts, each an offset from the file's start to a table
constexpr std::uint32_t kPlayerTwoTable = 0x00568a4c;      // PlayerTwoTrickScriptTable
constexpr std::uint32_t kPlayerOneTable = 0x00568a50;      // PlayerOneTrickScriptTable
constexpr std::uint32_t kBinaryTable5 = 0x0056a844;
constexpr std::uint32_t kBinaryTable6 = 0x0056a850;
constexpr std::uint32_t kAnimationList = 0x005365ac;       // the animations the tricks use: shorts, -1 ends the fixed list; the extra animations follow, again ended by -1
constexpr std::uint32_t kExtraCursor = 0x0056b7f8;         // ExtraAnimationListCursor: where the next extra animation goes
constexpr std::uint32_t kExtraCount = 0x0056b7bc;          // ExtraAnimationCount
constexpr std::uint32_t kNumberOfPlayers = 0x00533f3c;     // GNumberOfPlayers
// the stat data
constexpr std::uint32_t kStatBonusScale = 0x005368ac;      // dword per stat: the share (percent of 30) a cheat or a power-up adds
constexpr std::uint32_t kStatLimit = 0x005368d4;           // dword per stat: the highest value
constexpr std::uint32_t kStatFacingScale = 0x00536884;     // dword per stat: how much a skater riding the wrong way loses (percent)
constexpr std::uint32_t kStatLockedBonus = 0x005368fc;     // dword per stat: what the bail lock adds
constexpr std::uint32_t kStatLockedLimit = 0x00536924;     // dword per stat
constexpr std::uint32_t kStatLevels = 0x0053a4fc;          // byte per stat and per character (0x170 bytes a character): the level in the multi-player games
constexpr std::uint32_t kSetRecords = 0x0053a3fc;          // the decks of each character's set: 0x170 bytes a set, 0x20 a deck (flags byte of the set at +0x118)
constexpr std::uint32_t kExtraDecks = 0x0053c078;          // the extra decks
constexpr std::uint32_t kPlayerBoost = 0x0056a2f0;         // dword per player: levels a player is given
constexpr std::uint32_t kPlayerChoice = 0x0056a2e0;        // byte per player
constexpr std::uint32_t kStatFlags = 0x0056b78c;           // byte per stat: set to 10 whenever the stat is read
constexpr std::uint32_t kAutoKickSettings = 0x0056a3d8;    // dword per player: the auto-kick option
constexpr std::uint32_t kTwoPlayers = 0x0056a85c;          // dword: the collision with the other skater is on
constexpr std::uint32_t kStaticGuard = 0x0056b7e4;         // byte: bit 0 = the function-local static at 0x0056b780 is built
constexpr std::uint32_t kLastMove = 0x0056b780;            // the function-local static: the direction of the last move (three dwords)
constexpr std::uint32_t kTriggerOwner = 0x0056a954;        // dword: the skater the trigger nodes are being checked for
constexpr std::uint32_t kStatOverride = 0x0056b7f4;        // dword: all stats are this level when it is not 0

}  // namespace globals

// the trick state of a skater (physics.cpp, the bail and the pad history)
namespace trick {

constexpr std::uint32_t kPlayerIndex = 0x2cc4;             // dword: which player the skater is (indexes the vibration settings)
constexpr std::uint32_t kPanel = 0x168;                    // the panel data embedded in the skater (CPanelData)
constexpr std::uint32_t kMovement = 0x4c;                  // three dwords: the velocity (its length is the speed)
constexpr std::uint32_t kScriptRunning = 0x2c68;           // dword: a trick script is running
constexpr std::uint32_t kScriptFlag = 0x2c6c;              // dword
constexpr std::uint32_t kBailAnimation = 0x29f0;           // word: the animation to fall back on after a bail
constexpr std::uint32_t kPadHistoryHead = 0x2b14;          // dword: the next entry of the pad history (32 entries of 8 bytes from 0x2a14: button, pressed, two pad bytes, time)
constexpr std::uint32_t kPadHistory = 0x2a14;

}  // namespace trick

constexpr std::uint32_t kPanelSetPlayerNumber = 0x0048d660;   // Panel_SetPlayerNumber(number, skater)
constexpr std::uint32_t kAnimationStart = 0x00480730;         // Animation_Start(skater; animation, from, to, blend) (thiscall, four arguments)
constexpr std::uint32_t kSoundPlay = 0x004ad620;              // SFX_PlayX(sound, volume, pan)
constexpr std::uint32_t kPauseAgreement = 0x004dece0;         // NetGame_PauseAgreement()
constexpr std::uint32_t kInitializeAndDispatch = 0x00466c10;  // LevelEvent_InitializeAndDispatch()
constexpr std::uint32_t kLandedValue = 0x0048cd40;            // TrickScore_ComputeLandedValue() (thiscall on the panel data)
constexpr std::uint32_t kCrowdReactToTrick = 0x0042f700;      // Crowd_ReactToTrick(value)
constexpr std::uint32_t kCrowdReactToBail = 0x0042f680;       // Crowd_ReactToBail(speed)
constexpr std::uint32_t kPanelBail = 0x0048d7b0;              // Panel_Bail(skater)
constexpr std::uint32_t kVideoPlaying = 0x004cde70;           // Video_Playing(): a movie is playing
constexpr std::uint32_t kActuatorOn = 0x004896a0;             // Pad_ActuatorOn(pad, a, b, c)
constexpr std::uint32_t kIsRemotePlayer = 0x004de010;         // NetGame_IsRemotePlayer(skater)
constexpr std::uint32_t kSetLocalFlag = 0x004de4d0;           // NetGame_SetLocalFlag(flag, value)
constexpr std::uint32_t kScriptExecute = 0x004be450;          // TrickScript_Execute() (thiscall)
constexpr std::uint32_t kOpenLoad = 0x00448dd0;               // FileIO_OpenLoad(name, 1) -> the loaded file
constexpr std::uint32_t kMemDelete = 0x0046f4d0;              // Mem_Delete(block)
constexpr std::uint32_t kAtExit = seam::crt::CRT_atexit;                 // CRT atexit(function)
constexpr std::uint32_t kResetCollisionFlags = 0x004c4b80;    // Trig_ResetCPCollisionFlags(player)
constexpr std::uint32_t kResetExecutedFlags = 0x004c4ba0;     // Trig_ResetCPExecutedFlags(player)
constexpr std::uint32_t kZoneBroadphase = 0x004660b0;         // M3D_ZoneCollisionBroadphase(info, 1)
constexpr std::uint32_t kApplyGapDefinition = 0x0048f690;     // Gap_ApplyDefinitionState(skater; name, value, flag, index) (thiscall: the callee pops the four arguments)
constexpr std::uint32_t kAdvancePointTimer = 0x0048f720;      // TrickScore_AdvancePointTimer(value) (thiscall on the skater)
constexpr std::uint32_t kRollbackStack = 0x0048e8a0;          // TrickScore_RollbackStack() (thiscall on the panel data)
constexpr std::uint32_t kAnglesFromMatrix = 0x004657c0;       // FUN_004657c0(out, matrix): three angles (shorts) from an orientation matrix
constexpr std::uint32_t kFindTrickConfig = 0x00416050;        // TrickConfig_FindByCharacter(character) -> the character's trick configuration record
constexpr std::uint32_t kGetCheat = 0x00416980;               // Career_GetCheat(number) -> al
constexpr std::uint32_t kTagStatModifier = 0x004b8410;        // Tag_StatModifier(skater, stat, value) -> the value in the tag mode
constexpr std::uint32_t kScriptReadWord = 0x004be3c0;         // TrickScript_ReadWord(&stream) -> a word, the stream moved on
constexpr std::uint32_t kScriptCommandSize = 0x004bf6c0;      // TrickScript_CommandSize(command) -> the next command
constexpr std::uint32_t kSelectManagerSlot = 0x004b18a0;      // Skater_SelectManagerSlot(0)
constexpr std::uint32_t kBuildPlayerTables = 0x004bd1e0;      // TrickRuntime_BuildPlayerTables()

// where the port builds the small vectors the retail code keeps in stack locals
constexpr std::uint32_t kLocalVector = pcplatform::kScratchBase + 0x600;

class SkaterKit final : public collision::CollisionKit {
public:
    SkaterKit(RetailMemory& memory, RetailServices& services) : CollisionKit(memory, services), math_(memory, services), rail_(memory, services) {}

    // ---- the pose and the state of a skater (physics.cpp, 0x0048eb70..0x0049e564) ----
    std::uint32_t ping_pong(std::int32_t low, std::int32_t high, std::int32_t rate);   // 0x0048eb70 (low, high, frames per second): a value that runs between low and high and back with the animation clock
    void restore_saved_pose(std::uint32_t self);                                       // 0x0048f590 (fastcall): the saved orientation and position put back
    void set_panel_number(std::uint32_t self, std::uint32_t number);                   // 0x0048f5d0 (thiscall, one argument): hands the number and the skater to the panel
    void state_transition(std::uint32_t self, std::uint32_t state);                    // 0x004900b0 (thiscall, two arguments, the second unused)
    void flip_animation_transition(std::uint32_t self);                                // 0x004902e0 (fastcall)
    void reset_trick_buffers(std::uint32_t self);                                      // 0x004904c0 (fastcall)
    void remove_normal_component(std::uint32_t vector, std::uint32_t xy, std::uint32_t z);   // 0x00490610 (cdecl; a normal given as two packed shorts and a short)
    void damp_to_ground_plane(std::uint32_t vector, std::uint32_t normal, std::uint32_t out);   // 0x00490680 (cdecl): the vector without its component along the normal, with its length restored
    void fru_from_rotation_matrix(std::uint32_t self);                                 // 0x0049c7d0 (fastcall): the orientation shorts as the integer basis
    void publish_orientation_basis(std::uint32_t self);                                // 0x0049c850 (fastcall): the integer basis as the orientation shorts
    void handle_flips(std::uint32_t self);                                             // 0x0049e430 (fastcall)
    void handle_timer(std::uint32_t self);                                             // 0x0049e480 (fastcall)
    void animation_request_start(std::uint32_t self, std::uint32_t animation);                                                                          // 0x004903f0 (thiscall, two arguments, the second unused)
    void animation_request_start_default_end(std::uint32_t self, std::uint32_t animation, std::uint32_t from);                                         // 0x00490420 (thiscall, three)
    void animation_request_range(std::uint32_t self, std::uint32_t animation, std::uint32_t from, std::uint32_t to);                                   // 0x00490450 (thiscall, four)
    void animation_request_full(std::uint32_t self, std::uint32_t animation, std::uint32_t from, std::uint32_t to, std::uint32_t blend);               // 0x00490480 (thiscall, five)
    void animation_cycle(std::uint32_t self, std::uint32_t animation, std::uint32_t direction);                                                       // 0x00480890 (thiscall, two; any CSuper, not only a skater)

    // ---- the bail, the vibration and the pad history (physics.cpp, 0x0048fb20..0x00492030) ----
    void trick_bail(std::uint32_t self);                                               // 0x0048fb20 (fastcall): the trick state cleared, the crowd and the panel told, the pad rumbles
    void vibrate(std::uint32_t self, std::uint32_t a, std::uint32_t b, std::uint32_t c);   // 0x0048ffc0 (thiscall, three arguments): rumble the pad of the skater's player
    void bail(std::uint32_t self, std::uint32_t script, std::uint32_t cause);          // 0x004904d0 (thiscall, two arguments)
    void bail_when_flipping(std::uint32_t self);                                       // 0x004916d0 (fastcall, one argument not used)
    void grind_trick_start(std::uint32_t self, std::uint32_t index);                   // 0x00491c10 (thiscall, one argument)
    std::uint32_t pad_press_time(std::uint32_t self, std::uint32_t button, std::uint32_t skip);          // 0x00491da0 (thiscall, two arguments): when a button was last pressed, -1 for never
    std::uint32_t pad_pressed_with(std::uint32_t self, std::uint32_t first, std::uint32_t second, std::uint32_t window);   // 0x00491e50 (thiscall, three): one of two buttons pressed now, the other within the window
    std::uint32_t pad_both_recent(std::uint32_t self, std::uint32_t first, std::uint32_t second, std::uint32_t window);    // 0x00491f40 (thiscall, three): both buttons pressed within the window

    // ---- the tricks.bin loader (physics.cpp, 0x00492a90..0x00492e90) ----
    std::uint32_t tricks_load_binary();                                                // 0x00492a90 Tricks_LoadBinary: 1 when it loaded the file just now, 0 when it was loaded
    void unload_gtricks();                                                             // 0x00492b20 UnloadGTricks
    void reset_extra_animations();                                                     // 0x00492b60: the list of extra animations empty, after the fixed list
    void register_animation(std::uint32_t animation);                                  // 0x00492ba0 TrickScript_RegisterAnimation (cdecl, a short)
    std::uint32_t animation_in_lists(std::uint32_t animation);                         // 0x00492c70 (cdecl, a short): is the animation in the fixed or the extra list (al)
    void parse_stream(std::uint32_t stream);                                           // 0x00492d10 TrickScript_ParseStream (cdecl)
    void parse_table(std::uint32_t table);                                             // 0x00492d50 TrickScript_ParseTable (cdecl)
    void trick_script_initialize();                                                    // 0x00492de0 TrickScript_Initialize (cdecl)
    std::uint32_t trick_script_command_size(std::uint32_t command);                    // 0x004bf6c0 TrickScript_CommandSize (cdecl)
    std::uint32_t trick_script_read_word(std::uint32_t cursor);                        // 0x004be3c0 TrickScript_ReadWord (cdecl)

    // ---- the stats of a skater (physics.cpp, 0x0048f090..0x0048f590) ----
    std::uint32_t deck_record(std::uint32_t set, std::uint32_t deck);                  // 0x0048f090 (cdecl): the record of a deck of a skater's set (the standard decks, or the extra ones the set allows)
    void init_stats(std::uint32_t self);                                               // 0x0048f1d0 (fastcall): the stat levels of a skater from the best deck its character has, and the bonus points they give
    std::int32_t profile_value(std::uint32_t self, std::uint32_t selector);            // 0x0048f3a0 (thiscall, one argument): a stat (0..10) of the skater, 10 per level, with the cheats, the modes and the facing

    // ---- small handlers of physics.cpp (0x00494020..0x0049e670) ----
    void advance_point_timer_when_moved(std::uint32_t self);                           // 0x00494020 (fastcall): the point timer is advanced by the distance the skater moved since the last time
    void animation_completion_check(std::uint32_t self);                               // 0x00496280 (fastcall)
    void nothing() {}                                                                  // 0x004962f0 (empty)
    void clear_trick_flags(std::uint32_t self);                                        // 0x00496300 (fastcall): a trick script that is not carried on stops and its score is taken back
    void play_takeoff_sound(std::uint32_t kind);                                       // 0x0049a1b0 (cdecl)
    void play_landing_sound(std::uint32_t kind);                                       // 0x0049a1e0 (cdecl)
    void orientation_angles(std::uint32_t self);                                       // 0x0049c8d0 (fastcall): the angles of the orientation (with the up axis negated for the conversion)
    void blocked_physics_reset(std::uint32_t self);                                    // 0x0049d8a0 (fastcall): a blocked skater loses its sideways speed and counts recent pad presses
    void update_nollie_flip(std::uint32_t self);                                       // 0x0049e570 (fastcall)

    // ---- the damping and the collisions with the world and with the other skater (physics.cpp, 0x0049d2f0, 0x0049d480, 0x0049d740) ----
    void damp_response_vector(std::uint32_t self);                                     // 0x0049d480 (fastcall): the speed held under a stat-set limit, slowly bled off when auto-kick is off
    void collide_with_other_player(std::uint32_t self);                                // 0x0049d2f0 (fastcall): two skaters close together: one of them bails
    void check_for_triggers(std::uint32_t self);                                       // 0x0049d740 (fastcall): the trigger nodes along the line the skater moved this tick

    // ---- the footplant, the manual and the special basis (physics.cpp, 0x004993f0, 0x004995d0, 0x00497bb0) ----
    void enter_footplant_state(std::uint32_t self);                                    // 0x004993f0 (fastcall)
    void update_special_basis_state(std::uint32_t self);                               // 0x004995d0 (fastcall)
    void maybe_manual(std::uint32_t self, std::uint32_t unused, std::uint32_t reset);  // 0x00497bb0 (thiscall, two arguments)

    // ---- the basis and the rail exit (physics.cpp, 0x00493e10, 0x00494090, 0x00494180) ----
    void rebuild_sideways_basis(std::uint32_t self);                                   // 0x00493e10 (fastcall): the up vector straight up, the right vector from the forward and up, the up vector from right and forward
    void grind_off_sound(std::uint32_t self);                                          // 0x00494090 (fastcall): the sound of leaving a rail by the rail's type
    void leave_rail(std::uint32_t self);                                               // 0x00494180 (fastcall)

    // ---- the model parts (physics.cpp, 0x0048fbe0..0x0048fd60) ----
    std::uint32_t find_part_index(std::uint32_t self, std::uint32_t part);             // 0x0048fbe0 (thiscall, one argument): where a part number is in the skater's order of parts
    void offset_part_vertices(std::uint32_t self, std::uint32_t amount);               // 0x0048fc90 (thiscall, one argument): the vertices of three parts moved by the change of an amount
    void fatten(std::uint32_t self, std::uint32_t amount, std::uint32_t undo, std::uint32_t kind);   // 0x0048fd60 (thiscall, three arguments): the parts of the model scaled

    // ---- the rail events and the effects (physics.cpp, 0x00490ae0, 0x00490c70, 0x00490e30, 0x00493f40, 0x00495af0) ----
    void register_rail_trick_object(std::uint32_t self, std::uint32_t node);           // 0x00490ae0 (thiscall, one argument)
    void complete_rail_gap(std::uint32_t self);                                        // 0x00490c70 (fastcall)
    void enter_handplant(std::uint32_t self);                                          // 0x00490e30 (fastcall)
    void spawn_tag_glass(std::uint32_t self);                                          // 0x00493f40 (fastcall)
    void do_terrain_effects(std::uint32_t self);                                       // 0x00495af0 (fastcall)

    // ---- the temporary transform (physics.cpp, 0x0048ec00) ----
    void temp_transform(std::uint32_t self);                                           // 0x0048ec00 (fastcall): the orientation turned by the pending rotation and the balance lean
    void rail_update(std::uint32_t self);                                              // 0x004aaf70 Rail_Update (cdecl, the skater): which rail the skater may grind
    void update_rail_physics(std::uint32_t self);                                      // 0x00494210 (fastcall): the skater on a rail
    void update_swept_contact_state(std::uint32_t self);                               // 0x00499710 (fastcall): the move over the tick, the swept line and the ground under the skater
    void pre_physics_ollie(std::uint32_t self);                                        // 0x0049a280 (fastcall): the charge of the jump, the jump and the landing before the physics step
    void upright_correction(std::uint32_t self);                                       // 0x0049c330 (fastcall): the skater turned upright a little
    void handle_continuous_sounds(std::uint32_t self);                                 // 0x0049c970 (fastcall): the grind, roll and wheel voices
    // ---- the trick recognition (skater_trick_input.cpp, physics.cpp 0x00491c90..0x00492ea0) ----
    std::uint32_t derive_trick_pattern(std::uint32_t self);                            // 0x00492120 (thiscall): the stick's direction pattern 0..8
    void record_trick_state(std::uint32_t self, std::uint32_t button, std::uint32_t value);   // 0x00491c90 (thiscall, two arguments): a changed button state into the history ring
    void refresh_trick_history(std::uint32_t self);                                    // 0x00492190 (thiscall)
    std::int32_t match_trick_history(std::uint32_t self, std::uint32_t start, std::uint32_t direction, std::uint32_t max_age, std::uint32_t stop, std::uint32_t skip_special);   // 0x00492560 (thiscall, five)
    void consume_trick_record(std::uint32_t self, std::uint32_t action, std::uint32_t pressed);   // 0x00492030 (thiscall, two)
    void queue_trick_start(std::uint32_t self, std::uint32_t script, std::uint32_t action, std::uint32_t timestamp, std::uint32_t flag_4000, std::uint32_t flag_2000, std::uint32_t flag_8000);   // 0x00492290 (thiscall, six)
    void start_pending_trick(std::uint32_t self);                                      // 0x00492400 (thiscall)
    void select_trick_sequence(std::uint32_t self);                                    // 0x004925e0 (thiscall)
    void trick_script_update(std::uint32_t self);                                      // 0x00492ea0 TrickScript_Update (thiscall)
    void update_brake_mode(std::uint32_t self);                                        // 0x0049df00 Skater_UpdateBrakeMode (thiscall): the stop (skater_brake.cpp)
    void physics_dispatcher(std::uint32_t self);                                       // 0x0049db80 Skater_PhysicsDispatcher (thiscall): the physics of the state (skater_dispatcher.cpp)
    void ground_preparation(std::uint32_t self);                                       // 0x0049dad0 Skater_GroundPreparation (thiscall): the ollie button's second press (skater_ground_steps.cpp)
    void run_grounded_bounce_recovery(std::uint32_t self);                             // 0x0049d9c0 Skater_RunGroundedBounceRecovery (thiscall): the position put back after a double bounce
    void outer_floor_recovery(std::uint32_t self, std::uint32_t x, std::uint32_t y, std::uint32_t z);   // 0x00490730 Skater_OuterFloorRecoveryCheck (cdecl, the skater, the position and a message): the floor under the skater (skater_floor_recovery.cpp)
    void update_acceleration(std::uint32_t self);                                      // 0x0049b010 Skater_UpdateAcceleration (thiscall): the push-off and the brake (skater_acceleration.cpp)
    void update_heading(std::uint32_t self);                                           // 0x00492f20 Skater_UpdateHeading (thiscall): the lean animation of the turn (skater_heading.cpp)
    void action_physics_step(std::uint32_t self);                                      // 0x00493370 Skater_ActionPhysicsStep (thiscall): the queued turn, the spin counter and the turn accumulator (skater_action_step.cpp)
    void physics_frame(std::uint32_t self);                                            // 0x0049e680 Skater_PhysicsFrame (thiscall): the skater's physics frame (skater_physics_frame.cpp)
    void do_ground_physics(std::uint32_t self);                                        // 0x00496550 Skater_DoGroundPhysics (thiscall): the move, its collision, the floor and the ground's gravity and brakes (skater_ground_physics.cpp)
    void platform_bounce(std::uint32_t skater);                                        // 0x004a12d0 Platform_HandleBounce (cdecl): the bouncy props the skater's swept box touches are thrown (skater_ground_physics.cpp)
    void ground_surface_response(std::uint32_t self);                                  // 0x00496360 Skater_GroundSurfaceResponseStep (thiscall): the slope response and the phase accumulator (skater_ground_physics.cpp)
    void apply_turn_velocity_phase(std::uint32_t self, std::uint32_t amount, std::uint32_t follow, std::uint32_t second);   // 0x0049b500 Skater_ApplyTurnVelocityPhase (thiscall, three): the orientation turned about the vertical, the velocity with it (skater_ground_physics.cpp)
    void surface_response_correction(std::uint32_t self, std::uint32_t argument);      // 0x0049c060 Skater_ApplySurfaceResponseCorrection (thiscall, one): the slope's correction +0x3124 and its side state +0x2e24 (skater_ground_physics.cpp)
    void position_write_path(std::uint32_t self, std::uint32_t x, std::uint32_t y, std::uint32_t z);   // 0x00496060 Skater_PositionWritePath (thiscall, three): the new position, along fewer axes where the zone query meets something (skater_ground_physics.cpp)
    void collision_response(std::uint32_t self, std::uint32_t normal_xy, std::uint32_t normal_z, std::uint32_t bump);   // 0x0049bad0 Skater_ApplyCollisionResponse (thiscall, three): the velocity off the surface, the orientation turned about the vertical (skater_ground_physics.cpp)
    void rebuild_orientation_recovery(std::uint32_t self);                             // 0x0049d080 Skater_RebuildOrientationRecovery (thiscall): the basis eased towards the ground's normal (skater_ground_physics.cpp)
    void exit_ground_collision_recovery(std::uint32_t self);                           // 0x004956f0 Skater_ExitGroundCollisionRecovery (thiscall): the skater leaves the ground after a hit (skater_ground_physics.cpp)
    void air_collision_recovery(std::uint32_t self, std::uint32_t normal_xy, std::uint32_t normal_z);   // 0x00491780 Skater_AirCollisionOrientationRecovery (thiscall, two): the air hit's response, the landing or the bail (skater_ground_physics.cpp)
    void landing_cleanup(std::uint32_t self);                                          // 0x004914d0 Skater_LandingCleanup (thiscall): the landing's counters, the bail or the grind (skater_ground_physics.cpp)
    void ground_post_collision(std::uint32_t self);                                    // 0x00495cc0 Skater_GroundPostCollision (thiscall): the sway after a ground collision, the bail or the landing (skater_ground_physics.cpp)
    std::uint32_t grounded_bounce_probe(std::uint32_t self, std::uint32_t side, std::uint32_t distance, std::uint32_t respond);   // 0x004957c0 Skater_GroundedBounceProbe (thiscall, three): 1 when a wall was met and the skater moved off it (skater_ground_physics.cpp)
    void air_landing_orientation_recovery(std::uint32_t self, std::uint32_t normal_xy, std::uint32_t normal_z);   // 0x00497960 Skater_AirLandingOrientationRecovery (thiscall, two): the basis turned over on a landing on a wall (skater_ground_physics.cpp)
    void air_recovery_for_hit(std::uint32_t self, std::uint32_t record);                // 0x00497aa0 Skater_AirCollisionRecovery (thiscall, one): the lip the skater met in the air: the state transition (2 or 1), the speed taken off along the hit's normal, the take-off (skater_air_physics.cpp)
    void update_air_basis(std::uint32_t self);                                         // 0x00497df0 Skater_UpdateAirBasis (thiscall): the up vector's y eased down 500 (or reset), the right and forward vectors rebuilt by two cross products, the basis published (skater_air_physics.cpp)
    void do_air_physics(std::uint32_t self);                                         // 0x00497f40 Skater_DoPhysicsInAir (thiscall): the air control, the move, the lean and the spin, then the line of the move: a landing, a wall ride, a bail or a throw back (skater_air_physics.cpp)
    void start_grind(std::uint32_t self, std::uint32_t geometry_hint);                 // 0x00490ef0 Skater_StartGrind (thiscall, one): the rail entry: the skater on the rail point, the speed, the turn flags, the grind trick, the sound and the velocity along the rail (skater_start_grind.cpp)
    void trick_script_execute(std::uint32_t self);                                     // 0x004be450 TrickScript_Execute (thiscall): the skater's trick script runs until a command returns to the frame loop (skater_trick_script.cpp)

    mathkit::MathKit& math() { return math_; }

private:
    std::uint32_t node_type(std::uint32_t node);                                       // the type of a node of the level's node table
    std::uint32_t linked_object(std::uint32_t links);                                  // the first of the first two linked nodes that is a trick object (type 0xe or 0xc), 0 for none
    std::uint32_t linked_identifier_at(std::uint32_t object);                          // where the identifier that follows an object's data is
    std::string bytes_string(std::uint32_t at, std::uint32_t size) {
        const auto bytes = m_.bytes(at, size);
        return std::string(bytes.begin(), bytes.end());
    }
    void unit_vector(std::uint32_t out, std::uint32_t in);                             // 0x004c9470 Utils_CalcUnit (through the GTE)
    void vector_normalize(std::uint32_t vector);                                       // 0x004ca980 Math_Vector3Normalize
    void normalize_fixed12(std::uint32_t vector);                                      // 0x00465f60 Math_NormalizeFixed12 (in place, through the GTE)
    std::int32_t vector_length(std::uint32_t vector);                                  // 0x004ca8f0 Math_Vector3Length
    void trick_script_start(std::uint32_t self, std::uint32_t script);                 // 0x00491b80 (the retail-memory copy of the 4.7a function)
    std::uint32_t trick_history_clear(std::uint32_t self);                             // 0x004924f0 (thiscall): 1 unless the history holds a newer release of the selected button
    std::int32_t trick_resolve_frame(std::uint32_t self, std::int32_t operand);        // 0x004be3e0 (thiscall, one argument): a script's frame number in the animation's range
    using Word = std::uint32_t;
    static Word u(std::int32_t v) { return static_cast<Word>(v); }
    static std::int32_t s(Word v) { return static_cast<std::int32_t>(v); }
    void request_animation(Word self, Word animation, Word from, Word to, Word blend);
    void rotate_orientation(Word self, Word source);
    void multiply_columns(Word source, Word factor, Word target);                      // the GTE's rotation from 'source' (nine shorts), then column c of 'target' = that rotation times column c of 'factor' (rtir)
    void cast_line(Word info);
    void zone_query(Word info);
    std::array<std::uint32_t, 3> decode_node_position(Word node);
    std::array<std::uint32_t, 3> unit_of(const std::array<std::uint32_t, 3>& v);
    void stand_on_surface(Word self, Word info);
    void remember_normal(Word self, Word info);
    void negate_vector(Word source, Word destination);
    void wall_bounce(Word self);                                                       // (inside 0x0049e680) the wall test and the lean's distance query
    std::string query_line(Word record) const;                                         // the first 0x18 bytes of a query record, as the call's text
    void thiscall(Word target, Word self, std::vector<Word> args = {}) {
        args.insert(args.begin(), self);
        engine(target, std::move(args));
    }
    Word thiscall_value(Word target, Word self, std::vector<Word> args = {}) {
        args.insert(args.begin(), self);
        return engine_value(target, std::move(args));
    }

    mathkit::MathKit math_;
    rail::RailKit rail_;
};

}  // namespace opentony::runtime::skaterkit
