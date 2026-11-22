#include "world_views.hpp"

// Milestone 10e: the level's node walk of `trig.cpp` on the retail memory (TRG_ParseNodes 0x004c8130), ported from the disassembly (build/m10c/parse.dis; the dispatch table at 0x004c848c gives
// each node type its handler) and replayed against the retail trace of re/evidence/fixtures/trg-parse-nodes-*.txt (src/runtime/world_trig_nodes_trace_test.cpp). Every call out of it is a service
// by address, so the replay can stub each callee from the trace. Like the interpreter of 10c it writes the assertion location (the line at 0x00568638 and the file name at 0x0056881c) before the
// assertions that follow a stamp, as the retail macro does.

namespace opentony::runtime::worldkit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kFindPreambleFlag = 0x004c5420;        // TRG_FindObjectPreambleFlag(flag, preamble) -> al
constexpr Word kCreateObject = 0x004c5460;            // TRG_CreateObject(index)
constexpr Word kCreateLinkRecord = 0x004c84d0;        // TRG_CreateLinkRecord(key, index, stream)
constexpr Word kDecodeNodePosition = 0x004c8650;      // TRG_DecodeNodePosition(out, index) -> the node's angle words
constexpr Word kScanRestart = 0x004c7ea0;             // Trig_ScanRestart
constexpr Word kClearTrigMenu = 0x004c4b10;           // Trig_ClearTrigMenu
constexpr Word kCountThings = 0x00415380;             // Career_CountThings
constexpr Word kPrepareVideoMasks = 0x00469900;       // Game_PrepareVideoMasksWrapper -> nonzero: no level to parse
constexpr Word kBitNew = 0x00403d30;                  // CBit_New(size)
constexpr Word kInitExtendedRecord = 0x0045c390;      // TRG_InitializeExtendedRecord (thiscall, twelve arguments)
constexpr Word kClassNew = 0x00466b70;                // CClass_OpNew(size)
constexpr Word kTrickObjectConstruct = 0x004bd760;    // TrickObject_Construct (thiscall: node, 0)
constexpr Word kRegisterCameraPoint = 0x00411f30;     // TRG_RegisterCameraPoint(index)

constexpr Word kStatus = 0x00561bfc;                  // GStatus
constexpr Word kPrepared = 0x005614c4;                // byte: nonzero skips Game_PrepareVideoMasksWrapper
constexpr Word kTrigFile = 0x0056e20c;
constexpr Word kRecords = 0x0056e210;                 // OffsetList
constexpr Word kRecordCount = 0x0056e214;             // NumNodes
constexpr Word kRestartCount = 0x0056e21c;
constexpr Word kRestartTable = 0x0056dcb8;            // 0x28 pointers to the restart nodes' angle words + 6
constexpr Word kAssertionLine = 0x00568638;
constexpr Word kAssertionFile = 0x0056881c;
constexpr Word kBitFlag = 0x005244cc;
constexpr Word kLinkListStatic = 0x005477f0;          // the stream of the link records of types 2 and 9

constexpr Word kLocalVector = pcplatform::kScratchBase + 0xe230;   // 0x7ff0ea30, the out vector of TRG_DecodeNodePosition (the retail stack locals; 10c's kLocalPosition)

constexpr Word kAllocObjectStorage = 0x0047fd30;      // TRG_AllocObjectStorage(size)
constexpr Word kConstructScriptObject = 0x00403000;   // TRG_ConstructSubtype0x00cb (thiscall on the storage: payload end, node)
constexpr Word kConstructTrafficObject = 0x00412640;  // TRG_ConstructTrafficObject (thiscall: payload end, node, object type)
constexpr Word kConstructSubtype0x0192 = 0x0049f250;  // thiscall: payload end, node
constexpr Word kBaddyConstruct = 0x00401270;          // Baddy_Construct (thiscall)
constexpr Word kSelectPsxRegion = 0x0047fe30;         // Object_SelectNamedPsxRegion (thiscall: the name)
constexpr Word kListPushHead = 0x004801d0;            // ObjectList_PushHead (thiscall: the list head's address)
constexpr Word kReadFixedPosition = 0x00480240;       // TRG_ReadFixedPosition (thiscall: the record) -> the address after the three dwords
constexpr Word kReadObjectParameters = 0x004802c0;    // TRG_ReadObjectParameters (thiscall: the record) -> the address after the three words
constexpr Word kPowerupCreate = 0x004a8e50;           // Powerup_CreateFromTrigger(type, position, flag, word, node)
constexpr Word kPowerupSetNode = 0x004a82c0;          // CPowerUp__SetNode (thiscall: node)
constexpr Word kPowerupConstruct = 0x004a7c50;        // Powerup_Construct (thiscall: type, position, velocity, flags, word, node)
constexpr Word kObjectConstruct = 0x00480060;         // Object_Construct (thiscall)
constexpr Word kWantSprite = 0x00480420;              // Object_WantSprite (thiscall)
constexpr Word kRnd = 0x004c9340;                     // Rnd(limit)
constexpr Word kGroundHeight = 0x004ca140;            // Utils_GetGroundHeight(position, 0, 8000)
constexpr Word kFindModelIndex = 0x004b1de0;          // PSX_FindModelIndexByChecksum(checksum, region)
constexpr Word kGoalIndex = 0x00414540;               // Career_GoalIndex(goal)
constexpr Word kGotMoney = 0x00415060;                // Career_GotMoney(node) -> nonzero: already collected
constexpr Word kPowerupHide = 0x004a8250;             // maybe_destroy_model (thiscall)
constexpr Word kGetNodeLinks = 0x004c8550;            // TRG_GetNodeLinks(index) -> the links
constexpr Word kFindPreambleEnd = 0x004c5440;         // TRG_FindObjectPreambleEnd(preamble)
constexpr Word kSuspendToList = 0x004802f0;           // Object_SuspendToList (thiscall: the list head's address)
constexpr Word kOwner = 0x0056a954;                   // the owner record every object of the level points to (+0x1d8)
constexpr Word kRegionName = 0x0056e218;              // what the level's objects select as their PSX region
constexpr Word kPlatformVtable = 0x005194f8;
constexpr Word kPowerupVtable = 0x00519684;
constexpr Word kItemsRegion = 0x0056dcac;             // byte: the PSX region of "items"
constexpr Word kMedalsRegion = 0x0056dcad;            // byte: the PSX region of "skmedals"
constexpr Word kGoalRegion = 0x0056dcb4;              // byte: the region of the level goal's item
constexpr Word kCurrentLevel = 0x0056a898;
constexpr Word kGameMode = 0x00533f38;                // LevelEventGameModeSelector
constexpr Word kPowerupMask = 0x00568a24;             // byte: bit 0 / 1 / 2 / 4 of the competition's power-up kinds
constexpr Word kGoalTable = 0x0053c2b4;               // 0x1ac bytes per level, 0x18 per goal
constexpr Word kItemsName = 0x0052ca44;               // "items"
constexpr Word kMedalsName = 0x005348e8;              // "skmedals"
constexpr Word kBadPowerup = 0x005373d8;              // "Bad powerup type"
constexpr Word kNoPowerups = 0x0056a86c;              // nonzero: the level has no power-ups
constexpr Word kBaddyList = 0x0055f6bc;               // BaddyObjectListHead
constexpr Word kGameList = 0x0056af40;                // GameObjectListHead
constexpr Word kPowerUpList = 0x0056b830;
constexpr Word kScriptObjectList = 0x0055f6b0;        // the live list of the opcode 0xab objects; the word at +4 (0x0055f6b4) is the next list identifier
constexpr Word kScriptObjectVtable = 0x0051836c;
constexpr Word kTrafficVtable = 0x005184e0;
constexpr Word kTrafficCount = 0x0055faec;            // the cars made so far (each takes its number: +0x1e0)
constexpr Word kTrafficTimeScale = 0x0055fae8;        // set to 0x12c by every car's constructor
constexpr Word kPlaySound = 0x004ad7d0;               // Audio_PlayPositionalSound(sound, position, 0) -> the voice
constexpr Word kUpdateSound = 0x004ad950;             // Audio_UpdatePositionalSound(voice, 0, 0)
constexpr Word kAnimationCycle = 0x00480890;          // Animation_Cycle (thiscall: 0, 1)
constexpr Word kCarMessage = 0x00524e04;              // "Incorrect car segment link"
const std::string kCarFile = "H:\\TonyHawk\\Pc2\\car.cpp";   // 0x00524e20
constexpr Word kCameraPointNodes = 0x0055fa58;        // CameraPointNodeIndices: 0x46 words of node indexes
constexpr Word kCameraPointCount = 0x0055fae4;
const std::string kCameraPointFile = "H:\\TonyHawk\\Pc2\\campt.cpp";   // 0x00524de8
const std::string kPowerupFile = "H:\\TonyHawk\\Pc2\\powerup.cpp";     // 0x005373ec
constexpr Word kPowerupVelocity = pcplatform::kScratchBase + 0xe260;   // 0x7ff0ea60, the zero vector Powerup_CreateFromTrigger keeps on its stack

constexpr std::int32_t kExtendedRecordType = 500;

}  // namespace

// 0x004c8130 (cdecl): creates the level's objects from the node table. Types 1 and 7 are objects (7 asserts: "TRYING TO SEED"), 5 power-ups, 2 / 6 / 9 link records, 12 / 14 trick objects,
// 13 camera points and 500 extended records; the restart table (type 8) is built in a second pass after Trig_ClearTrigMenu.
void WorldKit::trg_parse_nodes() {
    const auto record_of = [this](Word index) { return word_at(m_, global_word(m_, kRecords) + 4 * index); };
    const auto zero_vector = [this]() {
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, kLocalVector + 4 * k, 0);
        }
    };
    // the dword after a link record's payload words (rounded up to a dword): the key of the link list
    const auto link_key_at = [this](Word record) {
        Word at = record + 2 + 2 * static_cast<Word>(short_at(m_, record + 2)) + 2;
        if ((at & 2) != 0) {
            at += 2;
        }
        return at;
    };

    trig_stamp(0xa3d);
    check(global_word(m_, kTrigFile) != 0, 0x0054802c);
    if (global_byte(m_, kPrepared) == 0 && engine_value(kPrepareVideoMasks, {}) != 0) {
        return;
    }
    engine(kScanRestart, {});
    if (global_word(m_, kStatus) != 0x16) {
        engine(kCountThings, {});
    }
    for (Word index = 0; s(index) < s(global_word(m_, kRecordCount)); ++index) {
        const Word record = record_of(index);
        const std::int32_t type = short_at(m_, record);
        if (type > kExtendedRecordType) {
            continue;
        }
        if (type == kExtendedRecordType) {
            zero_vector();
            const Word angles = engine_value(kDecodeNodePosition, {kLocalVector, index});
            set_global_word(m_, kBitFlag, 0);
            const Word bit = engine_value(kBitNew, {0x58});
            if (bit != 0) {
                thiscall(kInitExtendedRecord, bit, {kLocalVector, index, half_at(m_, angles), half_at(m_, angles + 2), half_at(m_, angles + 4), half_at(m_, angles + 6),
                                                    byte_at(m_, angles + 8), byte_at(m_, angles + 0xa), byte_at(m_, angles + 0xc), byte_at(m_, angles + 0xe), byte_at(m_, angles + 0x10),
                                                    byte_at(m_, angles + 0x12)});
            }
            set_global_word(m_, kBitFlag, 1);
            continue;
        }
        const Word preamble = record + 8 + 2 * static_cast<Word>(short_at(m_, record + 6));
        switch (type) {
        case 1:
            if ((engine_value(kFindPreambleFlag, {1, preamble}) & 0xff) != 0) {
                engine(kCreateObject, {index});
            }
            break;
        case 5:
            zero_vector();
            if (half_at(m_, engine_value(kDecodeNodePosition, {kLocalVector, index})) == 1) {
                engine(kCreateObject, {index});
            }
            break;
        case 7:
            if ((engine_value(kFindPreambleFlag, {1, preamble}) & 0xff) != 0) {
                trig_stamp(0xa62);
            }
            check(false, 0x0054801c);
            break;
        case 2:
        case 9: {
            const Word at = link_key_at(record);
            engine(kCreateLinkRecord, {word_at(m_, at), index, kLinkListStatic});
            break;
        }
        case 6: {
            const Word at = link_key_at(record);
            engine(kCreateLinkRecord, {word_at(m_, at), index, at + 4});
            break;
        }
        case 12:
        case 14: {
            const Word object = engine_value(kClassNew, {0x18});
            if (object != 0) {
                thiscall(kTrickObjectConstruct, object, {index, 0});
            }
            break;
        }
        case 13:
            engine(kRegisterCameraPoint, {index});
            break;
        default:
            break;
        }
    }
    engine(kClearTrigMenu, {});
    for (Word index = 0; s(index) < s(global_word(m_, kRecordCount)); ++index) {
        if (short_at(m_, record_of(index)) != 8) {
            continue;
        }
        zero_vector();
        const Word angles = engine_value(kDecodeNodePosition, {kLocalVector, index});
        trig_stamp(0xacb);
        const Word count = global_word(m_, kRestartCount);
        check(s(count) < 0x28, 0x00548004);
        set_word_at(m_, kRestartTable + 4 * count, angles + 6);
        set_global_word(m_, kRestartCount, count + 1);
    }
}

// 0x004c5460 (cdecl): makes the object of a node: type 1 or 7 (a seeded object: +0x6a bit 2) by its object type (0xcb script object, 0xd5..0xdc traffic, 0x192), type 5 a power-up (built
// by the factory unless the level has none: 0x0056a86c). The preamble flags decide the rest: flag 4 clears bit 1 of +0x6a, flag 2 keeps the object out of its list (power-ups always do);
// otherwise it is suspended into the list it heads (baddy, game object or power-up). Answers the object, or 0.
Word WorldKit::trg_create_object(Word index) {
    trig_stamp(0x29c);
    check(s(index) >= 0 && s(index) < s(global_word(m_, kRecordCount)), 0x005479fc);
    const Word record = word_at(m_, global_word(m_, kRecords) + 4 * index);
    const std::int32_t type = short_at(m_, record);
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, kLocalVector + 4 * k, 0);
    }
    bool seeded = false;
    bool unlisted = false;
    Word clear_bit = 0;
    Word object = 0;
    if (type == 5) {
        const Word power_type = byte_at(m_, record + 2);
        const Word angles = engine_value(kDecodeNodePosition, {kLocalVector, index});
        const Word flag = half_at(m_, angles + 2) == 0 ? 4 : 0;
        const Word word = half_at(m_, angles + 4);
        if (global_word(m_, kNoPowerups) != 0) {
            return 0;
        }
        object = engine_value(kPowerupCreate, {power_type, kLocalVector, flag, word, index});
        const Word links = engine_value(kGetNodeLinks, {index});
        if (half_at(m_, links) != 0) {
            thiscall(kPowerupSetNode, object, {index});
        }
        unlisted = true;
    } else if (type == 1 || type == 7) {
        seeded = type == 7;
        const Word object_type = static_cast<Word>(short_at(m_, record + 2));
        const Word preamble = record + 8 + 2 * static_cast<Word>(short_at(m_, record + 6));
        unlisted = (engine_value(kFindPreambleFlag, {2, preamble}) & 0xff) != 0;
        clear_bit = engine_value(kFindPreambleFlag, {4, preamble}) & 0xff;
        const Word payload_end = (engine_value(kFindPreambleEnd, {preamble}) + 3) & ~3u;
        const bool traffic = object_type >= 0xd5 && object_type <= 0xdc;
        if (object_type != 0xcb && !traffic && object_type != 0x192) {
            trig_stamp(0x2e8);
            check(false, 0x0054797c);
            return 0;
        }
        const Word storage = engine_value(kAllocObjectStorage, {object_type == 0xcb ? 0x1f4u : traffic ? 0x1e8u : 0x218u});
        if (storage == 0) {
            return 0;
        }
        if (object_type == 0xcb) {
            object = thiscall_value(kConstructScriptObject, storage, {payload_end, index});
        } else if (traffic) {
            object = thiscall_value(kConstructTrafficObject, storage, {payload_end, index, object_type});
        } else {
            object = thiscall_value(kConstructSubtype0x0192, storage, {payload_end, index});
        }
    } else {
        trig_stamp(0x30e);
        check(false, 0x005479d4);
        return 0;
    }
    if (object == 0) {
        return 0;
    }
    set_half_at(m_, object + 0xb0, index);
    if (clear_bit != 0) {
        set_half_at(m_, object + 0x6a, half_at(m_, object + 0x6a) & 0xfffd);
    }
    if (seeded) {
        set_byte_at(m_, object + 0x6a, byte_at(m_, object + 0x6a) | 4);
    }
    if (!unlisted) {
        Word list = 0;
        if (object == global_word(m_, kBaddyList)) {
            list = kBaddyList;
        }
        if (object == global_word(m_, kGameList)) {
            list = kGameList;
        }
        if (object == global_word(m_, kPowerUpList)) {
            list = kPowerUpList;
        }
        trig_stamp(0x325);
        check(list != 0, 0x005479a4);
        thiscall(kSuspendToList, object, {list});
    }
    return object;
}

// 0x0049f250 (thiscall on the storage): the type-0x192 object (a platform: the base baddy, the platform vtable, the level's PSX region, the head of the game object list) reads its position and
// parameters from the node's payload (`record`, the address after the preamble) and keeps the address after them (+0x17c). `node` is the object's node index (+0xb0). The callees are services.
Word WorldKit::construct_subtype_0x0192(Word self, Word record, Word node) {
    thiscall(kBaddyConstruct, self);
    subtype_192_at(m_, self).set_constructor_zero_word_1(0);
    subtype_192_at(m_, self).set_field_1fe(0);
    subtype_192_at(m_, self).set_constructor_zero_word_2(0);
    subtype_192_at(m_, self).set_constructor_zero_word_3(0);
    subtype_192_at(m_, self).set_constructor_zero_word_4(0);
    subtype_192_at(m_, self).set_constructor_zero_word_5(0);
    subtype_192_at(m_, self).set_vtable_or_class_dispatch(GuestPtr<void>{static_cast<Word>(kPlatformVtable)});
    subtype_192_at(m_, self).set_runtime_context(GuestPtr<void>{static_cast<Word>(global_word(m_, kOwner))});
    thiscall(kSelectPsxRegion, self, {global_word(m_, kRegionName)});
    thiscall(kListPushHead, self, {kGameList});
    subtype_192_at(m_, self).set_object_flags(static_cast<std::uint16_t>(static_cast<std::uint16_t>((subtype_192_at(m_, self).object_flags() & 0xfffd) | 0x111)));
    subtype_192_at(m_, self).set_object_subtype(0x192);
    const Word after_position = thiscall_value(kReadFixedPosition, self, {record});
    const Word after_parameters = thiscall_value(kReadObjectParameters, self, {after_position});
    subtype_192_at(m_, self).set_script_stream_cursor(GuestPtr<void>{static_cast<Word>(after_parameters)});
    subtype_192_at(m_, self).set_source_trg_node_index(static_cast<std::uint16_t>(node));
    subtype_192_at(m_, self).set_constructor_sentinel(static_cast<std::int32_t>(0xffffffff));
    subtype_192_at(m_, self).set_constructor_mode_word(0x20);
    subtype_192_at(m_, self).set_constructor_active_byte(1);
    return self;
}

// 0x00401060 (thiscall on the 0xcc-byte storage; `parameters`, the three words of the 0xab command, is not read by this PC build): the opcode 0xab script object. The base object, the script
// object vtable, the level's PSX region, the model the key resolves to in that region (+0x1a), the state bit 0x02 and the bits 0x06 of +0x6a cleared, the head of the script object list, and the
// list identifier +0xc8 taken from the counter behind the head (0x0055f6b4, which then advances). The callees are services.
Word WorldKit::construct_script_object(Word self, Word checksum, Word parameters) {
    (void)parameters;
    thiscall(kObjectConstruct, self);
    set_word_at(m_, self, kScriptObjectVtable);
    thiscall(kSelectPsxRegion, self, {global_word(m_, kRegionName)});
    const Word model = engine_value(kFindModelIndex, {checksum, byte_at(m_, self + 0x1f)});
    set_half_at(m_, self + 4, half_at(m_, self + 4) & 0xfffd);
    set_half_at(m_, self + 0x6a, half_at(m_, self + 0x6a) & 0xfff9);
    set_half_at(m_, self + 0x1a, model);
    thiscall(kListPushHead, self, {kScriptObjectList});
    const Word identifier = half_at(m_, kScriptObjectList + 4);
    set_half_at(m_, self + 0xc8, identifier);
    set_half_at(m_, kScriptObjectList + 4, identifier + 1);
    return self;
}

// 0x00412640 (thiscall on the 0x1e8-byte storage): a car of object type 0xd5..0xdc (taxi, police car, bus, cable car, kart, mar, bull, gull). The base baddy, the car vtable, its number from the counter
// (+0x1e0), the type (+0x3c); the type picks the PSX region by name and the sound the car plays at its position (+0x1d0 the voice; the police car has no sound, +0x1d4 is the "has a voice" flag; the
// gull's sound is -1, and a type outside the eight keeps the flag with whatever voice word the storage held). The head of the baddy list, the animation cycle, the size words, the position and
// parameters read from the payload (`record`, the address after the preamble), the node (+0xb0), the node's link record (its first word must be 1, "Incorrect car segment link") and the node it
// names (+0x1dc), whose position is decoded into +0x08 and then lowered by 0x6e000. The callees are services.
Word WorldKit::construct_traffic_object(Word self, Word record, Word node, Word object_type) {
    static const Word kNames[8] = {0x00524e74, 0x00524e68, 0x00524e60, 0x00524e58, 0x00524e50, 0x00524e48, 0x00524e40, 0x00524e38};   // c_taxi, c_police, c_bus, c_cable, c_kart, c_mar, c_bull, c_gull
    static const Word kSounds[8] = {0x71, 0, 0x121, 0x86, 0x111, 0xdd, 0x145, 0xffffffff};
    thiscall(kBaddyConstruct, self);
    set_word_at(m_, self, kTrafficVtable);
    const Word number = global_word(m_, kTrafficCount);
    set_word_at(m_, self + 0x1e0, number);
    set_global_word(m_, kTrafficCount, number + 1);
    set_half_at(m_, self + 0x3c, object_type);
    set_word_at(m_, self + 0x1d4, 1);
    const Word kind = object_type - 0xd5;
    if (kind <= 7) {
        if (kind == 1) {
            set_word_at(m_, self + 0x1d4, 0);
        }
        thiscall(kSelectPsxRegion, self, {kNames[kind]});
        if (kind != 1) {
            set_word_at(m_, self + 0x1d0, engine_value(kPlaySound, {kSounds[kind], self + 8, 0}));
        }
    }
    if (word_at(m_, self + 0x1d4) != 0) {
        engine(kUpdateSound, {word_at(m_, self + 0x1d0), 0, 0});
    }
    set_global_word(m_, kTrafficTimeScale, 0x12c);
    thiscall(kListPushHead, self, {kBaddyList});
    set_word_at(m_, self + 0x108, 0x10000);
    thiscall(kAnimationCycle, self, {0, 1});
    set_half_at(m_, self + 0x1ac, 0x34);
    set_half_at(m_, self + 0x1ae, 0x20);
    set_half_at(m_, self + 0xb4, 0x3c);
    const Word after_position = thiscall_value(kReadFixedPosition, self, {record});
    thiscall_value(kReadObjectParameters, self, {after_position});
    set_half_at(m_, self + 0xb0, node);
    const Word links = engine_value(kGetNodeLinks, {node});
    set_global_word(m_, kAssertionLine, 0xbe);
    set_string(kAssertionFile, kCarFile);
    check(half_at(m_, links) == 1, kCarMessage);
    const Word target = half_at(m_, links + 2);
    set_word_at(m_, self + 0x1dc, target);
    engine_value(kDecodeNodePosition, {self + 8, target});
    set_word_at(m_, self + 0xc, word_at(m_, self + 0xc) + 0xfff92000u);
    return self;
}

// 0x004a8e50 (cdecl): the factory TRG_CreateObject calls for a type-5 node: 0x100 bytes of object storage, then Powerup_Construct with a zero velocity (the three words are the factory's stack
// locals; the Kit keeps them in the scratch window). The answer is the power-up, zero when the storage could not be allocated.
Word WorldKit::powerup_create_from_trigger(Word type, Word position, Word flags, Word word, Word node) {
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, kPowerupVelocity + 4 * k, 0);
    }
    const Word storage = engine_value(kAllocObjectStorage, {0x100});
    if (storage == 0) {
        return 0;
    }
    return thiscall_value(kPowerupConstruct, storage, {type, position, kPowerupVelocity, flags, word, node});
}

// 0x004a7c50 (thiscall on the storage): a collectable power-up of `type` at the position vector `position` (copied to +0x08 and the home position +0xdc) with the velocity vector `velocity`
// (+0x4c; flag 8 sets a fixed upward speed instead). `flags` bit 2 snaps the home height to the ground, bit 4 sets the settled bytes; the model comes from the "items" PSX region (the medals from
// "skmedals"; type 0x21 is the level goal's item), a type that none of them names asserts "Bad powerup type". The callees are services.
Word WorldKit::powerup_construct(Word self, Word type, Word position, Word velocity, Word flags, Word word, Word node) {
    thiscall(kObjectConstruct, self);
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + 0xdc + 4 * k, 0);
    }
    powerup_at(m_, self).set_source_trg_node_index(static_cast<std::uint16_t>(static_cast<std::uint16_t>(node)));
    powerup_at(m_, self).set_item_subtype(static_cast<std::uint16_t>(static_cast<std::uint16_t>(type)));
    powerup_at(m_, self).set_field_f0(static_cast<std::uint16_t>(static_cast<std::uint16_t>(word)));
    powerup_at(m_, self).set_vtable(GuestPtr<void>{static_cast<Word>(kPowerupVtable)});
    powerup_at(m_, self).set_field_fc(0);
    powerup_at(m_, self).set_field_d4(0);
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + 0xdc + 4 * k, word_at(m_, position + 4 * k));
    }
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + 0x08 + 4 * k, word_at(m_, position + 4 * k));
    }
    if ((flags & 8) != 0) {
        powerup_at(m_, self).set_field_50(0xfffc4000);
    } else {
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, self + 0x4c + 4 * k, word_at(m_, velocity + 4 * k));
        }
    }
    powerup_at(m_, self).set_field_6c(5);
    powerup_at(m_, self).set_field_6d(5);
    powerup_at(m_, self).set_field_6e(5);
    powerup_at(m_, self).set_field_72(0x64);
    powerup_at(m_, self).set_field_5c(0x8000);
    const Word random = engine_value(kRnd, {0x14});
    powerup_at(m_, self).set_field_ee(static_cast<std::uint16_t>(static_cast<std::uint16_t>(random + 0x5a)));
    powerup_at(m_, self).set_field_ea(0x32);
    powerup_at(m_, self).set_field_ec(0x32);
    if ((flags & 0x10) != 0) {
        powerup_at(m_, self).set_motion_state(1);
        powerup_at(m_, self).set_field_d3(1);
    } else if ((flags & 4) != 0) {
        powerup_at(m_, self).set_motion_state(1);
        powerup_at(m_, self).set_field_d8(static_cast<Word>(engine_value(kGroundHeight, {self + 0xdc, 0, 8000})));
    }
    powerup_at(m_, self).set_field_ae(0x12c);
    thiscall(kWantSprite, self);
    powerup_at(m_, self).set_field_ac(0x12c);
    thiscall(kSelectPsxRegion, self, {kItemsName});

    const auto find_model = [this, self](Word checksum, Word region) {
        powerup_at(m_, self).set_psx_model_index(static_cast<std::uint16_t>(static_cast<std::uint16_t>(engine_value(kFindModelIndex, {checksum, byte_at(m_, region)}))));
    };
    const auto items_model = [this, self, &find_model](Word checksum) {
        powerup_at(m_, self).set_visual_active(1);
        find_model(checksum, kItemsRegion);
    };
    // the power-ups that pay out (the money bags) are hidden once the node's money was collected: the word is 1
    const auto money_model = [this, self, &find_model](Word checksum) {
        powerup_at(m_, self).set_field_72(200);
        powerup_at(m_, self).set_visual_active(1);
        find_model(checksum, kItemsRegion);
        if (engine_value(kGotMoney, {powerup_at(m_, self).source_trg_node_index()}) != 0) {
            powerup_at(m_, self).set_field_f0(1);
        }
    };
    const auto medal_model = [this, self, &find_model](Word checksum) {
        thiscall(kSelectPsxRegion, self, {kMedalsName});
        powerup_at(m_, self).set_visual_active(1);
        find_model(checksum, kMedalsRegion);
    };
    const Word kind = powerup_at(m_, self).item_subtype();
    switch (kind) {
    case 4: items_model(0x2328a71c); break;
    case 5: items_model(0x311d55d4); break;
    case 6: items_model(0x2ebf22ca); break;
    case 10: items_model(0x29b68a16); break;
    case 0xf: items_model(0x34524351); break;
    case 0x10:
    case 0x12:
        powerup_at(m_, self).set_field_72(0x96);
        powerup_at(m_, self).set_visual_active(1);
        find_model(0x7c1b2c4a, kItemsRegion);
        powerup_at(m_, self).set_field_ae(500);
        break;
    case 0x18: money_model(0x694ed947); break;
    case 0x19: money_model(0x260f4f80); break;
    case 0x1a: money_model(0xcc4e141f); break;
    case 0x21: {
        powerup_at(m_, self).set_visual_active(1);
        const Word goal = engine_value(kGoalIndex, {3});
        const Word entry = word_at(m_, kGoalTable + goal * 0x18 + global_word(m_, kCurrentLevel) * 0x1ac);
        if (entry == 0) {
            find_model(0x2328a71c, kItemsRegion);
            powerup_at(m_, self).set_field_72(400);
        } else {
            find_model(entry, kGoalRegion);
            powerup_at(m_, self).set_psx_region_slot(static_cast<std::uint8_t>(global_byte(m_, kGoalRegion)));
        }
        break;
    }
    case 0x664: medal_model(0x54636518); break;
    case 0x665: medal_model(0xba6d0434); break;
    case 0x666: medal_model(0x2364558e); break;
    default:
        set_global_word(m_, kAssertionLine, 0x149);
        set_string(kAssertionFile, kPowerupFile);
        check(false, kBadPowerup);
        break;
    }

    // the power-ups a game of the competition modes starts collected (word +0xf0 = 1)
    const Word index = kind - 4;
    bool collected = false;
    if (index <= 0x1d) {
        if (global_word(m_, kGameMode) != 1) {
            collected = kind == 4 || kind == 5 || kind == 6 || kind == 10 || kind == 0xf || kind == 0x10 || (kind >= 0x18 && kind <= 0x1a) || kind == 0x21;
        } else if (kind == 4 || kind == 5 || kind == 6 || kind == 10 || kind == 0xf) {
            collected = (global_byte(m_, kPowerupMask) & 2) != 0;
        } else if (kind == 0x10) {
            collected = (global_byte(m_, kPowerupMask) & 1) != 0;
        } else if (kind == 0x21) {
            collected = (global_byte(m_, kPowerupMask) & 4) != 0;
        }
    }
    if (collected) {
        powerup_at(m_, self).set_field_f0(1);
    }
    thiscall(kListPushHead, self, {kPowerUpList});
    if (powerup_at(m_, self).visual_active() == 0) {
        thiscall(kPowerupHide, self);
    }
    if (s(global_word(m_, kCurrentLevel)) >= 10 && global_word(m_, kCurrentLevel) != 0xd) {
        thiscall(kPowerupHide, self);
        set_byte_at(m_, self + 0x6a, byte_at(m_, self + 0x6a) | 0x40);
    }
    return self;
}

// 0x00411f30 (cdecl): records a camera point node (type 13) in the level's list of at most 0x45. The decode of its position is made and its result dropped (the call is kept: it asserts on a
// node that has no position record).
void WorldKit::trg_register_camera_point(Word index) {
    set_global_word(m_, kAssertionLine, 0x60);
    set_string(kAssertionFile, kCameraPointFile);
    check(s(global_word(m_, kCameraPointCount)) < 0x46, 0x00524dd0);
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, kLocalVector + 4 * k, 0);
    }
    engine_value(kDecodeNodePosition, {kLocalVector, index});
    const Word count = global_word(m_, kCameraPointCount);
    set_half_at(m_, kCameraPointNodes + 2 * count, index);
    set_global_word(m_, kCameraPointCount, count + 1);
}

}  // namespace opentony::runtime::worldkit
