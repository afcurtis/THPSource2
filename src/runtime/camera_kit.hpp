#pragma once

#include "collision_kit.hpp"
#include "core_kit.hpp"
#include "math_kit.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace opentony::runtime::camerakit {

// Phase 4.15b: camera.cpp, the game camera. A camera (CCamera, 0x6a0-odd bytes) follows a skater: a look-at position and a rotation kept as a quaternion of five dwords {vtable 0x005184d8, x, y, z, w}
// in Q12, smoothed from frame to frame and pushed to the viewport. Like the rest of Phase 4 the functions work on the retail memory at the retail addresses (`self` is the camera); everything outside
// them (the object lists, the collision queries, the trigger nodes, the competition screens) is a service reported by address (`engine_<address>`).

// the quaternion record (a "transform": a vtable and four dwords)
namespace quaternion {

constexpr std::uint32_t kVtable = 0x00;                    // 0x005184d8
constexpr std::uint32_t kX = 0x04;
constexpr std::uint32_t kY = 0x08;
constexpr std::uint32_t kZ = 0x0c;
constexpr std::uint32_t kW = 0x10;
constexpr std::uint32_t kBytes = 0x14;
constexpr std::uint32_t kVtableValue = 0x005184d8;
constexpr std::uint32_t kPlainVtable = 0x005184b4;         // the base class the destructor leaves behind

}  // namespace quaternion

constexpr std::uint32_t kAxisTable = 0x0053757c;           // dwords {1, 2, 0}: the axis that follows each axis in a cycle

// where the port keeps the quaternions the retail code builds in stack locals
constexpr std::uint32_t kScratch = pcplatform::kScratchBase + 0xa00;

class CameraKit final : public collision::CollisionKit {
public:
    CameraKit(RetailMemory& memory, RetailServices& services) : CollisionKit(memory, services), core_(memory, services), math_(memory, services) { core_.use_native_square_root(); }

    // ---- quaternions of the camera (0x004a9650..0x004a9bf0) ----
    std::uint32_t compose(std::uint32_t out, std::uint32_t a, std::uint32_t b);                 // 0x004a9650 Camera_ComposeTransformQ12 (cdecl): out = a * b; answers out
    void from_rotation_vector(std::uint32_t out, std::uint32_t vector);                         // 0x004a9750 (cdecl): axis * angle -> quaternion
    void about_x(std::uint32_t out, std::int32_t angle);                                        // 0x004a9820 (cdecl, angle is a short)
    void about_y(std::uint32_t out, std::int32_t angle);                                        // 0x004a9870
    void about_z(std::uint32_t out, std::int32_t angle);                                        // 0x004a98c0
    void to_matrix(std::uint32_t quaternion, std::uint32_t matrix);                             // 0x004a9910 Camera_TransformToMatrixQ12 (cdecl)
    void from_matrix(std::uint32_t matrix, std::uint32_t quaternion);                           // 0x004a9a00 Camera_MatrixToTransformQ12 (cdecl)
    void slerp(const std::array<std::uint32_t, 5>& a, const std::array<std::uint32_t, 5>& b, std::int32_t t, std::uint32_t out);   // 0x004a9bf0 Camera_SlerpTransformQ12 (cdecl: both quaternions by value)

    static std::int32_t angle_delta(std::uint32_t from, std::uint32_t to);                      // 0x004103b0 Camera_AngleDelta12 (cdecl): the shortest signed turn between two 12-bit angles

    // ---- the quaternion class (vtable 0x005184d8) ----
    std::uint32_t quaternion_construct(std::uint32_t self, std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t w);   // 0x0040e000 (thiscall, four arguments)
    void quaternion_destruct(std::uint32_t self);                                               // 0x0040e050
    std::uint32_t quaternion_scalar_delete(std::uint32_t self, std::uint32_t flags);            // 0x0040e030 (thiscall)
    std::uint32_t quaternion_copy_construct(std::uint32_t self, std::uint32_t source);          // 0x0040e060 (thiscall)

    // ---- the camera rigs (0x94 bytes) and camera points ----
    void rig_construct(std::uint32_t self);                                                     // 0x004117f0 (fastcall)
    void rig_destruct(std::uint32_t self);                                                      // 0x004119e0 (fastcall)
    std::uint32_t rig_scalar_delete(std::uint32_t self, std::uint32_t flags);                   // 0x00411880 (thiscall)
    std::uint32_t rig_construct_from_record(std::uint32_t self, std::uint32_t record);          // 0x004118a0 (thiscall)
    void rig_copy_matrix(std::uint32_t self, std::uint32_t destination);                        // 0x00411c50 (thiscall)
    void rig_get_position(std::uint32_t self, std::uint32_t destination);                       // 0x00411c70 (thiscall)
    void rig_get_target(std::uint32_t self, std::uint32_t destination);                         // 0x00411ca0 (thiscall)
    void point_destruct(std::uint32_t self);                                                    // 0x00411ec0 (fastcall)
    std::uint32_t point_scalar_delete(std::uint32_t self, std::uint32_t flags);                 // 0x00411ed0 (thiscall)
    void points_clear();                                                                        // 0x00411ef0
    void points_clear_and_reset_tuning();                                                       // 0x00411f00

    // ---- the camera object ----
    std::uint32_t construct(std::uint32_t self, std::uint32_t tripod, std::uint32_t player);    // 0x0040b650 SkaterCamera_Construct (thiscall, two arguments: the skater, the player number as a short)
    void destruct(std::uint32_t self);                                                          // 0x0040bc00 Camera_Destruct (fastcall)
    std::uint32_t scalar_delete(std::uint32_t self, std::uint32_t flags);                       // 0x0040bbe0 (thiscall)
    void set_overrides(std::uint32_t self, std::uint32_t value);                                // 0x0040bce0
    void set_override_a(std::uint32_t self, std::uint32_t value);                               // 0x0040bd00
    void set_override_b(std::uint32_t self, std::uint32_t value);                               // 0x0040bd10
    void set_window_width(std::uint32_t self, std::uint32_t value);                             // 0x0040bd20
    void set_window_height(std::uint32_t self, std::uint32_t value);                            // 0x0040bd30
    void push_overrides(std::uint32_t self);                                                    // 0x0040c070
    void pop_overrides(std::uint32_t self);                                                     // 0x0040c0e0
    void push_override_b(std::uint32_t self);                                                   // 0x0040c130
    void pop_override_b(std::uint32_t self);                                                    // 0x0040c170
    void flag_objects_for_shake(std::uint32_t unused, std::uint32_t list, std::uint32_t type);  // 0x0040be40 (cdecl)
    void apply_shake(std::uint32_t self, std::uint32_t argument, std::uint32_t type);           // 0x0040bd40 Camera_ApplyShake (thiscall, two arguments)
    void set_blend_target(std::uint32_t self, std::uint32_t target, std::uint32_t length);      // 0x0040f1a0 (thiscall, two arguments)
    std::uint32_t blend_value(std::uint32_t self);                                              // 0x0040f1f0
    void clear_flag_419(std::uint32_t self);                                                    // 0x0040f130
    std::uint32_t in_competition_intro(std::uint32_t self);                                     // 0x004113e0

    // ---- the camera modes (camera_modes.cpp) ----
    void follow_target(std::uint32_t self);                                                     // 0x00410610 Camera_FollowTarget (fastcall)
    void mode_2(std::uint32_t self);                                                            // 0x004113f0 Camera_Mode2 (fastcall)
    void build_tripod_rotation(std::uint32_t self);                                             // 0x004103e0 (fastcall)
    void reset_orientation(std::uint32_t self);                                                 // 0x0040c1a0 CCamera__ResetOrientation (fastcall)
    void commit_viewport(std::uint32_t self);                                                   // 0x0040be70 Camera_CommitViewportEffects (fastcall)
    void reset_and_settle(std::uint32_t self);                                                  // 0x00410c40 (fastcall)
    void death_mode(std::uint32_t self);                                                        // 0x00410c90 Camera_DeathMode (fastcall)
    void smooth_and_validate(std::uint32_t self, std::uint32_t step);                           // 0x0040e090 Camera_SmoothAndValidate (fastcall, one stack argument)
    void handle_pad(std::uint32_t self);                                                        // 0x0040f270 Camera_HandlePad (fastcall)
    void point_mode(std::uint32_t self);                                                        // 0x00410f70 Camera_PointMode (fastcall)
    void start_competition_intro(std::uint32_t self);                                           // 0x00410bb0 (fastcall)
    void draw_debug_bars();                                                                     // 0x0040f200
    void rig_update(std::uint32_t self);                                                        // 0x00411a00 CameraRig_Update (fastcall)
    void rig_build_matrix(std::uint32_t self);                                                  // 0x00411c00 (fastcall)
    void rig_blend(std::uint32_t a, std::uint32_t b, std::uint32_t t, std::uint32_t out);       // 0x00411ce0 (cdecl)

    // ---- the camera's frame (camera_update.cpp) ----
    void update(std::uint32_t self);                                                            // 0x0040f850 Camera_Update (fastcall, vtable slot 4)

    // ---- the camera's collision leans (camera_effects.cpp) ----
    void apply_effects(std::uint32_t self);                                                     // 0x0040c370 Camera_ApplyEffects (thiscall)

    // ---- the tuning initialisers (camera_tuning.cpp, generated from the disassembly) ----
    void tune_0040ed80();                                                       // 0x0040ed80 (and its jump thunk at 0x0040ed70)
    void tune_0040edb0();                                                       // 0x0040edb0 (and its jump thunk at 0x0040eda0)
    void tune_0040ede0();                                                       // 0x0040ede0 (and its jump thunk at 0x0040edd0)
    void tune_0040ee10();                                                       // 0x0040ee10 (and its jump thunk at 0x0040ee00)
    void tune_0040ee40();                                                       // 0x0040ee40 (and its jump thunk at 0x0040ee30)
    void tune_0040ee70();                                                       // 0x0040ee70 (and its jump thunk at 0x0040ee60)
    void tune_0040eea0();                                                       // 0x0040eea0 (and its jump thunk at 0x0040ee90)
    void tune_0040eed0();                                                       // 0x0040eed0 (and its jump thunk at 0x0040eec0)
    void tune_0040ef00();                                                       // 0x0040ef00 (and its jump thunk at 0x0040eef0)
    void tune_0040ef30();                                                       // 0x0040ef30 (and its jump thunk at 0x0040ef20)
    void tune_0040ef60();                                                       // 0x0040ef60 (and its jump thunk at 0x0040ef50)
    void tune_0040ef90();                                                       // 0x0040ef90 (and its jump thunk at 0x0040ef80)
    void tune_0040efc0();                                                       // 0x0040efc0 (and its jump thunk at 0x0040efb0)
    void tune_0040eff0();                                                       // 0x0040eff0 (and its jump thunk at 0x0040efe0)
    void tune_0040f020();                                                       // 0x0040f020 (and its jump thunk at 0x0040f010)
    void tune_0040f050();                                                       // 0x0040f050 (and its jump thunk at 0x0040f040)
    void tune_0040f080();                                                       // 0x0040f080 (and its jump thunk at 0x0040f070)
    void tune_0040f0b0();                                                       // 0x0040f0b0 (and its jump thunk at 0x0040f0a0)

private:
    using Word = std::uint32_t;
    void rig_clear(Word self);
    std::array<Word, 5> copy_quaternion(Word at);
    void copy_vector(Word to, Word from);
    void take_filled(Word destination, Word bytes, const std::string& service);
    bool vectors_equal(Word a, Word b);
    Word constant(Word slot, Word value);
    void steer_to_view(Word self, Word up);
    void smooth_without_target(Word self);
    void turn_target_about_y(Word self);
    void camera_stamp(Word line);
    static std::int32_t abs_wrap(std::int32_t v) {
        const Word mask = static_cast<Word>(v >> 31);
        return static_cast<std::int32_t>((mask ^ static_cast<Word>(v)) - mask);
    }
    static Word u(std::int32_t v) { return static_cast<Word>(v); }
    static std::int32_t s(Word v) { return static_cast<std::int32_t>(v); }
    static Word shl(Word v, Word count) { return v << (count & 31u); }
    static std::int32_t sar(std::int32_t v, Word count) { return v >> (count & 31u); }
    // idiv: retail faults on a zero divisor (and on INT_MIN / -1), the port refuses instead
    static std::int32_t divide(std::int32_t numerator, std::int32_t denominator) {
        if (denominator == 0 || (numerator == INT32_MIN && denominator == -1)) {
            throw std::runtime_error("division fault in the camera quaternion math");
        }
        return numerator / denominator;
    }

    core::CoreKit core_;
    mathkit::MathKit math_;
};

}  // namespace opentony::runtime::camerakit
