// Replays a retail Camera_Update trace (see scripts/make_camera_trace_fixture.py)
// through the native update_camera model and compares every modeled camera
// field with the state retail left in the CCamera object.

#include "camera_system.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace opentony::camera;

namespace {

constexpr std::size_t kCameraBytes = 0x674;
using Image = std::array<std::uint8_t, kCameraBytes>;

std::uint32_t rd32(const Image& b, std::size_t o) {
    std::uint32_t v;
    std::memcpy(&v, &b[o], 4);
    return v;
}
std::uint16_t rd16(const Image& b, std::size_t o) {
    std::uint16_t v;
    std::memcpy(&v, &b[o], 2);
    return v;
}
void wr32(Image& b, std::size_t o, std::uint32_t v) { std::memcpy(&b[o], &v, 4); }
void wr16(Image& b, std::size_t o, std::uint16_t v) { std::memcpy(&b[o], &v, 2); }

Q16Vec3 rdv(const Image& b, std::size_t o) {
    return {static_cast<Raw>(rd32(b, o)), static_cast<Raw>(rd32(b, o + 4)),
            static_cast<Raw>(rd32(b, o + 8))};
}
void wrv(Image& b, std::size_t o, const Q16Vec3& v) {
    wr32(b, o, static_cast<std::uint32_t>(v.x));
    wr32(b, o + 4, static_cast<std::uint32_t>(v.y));
    wr32(b, o + 8, static_cast<std::uint32_t>(v.z));
}
TransformQ12 rdt(const Image& b, std::size_t o) {
    return {static_cast<Raw>(rd32(b, o)), static_cast<Raw>(rd32(b, o + 4)),
            static_cast<Raw>(rd32(b, o + 8)), static_cast<Raw>(rd32(b, o + 12))};
}
void wrt(Image& b, std::size_t o, const TransformQ12& v) {
    wr32(b, o, static_cast<std::uint32_t>(v.x));
    wr32(b, o + 4, static_cast<std::uint32_t>(v.y));
    wr32(b, o + 8, static_cast<std::uint32_t>(v.z));
    wr32(b, o + 12, static_cast<std::uint32_t>(v.w));
}

// CCamera offsets. Names follow CameraStateRaw.
enum : std::size_t {
    kPosition = 0x08, kAngles = 0x14, kLinkTripod = 0x3a4, kAnchorUpdate = 0x3ac,
    kMirroredAnchor = 0x3b0, kTripodAnchor = 0x3bc, kAnchorTarget = 0x3c0,
    kLinkSecondary = 0x3dc, kTargetValid = 0x3e0, kSecondaryAnchor = 0x3e4,
    kLookTarget = 0x3f0, kViewportParam = 0x40c, kViewportTimer = 0x410,
    kViewportDelta = 0x414, kFollowStateFlag = 0x418, kAlternatePhaseA = 0x434,
    kAlternatePhaseB = 0x436, kCurrentTransform = 0x448, kTargetTransform = 0x45c,
    kPreviousTransform = 0x470, kSavedTransform = 0x498, kSavedValid = 0x4a8, kTransformFallback = 0x4a9,
    kShakeX = 0x4f2, kShakeRateX = 0x4f8, kShakeAngle = 0x4fc, kShakePhase = 0x500,
    kMode = 0x504, kUpdateTick = 0x510, kEffectVertical = 0x530,
    kEffectMagnitude = 0x534, kEffectFallback = 0x544, kEffectPhase = 0x550,
    kEffectCounter = 0x554, kEffectDistance = 0x558, kPointTick = 0x55c,
    kPointFlag = 0x560, kPointStart = 0x564, kDeathTick = 0x570,
    kDeathTarget = 0x574, kDeathStart = 0x594, kFollowRotation = 0x5b4,
    kHistoryA = 0x5b8, kHistoryB = 0x5c4, kDistanceQ4 = 0x5d0,
    kTransitionActive = 0x5d4, kRampA = 0x5d8, kRampB = 0x5dc, kRampC = 0x5e0,
    kRampD = 0x5e4, kFollowEffectCounter = 0x5e8, kAlternateIntegrator = 0x5ec,
    kAlternateCounter = 0x5f0, kPreparationCounter = 0x60c, kModeVector = 0x610,
    kDistanceStep = 0x61c, kDistanceHistory = 0x620, kScreenEffect = 0x63c,
    kCachedTransform = 0x650, kScreenDelta = 0x660, kTiltHold = 0x670,
};

CameraStateRaw unpack(const Image& b, Raw vertical_effect, Raw shared_angle) {
    CameraStateRaw s;
    s.position = rdv(b, kPosition);
    s.look_angles = {rd16(b, kAngles), rd16(b, kAngles + 2), rd16(b, kAngles + 4)};
    s.mirrored_anchor = rdv(b, kMirroredAnchor);
    s.anchor_target = rdv(b, kAnchorTarget);
    s.secondary_anchor = rdv(b, kSecondaryAnchor);
    s.look_target = rdv(b, kLookTarget);
    s.current_transform = rdt(b, kCurrentTransform);
    s.target_transform = rdt(b, kTargetTransform);
    s.previous_transform = rdt(b, kPreviousTransform);
    s.saved_transform = rdt(b, kSavedTransform);
    s.cached_render_transform = rdt(b, kCachedTransform);
    s.screen_effect_offset = rdv(b, kScreenEffect);
    s.history_a = rdv(b, kHistoryA);
    s.history_b = rdv(b, kHistoryB);
    s.mode_vector = rdv(b, kModeVector);
    s.screen_delta = rdv(b, kScreenDelta);
    s.tilt_hold_counter_raw = static_cast<Raw>(rd32(b, kTiltHold));
    s.viewport_parameter_raw = static_cast<Raw>(rd32(b, kViewportParam));
    s.viewport_timer_raw = static_cast<Raw>(rd32(b, kViewportTimer));
    s.viewport_parameter_delta_raw = static_cast<Raw>(rd32(b, kViewportDelta));
    s.mode = rd32(b, kMode);
    s.update_tick = rd32(b, kUpdateTick);
    s.death_camera_tick = rd32(b, kDeathTick);
    s.death_target_position = rdv(b, kDeathTarget);
    s.death_start_position = rdv(b, kDeathStart);
    s.point_camera_tick = rd32(b, kPointTick);
    s.point_start_position = rdv(b, kPointStart);
    s.point_acceleration_flag = b[kPointFlag];
    s.follow_effect_counter_raw = static_cast<Raw>(rd32(b, kFollowEffectCounter));
    s.follow_preparation_counter = rd32(b, kPreparationCounter);
    s.effect_ramp_counter_a = static_cast<Raw>(rd32(b, kRampA));
    s.effect_ramp_counter_b = static_cast<Raw>(rd32(b, kRampB));
    s.effect_ramp_counter_c = static_cast<Raw>(rd32(b, kRampC));
    s.effect_ramp_counter_d = static_cast<Raw>(rd32(b, kRampD));
    s.effect_sample_vertical_raw = static_cast<std::int16_t>(rd16(b, kEffectVertical));
    s.effect_sample_magnitude_raw = static_cast<std::int16_t>(rd16(b, kEffectMagnitude));
    s.effect_sample_fallback_raw = static_cast<std::int16_t>(rd16(b, kEffectFallback));
    s.effect_sample_phase_raw = b[kEffectPhase];
    s.effect_sample_counter_raw = static_cast<Raw>(rd32(b, kEffectCounter));
    s.effect_sample_distance_raw = static_cast<Raw>(rd32(b, kEffectDistance));
    s.shared_vertical_effect_q16 = vertical_effect;
    s.alternate_integrator_raw = static_cast<Raw>(rd32(b, kAlternateIntegrator));
    s.alternate_counter_raw = static_cast<Raw>(rd32(b, kAlternateCounter));
    s.alternate_phase_a_raw = static_cast<std::int16_t>(rd16(b, kAlternatePhaseA));
    s.alternate_phase_b_raw = static_cast<std::int16_t>(rd16(b, kAlternatePhaseB));
    s.alternate_shared_angle_raw = shared_angle;
    s.distance_q4 = static_cast<Raw>(rd32(b, kDistanceQ4));
    s.distance_step_q4 = static_cast<Raw>(rd32(b, kDistanceStep));
    for (std::size_t i = 0; i < 6; ++i) {
        s.distance_history[i] = static_cast<Raw>(rd32(b, kDistanceHistory + 4 * i));
    }
    s.primary_tripod_link_raw = rd32(b, kLinkTripod);
    s.secondary_target_link_raw = rd32(b, kLinkSecondary);
    s.target_valid_raw = b[kTargetValid];
    s.follow_rotation_raw = static_cast<std::int16_t>(rd16(b, kFollowRotation));
    s.follow_state_flag = b[kFollowStateFlag];
    s.follow_transition_active = b[kTransitionActive];
    s.saved_transform_valid = b[kSavedValid];
    s.transform_fallback = b[kTransformFallback];
    s.anchor_update_flag = b[kAnchorUpdate];
    s.tripod_anchor_flag = b[kTripodAnchor];
    s.shake_x = static_cast<std::int16_t>(rd16(b, kShakeX));
    s.shake_y = static_cast<std::int16_t>(rd16(b, kShakeX + 2));
    s.shake_z = static_cast<std::int16_t>(rd16(b, kShakeX + 4));
    s.shake_rate_x = b[kShakeRateX];
    s.shake_rate_y = b[kShakeRateX + 1];
    s.shake_rate_z = b[kShakeRateX + 2];
    s.shake_angle_raw = rd32(b, kShakeAngle);
    s.shake_phase_raw = rd16(b, kShakePhase);
    return s;
}

// Writes only the modeled fields; every other byte keeps the value in `b`.
void pack(const CameraStateRaw& s, Image& b) {
    wrv(b, kPosition, s.position);
    wr16(b, kAngles, s.look_angles.first);
    wr16(b, kAngles + 2, s.look_angles.second);
    wr16(b, kAngles + 4, s.look_angles.third);
    wrv(b, kMirroredAnchor, s.mirrored_anchor);
    wrv(b, kAnchorTarget, s.anchor_target);
    wrv(b, kSecondaryAnchor, s.secondary_anchor);
    wrv(b, kLookTarget, s.look_target);
    wrt(b, kCurrentTransform, s.current_transform);
    wrt(b, kTargetTransform, s.target_transform);
    wrt(b, kPreviousTransform, s.previous_transform);
    wrt(b, kSavedTransform, s.saved_transform);
    wrt(b, kCachedTransform, s.cached_render_transform);
    wrv(b, kScreenEffect, s.screen_effect_offset);
    wrv(b, kHistoryA, s.history_a);
    wrv(b, kHistoryB, s.history_b);
    wrv(b, kModeVector, s.mode_vector);
    wrv(b, kScreenDelta, s.screen_delta);
    wr32(b, kTiltHold, static_cast<std::uint32_t>(s.tilt_hold_counter_raw));
    wr32(b, kViewportParam, static_cast<std::uint32_t>(s.viewport_parameter_raw));
    wr32(b, kViewportTimer, static_cast<std::uint32_t>(s.viewport_timer_raw));
    wr32(b, kViewportDelta, static_cast<std::uint32_t>(s.viewport_parameter_delta_raw));
    wr32(b, kMode, s.mode);
    wr32(b, kUpdateTick, s.update_tick);
    wr32(b, kFollowEffectCounter, static_cast<std::uint32_t>(s.follow_effect_counter_raw));
    wr32(b, kPreparationCounter, s.follow_preparation_counter);
    wr32(b, kRampA, static_cast<std::uint32_t>(s.effect_ramp_counter_a));
    wr32(b, kRampB, static_cast<std::uint32_t>(s.effect_ramp_counter_b));
    wr32(b, kRampC, static_cast<std::uint32_t>(s.effect_ramp_counter_c));
    wr32(b, kRampD, static_cast<std::uint32_t>(s.effect_ramp_counter_d));
    wr16(b, kEffectVertical, static_cast<std::uint16_t>(s.effect_sample_vertical_raw));
    wr16(b, kEffectMagnitude, static_cast<std::uint16_t>(s.effect_sample_magnitude_raw));
    wr16(b, kEffectFallback, static_cast<std::uint16_t>(s.effect_sample_fallback_raw));
    b[kEffectPhase] = s.effect_sample_phase_raw;
    wr32(b, kEffectCounter, static_cast<std::uint32_t>(s.effect_sample_counter_raw));
    wr32(b, kEffectDistance, static_cast<std::uint32_t>(s.effect_sample_distance_raw));
    wr32(b, kDistanceQ4, static_cast<std::uint32_t>(s.distance_q4));
    wr32(b, kDistanceStep, static_cast<std::uint32_t>(s.distance_step_q4));
    for (std::size_t i = 0; i < 6; ++i) {
        wr32(b, kDistanceHistory + 4 * i, static_cast<std::uint32_t>(s.distance_history[i]));
    }
    wr16(b, kFollowRotation, static_cast<std::uint16_t>(s.follow_rotation_raw));
    b[kFollowStateFlag] = s.follow_state_flag;
    b[kTransitionActive] = s.follow_transition_active;
    b[kSavedValid] = s.saved_transform_valid;
    b[kTransformFallback] = s.transform_fallback;
    b[kAnchorUpdate] = s.anchor_update_flag;
    b[kTripodAnchor] = s.tripod_anchor_flag;
}

struct FieldSpan {
    const char* name;
    std::size_t offset;
    std::size_t size;
};

const FieldSpan kSpans[] = {
    {"position", kPosition, 12},
    {"look_angles", kAngles, 6},
    {"mirrored_anchor", kMirroredAnchor, 12},
    {"anchor_target", kAnchorTarget, 12},
    {"secondary_anchor", kSecondaryAnchor, 12},
    {"look_target", kLookTarget, 12},
    {"current_transform", kCurrentTransform, 16},
    {"target_transform", kTargetTransform, 16},
    {"previous_transform", kPreviousTransform, 16},
    {"saved_transform", kSavedTransform, 16},
    {"cached_render_transform", kCachedTransform, 16},
    {"screen_effect_offset", kScreenEffect, 12},
    {"history_a", kHistoryA, 12},
    {"history_b", kHistoryB, 12},
    {"mode_vector", kModeVector, 12},
    {"screen_delta", kScreenDelta, 12},
    {"tilt_hold_counter", kTiltHold, 4},
    {"viewport_parameter", kViewportParam, 12},
    {"mode", kMode, 4},
    {"update_tick", kUpdateTick, 4},
    {"follow_effect_counter", kFollowEffectCounter, 4},
    {"follow_preparation_counter", kPreparationCounter, 4},
    {"effect_ramp_counters", kRampA, 16},
    {"effect_sample_vertical", kEffectVertical, 2},
    {"effect_sample_magnitude", kEffectMagnitude, 2},
    {"effect_sample_fallback", kEffectFallback, 2},
    {"effect_sample_phase", kEffectPhase, 1},
    {"effect_sample_counter", kEffectCounter, 4},
    {"effect_sample_distance", kEffectDistance, 4},
    {"distance_q4", kDistanceQ4, 4},
    {"distance_step_q4", kDistanceStep, 4},
    {"distance_history", kDistanceHistory, 24},
    {"follow_rotation", kFollowRotation, 2},
    {"follow_state_flag", kFollowStateFlag, 1},
    {"follow_transition_active", kTransitionActive, 1},
    {"saved_transform_valid", kSavedValid, 1},
    {"transform_fallback", kTransformFallback, 1},
    {"anchor_update_flag", kAnchorUpdate, 1},
    {"tripod_anchor_flag", kTripodAnchor, 1},
};

bool lean_active(const Image& image) {
    for (const std::size_t offset : {0x530u, 0x532u, 0x536u, 0x538u, 0x540u, 0x542u, 0x546u}) {
        if (rd16(image, offset) != 0) {
            return true;
        }
    }
    return false;
}

struct Reader {
    std::istringstream in;
    explicit Reader(const std::string& line) : in(line) {}
    long long next() {
        long long v = 0;
        in >> v;
        return v;
    }
};

void apply_diff(Reader& r, Image& image) {
    const long long count = r.next();
    for (long long i = 0; i < count; ++i) {
        const auto index = static_cast<std::size_t>(r.next());
        const auto value = static_cast<std::uint32_t>(r.next());
        wr32(image, index * 4, value);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s FIXTURE [max-report]\n", argv[0]);
        return 2;
    }
    std::ifstream file(argv[1]);
    if (!file) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    std::string line;
    std::getline(file, line);
    Image previous_exit{};
    {
        Reader r(line);
        apply_diff(r, previous_exit);
    }

    std::map<std::string, int> mismatches;
    std::map<std::string, int> first_seen;
    int updates = 0;
    int lean_excluded = 0;
    int follow_gap_excluded = 0;
    int reported = 0;
    const int max_report = argc > 2 ? std::atoi(argv[2]) : 8;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }
        Reader r(line);
        const long long seq = r.next();
        Image entry = previous_exit;
        apply_diff(r, entry);
        Image exit_image = entry;
        apply_diff(r, exit_image);

        Q16Vec3 tripod_pos{static_cast<Raw>(r.next()), static_cast<Raw>(r.next()),
                           static_cast<Raw>(r.next())};
        Q16Vec3 follow_off{static_cast<Raw>(r.next()), static_cast<Raw>(r.next()),
                           static_cast<Raw>(r.next())};
        Q16Vec3 dist_off{static_cast<Raw>(r.next()), static_cast<Raw>(r.next()),
                         static_cast<Raw>(r.next())};
        const auto tripod_state = static_cast<Raw>(r.next());
        const auto behavior_flag = static_cast<std::uint32_t>(r.next());
        const auto effect_gate = static_cast<std::uint32_t>(r.next());
        const auto effect_transform_gate = r.next() != 0;
        const bool history_valid = r.next() != 0;
        const auto history_sample = static_cast<Raw>(r.next());
        const auto direction_result = static_cast<std::int32_t>(r.next());
        const auto distance_bias = static_cast<Raw>(r.next());
        const auto delta_q8 = static_cast<Raw>(r.next());
        const auto shake_mult = static_cast<std::int32_t>(r.next());
        const auto vertical_effect = static_cast<Raw>(r.next());
        const auto shared_angle = static_cast<Raw>(r.next());
        CameraViewportParameterControlRaw viewport_control{};
        CameraFramingInputControlRaw framing{};
        framing.global_override = r.next() != 0;
        viewport_control.restore_from_global = r.next() != 0;
        viewport_control.global_parameter_raw = static_cast<Raw>(r.next());
        viewport_control.decrement = r.next() != 0;
        viewport_control.increment = r.next() != 0;
        viewport_control.reset_to_default = r.next() != 0;
        framing.restore_axes_from_globals = r.next() != 0;
        framing.rotation_decrement = r.next() != 0;
        framing.rotation_increment = r.next() != 0;
        framing.directional_input_raw = static_cast<std::uint8_t>(r.next());
        framing.x_decrement = r.next() != 0;
        framing.x_increment = r.next() != 0;
        framing.y_decrement = r.next() != 0;
        framing.y_increment = r.next() != 0;
        framing.z_decrement = r.next() != 0;
        framing.z_increment = r.next() != 0;
        const auto primary_path_state = static_cast<Raw>(r.next());
        (void)primary_path_state;
        const auto exit_vertical_effect = static_cast<Raw>(r.next());
        (void)r.next(); // exit shared angle
        Q16Vec3 facing{static_cast<Raw>(r.next()), static_cast<Raw>(r.next()),
                       static_cast<Raw>(r.next())};
        const bool tripod_is_player = r.next() != 0;
        const bool camera_override = r.next() != 0;
        Q16Vec3 framing_globals{static_cast<Raw>(r.next()), static_cast<Raw>(r.next()),
                                static_cast<Raw>(r.next())};
        const auto exit_seed_x = static_cast<Raw>(r.next());
        const auto exit_seed_z = static_cast<Raw>(r.next());
        const bool retarget_block = r.next() != 0;
        const bool refresh_block = r.next() != 0;
        CameraAlternateFollowInputRaw alternate{};
        alternate.tripod_present = true;
        alternate.tripod_basis_valid = true;
        for (auto& cell : alternate.tripod_basis_q12) {
            cell = static_cast<std::int16_t>(r.next());
        }

        CameraStateRaw camera = unpack(entry, vertical_effect, shared_angle);
        camera.framing_globals_raw = framing_globals;

        CameraTargetRaw target{};
        target.position = tripod_pos;
        target.secondary_position = tripod_pos;
        target.has_secondary = camera.target_valid_raw != 0;
        target.follow_offset = follow_off;
        target.tripod_state = static_cast<std::uint32_t>(tripod_state);
        target.tripod_behavior_flag = behavior_flag;
        target.tripod_effect_gate = effect_gate;
        target.distance_sample_offset = dist_off;
        target.distance_sample_valid = true;
        target.tripod_facing_q12 = facing;
        target.tripod_is_player = tripod_is_player;

        CameraFollowInput follow{};
        follow.tripod_state = target.tripod_state;
        follow.direction_state_result = direction_result;

        CameraUpdateHooks hooks{};
        hooks.shake_phase_multiplier = shake_mult;
        hooks.smoothing_delta_q8 = delta_q8;

        CameraMode25ProducerInputRaw mode25{};
        mode25.tripod_present = true;
        mode25.tripod_follow_offset_y_raw = follow_off.y;
        mode25.tripod_physics_state = tripod_state;

        CameraSmoothingProducerInputRaw smoothing{};
        smoothing.valid = true;
        smoothing.history_sample_valid = history_valid;
        smoothing.history_sample_raw = history_sample;
        smoothing.distance_bias_q4 = distance_bias;
        smoothing.global_override = camera_override;
        smoothing.tripod_effect_gate = effect_transform_gate;
        smoothing.vertical_effect_valid = false;
        // Camera_ApplyEffects' collision query is a world-side producer: feed
        // its observed result (distance -1 means no hit) back as the boundary.
        smoothing.effect_sample_valid = true;
        smoothing.effect_sample.primary_path_valid = true;
        smoothing.effect_sample.tripod_physics_state = tripod_state;
        const auto observed_distance = static_cast<Raw>(rd32(exit_image, kEffectDistance));
        smoothing.effect_sample.hit = observed_distance >= 0;
        smoothing.effect_sample.hit_distance_raw = observed_distance;
        smoothing.effect_sample.retarget_block = retarget_block;
        smoothing.effect_sample.refresh_block = refresh_block;
        smoothing.effect_seed_valid = true;
        smoothing.effect_seed = {exit_seed_x, exit_vertical_effect, exit_seed_z};

        (void)update_camera(camera, target, follow, {}, hooks, {}, mode25, alternate,
                            viewport_control, framing, smoothing);

        Image native = entry;
        pack(camera, native);
        // Camera_ApplyEffects' Y-lean raycast subsystem (+0x530/+0x532/+0x536/
        // +0x538 and their +0x540.. histories, driven by collision queries at
        // 0x0040c740..0x0040d1b1) is not modelled. Updates in which retail
        // has any of those terms live are excluded, not compared.
        if (lean_active(entry) || lean_active(exit_image)) {
            ++lean_excluded;
            ++updates;
            previous_exit = exit_image;
            continue;
        }
        // Open gap: with the tripod in physics state 2 and the follow
        // preparation counter (+0x60c) at 4 or more, retail's history collapses
        // toward the up vector and its target transform diverges from the
        // recovered basis (idle Warehouse, seq 241..251). Not compared.
        if (tripod_state == 2 && rd32(exit_image, 0x60c) >= 4) {
            ++follow_gap_excluded;
            ++updates;
            previous_exit = exit_image;
            continue;
        }
        bool bad = false;
        for (const auto& span : kSpans) {
            if (std::memcmp(&native[span.offset], &exit_image[span.offset], span.size) != 0) {
                ++mismatches[span.name];
                first_seen.emplace(span.name, static_cast<int>(seq));
                bad = true;
                if (reported < max_report) {
                    ++reported;
                    std::printf("seq %lld %s: native", seq, span.name);
                    const std::size_t unit =
                        span.size % 4 == 0 ? 4 : (span.size == 6 ? 2 : span.size);
                    for (std::size_t i = 0; i < span.size; i += unit) {
                        std::uint32_t nv = 0, rv = 0;
                        std::memcpy(&nv, &native[span.offset + i], unit);
                        std::memcpy(&rv, &exit_image[span.offset + i], unit);
                        std::printf(" %d/%d", static_cast<int>(nv), static_cast<int>(rv));
                    }
                    std::printf(" (native/retail)\n");
                }
            }
        }
        if (camera.shared_vertical_effect_q16 != exit_vertical_effect) {
            std::printf("seq %lld shared_vertical_effect: native %d retail %d (entry %d) framing %d %d %d\n", seq,
                        camera.shared_vertical_effect_q16, exit_vertical_effect, vertical_effect,
                        framing_globals.x, framing_globals.y, framing_globals.z);
            ++mismatches["shared_vertical_effect"];
            first_seen.emplace("shared_vertical_effect", static_cast<int>(seq));
            bad = true;
        }
        (void)bad;
        ++updates;
        previous_exit = exit_image;
    }

    std::printf("%d updates replayed, %d compared, %d excluded (unmodelled Y-lean terms live), "
                "%d excluded (state-2 follow-transition gap)\n",
                updates, updates - lean_excluded - follow_gap_excluded, lean_excluded,
                follow_gap_excluded);
    int total = 0;
    for (const auto& [name, count] : mismatches) {
        std::printf("MISMATCH %-28s %4d updates (first seq %d)\n", name.c_str(), count,
                    first_seen[name]);
        total += count;
    }
    if (total == 0) {
        std::printf("all modeled camera fields match retail\n");
    }
    return total == 0 ? 0 : 1;
}
