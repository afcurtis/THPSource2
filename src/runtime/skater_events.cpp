#include "frame_pacing.hpp"
#include "skater_views.hpp"

// Phase 4.15a: the rail events and the terrain effects of physics.cpp (0x00490ae0, 0x00490c70, 0x00490e30, 0x00493f40, 0x00495af0): what the skater does when a rail node leads to a trick object or a gap, the
// handplant, the burst of glass when a tag is acknowledged, and the dust and sparks of the terrain under the skater. The level's node table is `OffsetList` (the first short of a node is its type), the link
// table of a node (a short count, then the linked nodes) comes from the trigger layer (`TRG_GetNodeLinks`, a service), and the effects are services reported by address.

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

inline std::int32_t sext16(Word v) { return static_cast<std::int16_t>(static_cast<std::uint16_t>(v)); }

constexpr Word kOffsetList = 0x0056e210;        // OffsetList: the array of node pointers
constexpr Word kGapObjectList = 0x0056e220;     // the list of script objects (a chain: the object at +0, the next at +0x14)
constexpr Word kGapTable = 0x0056dd58;          // ActiveGapDefinitionTable: 0x2c bytes an entry, the gap number (short) at +4, -1 ends it
constexpr Word kEffectGate = 0x005244cc;        // dword: cleared while the effects are made, 1 afterwards
constexpr Word kParticleCount = 0x0055f72c;     // dword: the effects stop being made at 250

constexpr Word kGetNodeLinks = 0x004c8550;      // TRG_GetNodeLinks(node)
constexpr Word kExecuteCommands = 0x004c5b00;   // TRG_ExecuteCommandList(links)
constexpr Word kCompleteGap = 0x00414c50;       // Gap_CompleteDefinition(entry)
constexpr Word kFindEnviroItem = 0x004b1ef0;    // Spool_FindEnviroItem(id) -> the item
constexpr Word kRegisterTrickObject = 0x0048f800;   // Trick_RegisterObject(item) (thiscall on the skater)
constexpr Word kActivateTrickObject = 0x004bd980;   // TrickObject_ActivateForObject(item, skater, -1)
constexpr Word kTakeOff = 0x0048f5f0;           // Trick_TakeOff() (thiscall on the skater)
constexpr Word kEffectSetSource = 0x00406070;   // (position pointer)
constexpr Word kEffectSetAngles = 0x00406090;   // (three shorts)
constexpr Word kEffectSetMode = 0x00406160;     // (1)
constexpr Word kEffectSetColourA = 0x004061c0;  // (a, b, c)
constexpr Word kEffectSetColourB = 0x004061e0;  // (a, b, c)
constexpr Word kMemGetFree = 0x0046f2c0;        // Mem_GetFree(1)
constexpr Word kNewObject = 0x00403d30;         // the object allocator (size) -> the block, 0 when there is none
constexpr Word kSpawnDust = 0x00406200;         // (position, 0x14, size, 0x14) (thiscall on the new object)
constexpr Word kSpawnGlass = 0x0040b060;        // (skater, direction, -40, 0x1000, 5) (thiscall on the new object)
constexpr Word kTagAcknowledged = 0x004de5c0;   // NetGame_TagAcknowledged(skater)

constexpr Word kMsgNotRail = 0x00536b74;        // "Node %d not Raildef"
constexpr Word kMsgInvalidCheck = 0x00536b40;   // "RAIL at node %d HAS INVALID CHECK..."
constexpr Word kHandplantName = 0x00536b88;     // "Handplant"

constexpr Word kScratchShorts = pcplatform::kScratchBase + 0x640;     // the three shorts handed to the effect layer
constexpr Word kScratchVector = pcplatform::kScratchBase + 0x660;     // the direction of the glass

}  // namespace

// the type of a node of the node table
Word SkaterKit::node_type(Word node) { return half_at(m_, word_at(m_, global_word(m_, kOffsetList) + 4 * node)); }

// the first linked object of a node: a node of type 0xe or 0xc among its first two links (0 when there is none)
Word SkaterKit::linked_object(Word links) {
    const Word count = half_at(m_, links);
    for (Word link = 1; link <= 2; ++link) {
        if (count >= link) {
            const Word target = word_at(m_, global_word(m_, kOffsetList) + 4 * half_at(m_, links + 2 * link));
            const Word type = half_at(m_, target);
            if (type == 0xe || type == 0xc) {
                return target;
            }
        }
    }
    return 0;
}

// the dword that follows a linked object's data (its identifier): after the short at +2 counted in shorts from +4, on a 4-byte boundary
Word SkaterKit::linked_identifier_at(Word object) {
    Word at = object + 4 + half_at(m_, object + 2) * 2;
    if ((at & 3) != 0) {
        at += 2;
    }
    return at;
}

// 0x00490ae0 (thiscall, one argument: the node, a short): from a rail node (type 0xb) the first linked object is looked up as an environment item (`Spool_FindEnviroItem`), registered with the trick layer
// and, in the mode 1, activated for the skater. A node that links no such object leads on to the node before it (`Rail_PrevNodeQuick`, in the mode 8 only) until there is none or the walk is back at
// the start
void SkaterKit::register_rail_trick_object(Word self, Word node_argument) {
    Word raw = node_argument;
    const std::int32_t origin = sext16(raw);
    std::int32_t node = origin;
    Word object = 0;
    for (;;) {
        check(node_type(u(node)) == 0xb, kMsgNotRail);
        const Word links = engine_value(kGetNodeLinks, {u(node)});
        object = linked_object(links);
        if (object != 0) {
            break;
        }
        if (global_word(m_, globals::kGameMode) != 8) {
            return;
        }
        raw = rail_.prev_node_quick(raw, half_at(m_, self + 0x2e9c));
        if ((raw & 0xffff) == 0) {
            return;
        }
        node = sext16(raw);
        if (node == origin) {
            return;
        }
    }
    const Word item = engine_value(kFindEnviroItem, {word_at(m_, linked_identifier_at(object))});
    check(item != 0, kMsgInvalidCheck);
    engine(kRegisterTrickObject, {self, item});
    if (global_word(m_, globals::kGameMode) == 1) {
        engine(kActivateTrickObject, {item, self, 0xffffffffu});
    }
}

// 0x00490c70 (fastcall): the same walk from the node the skater stands at (+0x2ea0); the object found names a gap (a script object of type 0xc9 in the list at 0x0056e220 carries the same identifier and
// then the gap's number); the gap's definition is applied with its value negated, the skater is told (+0x2c70), the gap is completed, a sound plays and the node's commands run
void SkaterKit::complete_rail_gap(Word self) {
    const Word origin = word_at(m_, self + 0x2ea0);
    Word node = origin;
    Word object = 0;
    for (;;) {
        check(node_type(node) == 0xb, kMsgNotRail);
        const Word links = engine_value(kGetNodeLinks, {node});
        object = linked_object(links);
        if (object != 0) {
            break;
        }
        node = rail_.prev_node_quick(node, half_at(m_, self + 0x2e9c)) & 0xffff;
        if (node == 0 || node == origin) {
            return;
        }
    }
    const Word identifier_at = linked_identifier_at(object);
    const Word identifier = word_at(m_, identifier_at);
    Word chain = global_word(m_, kGapObjectList);
    Word number_at = 0;
    while (chain != 0) {
        const Word script = word_at(m_, chain);
        if (half_at(m_, script) == 0xc9) {
            const Word at = (script + 5) & ~3u;
            if (word_at(m_, at) == identifier) {
                number_at = at + 4;
                break;
            }
        }
        chain = word_at(m_, chain + 0x14);
    }
    if (number_at == 0) {
        return;
    }
    const Word number = half_at(m_, number_at);
    Word entry = global_word(m_, kGapTable);
    Word index = 0;
    Word entry_number = half_at(m_, entry + 4);
    while (entry_number != 0xffff) {
        if (u(sext16(entry_number)) == number) {
            break;
        }
        entry_number = half_at(m_, entry + 0x30);
        entry += 0x2c;
        ++index;
    }
    if (half_at(m_, entry + 4) == 0xffff) {
        return;
    }
    engine(kApplyGapDefinition, {self, entry + 8, u(-sext16(half_at(m_, entry + 6))), 0, index});
    skater_at(m_, self).set_field_2c70(1);
    engine(kCompleteGap, {entry});
    engine(kSoundPlay, {0xa1, 0x2000, 0});
    const Word again = engine_value(kGetNodeLinks, {node});
    engine(kExecuteCommands, {again});
}

// 0x00490e30 (fastcall): the handplant. Taking off first when the skater is not in the air (+0x3064); without a handplant to do (still 0) the flag +0x2e94 is cleared. Otherwise the gap "Handplant" is applied, the
// animation 0x22 starts, the state becomes 8, a sound plays, the position is the handplant position (+0x2ea8), the rail object of the node (+0x2ea0) is registered, the speed in x and z stops and y is negated
void SkaterKit::enter_handplant(Word self) {
    skater_at(m_, self).set_field_2f54(0);
    if (skater_at(m_, self).field_3064() == 0) {
        // the take-off may put the skater into the air (+0x3064), which the rest of the function then acts on
        skater_at(m_, self).set_field_3064(static_cast<Word>(engine_answer(kTakeOff, {self}, skater_at(m_, self).field_3064())));
        if (skater_at(m_, self).field_3064() == 0) {
            skater_at(m_, self).set_field_2e94(0);
            return;
        }
    }
    engine(kApplyGapDefinition, {self, kHandplantName, 1000, 1, 0xffffffffu});
    animation_request_start_default_end(self, 0x22, 8);
    state_transition(self, 8);
    engine(kSoundPlay, {0xe7, 0x1000, 0});
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + 8 + 4 * k, word_at(m_, self + 0x2ea8 + 4 * k));
    }
    register_rail_trick_object(self, ((self + 8) & 0xffff0000u) | half_at(m_, self + 0x2ea0));
    const Word flipped = u(-s(word_at(m_, self + 0x50)));
    set_word_at(m_, self + 0x4c, 0);
    set_word_at(m_, self + 0x54, 0);
    skater_at(m_, self).set_field_2ec8(static_cast<Word>(word_at(m_, self + 0x2e9c)));
    set_word_at(m_, self + 0x50, flipped);
}

// 0x00493f40 (fastcall): a tag acknowledged shatters nine pieces of glass off the skater (while fewer than 250 effects exist): the effect layer is aimed at the skater (its orientation angles first) and every
// piece is a new object handed the skater, the direction (0, 0x19000, 0), -40, 0x1000 and 5; the effect gate is closed while they are made. The tag layer is told last
void SkaterKit::spawn_tag_glass(Word self) {
    if (s(global_word(m_, kParticleCount)) >= 0xfa) {
        return;
    }
    if (!framepacing::runs_30hz_frame([&] { return global_word(m_, 0x0056a940); })) {
        return;   // F2C: the rail code calls this on every frame while the flag stays set (a grind): nine pieces per 30 Hz frame, as retail's frame, not per frame (4 times as dense at 120)
    }
    orientation_angles(self);
    engine(kEffectSetSource, {self + 0x14});
    set_global_half(m_, kScratchShorts, 0x1c2);
    set_global_half(m_, kScratchShorts + 2, 900);
    set_global_half(m_, kScratchShorts + 4, 0);
    engine(kEffectSetAngles, {kScratchShorts}, bytes_string(kScratchShorts, 6));
    set_global_word(m_, kEffectGate, 0);
    set_global_word(m_, kScratchVector, 0);
    set_global_word(m_, kScratchVector + 4, 0x19000);
    set_global_word(m_, kScratchVector + 8, 0);
    for (Word piece = 0; piece < 9; ++piece) {
        const Word glass = engine_value(kNewObject, {0x6c});
        if (glass != 0) {
            engine(kSpawnGlass, {glass, self, kScratchVector, 0xffffffd8u, 0x1000, 5}, bytes_string(kScratchVector, 12));
        }
    }
    set_global_word(m_, kEffectGate, 1);
    engine(kTagAcknowledged, {self});
}

// 0x00495af0 (fastcall): the terrain under a moving skater. The speed is the integer root of the velocity's squared length (Q12) times 64. On a surface of kind 3, 5 or 6 (+0x30b0) and while fewer than 250
// effects exist, a fast skater (over 0x10000) throws six puffs of dust (when the heap has more than 10000 bytes) in the colour of the surface: each is a new object started at the skater's position and
// pushed along the velocity. A fast skater on such a surface also rumbles the pad (stronger when slow, the surface's strength over the speed in whole units, at most 64)
void SkaterKit::do_terrain_effects(Word self) {
    const Word velocity = self + 0x4c;
    std::int32_t speed = s(sqrt_truncated(u(math_.dot_q12(velocity, velocity))) << 6);
    const Word surface = skater_at(m_, self).field_30b0();
    if ((surface == 5 || surface == 6 || surface == 3) && s(global_word(m_, kParticleCount)) < 0xfa) {
        orientation_angles(self);
        engine(kEffectSetSource, {self + 0x14});
        set_global_half(m_, kScratchShorts, 0x200);
        set_global_half(m_, kScratchShorts + 2, 0x1000);
        set_global_half(m_, kScratchShorts + 4, 0);
        engine(kEffectSetAngles, {kScratchShorts}, bytes_string(kScratchShorts, 6));
        engine(kEffectSetMode, {1});
        if (speed <= 0x10000) {
            return;
        }
        switch (skater_at(m_, self).field_30b0()) {
        case 3:
            engine(kEffectSetColourA, {0x80, 0x40, 0x20});
            break;
        case 5:
            engine(kEffectSetColourA, {2, 0xa0, 0x20});
            break;
        case 6:
            engine(kEffectSetColourA, {0xa0, 0xa0, 0xa0});
            break;
        default:
            break;
        }
        engine(kEffectSetColourB, {0, 0, 0});
        set_global_word(m_, kEffectGate, 0);
        const Word puffs = framepacing::runs_30hz_frame([&] { return global_word(m_, 0x0056a940); }) ? 6u : 0u;   // F1.5F: six puffs per 30 Hz frame
        for (Word puff = 0; puff < puffs; ++puff) {
            if (s(engine_value(kMemGetFree, {1})) > 10000) {
                const Word dust = engine_value(kNewObject, {0x50});
                Word placed = 0;       // (what the constructor answers, its object; with no object the vector is added at 0x1c, which faults in retail)
                if (dust != 0) {
                    placed = engine_value(kSpawnDust, {dust, self + 8, 0x14, u(speed >> 16), 0x14});
                }
                for (Word k = 0; k < 3; ++k) {
                    set_word_at(m_, placed + 0x1c + 4 * k, word_at(m_, placed + 0x1c + 4 * k) + word_at(m_, velocity + 4 * k));
                }
            }
        }
        set_global_word(m_, kEffectGate, 1);
    }
    if (speed > 0x10000) {
        speed >>= 12;
        if (speed > 0x40) {
            speed = 0x40;
        }
        std::int32_t strength = 0;
        switch (skater_at(m_, self).field_30b0()) {
        case 3:
            strength = 0x3fc0;
            break;
        case 5:
            strength = 0x2580;
            break;
        case 6:
            strength = 0x1180;
            break;
        default:
            return;
        }
        vibrate(self, 8, 1, u(strength / speed));
    }
}

}  // namespace opentony::runtime::skaterkit
