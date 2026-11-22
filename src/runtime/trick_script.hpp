#pragma once

#include "retail_image.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace opentony::runtime {

// Phase 4.7a: the TRICKS.BIN script interpreter TrickScript_Execute (0x004be450) and its
// per-frame recognition wrapper TrickScript_Update (0x00492ea0), run over the raw skater
// image (0x3538 bytes) at the retail offsets.
constexpr std::size_t kSkaterImageSize = 0x3538;
constexpr std::size_t kControllerImageSize = 0x100;

// Calls the trick code makes to code outside 4.7a. Each is identified by its retail
// address and reported to the services object in call order; the object may modify the
// skater image (a service's own effect) and returns eax.
enum class TrickService : std::uint8_t {
    AnimStart,           // 0x004903f0 (anim, source tag)
    AnimStartDefaultEnd, // 0x00490420
    AnimRange,           // 0x00490450
    AnimFull,            // 0x00490480 (5 words)
    StateTransition,     // 0x004900b0 (new state, source tag)
    PointTimer,          // 0x0048f720 TrickScore_AdvancePointTimer (points)
    Bail,                // 0x004904d0 CBruce__Bail (state script index, flags)
    TakeOff,             // 0x0048f5f0 CBruce__Trick_TakeOff
    Land,                // 0x0048f8f0 CBruce__Trick_Land
    GapApply,            // 0x0048f690 Gap_ApplyDefinitionState (name, points, flag, index)
    BoardOn,             // 0x0046c630 (opcode 0x32)
    BoardOff,            // 0x0046c5f0 (opcode 0x33)
    SfxPlay,             // 0x004ad620 SFX_PlayX (id, volume, pan)
    Vibrate,             // 0x0048ffc0 CBruce__Vibrate (pad, motor, power)
    RailRestart,         // 0x00490c70
    Fn46dd20,            // 0x0046dd20 (0, 0xfa)
    Fn431260,            // 0x00431260 (+0x30b0, 1)
    MaybeManual,         // 0x00497bb0 CBruce__MaybeManual (from TrickScript_QueueStart)
    Assert,              // 0x004011e0 M3dUtils_InBetween (condition first); only logs
    Rnd,                 // 0x004c9340 Rnd(n), returns the roll
};

[[nodiscard]] constexpr std::size_t trick_service_arg_count(TrickService service) noexcept {
    switch (service) {
    case TrickService::AnimStart: return 2;
    case TrickService::AnimStartDefaultEnd: return 3;
    case TrickService::AnimRange: return 4;
    case TrickService::AnimFull: return 5;
    case TrickService::StateTransition: return 2;
    case TrickService::PointTimer: return 1;
    case TrickService::Bail: return 2;
    case TrickService::TakeOff: return 0;
    case TrickService::Land: return 0;
    case TrickService::GapApply: return 4;
    case TrickService::BoardOn: return 0;
    case TrickService::BoardOff: return 0;
    case TrickService::SfxPlay: return 3;
    case TrickService::Vibrate: return 3;
    case TrickService::RailRestart: return 0;
    case TrickService::Fn46dd20: return 2;
    case TrickService::Fn431260: return 2;
    case TrickService::MaybeManual: return 2;
    case TrickService::Assert: return 1;
    case TrickService::Rnd: return 1;
    }
    return 0;
}

struct TrickServiceCall final {
    TrickService service{};
    std::array<std::uint32_t, 6> args{};
};

class TrickServices {
public:
    virtual ~TrickServices() = default;
    // Performs (or replays) the call; may edit `skater`; returns eax.
    virtual std::uint32_t call(RetailImage& skater, const TrickServiceCall& call) = 0;
};

// Inputs the trick code reads from outside the skater object.
struct TrickWorld final {
    std::span<const std::uint8_t> tricks_bin;   // TRICKS.BIN as loaded
    std::uint32_t tricks_base{};                // TricksBinaryBase: address of tricks_bin[0]
    std::uint32_t clock{};                      // AnimationClock 0x005685f4
    std::int32_t time_scale{};                  // AnimationTimeScale 0x0056865c
    std::int32_t step{};                        // DAT_0056a93c (tick delta)
    std::int32_t event_latch{};                 // LevelEventLatch 0x00568658
    const RetailImage* controller{};            // the record at skater +0x2ccc (0x100 bytes)
    std::span<const std::uint16_t> key_table;   // generated per-player trick table
    std::array<std::uint32_t, 256>* opcode_counts{};  // optional coverage counter for Execute
};

// A script read left the TRICKS.BIN image (retail would read wild memory).
class TrickScriptFault final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct TrickExecuteResult final {
    std::size_t commands{};
    bool finished{};  // opcode 0x07
    bool yielded{};   // a wait that returns to the frame loop
};

constexpr std::size_t kTrickCommandBudget = 100000;

// TrickScript_Execute: runs commands from the cursor at skater +0x29cc until one
// returns to the frame loop.
TrickExecuteResult trick_script_execute(
    RetailImage& skater, const TrickWorld& world, TrickServices& services);

// TrickScript_Update: history refresh, sequence selection, pending start, execute.
void trick_script_update(RetailImage& skater, const TrickWorld& world, TrickServices& services);

// Pieces of the update, exposed for tests and for the slice-level replays.
void trick_input_refresh_history(RetailImage& skater, const TrickWorld& world, TrickServices& services);
void trick_input_select_sequence(RetailImage& skater, const TrickWorld& world, TrickServices& services);
void trick_script_start_pending(RetailImage& skater, const TrickWorld& world, TrickServices& services);
void trick_script_start(RetailImage& skater, std::uint32_t cursor);
[[nodiscard]] bool trick_history_query(const RetailImage& skater);  // FUN_004924f0
[[nodiscard]] std::int32_t trick_resolve_frame_operand(const RetailImage& skater, std::int32_t operand);

} // namespace opentony::runtime
