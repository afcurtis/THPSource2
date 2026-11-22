#include "world_views.hpp"

#include <map>

// Milestone 10c: the trigger command interpreter of `trig.cpp` on the retail memory (TRG_InterpretCommandStream 0x004c5dc0) and the helpers under it that had only the value-owned model of
// src/trg/trg_runtime.cpp (the pulse, the command lists, the signal / suspend / visible / kill senders, the payload skipper). Ported from the disassembly (build/m10/trg_interp.dis), not the
// decompile: the dispatch tables at 0x004c7718 / 0x004c75fc and 0x004c7dc0 / 0x004c7e60 / 0x004c7e80 give each opcode its handler. Every call out of the interpreter is a service by address
// (the replay of re/evidence/fixtures/trg-interpreter-*.txt stubs each one from the retail trace); the helpers call their leaves directly. Unlike most Kits these write the assertion location
// (the line at 0x00568638 and the file name at 0x0056881c) before each assertion, as the retail macro does: the trace records those words.

namespace opentony::runtime::worldkit {

using namespace views;

namespace {

using Word = std::uint32_t;

// the interpreter's callees
constexpr Word kScriptObjectClearFlag = 0x00401000;   // ScriptObject_ClearFlag(id)
constexpr Word kScriptObjectSetFlag = 0x00401030;     // ScriptObject_SetFlag(id)
constexpr Word kCreateScriptObject = 0x00401060;      // TRG_CreateScriptObject (thiscall on the storage: checksum, the parameter block)
constexpr Word kCameraOverrideA = 0x0040bd00;         // the camera object's methods (thiscall on CameraList)
constexpr Word kCameraOverrideB = 0x0040bd10;
constexpr Word kCameraWindowWidth = 0x0040bd20;
constexpr Word kCameraWindowHeight = 0x0040bd30;
constexpr Word kCameraShake = 0x0040be60;
constexpr Word kCameraHookF140 = 0x0040f140;
constexpr Word kCameraHookF150 = 0x0040f150;
constexpr Word kCameraHookF160 = 0x0040f160;
constexpr Word kCameraHookF170 = 0x0040f170;
constexpr Word kCameraHookF180 = 0x0040f180;
constexpr Word kCameraHookF190 = 0x0040f190;
constexpr Word kCameraBlendTarget = 0x0040f1a0;
constexpr Word kCameraResetAndSettle = 0x00410c40;    // (thiscall on the skater's camera, +0x29b0)
constexpr Word kGapComplete = 0x00414c50;             // Gap_CompleteDefinition(definition)
constexpr Word kTrickConfigFind = 0x00416050;         // TrickConfig_FindByCharacter(character) -> the record
constexpr Word kUpdateSky = 0x0042fc70;               // Db_UpdateSky
constexpr Word kSetFogRange = 0x00464710;             // M3D_SetFogRange(three words of the last 0x68 command)
constexpr Word kLevelEvent = 0x00466c10;              // LevelEvent_InitializeAndDispatch
constexpr Word kResetOrientationRows = 0x0046c670;    // Skater_ResetOrientationRows (thiscall)
constexpr Word kMessDeleteAll = 0x00473650;           // Mess_DeleteAll
constexpr Word kAllocObjectStorage = 0x0047fd30;      // TRG_AllocObjectStorage(size)
constexpr Word kAnimationStart = 0x00480730;          // Animation_Start (thiscall, four arguments)
constexpr Word kOutOfBounds = 0x0048e9c0;             // CPanelData__OutOfBounds (thiscall on the skater's panel, +0x168)
constexpr Word kTrickTakeOff = 0x0048f5f0;            // Trick_TakeOff
constexpr Word kGapApply = 0x0048f690;                // Gap_ApplyDefinitionState(definition + 8, -score, 0, slot definition)
constexpr Word kTrickLand = 0x0048f8f0;               // Trick_Land
constexpr Word kTrickBail = 0x0048fb20;               // CBruce__Trick_Bail (thiscall)
constexpr Word kResetTrickBuffers = 0x004904c0;       // CBruce__ResetTrickBuffers (thiscall)
constexpr Word kClearTrickFlags = 0x00496300;         // Skater_ClearTrickFlags (thiscall)
constexpr Word kAudioNothing = 0x004ad5e0;            // Audio_Nothing(name)
constexpr Word kSfxPlay = 0x004ad620;                 // SFX_PlayX(sound, volume, pitch)
constexpr Word kSfxStopVoice = 0x004ad9f0;            // SFX_StopVoice(sound)
constexpr Word kSfxSetReverb = 0x004ada20;            // SFX_SetReverb(kind)
constexpr Word kSfxNothing1 = 0x004adc30;
constexpr Word kSfxNothing2 = 0x004adc50;
constexpr Word kSfxNothing3 = 0x004adc60;
constexpr Word kFindLoadedRegion = 0x004b3230;        // PSX_FindLoadedRegionByName(name) -> al
constexpr Word kClearNamedRegion = 0x004b3270;        // PSX_ClearNamedRegion(name)
constexpr Word kQueueResource = 0x004b37a0;           // PSX_QueueResource(name, kind, 0, 1)
constexpr Word kDrainSpooler = 0x004b3df0;            // PSX_DrainSpooler
constexpr Word kTagSwapRoles = 0x004b8500;            // Tag_SwapRoles
constexpr Word kFindRestartByName = 0x004c4c50;       // Trig_FindRestartByName(name)
constexpr Word kFudgeAngles = 0x004c4d10;             // FudgeAngles(object)
constexpr Word kFindCommandPointLink = 0x004c5ac0;    // TRG_FindCommandPointLink(index) -> the command point
constexpr Word kExecuteCommandList = 0x004c5b00;      // TRG_ExecuteCommandList(links)
constexpr Word kSendSignalToNodes = 0x004c5b60;       // TRG_SendSignalToNodes(links)
constexpr Word kActivateOrSuspend = 0x004c5c70;       // TRG_ApplyActivationOrSuspendList(links, kind)
constexpr Word kSkipAlignedString = 0x004c5d70;       // TRG_SkipAlignedCString(string) -> the address after it
constexpr Word kAllocateDualBuffer = 0x004c5d90;      // Trig_AllocateDualBuffer(bytes)
constexpr Word kRemoveDualBuffer = 0x004c5da0;        // Trig_RemoveDualBuffer
constexpr Word kStreamInterpreter = 0x004c5dc0;       // TRG_InterpretCommandStream(stream, index, mode)
constexpr Word kSendVisible = 0x004c77f0;             // TRG_SendVisible(index, visible)
constexpr Word kSendKill = 0x004c7a00;                // TRG_SendKillFromNode(index, mode)
constexpr Word kSkipPayload = 0x004c7c50;             // TRG_SkipCommandPayload(command) -> the next command
constexpr Word kGetNodeLinks = 0x004c8550;            // TRG_GetNodeLinks(index) -> the links
constexpr Word kDecodeNodePosition = 0x004c8650;      // TRG_DecodeNodePosition(out, index) -> the angles
constexpr Word kRnd = 0x004c9340;                     // Rnd(limit)
constexpr Word kApplyBounds = 0x004ca270;             // RuntimeObject_ApplyBoundsToLists(low, high)
constexpr Word kUpdateVisibility = 0x004ca2d0;        // RuntimeObject_UpdateVisibilityBounds(low, high, mode, flag)
constexpr Word kVideoPlaying = 0x004cde70;            // Video_Playing -> al
constexpr Word kIsRemotePlayer = 0x004de010;          // NetGame_IsRemotePlayer(skater) -> al
// the helpers' callees
constexpr Word kTrickObjectTake = 0x004bdbd0;         // TrickObject_Take(index)
constexpr Word kCreateObject = 0x004c5460;            // TRG_CreateObject(index)
constexpr Word kRailActivate = 0x004aa420;            // Rail_ActivateNode(index)
constexpr Word kRailClear = 0x004aa410;               // Rail_ClearState(index)
constexpr Word kBitNew = 0x00403d30;                  // CBit_New(size)
constexpr Word kBitDie = 0x00403e40;                  // CBit_Die (thiscall)
constexpr Word kInitExtendedRecord = 0x0045c390;      // TRG_InitializeExtendedRecord (thiscall, twelve arguments)
constexpr Word kFindEnviroItem = 0x004b1ef0;          // Spool_FindEnviroItem(checksum) -> the item
constexpr Word kHitEnvItem = 0x00448860;              // Exp_HitEnvItem(item, 0, 0xffff)
constexpr Word kPlatformBounce = 0x0049f4c0;          // CPlatform__Bounce (thiscall: skater, skater + 0x4c)
constexpr Word kSuspendToList = 0x004802f0;           // Object_SuspendToList (thiscall: the list)
constexpr Word kUnsuspend = 0x004803b0;               // CBody__UnSuspend (thiscall)

// the globals
constexpr Word kGameMode = 0x00533f38;                // LevelEventGameModeSelector
constexpr Word kNumberOfPlayers = 0x00533f3c;
constexpr Word kSkyWord = 0x0056114c;
constexpr Word kWaterColour = 0x00535af8;
constexpr Word kRestartIndex = 0x005477ec;            // RestartNode
constexpr Word kCameraList = 0x0055fa38;
constexpr Word kTrackBufferCount = 0x0055ff94;        // numTrackSpoolingBuffers
constexpr Word kTrackBuffers = 0x0055ff98;            // 15 bytes per name
constexpr Word kTrackSpoolFlag = 0x00560080;          // byte
constexpr Word kKillSoundFlag = 0x005614c4;           // byte
constexpr Word kFogColour = 0x00563a60;
constexpr Word kOtPushback = 0x00564368;              // word M3d_OTPushback
constexpr Word kOtPushback2 = 0x0056436a;             // word M3d_OTPushback2
constexpr Word kMusicWord = 0x005685ec;
constexpr Word kHorseIndex = 0x0056867c;
constexpr Word kCareerFlagsWhilePlaying = 0x0056a838; // byte: the flags IfCareerFlag tests while Video_Playing answers true
constexpr Word kPlayer = 0x0056a858;
constexpr Word kCurrentLevel = 0x0056a898;
constexpr Word kOwner = 0x0056a954;                   // the skater the commands act on
constexpr Word kPlayerRecord = 0x0056a960;            // the first skater
constexpr Word kTagSkater = 0x0056db60;
constexpr Word kRegionIndex = 0x0056dcb4;             // byte
constexpr Word kGapTable = 0x0056dd58;                // ActiveGapDefinitionTable: 0x2c bytes per gap, ended by -1 at +4
constexpr Word kCheatNames = 0x0056e164;
constexpr Word kCheatCount = 0x0056e1b4;
constexpr Word kHomeNames = 0x0056e1b8;
constexpr Word kHomeCount = 0x0056e208;
constexpr Word kRecords = 0x0056e210;
constexpr Word kRecordCount = 0x0056e214;
constexpr Word kRegionName = 0x0056e218;
constexpr Word kBaddyList = 0x0055f6bc;               // BaddyObjectListHead
constexpr Word kGameList = 0x0056af40;                // GameObjectListHead
constexpr Word kPowerUpList = 0x0056b830;
constexpr Word kBitList = 0x0055f704;
constexpr Word kBitFlag = 0x005244cc;
constexpr Word kKillTarget = 0x0056af28;

// the skater's fields the interpreter writes
constexpr Word kGapBlocked = 0x2f64;
constexpr Word kGapSlots = 0x2f74;                    // 20 command point checksums
constexpr Word kGapSlotsEnd = 0x2fc4;                 // then 20 gap definition indexes
constexpr Word kGapSlotCount = 20;

// the stack locals of the retail functions, at fixed scratch addresses (the recorded calls pass pointers to them)
constexpr Word kLocals = pcplatform::kScratchBase + 0xe200;    // 0x7ff0ea00
constexpr Word kLocalLow = kLocals;                             // a box's first corner (three dwords)
constexpr Word kLocalHigh = kLocals + 0x10;                     // its second corner
constexpr Word kLocalParameters = kLocals + 0x20;               // the script object's three words
constexpr Word kLocalPosition = kLocals + 0x30;                 // TRG_DecodeNodePosition's out vector (0x98)
constexpr Word kLocalPulsePosition = kLocals + 0x40;            // ... and TRG_SendPulse's

}  // namespace

// 0x004c58b0 (cdecl): what a pulse does to a node, by the type of its record
void WorldKit::trg_send_pulse(Word index) {
    trig_stamp(0x332);
    check(s(index) >= 0 && s(index) < s(global_word(m_, kRecordCount)), 0x00547a64);
    const std::int32_t type = short_at(m_, word_at(m_, global_word(m_, kRecords) + 4 * index));
    if (type == 12 || type == 14) {
        engine(kTrickObjectTake, {index});
    } else if (type == 10 || type == 11) {
        engine(kRailActivate, {index});
    } else if (type == 0x1f5) {
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, kLocalPulsePosition + 4 * k, 0);
        }
        const Word angles = engine_value(kDecodeNodePosition, {kLocalPulsePosition, index});
        set_global_word(m_, kBitFlag, 0);
        const Word bit = engine_value(kBitNew, {0x58});
        if (bit != 0) {
            thiscall(kInitExtendedRecord, bit, {kLocalPulsePosition, index, half_at(m_, angles), half_at(m_, angles + 2), half_at(m_, angles + 4), half_at(m_, angles + 6),
                                                byte_at(m_, angles + 8), byte_at(m_, angles + 0xa), byte_at(m_, angles + 0xc), byte_at(m_, angles + 0xe), byte_at(m_, angles + 0x10),
                                                byte_at(m_, angles + 0x12)});
        }
        set_global_word(m_, kBitFlag, 1);
    } else if (type == 1 || type == 5 || type == 7) {
        engine(kCreateObject, {index});
    } else if (type == 6) {
        const Word point = engine_value(kFindCommandPointLink, {index});
        trig_stamp(0x354);
        check(point != 0, 0x00547a20);
        const Word stream = word_at(m_, point);
        set_byte_at(m_, point + 7, byte_at(m_, point + 7) + 1);
        engine(kStreamInterpreter, {stream, index, 0});
    }
}

// 0x004c5b00 (cdecl): a pulse to every node of a link list (a count word, then the node indexes)
void WorldKit::trg_execute_command_list(Word links) {
    const Word count = half_at(m_, links);
    for (Word k = 1; k <= count; ++k) {
        trg_send_pulse(half_at(m_, links + 2 * k));
    }
}

// 0x004c5b30 (cdecl): the signal bit (+0x68 bit 0) of every object of a list made from this node
void WorldKit::send_signal_to_node(Word list, Word index) {
    for (Word object = list; object != 0; object = word_at(m_, object + object::kNext)) {
        if (half_at(m_, object + 0xb0) == index) {
            set_half_at(m_, object + 0x68, half_at(m_, object + 0x68) | 1);
        }
    }
}

// 0x004c5b60 (cdecl): the signal to the objects of every linked object node (types 1 and 7), baddies and game objects
void WorldKit::trg_send_signal_to_nodes(Word links) {
    const Word count = half_at(m_, links);
    trig_stamp(0x4f8);
    check(count != 0, 0x00547a8c);
    for (Word k = 1; k <= count; ++k) {
        const Word index = half_at(m_, links + 2 * k);
        const std::int32_t type = short_at(m_, word_at(m_, global_word(m_, kRecords) + 4 * index));
        if (type == 1 || type == 7) {
            send_signal_to_node(global_word(m_, kBaddyList), index);
            send_signal_to_node(global_word(m_, kGameList), index);
        }
    }
}

// 0x004c5c00 (cdecl, the address of a list head): suspends the objects of the list made from this node (the next object is read after the call, as retail does: a suspended object
// already heads the suspended list)
void WorldKit::trg_suspend_object_by_node(Word list, Word index) {
    for (Word object = word_at(m_, list); object != 0; object = word_at(m_, object + object::kNext)) {
        if (half_at(m_, object + 0xb0) == index) {
            thiscall(kSuspendToList, object, {list});
        }
    }
}

// 0x004c5c40 (cdecl): wakes the suspended objects made from this node
void WorldKit::send_unsuspend(Word index) {
    for (Word object = global_word(m_, kSuspendedList); object != 0; object = word_at(m_, object + object::kNext)) {
        if (half_at(m_, object + 0xb0) == index) {
            thiscall(kUnsuspend, object);
        }
    }
}

// 0x004c5c70 (cdecl): command 4 (activate) or 5 (suspend) to every linked object node
void WorldKit::trg_apply_activation_or_suspend_list(Word links, Word kind) {
    if (kind != 4 && kind != 5) {
        trig_stamp(0x537);
        check(false, 0x00547b00);
    }
    const Word count = half_at(m_, links);
    trig_stamp(0x540);
    check(count != 0, 0x00547ac0);
    for (Word k = 1; k <= count; ++k) {
        const Word index = half_at(m_, links + 2 * k);
        const std::int32_t type = short_at(m_, word_at(m_, global_word(m_, kRecords) + 4 * index));
        if (type != 1 && type != 7) {
            continue;
        }
        if (kind == 5) {
            trg_suspend_object_by_node(kBaddyList, index);
            trg_suspend_object_by_node(kGameList, index);
        } else {
            send_unsuspend(index);
        }
    }
}

// the checksum after a record's payload words (rounded up to a dword), the key of its enviro item
Word WorldKit::trig_enviro_checksum(Word record) {
    Word at = record + 4 + 2 * static_cast<Word>(short_at(m_, record + 2));
    if ((at & 2) != 0) {
        at += 2;
    }
    return word_at(m_, at);
}

// 0x004c77f0 (cdecl): shows or hides the crates, enviro items and power-ups linked to a node. The hidden bits are 0x41 in a one-player game, else 0x01 for the first skater and 0x40 for the
// second. The record type is read as a byte (so the 0x1f5 branch of the source is dead: such a record reaches the "non crate" assertion).
void WorldKit::trg_send_visible(Word index, Word visible) {
    const Word owner = global_word(m_, kOwner);
    Word bits = 0x41;
    if (owner != 0 && global_word(m_, kNumberOfPlayers) != 1) {
        bits = owner != global_word(m_, kPlayerRecord) ? 0x01 : 0x40;
    }
    Word keep = ~bits;
    Word set = 0;
    if (visible == 0) {
        keep = 0xffffffff;
        set = bits;
    }
    trig_stamp(0x3d2);
    check(s(index) >= 0 && s(index) < s(global_word(m_, kRecordCount)), 0x00547f1c);
    const Word links = trg_get_node_links(index);
    const Word count = half_at(m_, links);
    for (Word k = 1; k <= count; ++k) {
        const Word node = half_at(m_, links + 2 * k);
        const Word record = word_at(m_, global_word(m_, kRecords) + 4 * node);
        const Word type = byte_at(m_, record);
        if (type == 2 || type == 9 || type == 0xc || type == 0xe) {
            const Word item = engine_value(kFindEnviroItem, {trig_enviro_checksum(record)});
            if (item != 0) {
                set_half_at(m_, item + 4, (half_at(m_, item + 4) & keep) | set);
            }
        } else if (type == 5) {
            for (Word powerup = global_word(m_, kPowerUpList); powerup != 0; powerup = word_at(m_, powerup + object::kNext)) {
                if (half_at(m_, powerup + 0xb0) == node) {
                    set_half_at(m_, powerup + 4, (half_at(m_, powerup + 4) & keep) | set);
                    break;
                }
            }
        } else {
            trig_stamp(0x43c);
            check(false, 0x00547f00);
        }
    }
}

// 0x004c7a00 (cdecl): kills what a node links to. Mode 1 is the explosion (enviro items hit, objects through slot 6), mode 0 the plain kill (items hidden, objects through slot 2); a
// platform object that is armed (+4 bit 0x100, +0x178 bit 0x10) bounces the skater and runs its own command list instead.
void WorldKit::trg_send_kill_from_node(Word index, Word mode) {
    trig_stamp(0x449);
    check(s(index) >= 0 && s(index) < s(global_word(m_, kRecordCount)), 0x00547f3c);
    const Word links = trg_get_node_links(index);
    const Word count = half_at(m_, links);
    for (Word k = 1; k <= count; ++k) {
        const Word node = half_at(m_, links + 2 * k);
        const Word record = word_at(m_, global_word(m_, kRecords) + 4 * node);
        const std::int32_t type = short_at(m_, record);
        if (type == 2 || type == 9) {
            const Word item = engine_value(kFindEnviroItem, {trig_enviro_checksum(record)});
            if (item != 0) {
                if (mode == 1) {
                    engine(kHitEnvItem, {item, 0, 0xffff});
                } else {
                    set_byte_at(m_, item + 4, byte_at(m_, item + 4) | 0x41);
                }
            }
        } else if (type == 1) {
            bool bounced = false;
            for (Word object = global_word(m_, kGameList); object != 0; object = word_at(m_, object + object::kNext)) {
                if (half_at(m_, object + 0xb0) != node || (half_at(m_, object + 4) & 0x100) == 0) {
                    continue;
                }
                const Word armed = word_at(m_, object + 0x178);
                if ((armed & 0x10) == 0) {
                    continue;
                }
                set_half_at(m_, object + 4, half_at(m_, object + 4) | 0x20);
                if ((armed & 0x20) != 0) {
                    continue;
                }
                trg_execute_command_list(trg_get_node_links(half_at(m_, object + 0xb0)));
                set_byte_at(m_, object + 0x171, 1);
                const Word skater = global_word(m_, kOwner);
                thiscall(kPlatformBounce, object, {skater, skater + 0x4c});
                bounced = true;
            }
            if (!bounced) {
                kill_in_list(node, global_word(m_, kBaddyList), mode);
                kill_in_list(node, global_word(m_, kGameList), mode);
            }
        } else if (type == 10 || type == 11) {
            engine(kRailClear, {node});
        } else if (type == 500 || type == 501) {
            for (Word bit = global_word(m_, kBitList); bit != 0; bit = word_at(m_, bit + 8)) {
                if (byte_at(m_, bit + 0x3b) == 2 && half_at(m_, bit + 0xe) == node) {
                    thiscall(kBitDie, bit);
                    break;
                }
            }
        }
    }
}

// 0x004c7bf0 (cdecl): the kill (slot 2) or explosion (slot 6) of every object of a list made from this node
void WorldKit::kill_in_list(Word index, Word list, Word mode) {
    for (Word object = list; object != 0; object = word_at(m_, object + object::kNext)) {
        if (half_at(m_, object + 0xb0) != index) {
            continue;
        }
        if (mode == 0) {
            virtual_call(object, 2);
        } else if (mode == 1) {
            virtual_call(object, 6, {static_cast<Word>(short_at(m_, object + 0xb4)), kKillTarget, 0});
        }
    }
}

// 0x004c7c50 (cdecl): the address of the command after this one (0 at the 0xffff terminator). An opcode the table does not know asserts and skips the opcode word only, as do the ones
// the table never learnt (0x96..0x98, 0x9d..0x9f, 0xa1, 0xa2: inside a false IfPulseCount / IfCareerFlag their operands are read as commands).
Word WorldKit::trg_skip_command_payload(Word command) {
    const Word opcode = half_at(m_, command);
    const Word at = command + 2;
    const auto aligned = [at](Word extra) { return ((at + 3) & ~3u) + extra; };
    switch (opcode) {
    case 0x03: case 0x04: case 0x05: case 0x0a: case 0x0b: case 0x0c: case 0x66: case 0x67: case 0x79: case 0x7a: case 0x81: case 0x88: case 0x89:
    case 0x95: case 0xad: case 0xaf:
        return at;
    case 0x0d: case 0x69: case 0x6a: case 0x83: case 0x84: case 0x86: case 0x8a: case 0x93: case 0x94: case 0x99: case 0x9a: case 0x9b: case 0x9c:
    case 0xa0: case 0xa3: case 0xa4: case 0xa5: case 0xa6: case 0xa8: case 0xa9: case 0xaa: case 0xac: case 0xb1: case 0xcb: case 0xcc: case 0xcd:
        return at + 2;
    case 0x82: case 0x87: case 0x8b: case 0x8f: case 0x90: case 0x91: case 0x92: case 0xa7: case 0xae: case 0xc8: case 0xca:
        return at + 4;
    case 0x68:
        return at + 6;
    case 0x73: case 0x77: case 0x7e: case 0x7f: case 0x80: case 0x8c: case 0x8e: case 0xb0: case 0xb2:
        return trg_skip_aligned_cstring(at);
    case 0x02: {
        Word name = at;
        while (byte_at(m_, name) != 0) {
            name = trg_skip_aligned_cstring(name);
        }
        return name + 2;
    }
    case 0x8d:
    case 0x85: {
        Word box = opcode == 0x8d ? at + 4 : at;
        while (half_at(m_, box) != 0xff) {
            box = ((box + 3) & ~3u) + 0x18;
        }
        return box + 2;
    }
    case 0xab:
        return aligned(0xa);
    case 0xc9:
        return aligned(6);
    case 0xffff:
        return 0;
    default:
        trig_stamp(0x5d3);
        check(false, 0x00547f60);
        return at;
    }
}

// 0x004c5dc0 (cdecl): runs a command stream (words, ended by 0xffff) for the node `index` (0xffff for none). `mode` != 0 lets the PSX resource commands wait for the spooler. The
// command point of the node (TRG_FindCommandPointLink) holds the pulse counters the pulse / IfPulseCount / SetInitialPulses commands use.
void WorldKit::trg_interpret_command_stream(Word stream, Word index, Word mode) {
    bool fog = false;
    Word fog_first = 0;
    Word fog_second = 0;
    Word fog_third = 0;
    std::int32_t depth = 0;    // the open IfPulseCount / IfCareerFlag / IfGoal blocks
    const Word point = engine_value(kFindCommandPointLink, {index});
    Word at = stream;
    const auto skip_string = [this](Word string) { return engine_value(kSkipAlignedString, {string}); };
    const auto camera = [this]() { return global_word(m_, kCameraList); };
    // a false condition: everything up to the Endif (0x95) is skipped, the Endif included
    const auto skip_block = [this, &at](bool nest_check) {
        while (half_at(m_, at) != 0x95) {
            if (nest_check) {
                trig_stamp(0x863);
                check(half_at(m_, at) != 0x94, 0x00547d10);
            }
            at = engine_value(kSkipPayload, {at});
        }
        at += 2;
    };
    while (half_at(m_, at) != 0xffff) {
        const Word opcode = half_at(m_, at);
        at += 2;
        switch (opcode) {
        case 0x02: {    // SetCheatRestarts: the names, ended by an empty one
            set_global_word(m_, kCheatCount, 0);
            while (byte_at(m_, at) != 0) {
                trig_stamp(0x610);
                check(s(global_word(m_, kCheatCount)) < 0x14, 0x00547ed8);
                const Word count = global_word(m_, kCheatCount);
                set_word_at(m_, kCheatNames + 4 * count, at);
                set_global_word(m_, kCheatCount, count + 1);
                at = skip_string(at);
            }
            at += 2;
            break;
        }
        case 0x03: {    // SendPulse: to the linked nodes, as often as the command point's count allows (-1 for always)
            trig_stamp(0x87b);
            check(index != 0xffff, 0x00547c7c);
            if (point == 0) {
                engine(kExecuteCommandList, {engine_value(kGetNodeLinks, {index})});
            } else if (half_at(m_, point + 8) != 0) {
                engine(kExecuteCommandList, {engine_value(kGetNodeLinks, {index})});
                const Word count = half_at(m_, point + 8);
                if (count != 0xffff) {
                    set_half_at(m_, point + 8, count - 1);
                }
            }
            break;
        }
        case 0x04:
        case 0x05:      // SendActivate / SendSuspend
            trig_stamp(0x897);
            check(index != 0xffff, 0x00547c28);
            engine(kActivateOrSuspend, {engine_value(kGetNodeLinks, {index}), opcode});
            break;
        case 0x0a:      // SendSignal
            trig_stamp(0x89d);
            check(index != 0xffff, 0x00547bdc);
            engine(kSendSignalToNodes, {engine_value(kGetNodeLinks, {index})});
            break;
        case 0x0b:
            engine(kSendKill, {index, 0});
            break;
        case 0x0c:
            engine(kSendKill, {index, 1});
            break;
        case 0x0d:
            engine(kSendVisible, {index, half_at(m_, at)});
            at += 2;
            break;
        case 0x66: case 0x67: case 0x88: case 0x89: case 0xaf:
            break;
        case 0x68:      // fog: applied once the stream has run (the last one wins)
            fog = true;
            fog_first = half_at(m_, at);
            fog_second = half_at(m_, at + 2);
            fog_third = half_at(m_, at + 4);
            at += 6;
            break;
        case 0x69:      // the sound and the stop: not for a remote player, whose operand word is then read as the next command (as retail does)
            if ((engine_value(kIsRemotePlayer, {global_word(m_, kOwner)}) & 0xff) == 0) {
                engine(kSfxPlay, {static_cast<Word>(short_at(m_, at)), 0x2000, 0});
                at += 2;
            }
            break;
        case 0x6a:
            if ((engine_value(kIsRemotePlayer, {global_word(m_, kOwner)}) & 0xff) == 0) {
                engine(kSfxStopVoice, {static_cast<Word>(short_at(m_, at))});
                at += 2;
            }
            break;
        case 0x73:
            trig_stamp(0x8b3);
            check(false, 0x00547bc0);
            break;
        case 0x77:
            at = skip_string(at);
            break;
        case 0x79:
            trig_stamp(0x8bf);
            check(false, 0x00547b98);
            break;
        case 0x7a:
            trig_stamp(0x851);
            check(false, 0x00547d60);
            break;
        case 0x7e:
        case 0x80:      // load a PSX resource (0x80: the second kind); in mode != 0 wait for it
            engine(kQueueResource, {at, opcode == 0x80 ? 1u : 0u, 0, 1});
            if (mode != 0) {
                engine(kDrainSpooler);
            }
            at = skip_string(at);
            break;
        case 0x7f:
            engine(kClearNamedRegion, {at});
            at = skip_string(at);
            break;
        case 0x81:
            engine(kDrainSpooler);
            break;
        case 0x82: case 0x87: case 0x8f: case 0x90: case 0x91: case 0x92: {    // the camera hooks (two words)
            static const std::map<Word, Word> hooks{{0x82, kCameraHookF140}, {0x87, kCameraHookF150}, {0x8f, kCameraHookF160},
                                                    {0x90, kCameraHookF170}, {0x91, kCameraHookF180}, {0x92, kCameraHookF190}};
            if (camera() != 0) {
                thiscall(hooks.at(opcode), camera(), {half_at(m_, at), half_at(m_, at + 2)});
            }
            at += 4;
            break;
        }
        case 0x83:
            engine(kScriptObjectClearFlag, {half_at(m_, at)});
            at += 2;
            break;
        case 0x84:
            engine(kScriptObjectSetFlag, {half_at(m_, at)});
            at += 2;
            break;
        case 0x85:      // bounds lists: boxes of six dwords (Q0, shifted to Q12), ended by 0xff
            for (Word k = 0; k < 3; ++k) {
                set_word_at(m_, kLocalLow + 4 * k, 0);
                set_word_at(m_, kLocalHigh + 4 * k, 0);
            }
            while (half_at(m_, at) != 0xff) {
                const Word box = (at + 3) & ~3u;
                for (Word k = 0; k < 3; ++k) {
                    set_word_at(m_, kLocalLow + 4 * k, word_at(m_, box + 4 * k) << 12);
                    set_word_at(m_, kLocalHigh + 4 * k, word_at(m_, box + 12 + 4 * k) << 12);
                }
                engine(kApplyBounds, {kLocalLow, kLocalHigh});
                at = box + 0x18;
            }
            at += 2;
            break;
        case 0x86: {    // SetInitialPulses: the command point's count, once
            trig_stamp(0x871);
            check(point != 0, 0x00547cc8);
            if (byte_at(m_, point + 6) == 0) {
                set_byte_at(m_, point + 6, 1);
                set_half_at(m_, point + 8, half_at(m_, at));
            }
            at += 2;
            break;
        }
        case 0x8a:
            trig_stamp(0x949);
            check(false, 0x00547b80);
            at += 2;
            break;
        case 0x8b:
            trig_stamp(0x951);
            check(false, 0x00547b68);
            at += 4;
            break;
        case 0xae:
            at += 4;
            break;
        case 0x8c:
        case 0xb0: {    // the restart point by name (the horse game takes its home node); a new restart clears the messages (not for 0xb0)
            const Word before = global_word(m_, kRestartIndex);
            if (global_word(m_, kGameMode) == 7) {
                trig_stamp(0x62a);
                check(global_word(m_, kHorseIndex) == 0, 0x0052c644);
                trig_stamp(0x62b);
                check(global_word(m_, kHomeCount) != 0, 0x0052c624);
                engine(kFindRestartByName, {word_at(m_, kHomeNames + 4 * global_word(m_, kHorseIndex))});
            } else {
                engine(kFindRestartByName, {at});
            }
            at = skip_string(at);
            if (before != 0xffff && global_word(m_, kRestartIndex) != before && opcode != 0xb0) {
                engine(kMessDeleteAll);
            }
            break;
        }
        case 0xb2:      // the second player's restart point (two players only)
            if (global_word(m_, kNumberOfPlayers) == 2) {
                if (global_word(m_, kGameMode) == 7) {
                    const Word horse = global_word(m_, kHorseIndex);
                    trig_stamp(0x64b);
                    check(horse == 0, 0x0052c644);
                    trig_stamp(0x64c);
                    check(global_word(m_, kHomeCount) != 0, 0x00547ea8);
                    engine(kFindRestartByName, {word_at(m_, kHomeNames + 4 * global_word(m_, kHorseIndex))});
                } else {
                    engine(kFindRestartByName, {at});
                }
            }
            at = skip_string(at);
            break;
        case 0x8d: {    // visibility bounds: a mode word and a flag word, then boxes as 0x85 (mode 2 and up for no skater)
            const Word kind = half_at(m_, at);
            const Word flag = half_at(m_, at + 2);
            at += 4;
            for (Word k = 0; k < 3; ++k) {
                set_word_at(m_, kLocalLow + 4 * k, 0);
                set_word_at(m_, kLocalHigh + 4 * k, 0);
            }
            while (half_at(m_, at) != 0xff) {
                const Word box = (at + 3) & ~3u;
                for (Word k = 0; k < 3; ++k) {
                    set_word_at(m_, kLocalLow + 4 * k, word_at(m_, box + 4 * k) << 12);
                    set_word_at(m_, kLocalHigh + 4 * k, word_at(m_, box + 12 + 4 * k) << 12);
                }
                at = box + 0x18;
                if (kind > 1) {
                    const Word owner = global_word(m_, kOwner);
                    set_global_word(m_, kOwner, 0);
                    engine(kUpdateVisibility, {kLocalLow, kLocalHigh, kind != 2 ? 1u : 0u, flag != 0 ? 1u : 0u});
                    set_global_word(m_, kOwner, owner);
                } else {
                    engine(kUpdateVisibility, {kLocalLow, kLocalHigh, kind != 0 ? 1u : 0u, flag != 0 ? 1u : 0u});
                }
            }
            at += 2;
            break;
        }
        case 0x8e: {    // select a PSX region by name; with track spooling on, remember the name too
            set_global_word(m_, kRegionName, at);
            at = skip_string(at);
            set_global_byte(m_, kRegionIndex, engine_value(kFindLoadedRegion, {global_word(m_, kRegionName)}));
            const Word name = global_word(m_, kRegionName);
            if (global_byte(m_, kTrackSpoolFlag) != 0) {
                trig_stamp(0x149, true);
                check(s(global_word(m_, kTrackBufferCount)) < 0xf, 0x00547e48);
                const Word count = global_word(m_, kTrackBufferCount);
                std::vector<std::uint8_t> text;
                for (Word k = 0;; ++k) {
                    text.push_back(byte_at(m_, name + k));
                    if (text.back() == 0) {
                        break;
                    }
                }
                m_.write(kTrackBuffers + 15 * count, text);
                set_global_word(m_, kTrackBufferCount, global_word(m_, kTrackBufferCount) + 1);
            }
            break;
        }
        case 0x93:
            set_global_word(m_, kMusicWord, half_at(m_, at));
            at += 2;
            break;
        case 0x94: {    // IfPulseCount: the command point's count of pulses received
            trig_stamp(0x857);
            check(point != 0, 0x00547d2c);
            const Word received = byte_at(m_, point + 7);
            const Word wanted = half_at(m_, at);
            at += 2;
            if (received == wanted) {
                ++depth;
            } else {
                skip_block(true);
            }
            break;
        }
        case 0x95:      // Endif
            trig_stamp(0x86b);
            check(depth != 0, 0x00547cfc);
            --depth;
            break;
        case 0x96:
            engine(kSfxNothing3, {half_at(m_, at)});
            at += 2;
            break;
        case 0x97:      // the dual buffer, in thousands of bytes
            engine(kRemoveDualBuffer);
            engine(kAllocateDualBuffer, {half_at(m_, at) * 1000u});
            at += 2;
            break;
        case 0x98: {    // KILLBRUCE: the skater back to the restart node it links to
            if (global_byte(m_, kKillSoundFlag) != 0 && (engine_value(kIsRemotePlayer, {global_word(m_, kOwner)}) & 0xff) == 0) {
                engine(kSfxPlay, {0x17b, 0x2000, 0});
                static constexpr std::array<Word, 3> kScreams{0xb9, 0xba, 0xbb};
                const Word pick = engine_value(kRnd, {3});
                if (pick >= kScreams.size()) {
                    throw std::runtime_error("KILLBRUCE: Rnd(3) out of range (retail reads its own frame)");
                }
                engine(kSfxPlay, {kScreams[pick], 0x2000, 0});
            }
            for (Word k = 0; k < 3; ++k) {
                set_word_at(m_, kLocalPosition + 4 * k, 0);
            }
            const Word links = engine_value(kGetNodeLinks, {index});
            const Word count = half_at(m_, links);
            trig_stamp(0x805);
            check(count != 0, 0x00547dcc);
            trig_stamp(0x806);
            check(count == 1, 0x00547da4);
            const Word target = half_at(m_, links + 2);
            trig_stamp(0x808);
            check(half_at(m_, word_at(m_, global_word(m_, kRecords) + 4 * target)) == 8, 0x00547d80);
            const Word angles = engine_value(kDecodeNodePosition, {kLocalPosition, target});
            const Word skater = global_word(m_, kOwner);
            set_word_at(m_, skater + 8, global_word(m_, kLocalPosition));
            set_word_at(m_, skater + 0xc, global_word(m_, kLocalPosition + 4));
            set_word_at(m_, skater + 0x4c, 0);
            set_word_at(m_, skater + 0x50, 0xffffc000);
            set_word_at(m_, skater + 0x10, global_word(m_, kLocalPosition + 8));
            set_word_at(m_, skater + 0x54, 0);
            set_word_at(m_, skater + 0x58, 0);
            set_word_at(m_, skater + 0x5c, 0);
            set_word_at(m_, skater + 0x60, 0);
            set_word_at(m_, skater + 0xc, word_at(m_, skater + 0xc) + 0xfffe7000);
            thiscall(kTrickBail, skater);
            set_word_at(m_, skater + 0x2c68, 0);
            set_word_at(m_, skater + 0x2de0, 0);
            set_word_at(m_, skater + 0x2de8, 0);
            thiscall(kResetTrickBuffers, skater);
            thiscall(kClearTrickFlags, skater);
            set_word_at(m_, skater + 0x2e94, 0);
            thiscall(kResetOrientationRows, skater);
            set_word_at(m_, skater + 0x14, word_at(m_, angles));
            set_half_at(m_, skater + 0x18, half_at(m_, angles + 4));
            engine(kFudgeAngles, {skater});
            set_word_at(m_, skater + kGapBlocked, 0);
            for (Word k = 0; k < 3; ++k) {
                set_word_at(m_, skater + 0xbc + 4 * k, word_at(m_, skater + 8 + 4 * k));
            }
            for (Word k = 0; k < 3; ++k) {
                set_word_at(m_, skater + 0x2e0c + 4 * k, word_at(m_, skater + 8 + 4 * k));
            }
            thiscall(kCameraResetAndSettle, word_at(m_, skater + 0x29b0));
            set_word_at(m_, skater + 0x2ddc, 0);
            set_word_at(m_, skater + 0x30c0, 0);
            set_word_at(m_, skater + 0x30b8, 0);
            thiscall(kAnimationStart, skater, {0, 0, 0xffffffff, 0xffffffff});
            set_word_at(m_, skater + 0x29c8, 0);
            set_word_at(m_, skater + 0x29e0, 0);
            set_word_at(m_, skater + 0x2c78, 0);
            set_word_at(m_, skater + 0x2c7c, 0);
            for (Word k = 0; k < 0x20; ++k) {
                set_byte_at(m_, skater + 0x2a14 + 8 * k, 0);
            }
            if (global_word(m_, kGameMode) == 9 && skater == global_word(m_, kTagSkater)) {
                engine(kTagSwapRoles);
            } else if ((engine_value(kIsRemotePlayer, {global_word(m_, kOwner)}) & 0xff) == 0 && (engine_value(kVideoPlaying) & 0xff) == 0) {
                thiscall(kOutOfBounds, skater + 0x168);
            }
            if (global_word(m_, kGameMode) == 7) {
                set_word_at(m_, skater + 0x2cc8, 0);
            }
            break;
        }
        case 0x99:
        case 0x9a:      // the camera overrides (a signed word)
            if (camera() != 0) {
                thiscall(opcode == 0x99 ? kCameraOverrideA : kCameraOverrideB, camera(), {static_cast<Word>(short_at(m_, at))});
            }
            at += 2;
            break;
        case 0x9b:
            engine(kSfxNothing2, {half_at(m_, at)});
            at += 2;
            break;
        case 0x9c:
            engine(kSfxNothing1, {half_at(m_, at)});
            at += 2;
            break;
        case 0x9d:
            engine(kSfxSetReverb, {byte_at(m_, at)});
            at += 2;
            break;
        case 0x9e:
            engine(kLevelEvent);
            break;
        case 0x9f:
            engine(kAudioNothing, {at});
            at = skip_string(at);
            break;
        case 0xa0:
            if (camera() != 0) {
                set_word_at(m_, camera() + 0x504, half_at(m_, at));
            }
            at += 2;
            break;
        case 0xa1:
            if (camera() != 0) {
                thiscall(kCameraShake, camera(), {half_at(m_, at)});
            }
            at += 2;
            break;
        case 0xa2:
            trig_stamp(0x65d);
            check(false, 0x00547e88);
            at = skip_string(at);
            break;
        case 0xa3:
        case 0xb1:      // the first skater's words; with no skater the operand is read as the next command (as retail does)
            if (global_word(m_, kPlayerRecord) != 0) {
                set_word_at(m_, global_word(m_, kPlayerRecord) + (opcode == 0xa3 ? 0x3198 : 0x319c), half_at(m_, at));
                at += 2;
            }
            break;
        case 0xa4:
        case 0xa5:
            if (camera() != 0) {
                thiscall(opcode == 0xa4 ? kCameraWindowWidth : kCameraWindowHeight, camera(), {half_at(m_, at)});
            }
            at += 2;
            break;
        case 0xa6:
            set_global_half(m_, kOtPushback, half_at(m_, at));
            at += 2;
            break;
        case 0xa7:
            if (camera() != 0) {
                thiscall(kCameraBlendTarget, camera(), {half_at(m_, at), half_at(m_, at + 2)});
            }
            at += 4;
            break;
        case 0xa8:
        case 0xac:
            if (camera() != 0) {
                set_half_at(m_, camera() + (opcode == 0xa8 ? 0x434 : 0x436), half_at(m_, at));
            }
            at += 2;
            break;
        case 0xa9:
            set_global_half(m_, kOtPushback2, half_at(m_, at));
            at += 2;
            break;
        case 0xaa:
            set_global_word(m_, kWaterColour, half_at(m_, at));
            at += 2;
            break;
        case 0xab: {    // a script object: the model checksum (dword aligned) and three words
            const Word block = (at + 3) & ~3u;
            const Word checksum = word_at(m_, block);
            set_global_half(m_, kLocalParameters, half_at(m_, block + 4));
            set_global_half(m_, kLocalParameters + 2, half_at(m_, block + 6));
            set_global_half(m_, kLocalParameters + 4, half_at(m_, block + 8));
            at = block + 10;
            const Word storage = engine_value(kAllocObjectStorage, {0xcc});
            if (storage != 0) {
                thiscall(kCreateScriptObject, storage, {checksum, kLocalParameters});
            }
            break;
        }
        case 0xad:
            if (camera() != 0) {
                set_word_at(m_, camera() + 0x3dc, word_at(m_, camera() + 0x3a4));
            }
            break;
        case 0xc8:
            set_global_word(m_, kFogColour, (half_at(m_, at) << 16) + half_at(m_, at + 2));
            at += 4;
            break;
        case 0xc9: {    // a gap: the skater's open gaps (slots of command point checksums) the command point closes complete; a gap id with a definition opens a slot
            const Word block = (at + 3) & ~3u;
            const Word gap = word_at(m_, block);
            const Word gap_word = half_at(m_, block + 4);
            at = block + 6;
            if (word_at(m_, global_word(m_, kOwner) + kGapBlocked) != 0 || (engine_value(kVideoPlaying) & 0xff) != 0) {
                break;
            }
            trig_stamp(0x738);
            check(word_at(m_, point + 0xc) != gap, 0x00547e28);
            for (Word slot = kGapSlots; slot < kGapSlotsEnd; slot += 4) {
                const Word open = word_at(m_, slot + global_word(m_, kOwner));
                if (open == 0 || open != word_at(m_, point + 0xc)) {
                    continue;
                }
                set_word_at(m_, slot + global_word(m_, kOwner), 0);
                const Word number = word_at(m_, slot + global_word(m_, kOwner) + 0x50);
                const Word definition = global_word(m_, kGapTable) + 0x2c * number;
                if (half_at(m_, definition + 4) == 0xffff) {
                    continue;
                }
                trig_stamp(0x74a);
                check(half_at(m_, definition + 4) != 0xfffe, 0x00547e08);
                const Word flags = half_at(m_, definition);
                if ((flags & 0x8) != 0) {
                    set_word_at(m_, global_word(m_, kOwner) + 0x3014, definition);
                    set_word_at(m_, global_word(m_, kOwner) + 0x301c, index);
                    continue;
                }
                if ((flags & 0x40) != 0) {
                    set_word_at(m_, global_word(m_, kOwner) + 0x3018, definition);
                    set_word_at(m_, global_word(m_, kOwner) + 0x3020, index);
                    continue;
                }
                Word skater = global_word(m_, kOwner);
                if (word_at(m_, skater + 0x30b8) == 0 && word_at(m_, skater + 0x2dd4) == 0) {
                    thiscall(kTrickTakeOff, skater);
                    skater = global_word(m_, kOwner);
                }
                thiscall(kGapApply, skater, {definition + 8, static_cast<Word>(-static_cast<std::int32_t>(short_at(m_, definition + 6))), 0, word_at(m_, slot + skater + 0x50)});   // (thiscall on the owner skater: ecx at 0x004c66c0)
                set_word_at(m_, global_word(m_, kOwner) + 0x2c70, 1);
                skater = global_word(m_, kOwner);
                if (word_at(m_, skater + 0x30b8) == 0 && word_at(m_, skater + 0x2dd4) == 0) {
                    thiscall(kTrickLand, skater);
                }
                engine(kGapComplete, {definition});
                engine(kSfxPlay, {0xa1, 0x2000, 0});
                engine(kExecuteCommandList, {engine_value(kGetNodeLinks, {index})});
            }
            if (gap == 0) {
                break;
            }
            Word number = 0;
            Word definition = global_word(m_, kGapTable);
            while (half_at(m_, definition + 4) != 0xffff && static_cast<Word>(short_at(m_, definition + 4)) != gap_word) {
                definition += 0x2c;
                ++number;
            }
            if (half_at(m_, definition + 4) == 0xffff) {
                break;
            }
            Word free = 0;
            while (free < kGapSlotCount && word_at(m_, global_word(m_, kOwner) + kGapSlots + 4 * free) != 0) {
                ++free;
            }
            if (free == kGapSlotCount) {
                for (Word slot = kGapSlots; slot < kGapSlotsEnd; slot += 4) {
                    set_word_at(m_, slot + global_word(m_, kOwner), 0);
                }
                free = 0;
            }
            set_word_at(m_, global_word(m_, kOwner) + kGapSlots + 4 * free, gap);
            set_word_at(m_, global_word(m_, kOwner) + kGapSlotsEnd + 4 * free, number);
            trig_stamp(0x7a1);
            check(free < kGapSlotCount, 0x00547df0);
            break;
        }
        case 0xca:
            set_global_word(m_, kSkyWord, (half_at(m_, at) << 16) + half_at(m_, at + 2));
            at += 4;
            engine(kUpdateSky);
            break;
        case 0xcb: {    // SetCareerFlag: a bit of the level's career byte in the player's trick config
            const Word flag = half_at(m_, at);
            at += 2;
            trig_stamp(0x967);
            check(flag <= 7, 0x0052434c);
            if ((engine_value(kVideoPlaying) & 0xff) == 0) {
                const Word config = engine_value(kTrickConfigFind, {word_at(m_, global_word(m_, kPlayer) + 0x2cc0)});
                const Word flags = global_word(m_, kCurrentLevel) + config + 0x26;
                set_byte_at(m_, flags, byte_at(m_, flags) | ((1u << (flag & 31)) & 0xff));
            }
            break;
        }
        case 0xcc: {    // IfCareerFlag (while a video plays, the flag byte at 0x0056a838 instead)
            trig_stamp(0x97c);
            check(point != 0, 0x00547b34);
            const Word flag = half_at(m_, at);
            at += 2;
            trig_stamp(0x97e);
            check(flag <= 7, 0x0052434c);
            std::int32_t flags = 0;
            if ((engine_value(kVideoPlaying) & 0xff) != 0) {
                flags = global_sbyte(m_, kCareerFlagsWhilePlaying);
            } else {
                const Word config = engine_value(kTrickConfigFind, {word_at(m_, global_word(m_, kPlayer) + 0x2cc0)});
                flags = sbyte_at(m_, global_word(m_, kCurrentLevel) + config + 0x26);
            }
            if ((static_cast<Word>(flags) & (1u << (flag & 31))) != 0) {
                ++depth;
            } else {
                skip_block(false);
            }
            break;
        }
        case 0xcd: {    // IfGoal: a bit of the level's goal word in the player's trick config
            trig_stamp(0x9a2);
            check(point != 0, 0x00547b34);
            const Word goal = half_at(m_, at);
            at += 2;
            trig_stamp(0x9a4);
            check(goal <= 10, 0x00524338);
            const Word config = engine_value(kTrickConfigFind, {word_at(m_, global_word(m_, kPlayer) + 0x2cc0)});
            const std::int32_t goals = short_at(m_, config + 2 * global_word(m_, kCurrentLevel) + 0xc);
            if ((static_cast<Word>(goals) & (1u << (goal & 31))) != 0) {
                ++depth;
            } else {
                skip_block(false);
            }
            break;
        }
        default:
            trig_stamp(0x9bd);
            check(false, 0x00547b20);
            break;
        }
    }
    if (fog) {
        engine(kSetFogRange, {fog_first, fog_second, fog_third});
    }
    trig_stamp(0x9c3);
    check(depth == 0, 0x00547b10);
}

}  // namespace opentony::runtime::worldkit
