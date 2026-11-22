#pragma once

#include "collision_kit.hpp"
#include "core_kit.hpp"
#include "math_kit.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include <string>

namespace opentony::runtime::worldkit {

// Phase 4.15c: the world objects of a level: the object base class (`ob.cpp`, the linked lists, the flash and the animation pose), the trigger loader's classes (`trig.cpp`), the
// mechanisms, cars, power-ups, platforms and trick objects. Like the rest of Phase 4 the functions work on the retail memory at the retail addresses (`self` is the object); everything outside
// them (the allocators, the sound, the career, the collision queries, the sprite objects) is a service reported by address (`engine_<address>`).

// the object record every world object starts with (the base class has vtable 0x0051938c, derived classes their own)
namespace object {

constexpr std::uint32_t kVtable = 0x00;
constexpr std::uint32_t kFlags = 0x04;               // word: bit 1 (0x2) the object moves, bit 2 (0x4) its colour changed, 0x400 flashing
constexpr std::uint32_t kPosition = 0x08;            // three dwords (Q12 world units)
constexpr std::uint32_t kAngles = 0x14;              // three words
constexpr std::uint32_t kRegion = 0x1f;              // byte: the PSX region the object's model lives in
constexpr std::uint32_t kNext = 0x20;
constexpr std::uint32_t kColour = 0x24;              // dword 0x00bbggrr
constexpr std::uint32_t kPrevious = 0x34;
constexpr std::uint32_t kSavedColour = 0x38;         // dword: the flash's saved colour and flags (0x1000000: saved, 0x2000000: the object was already flashing)
constexpr std::uint32_t kFlashCount = 0x44;          // word: frames left
constexpr std::uint32_t kFlashStep = 0x46;           // three words: the per-frame change of red, green and blue
constexpr std::uint32_t kList = 0x64;                // dword: the list head the object belongs to while suspended
constexpr std::uint32_t kState = 0x6a;               // word: 0x1 suspended, 0x2 can be suspended, 0x8 sprite wanted, 0x10, 0x40 dying, 0x80 delete queued
constexpr std::uint32_t kSprite = 0xa4;              // dword: the object's sprite object (0 for none)

constexpr std::uint32_t kBaseVtable = 0x0051938c;
constexpr std::uint32_t kRecordVtable = 0x00519388;  // what the record helper leaves in the vtable word

}  // namespace object

// where the port builds the records the retail code keeps in stack locals (a call's pointer argument is recorded as this address: fcall `stack_to`)
constexpr std::uint32_t kScratch = pcplatform::kScratchBase + 0xc00;

constexpr std::uint32_t kSuspendedList = 0x0056af44;     // the list of suspended objects (the active list is 0x0056af40)
constexpr std::uint32_t kSpriteFlag = 0x005244cc;        // dword: set while the sprite constructor's memory is the display's

class WorldKit final : public collision::CollisionKit {
public:
    WorldKit(RetailMemory& memory, RetailServices& services) : CollisionKit(memory, services), core_(memory, services), math_(memory, services) {}

    // ---- the object base class (world_object.cpp, 0x0047fe30..0x00481150) ----
    void select_psx_region(std::uint32_t self, std::uint32_t name);                             // 0x0047fe30 Object_SelectNamedPsxRegion (thiscall, one argument)
    std::uint32_t spool_find_enviro_item(std::uint32_t key);                                    // 0x004b1ef0 Spool_FindEnviroItem (cdecl)
    void item_flash(std::uint32_t self, std::int32_t frames, std::uint32_t red, std::uint32_t green, std::uint32_t blue);   // 0x0047fe90 CItem__Flash (thiscall, four arguments)
    void item_flash_update(std::uint32_t self);                                                 // 0x0047ffc0 CItem__FlashUpdate
    std::uint32_t object_construct(std::uint32_t self);                                         // 0x00480060 (the base constructor)
    std::uint32_t object_scalar_delete(std::uint32_t self, std::uint32_t flags);                // 0x00480140 (thiscall)
    void object_destruct(std::uint32_t self);                                                   // 0x00480160 (and its jump thunk at 0x00480720)
    void record_destruct_thunk(std::uint32_t self);                                             // 0x004801c0 (a jump to 0x0047fe20)
    void object_list_push_head(std::uint32_t self, std::uint32_t list);                         // 0x004801d0 ObjectList_PushHead (thiscall, the list head's address, ob.cpp)
    void object_list_remove(std::uint32_t self, std::uint32_t list);                            // 0x004801f0 ObjectList_Remove (thiscall, the list)
    void object_suspend_to_list(std::uint32_t self, std::uint32_t list);                        // 0x004802f0 Object_SuspendToList (thiscall, the list)
    void object_unsuspend(std::uint32_t self);                                                  // 0x004803b0 CBody__UnSuspend (thiscall)
    void ob_stamp(std::uint32_t line);                                                          // the assertion macro's line and file name of ob.cpp
    std::uint32_t read_fixed_position(std::uint32_t self, std::uint32_t record);                // 0x00480240 TRG_ReadFixedPosition (thiscall): answers the address after the three dwords
    std::uint32_t read_object_parameters(std::uint32_t self, std::uint32_t record);             // 0x004802c0 TRG_ReadObjectParameters (thiscall): answers the address after the three words
    std::uint32_t set_state_bit(std::uint32_t self, std::uint32_t mode, std::uint32_t bits);    // 0x004803e0 (thiscall, two arguments): set (mode 1) or clear the bits of the byte at +0x86
    void want_sprite(std::uint32_t self);                                                       // 0x00480420
    void release_sprite(std::uint32_t self);                                                    // 0x00480430
    void sprite_update(std::uint32_t self);                                                     // 0x00480460
    void mark_dying_if_idle(std::uint32_t self);                                                // 0x004805d0
    std::uint32_t is_dying(std::uint32_t self);                                                 // 0x004805f0
    std::uint32_t always_true();                                                                // 0x00480600 (cdecl, three arguments)
    std::uint32_t animated_construct(std::uint32_t self);                                       // 0x00480610
    std::uint32_t animated_scalar_delete(std::uint32_t self, std::uint32_t flags);              // 0x00480660 (thiscall)
    void animated_destruct(std::uint32_t self);                                                 // 0x00480680
    std::uint32_t frame_reached(std::uint32_t self, std::uint32_t track, std::int32_t frame);   // 0x00480c30 CSuper_FrameReached (thiscall, two arguments)
    void merge_model_parts(std::uint32_t self, std::uint32_t order_names, std::int32_t order_count, std::uint32_t part_names, std::int32_t part_count);   // 0x00480d90 Skater_MergeModelParts (thiscall, four arguments): each part name that equals an order name gets that place
    void assign_model_part_order(std::uint32_t self, std::uint32_t part, std::uint32_t order);   // 0x00480cd0 Skater_AssignModelPartOrder (thiscall, two arguments): part -> its place in the model
    void animation_advance(std::uint32_t self);        // 0x00480950 Animation_Advance (fastcall on the object): steps its animation cursor
    void invoke_update_hooks(std::uint32_t self);      // 0x00480fa0 Animation_InvokeUpdateHooks (fastcall on the object)
    void update_pose_frame(std::uint32_t self, std::uint32_t frame);                            // 0x00480f50 Animation_UpdatePoseFrame (thiscall, one argument)
    void animation_start(std::uint32_t self, std::uint32_t animation, std::int32_t from, std::int32_t to, std::uint32_t end_mode);   // 0x00480730 Animation_Start (thiscall, four arguments)
    void object_ai_frame(std::uint32_t list);                                                  // 0x00480ff0 Ob_AI (cdecl, the list head's address): the per-frame loop over one object list
    void object_clear_input_flags(std::uint32_t first);                                        // 0x00480fd0 Ob_ClearInputFlags (cdecl, the first object): clears all but bit 0 of the input flags (+0x68) down the chain
    void object_quick_ai(std::uint32_t list);                                                  // 0x00481110 Ob_QuickAI (cdecl, the list head's address): the loop without distance tests (update, or the dying object's two-step delete)
    void object_maybe_unsuspend();                                                             // 0x00481150 Ob_MaybeUnSuspendOrCull (cdecl): wakes the suspended objects a skater has come within the suspend distance of

    // ---- the script object, the baddy and the type-0xcb object (world_baddy.cpp, 0x00401000..0x00403410) ----
    void script_object_clear_flag(std::uint32_t id);                                            // 0x00401000 (cdecl): clears bit 0 of the flags of the script object with this id
    void script_object_set_flag(std::uint32_t id);                                              // 0x00401030 (cdecl): sets bit 0
    std::uint32_t script_object_scalar_delete(std::uint32_t self, std::uint32_t flags);         // 0x00401100 (thiscall)
    void script_object_destruct(std::uint32_t self);                                            // 0x00401120
    void add_spin(std::uint32_t self);                                                          // 0x004011d0: the angles += the spin
    void assertion(std::uint32_t condition, std::uint32_t format, std::uint32_t arguments);     // 0x004011e0 M3dUtils_InBetween (cdecl, the format's arguments follow): logs the failed assertion
    void empty_hook() {}                                                                        // 0x00401240, 0x00401250, 0x00401260
    std::uint32_t baddy_construct(std::uint32_t self);                                          // 0x00401270
    std::uint32_t baddy_scalar_delete(std::uint32_t self, std::uint32_t flags);                 // 0x004012d0 (thiscall)
    void baddy_destruct(std::uint32_t self);                                                    // 0x004012f0
    void step_physics(std::uint32_t self);                                                      // 0x00401410: the velocity, the position and the spin of a mover for one frame
    std::uint32_t ai_view_of_player(std::uint32_t self, std::uint32_t pitch, std::uint32_t yaw, std::uint32_t distance);   // 0x00402f80 (thiscall, three arguments)
    std::uint32_t ai_view_by_player(std::uint32_t self, std::uint32_t pitch, std::uint32_t yaw, std::uint32_t distance);   // 0x00402fc0
    std::uint32_t construct_subtype_0xcb(std::uint32_t self, std::uint32_t record, std::uint32_t link);   // 0x00403000 TRG_ConstructSubtype0x00cb (thiscall, two arguments)
    std::uint32_t subtype_0xcb_scalar_delete(std::uint32_t self, std::uint32_t flags);          // 0x004030b0 (thiscall)
    void subtype_0xcb_destruct(std::uint32_t self);                                             // 0x004030d0
    std::uint32_t apply_script_field_word_thunk(std::uint32_t self, std::uint32_t word);        // 0x00403400 (thiscall, one argument)

    // ---- the trigger file (world_trig.cpp, 0x004c4b10..0x004c8650) ----
    void trig_clear_menu();                                                                     // 0x004c4b10 Trig_ClearTrigMenu
    void trig_delete_command_points();                                                          // 0x004c4b30 Trig_DeleteCommandPoints
    void trig_reset_collision_flags(std::uint32_t mask);                                        // 0x004c4b80 Trig_ResetCPCollisionFlags (cdecl)
    void trig_reset_executed_flags(std::uint32_t mask);                                         // 0x004c4ba0 Trig_ResetCPExecutedFlags (cdecl)
    std::uint32_t trig_trigger_command_point(std::uint32_t id);                                 // 0x004c4bd0 Trig_TriggerCommandPoint (cdecl)
    void trig_find_restart_by_name(std::uint32_t name);                                         // 0x004c4c50 Trig_FindRestartByName (cdecl)
    void fudge_angles(std::uint32_t object);                                                    // 0x004c4d10 FudgeAngles (cdecl)
    void trig_execute_restart();                                                                // 0x004c4e30 Trig_ExecuteRestart
    void trig_delete_file();                                                                    // 0x004c5100 Trig_DeleteTrigFile
    void trg_load_file(std::uint32_t name);                                                     // 0x004c5130 TRG_LoadFile (cdecl)
    void trig_init_default_file();                                                              // 0x004c5380
    std::uint32_t trg_find_preamble_flag(std::uint32_t flag, std::uint32_t stream);             // 0x004c5420 TRG_FindObjectPreambleFlag (cdecl, al)
    std::uint32_t trg_find_preamble_end(std::uint32_t stream);                                  // 0x004c5440 TRG_FindObjectPreambleEnd (cdecl)
    std::uint32_t trg_find_command_point_link(std::uint32_t index);                             // 0x004c5ac0 TRG_FindCommandPointLink (cdecl)
    std::uint32_t trg_skip_aligned_cstring(std::uint32_t string);                               // 0x004c5d70 TRG_SkipAlignedCString (cdecl)
    void trig_allocate_dual_buffer() {}                                                         // 0x004c5d90
    void trig_remove_dual_buffer();                                                             // 0x004c5da0
    void trig_scan_restart();                                                                   // 0x004c7ea0
    void trg_execute_autoexec();                                                                // 0x004c8050 TRG_ExecuteAutoexec
    std::uint32_t trg_create_link_record(std::uint32_t key, std::uint32_t index, std::uint32_t stream);   // 0x004c84d0 TRG_CreateLinkRecord (cdecl)
    std::uint32_t trg_get_node_links(std::uint32_t index);                                      // 0x004c8550 TRG_GetNodeLinks (cdecl)
    std::uint32_t trg_decode_node_position(std::uint32_t out, std::uint32_t index);             // 0x004c8650 TRG_DecodeNodePosition (cdecl)

    // ---- the trigger command interpreter and its helpers (world_trig_commands.cpp, milestone 10c) ----
    void trg_send_pulse(std::uint32_t index);                                                   // 0x004c58b0 TRG_SendPulse (cdecl)
    void trg_execute_command_list(std::uint32_t links);                                         // 0x004c5b00 TRG_ExecuteCommandList (cdecl)
    void send_signal_to_node(std::uint32_t list, std::uint32_t index);                          // 0x004c5b30 SendSignalToNode (cdecl)
    void trg_send_signal_to_nodes(std::uint32_t links);                                         // 0x004c5b60 TRG_SendSignalToNodes (cdecl)
    void trg_suspend_object_by_node(std::uint32_t list, std::uint32_t index);                   // 0x004c5c00 TRG_SuspendObjectByNode (cdecl, the address of a list head)
    void send_unsuspend(std::uint32_t index);                                                   // 0x004c5c40 SendUnSuspend (cdecl)
    void trg_apply_activation_or_suspend_list(std::uint32_t links, std::uint32_t kind);         // 0x004c5c70 TRG_ApplyActivationOrSuspendList (cdecl)
    void trg_interpret_command_stream(std::uint32_t stream, std::uint32_t index, std::uint32_t mode);   // 0x004c5dc0 TRG_InterpretCommandStream (cdecl)
    void trg_send_visible(std::uint32_t index, std::uint32_t visible);                          // 0x004c77f0 TRG_SendVisible (cdecl)
    void trg_send_kill_from_node(std::uint32_t index, std::uint32_t mode);                      // 0x004c7a00 TRG_SendKillFromNode (cdecl)
    void kill_in_list(std::uint32_t index, std::uint32_t list, std::uint32_t mode);             // 0x004c7bf0 KillInList (cdecl)
    std::uint32_t trg_skip_command_payload(std::uint32_t command);                              // 0x004c7c50 TRG_SkipCommandPayload (cdecl)
    std::uint32_t construct_subtype_0x0192(std::uint32_t self, std::uint32_t record, std::uint32_t node);   // 0x0049f250 TRG_ConstructSubtype0x0192 (thiscall, two arguments, world_trig_nodes.cpp)
    std::uint32_t construct_script_object(std::uint32_t self, std::uint32_t checksum, std::uint32_t parameters);   // 0x00401060 TRG_CreateScriptObject (thiscall, two arguments, world_trig_nodes.cpp)
    std::uint32_t construct_traffic_object(std::uint32_t self, std::uint32_t record, std::uint32_t node, std::uint32_t object_type);   // 0x00412640 TRG_ConstructTrafficObject (thiscall, three arguments, world_trig_nodes.cpp)
    std::uint32_t powerup_create_from_trigger(std::uint32_t type, std::uint32_t position, std::uint32_t flags, std::uint32_t word, std::uint32_t node);   // 0x004a8e50 Powerup_CreateFromTrigger (cdecl, world_trig_nodes.cpp)
    std::uint32_t powerup_construct(std::uint32_t self, std::uint32_t type, std::uint32_t position, std::uint32_t velocity, std::uint32_t flags, std::uint32_t word, std::uint32_t node);   // 0x004a7c50 Powerup_Construct (thiscall, six arguments)
    std::uint32_t trg_create_object(std::uint32_t index);                                       // 0x004c5460 TRG_CreateObject (cdecl, world_trig_nodes.cpp)
    void trg_register_camera_point(std::uint32_t index);                                        // 0x00411f30 TRG_RegisterCameraPoint (cdecl, world_trig_nodes.cpp)
    void trg_parse_nodes();                                                                   // 0x004c8130 TRG_ParseNodes (cdecl, world_trig_nodes.cpp)
    // ---- the traffic objects' collision with the skaters (world_traffic.cpp) ----
    void traffic_check(std::uint32_t car);                                                      // 0x00412ef0 (cdecl)
    void update_traffic_object(std::uint32_t self);                                             // 0x00412960 (thiscall, no arguments; the car's vtable slot 3: one frame of a traffic car)
    void traffic_interaction(std::uint32_t car, std::uint32_t who, std::uint32_t matrix, std::uint32_t box);   // 0x00413000 TRG_ProcessTrafficInteraction (cdecl)

    // ---- the platform, power-up and traffic methods (world_extra.cpp) ----
    std::uint32_t platform_scalar_delete(std::uint32_t self, std::uint32_t flags);              // 0x0049f340 (thiscall)
    std::uint32_t platform_construct_at(std::uint32_t self, std::uint32_t position);            // 0x0049f360 (thiscall, one argument)
    void platform_destruct(std::uint32_t self);                                                 // 0x0049f440
    std::uint32_t powerup_scalar_delete(std::uint32_t self, std::uint32_t flags);               // 0x004a81d0 (thiscall)
    void powerup_destruct(std::uint32_t self);                                                  // 0x004a81f0
    std::uint32_t powerup_kill(std::uint32_t self);                                             // 0x004a8250 maybe_destroy_model
    void powerup_set_fields(std::uint32_t self, std::uint32_t value, std::uint32_t flag);       // 0x004a8260 (thiscall, two arguments)
    void powerup_allow_sprite();                                                                // 0x004a8280
    void powerup_release(std::uint32_t self);                                                   // 0x004a8290
    void powerup_set_node(std::uint32_t self, std::uint32_t node);                              // 0x004a82c0 CPowerUp__SetNode (thiscall, one argument)
    void powerup_mark_dying(std::uint32_t self);                                                // 0x004a82e0
    void powerup_do_physics(std::uint32_t self);                                                // 0x004a8300 CPowerUp__DoPhysics (thiscall)
    void baddy_die(std::uint32_t self);                                                         // 0x004013b0 CBaddy__Die (thiscall; world_extra.cpp)
    void baddy_send_death_pulse(std::uint32_t self);                                            // 0x004013d0 CBaddy__SendDeathPulse (thiscall)
    void powerup_take_effect(std::uint32_t self, std::uint32_t skater);                         // 0x004a86f0 CPowerUp__TakeEffect (thiscall, one argument; world_extra.cpp)
    void powerup_check_age(std::uint32_t self);                                                 // 0x004a8620 CPowerUp__CheckAge (thiscall)
    void platform_do_physics(std::uint32_t self);                                               // 0x004a03e0 CPlatform__DoPhysics (thiscall)
    void platform_bounce(std::uint32_t self, std::uint32_t skater, std::uint32_t velocity);     // 0x0049f4c0 CPlatform__Bounce (thiscall, two arguments: the skater and its velocity, ret 8)
    void platform_bouncy_ai(std::uint32_t self);                                                // 0x0049f780 CPlatform__BouncyAI (thiscall)
    void trg_run_script_stream(std::uint32_t self, std::uint32_t stream);                       // 0x00401520 TRG_RunScriptStream (thiscall, one argument)
    std::uint32_t platform_script_command(std::uint32_t self, std::uint32_t command);           // 0x004a07e0 CPlatform__ExecuteScriptCommand (vtable slot 7, thiscall, one argument; world_script.cpp)
    void platform_script_field(std::uint32_t self, std::uint32_t field);                        // 0x004a1060 CPlatform__ApplyScriptField (vtable slot 8, thiscall, one argument; world_script.cpp)
    std::uint32_t base_script_command(std::uint32_t self, std::uint32_t command);               // 0x004015e0 TRG_ExecuteBaddyScriptOpcode (thiscall, one argument; world_script.cpp)
    void skip_conditional_block(std::uint32_t self);                                            // 0x004014a0 TRG_SkipConditionalBlock (thiscall)
    void base_script_field(std::uint32_t self, std::uint32_t field);                            // 0x004027f0 (thiscall, one argument): the base class's script fields
    std::uint32_t base_script_variable(std::uint32_t self, std::uint32_t variable);             // 0x00402bb0 TRG_GetScriptVariable (thiscall, one argument)
    std::uint32_t platform_script_variable(std::uint32_t self, std::uint32_t variable);         // 0x004a1270 (vtable 0x005194f8 slot 9, thiscall, one argument)
    std::uint32_t script_object_command(std::uint32_t self, std::uint32_t command);             // 0x00403410 TRG_ApplyScriptCommand (the script object's slot 7, thiscall, one argument)
    void platform_update(std::uint32_t self);                                                   // 0x0049fef0 CPlatform__Update (vtable slot 3)
    std::uint32_t platform_hit(std::uint32_t self, std::uint32_t damage);                       // 0x004a0360 CPlatform__Hit (vtable slot 6, thiscall, three arguments: the damage first; world_extra.cpp)
    void tips_update();                                                                         // 0x004c4620 TrickTips_Update (cdecl, once a frame: the trick tip monitor at 0x0056db98; world_extra.cpp)
    void tips_display();                                                                        // 0x004c4420 TrickTips_Display (cdecl, from the HUD each frame: the slide-in panel)
    void tips_show(std::uint32_t self, std::uint32_t tip);                                     // 0x004c4950 TrickTips_Show (thiscall on the monitor, one argument)
    void script_object_update(std::uint32_t self);                                            // 0x004031a0 (vtable 0x005183b0 slot 3, thiscall): the script object's per-frame update (world_script.cpp)
    void level_event_dispatch();                                                                // 0x00466c10 LevelEvent_InitializeAndDispatch (cdecl, no arguments; world_extra.cpp)
    void powerup_update(std::uint32_t self);                                                  // 0x004a8ac0 Powerup_Update (vtable slot 3)
    void powerup_attach_glow(std::uint32_t self);                                               // 0x004a8b4c: the tail block of Powerup_Update (entered by a jump with the power-up in ebp)
    std::uint32_t traffic_scalar_delete(std::uint32_t self, std::uint32_t flags);               // 0x004128a0 TRG_DeleteTrafficObject (thiscall)
    void traffic_destruct(std::uint32_t self);                                                  // 0x004128c0 TRG_DestroyTrafficObject

    // ---- the skater object (world_skater.cpp, 0x0046c5f0..0x0046e4f0) ----
    std::uint32_t skater_construct(std::uint32_t self);                                         // 0x0046c720 Skater_Construct
    void blood_splat(std::uint32_t self);                                                       // 0x0046da70 CBruce__BloodSplat
    void handle_water(std::uint32_t self);                                                      // 0x0046e080 CBruce__HandleWater
    void skater_update(std::uint32_t self);                                                     // 0x0046dd60 Skater_Update (thiscall; vtable slot 3 of the skater): the skater's frame
    void probe_ground(std::uint32_t self);                                                      // 0x0046d7a0
    void skater_shade(std::uint32_t self, std::uint32_t face, std::uint32_t object, std::uint32_t offset_x, std::uint32_t offset_z);   // 0x0046d2e0 (thiscall, four arguments)
    void skater_apply_shade_lighting(std::uint32_t self);                                       // 0x0046d160 Skater_ApplyShadeLighting (thiscall; vtable slot 5 of the skater)
    void board_off(std::uint32_t self);                                                         // 0x0046c5f0 CBruce__BoardOff
    void board_on(std::uint32_t self);                                                          // 0x0046c630 CBruce__BoardOn
    void reset_orientation_rows(std::uint32_t self);                                            // 0x0046c670
    std::uint32_t skater_scalar_delete(std::uint32_t self, std::uint32_t flags);                // 0x0046cbe0 (thiscall)
    void skater_destroy_panel_members(std::uint32_t self);                                      // 0x0046cc00
    void skater_destruct(std::uint32_t self);                                                   // 0x0046cca0
    std::uint32_t name_hash(std::uint32_t string, std::uint32_t seed);                          // 0x0046cdf0 (cdecl)
    std::uint32_t name_find(std::uint32_t self, std::uint32_t string, std::uint32_t seed);      // 0x0046ce20 (thiscall, two arguments)
    void name_reset(std::uint32_t self);                                                        // 0x0046ce60
    std::uint32_t name_add(std::uint32_t self, std::uint32_t string, std::uint32_t seed);       // 0x0046ce70 (thiscall, two arguments)
    void name_release(std::uint32_t self, std::uint32_t string, std::uint32_t seed);            // 0x0046cf20 (thiscall, two arguments)
    std::uint32_t name_count(std::uint32_t self, std::uint32_t string, std::uint32_t seed);     // 0x0046cfe0 (thiscall, two arguments)
    static std::uint32_t step_toward(std::uint32_t value, std::uint32_t target, std::uint32_t step);   // 0x0046d010 (cdecl)
    void fade_light_colour(std::uint32_t self);                                                 // 0x0046d040
    std::uint32_t distance_to_line(std::uint32_t x, std::uint32_t y, std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1);   // 0x0046d1d0 (cdecl)
    void line_intersection(std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1, std::uint32_t x2, std::uint32_t y2, std::uint32_t x3, std::uint32_t y3,
                           std::uint32_t out_x, std::uint32_t out_y, std::uint32_t out_numerator, std::uint32_t out_denominator);   // 0x0046d220 (cdecl)
    void input_profile_prep(std::uint32_t self);                                                // 0x0046d6e0 Skater_InputProfilePrep
    void mark_dying_and_visible(std::uint32_t self);                                            // 0x0046d750
    void player_bind_hook_packet(std::uint32_t self, std::uint32_t name);                       // 0x0046d940 (thiscall, one argument)
    std::uint32_t flip_to_velocity(std::uint32_t self);                                         // 0x0046d970 CBruce__FlipToVelocity
    void request_blood_splat(std::uint32_t self, std::uint32_t a0, std::uint32_t a1);           // 0x0046dd20 (thiscall, two arguments)
    void skater_grunt();                                                                        // 0x0046e1d0 SFX_Grunt (cdecl)
    std::uint32_t droplet_scalar_delete(std::uint32_t self, std::uint32_t flags);               // 0x0046e3f0 (thiscall)
    void droplet_destruct(std::uint32_t self);                                                  // 0x0046e410
    void droplet_update(std::uint32_t self);                                                    // 0x0046e470
    void droplet_checked_update(std::uint32_t self);                                                   // 0x0046e4f0: the assertion on +0x6a bit 2, then droplet_update

    // ---- the trick objects (world_trick.cpp, 0x004bd700..0x004be350) ----
    void trick_delete_all();                                                                    // 0x004bd700 TrickObject_DeleteAll
    void trick_delete_all_preserve();                                                           // 0x004bd720 TrickObject_DeleteAllPreserve
    std::uint32_t trg_is_goal_object_node(std::uint32_t index);                                 // 0x004bd740 TRG_IsGoalObjectNode (cdecl)
    std::uint32_t trick_object_construct(std::uint32_t self, std::uint32_t index, std::uint32_t kind);   // 0x004bd760 TrickObject_Construct (thiscall, two arguments)
    std::uint32_t trick_object_scalar_delete(std::uint32_t self, std::uint32_t flags);          // 0x004bd920 (thiscall)
    void trick_object_destruct(std::uint32_t self);                                             // 0x004bd940
    void trick_object_activate_for_object(std::uint32_t object, std::uint32_t skater, std::uint32_t strength);   // 0x004bd980 TrickObject_ActivateForObject (cdecl)
    void trick_object_take(std::uint32_t index);                                                // 0x004bdbd0 (cdecl)
    void trick_object_activate_goal_object(std::uint32_t index);                                // 0x004bdc40 TrickObject_ActivateGoalObject (cdecl)
    void trick_object_update();                                                                 // 0x004bdd00 TrickObject_Update
    void trick_object_line_render(std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1, std::uint32_t colour);   // 0x004bdea0 TrickObject_LineRender (cdecl)
    void trick_object_draw_box(std::uint32_t object, std::uint32_t colour);                     // 0x004be0b0 (cdecl)
    void trick_object_offer_all();                                                              // 0x004be350

    // ---- the trigger-record writers (world_cmd.cpp, 0x004c87c0..0x004c8f30) ----
    std::uint32_t cmd_construct(std::uint32_t self);                                            // 0x004c87c0 (the base class)
    std::uint32_t cmd_scalar_delete(std::uint32_t self, std::uint32_t flags);                   // 0x004c87e0, 0x004c88c0, 0x004c8a50, 0x004c8bf0, 0x004c8c60, 0x004c8d00, 0x004c8dd0, 0x004c8e80 (thiscall)
    void cmd_destruct(std::uint32_t self);                                                      // 0x004c8800, 0x004c88e0, 0x004c8a70, 0x004c8c10, 0x004c8c80, 0x004c8d20, 0x004c8df0, 0x004c8ea0
    std::uint32_t cmd_size(std::uint32_t self);                                                 // 0x004c8810
    std::uint32_t node_construct(std::uint32_t self, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6);   // 0x004c8870 (thiscall, seven arguments)
    std::uint32_t node_write(std::uint32_t self, std::uint32_t buffer);                         // 0x004c88f0 (thiscall, one argument)
    std::uint32_t link_construct(std::uint32_t self, std::uint32_t record);                     // 0x004c8a20
    void link_set_ends(std::uint32_t self, std::uint32_t first, std::uint32_t second);          // 0x004c8a80
    std::uint32_t link_write(std::uint32_t self, std::uint32_t buffer);                         // 0x004c8ab0
    std::uint32_t end_construct(std::uint32_t self);                                            // 0x004c8bd0
    std::uint32_t end_write(std::uint32_t self, std::uint32_t buffer);                          // 0x004c8c20
    std::uint32_t command_c_construct(std::uint32_t self, std::uint32_t value);                 // 0x004c8c30
    std::uint32_t command_c_write(std::uint32_t self, std::uint32_t buffer);                    // 0x004c8c90
    std::uint32_t command_e_construct(std::uint32_t self, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2);   // 0x004c8cd0
    std::uint32_t command_e_write(std::uint32_t self, std::uint32_t buffer);                    // 0x004c8d30
    std::uint32_t command_f_construct(std::uint32_t self, std::uint32_t a0, std::uint32_t a1);  // 0x004c8da0
    std::uint32_t command_f_write(std::uint32_t self, std::uint32_t buffer);                    // 0x004c8e00
    std::uint32_t command_g_construct(std::uint32_t self, std::uint32_t value);                 // 0x004c8e50
    std::uint32_t command_g_write(std::uint32_t buffer);                                        // 0x004c8eb0 (thiscall, the buffer on the stack)
    std::uint32_t command_none_write();                                                         // 0x004c8ed0
    void clear_editor_words();                                                                  // 0x004c8ef0 (and the jump thunk at 0x004c8ee0)
    void start_fade(std::uint32_t first, std::uint32_t second);                                 // 0x004c8f10 Utils_StartFade (cdecl)
    void stop_fade();                                                                           // 0x004c8f30 Utils_StopFade

private:
    using Word = std::uint32_t;
    // FileIO_AllocLoadedBuffer(size, heap, one, zero): the loader's allocator
    Word alloc(Word size, Word heap, Word one, Word zero) {
        s_.note("alloc", {size, heap, one, zero});
        return static_cast<Word>(s_.value("alloc"));
    }
    // a service that filled a buffer: the probe scripts the bytes as dwords (the last one padded)
    void take_filled(Word destination, Word bytes, const std::string& service) {
        if (s_.live()) {
            return;  // (the real service already filled the bytes; the scripted words are replay-only)
        }
        std::vector<std::uint8_t> data;
        for (Word k = 0; k < (bytes + 3) / 4; ++k) {
            const auto word = static_cast<Word>(s_.value(service));
            for (unsigned b = 0; b < 4; ++b) {
                data.push_back(static_cast<std::uint8_t>(word >> (8 * b)));
            }
        }
        data.resize(bytes);
        if (bytes != 0) {
            m_.write(destination, data);
        }
    }
    void trig_stamp(Word line, bool competition = false);   // the assertion macro's line and file name (0x00568638, 0x0056881c)
    Word trig_enviro_checksum(Word record);
    Word script_operand(Word self);                     // 0x004027c0 (thiscall): the next operand word of the stream, a variable when it names one
    Word script_variable(Word self, Word word);          // the object's variable (slot 9) a script word names; the replay applies the cursor the probe's stub moved
    void baddy_stamp(Word line);                        // the assertion macro's line and file name of baddy.cpp
    void platform_set_target(Word self, Word target);   // 0x004a0710 (thiscall, one argument): a changed move-to target is taken
    void traffic_proximity(Word car, Word dx, Word dz);   // 0x004136e0 (cdecl, the car and its direction; the car's horn)
    void vec3_add(Word self, Word rhs);
    void vec3_decay_shift(Word self, Word counts);
    void vec3s_add(Word self, Word rhs);
    void vec3s_mask12(Word self);
    void vec3s_decay_shift(Word self, Word counts);
    void vec3s_snap_near_zero(Word self);
    static Word u(std::int32_t v) { return static_cast<Word>(v); }
    static std::int32_t s(Word v) { return static_cast<std::int32_t>(v); }
    // idiv: retail faults on a zero divisor (and on INT_MIN / -1), the port refuses instead
    static std::int32_t divide(std::int32_t numerator, std::int32_t denominator) {
        if (denominator == 0 || (numerator == INT32_MIN && denominator == -1)) {
            throw std::runtime_error("division fault in the world objects");
        }
        return numerator / denominator;
    }
    void mem_delete(Word block) { s_.note("mem_delete", {block}); }
    std::int32_t rnd(Word limit) {
        s_.note("rnd", {limit});
        return s_.value("rnd");
    }
    bool strings_equal(Word a, Word b) const;
    void thiscall(Word target, Word self, std::vector<Word> args = {}) {
        args.insert(args.begin(), self);
        engine(target, std::move(args));
    }
    Word thiscall_value(Word target, Word self, std::vector<Word> args = {}) {
        args.insert(args.begin(), self);
        return engine_value(target, std::move(args));
    }

    core::CoreKit core_;
    mathkit::MathKit math_;
};

}  // namespace opentony::runtime::worldkit
