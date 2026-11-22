#include "frame_pacing.hpp"
#include "skater_views.hpp"

// Milestone 10g: the physics dispatcher (physics.cpp, 0x0049db80 Skater_PhysicsDispatcher), written from the machine code. It builds a mask of what the skater is doing (state 0 on the ground 1 or in a
// lip trick 0x10, state 4 on a rail 2, state 5 4, states 1 and 2 0x20, everything with the physics lock), clears every one of the 20 pending flags (+0x2f74..) whose record in the table at 0x0056dd58
// (44 bytes each, the mask word first) shares a bit with it, drops the gap the skater was in when it left the ground or the rail (the trick-take-off, the gap's score, the sound), and runs the physics
// of the state: the ground, the air, the rail, the lip, the footplant or the special basis. A hard landing (a downward speed over 0x3e8 with a steep surface or a recent change) turns the skater into
// a bail. Everything it calls is a service (the parts of the ground physics are ports of their own).

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kTakeOff = 0x0048f5f0;             // CBruce__Trick_TakeOff (thiscall)
constexpr Word kApplyGap = 0x0048f690;            // Gap_ApplyDefinitionState(name, points, flag, index) (thiscall, the callee pops the four arguments)
constexpr Word kCompleteGap = 0x00414c50;         // Gap_CompleteDefinition(gap) (cdecl)
constexpr Word kGapSound = 0x004c8550;            // (cdecl) -> a value for the next call
constexpr Word kGapEffect = 0x004c5b00;           // (cdecl)
constexpr Word kPlaySound = 0x004ad620;           // (sound, volume, 0) (cdecl)
constexpr Word kStateTransition = 0x004900b0;     // CBruce__StateTransition(state, source line) (thiscall)
constexpr Word kBail = 0x004904d0;                // CBruce__Bail(script, cause) (thiscall)
constexpr Word kGroundPreparation = 0x0049dad0;
constexpr Word kDoGroundPhysics = 0x00496550;
constexpr Word kGroundPostCollision = 0x00495cc0;
constexpr Word kBounceRecovery = 0x0049d9c0;
constexpr Word kDoPhysicsInAir = 0x00497f40;
constexpr Word kRailPhysics = 0x00494210;
constexpr Word kSweptContact = 0x00499710;
constexpr Word kFootplant = 0x004993f0;
constexpr Word kSpecialBasis = 0x004995d0;

constexpr Word kFrameClock = 0x005685f4;
constexpr Word kTimeScale = 0x0056865c;
constexpr Word kFrameStep = 0x0056a93c;
constexpr Word kFlagTable = 0x0056dd58;

}  // namespace

// 0x0049db80 (thiscall)
void SkaterKit::physics_dispatcher(Word self) {
    Word mask = skater_at(m_, self).field_2f64() != 0 ? 0xffffffffu : 0;
    switch (skater_at(m_, self).physics_state()) {
    case 0:
        mask |= static_cast<Word>(skater_at(m_, self).field_2dd4()) == 0 ? 0x1 : 0x10;
        break;
    case 1:
    case 2:
        mask |= 0x20;
        break;
    case 4:
        skater_at(m_, self).set_field_2efc(static_cast<Word>(skater_at(m_, self).field_2efc() + global_word(m_, kFrameStep)));
        mask |= 0x2;
        break;
    case 5:
        mask |= 0x4;
        break;
    default:
        break;
    }
    for (Word k = 0; k < 20; ++k) {
        const Word flag = self + 0x2f74 + 4 * k;
        if (word_at(m_, flag) != 0) {
            const Word record = global_word(m_, kFlagTable) + word_at(m_, flag + 0x50) * 44;
            if ((mask & half_at(m_, record)) != 0) {
                set_word_at(m_, flag, 0);
            }
        }
    }

    // the gap the skater left the ground (+0x3018) or the rail (+0x3014) in is applied and completed
    const auto leave_gap = [&](Word field, bool sound_after) {
        const Word gap = word_at(m_, field);
        const Word score = static_cast<Word>(0) - static_cast<Word>(static_cast<std::int32_t>(static_cast<std::int16_t>(half_at(m_, gap + 6))));
        thiscall(kApplyGap, self, {gap + 8, score, 2, 0xffffffff});
        engine(kCompleteGap, {word_at(m_, field)});
        if (sound_after) {
            engine(kPlaySound, {0xa1, 0x2000, 0});
        }
    };
    if (skater_at(m_, self).physics_state() == 0) {
        if (static_cast<Word>(skater_at(m_, self).field_2dd4()) != 0 && skater_at(m_, self).deferred_gap_definition_ground().address != 0) {
            if (skater_at(m_, self).field_3064() == 0) {
                thiscall(kTakeOff, self);
            }
            leave_gap(self + 0x3018, false);
            const Word value = engine_value(kGapSound, {static_cast<Word>(skater_at(m_, self).deferred_gap_source_ground())});
            engine(kGapEffect, {value});
            engine(kPlaySound, {0xa1, 0x2000, 0});
        }
        skater_at(m_, self).set_deferred_gap_definition_ground(GuestPtr<guest::RuntimeGapDefinition>{0});
        skater_at(m_, self).set_deferred_gap_definition_state4(GuestPtr<guest::RuntimeGapDefinition>{0});
    }
    if (skater_at(m_, self).physics_state() == 4 && skater_at(m_, self).deferred_gap_definition_state4().address != 0) {
        leave_gap(self + 0x3014, true);
        const Word effect = static_cast<Word>(skater_at(m_, self).deferred_gap_source_state4());
        skater_at(m_, self).set_deferred_gap_definition_state4(GuestPtr<guest::RuntimeGapDefinition>{0});
        const Word value = engine_value(kGapSound, {effect});
        engine(kGapEffect, {value});
    }
    if (skater_at(m_, self).field_f6() != 0x82) {
        skater_at(m_, self).set_field_2dd0(0);
    }

    switch (skater_at(m_, self).physics_state()) {
    case 0: {
        skater_at(m_, self).set_field_30c4(0);
        if (static_cast<Word>(skater_at(m_, self).field_2dd4()) == 0) {
            for (const Word field : {0x2ef0u, 0x2ef4u, 0x2ef8u, 0x2efcu, 0x2e88u}) {
                set_word_at(m_, self + field, 0);
            }
        }
        skater_at(m_, self).set_field_2ec8(0);
        thiscall(kGroundPreparation, self);
        thiscall(kDoGroundPhysics, self);
        thiscall(kGroundPostCollision, self);
        thiscall(kBounceRecovery, self);
        if (skater_at(m_, self).physics_state() != 0 || int_at(m_, self + 0x3110) <= 0x3e8) {
            return;
        }
        // a fast fall: onto a steep slope, or soon after the last landing, it is a bail
        if (static_cast<std::int32_t>(skater_at(m_, self).field_3130()) <= 0x5000) {
            const std::int32_t since = static_cast<std::int32_t>(global_word(m_, kFrameClock) - skater_at(m_, self).field_2d98());
            const std::int32_t recent = framepacing::clock_threshold(global_int(m_, kTimeScale), 6);
            if (since >= recent) {
                return;
            }
        }
        set_word_at(m_, skater_at(m_, self).camera().address + 0x504, 0x19);
        thiscall(kStateTransition, self, {1, 0x2ba1});
        if (int_at(m_, self + 0x50) < 0) {
            set_word_at(m_, self + 0x50, 0);
        }
        thiscall(kBail, self, {0x14, 0});
        skater_at(m_, self).set_field_3204(0x28);
        return;
    }
    case 1:
        thiscall(kDoPhysicsInAir, self);
        return;
    case 2:
        skater_at(m_, self).set_field_30c4(1);
        thiscall(kDoGroundPhysics, self);
        return;
    case 3:
        thiscall(kDoPhysicsInAir, self);
        if (skater_at(m_, self).physics_state() == 3 && static_cast<std::int32_t>(global_word(m_, kFrameClock) - skater_at(m_, self).field_2da0()) > 0x19) {
            thiscall(kStateTransition, self, {1, 0x2bf2});
        }
        return;
    case 4:
        skater_at(m_, self).set_field_30c4(0);
        thiscall(kRailPhysics, self);
        return;
    case 5:
        skater_at(m_, self).set_field_30c4(0);
        skater_at(m_, self).set_field_2ec8(0);
        thiscall(kSweptContact, self);
        return;
    case 6:
        thiscall(kFootplant, self);
        thiscall(kDoPhysicsInAir, self);
        return;
    case 7:
        thiscall(kGroundPreparation, self);
        thiscall(kDoGroundPhysics, self);
        thiscall(kGroundPostCollision, self);
        for (Word k = 0; k < 12; k += 4) {
            set_word_at(m_, self + 0x8 + k, word_at(m_, self + 0xbc + k));
        }
        thiscall(kBounceRecovery, self);
        return;
    case 8:
        skater_at(m_, self).set_field_30c4(0);
        thiscall(kSpecialBasis, self);
        return;
    default:
        return;
    }
}

}  // namespace opentony::runtime::skaterkit
