// Replays the retail TRG_ParseNodes traces (re/evidence/fixtures/trg-parse-nodes-*.txt; scripts/make_trg_interpreter_fixture.py documents the format, build/m10e/parse_trace.py is the probe)
// through the retail-memory form of the node walk (WorldKit::trg_parse_nodes, world_trig_nodes.cpp), word for word. Same method as world_trig_commands_trace_test.cpp (Milestone 10c): each
// call's entry state is laid out in a RetailMemory at the retail addresses (the R regions: the level globals, the node table and the nodes' headers and link words, and the bytes behind the
// pointers TRG_DecodeNodePosition returned); every callee is a stub that checks the Kit's call against retail's next one (address, arguments, the out vector's bytes behind
// TRG_InitializeExtendedRecord's first argument), answers with retail's EAX and applies what the callee wrote (k lines; the v line is the out vector TRG_DecodeNodePosition filled); before
// every call and at the end the laid-out memory must equal retail's at that point (the entry state with the walk's own writes, the assertion line and file words included, and the callees'
// writes applied in order). Retail's passing M3dUtils_InBetween calls are not Kit calls (a Kit reports a failed check only), so they are skipped.
//
//   opentony_world_trig_nodes_trace_test FIXTURE.txt [--create | --lists | --tricks | --enviro | --construct | --scriptobject | --traffic | --powerup | --obai | --animstart] [--mutate]
//
// --animstart: the fixture traces Animation_Start 0x00480730 (build/m10e/anim_start_trace.py; WorldKit::animation_start): the call record carries the object (stream) and the four stack
// arguments (the S line); the object's animation fields, the region's table cell and the animation table are the entry state, the assertion is the only callee.
//
// --obai: the fixture traces Ob_AI 0x00480ff0 (build/m10e/ob_ai_trace.py; WorldKit::object_ai_frame): the call record carries the list head's address (mode); the globals, the player
// positions and the fields of every object of the list the loop reads are the entry state, the callees (the flash, the update hooks, the sprite update, the suspension and the virtual
// delete of a dead object) are stubs that apply their k lines.
//
// --powerup: the fixture traces Powerup_CreateFromTrigger 0x004a8e50 and Powerup_Construct 0x004a7c50 (build/m10e/powerup_trace.py; WorldKit::powerup_create_from_trigger / powerup_construct): the call
// record carries the function (index), the storage (stream, Powerup_Construct) and the pushed arguments as dwords (the S line); the storage, the position and velocity vectors, the level globals,
// the career goal row and the head of the power-up list are the entry state, the callees (the base object, Rnd, the ground height, the sprite request, the PSX region and model lookups, the
// career queries, the list push, the hide method, the storage allocation, and for the factory Powerup_Construct itself) are stubs that apply their k lines, and the Kit's answer must be retail's EAX.
//
// --construct: the fixture traces TRG_ConstructSubtype0x0192 0x0049f250 (build/m10e/construct_trace.py; WorldKit::construct_subtype_0x0192): the call record carries the storage (stream), the payload
// end (index) and the node (mode); the storage (all 0x218 bytes), the level globals and the head of the game object list are the entry state, the five callees (Baddy_Construct, the PSX region,
// ObjectList_PushHead, the two payload readers) are stubs that apply their k lines, and the Kit's answer must be retail's EAX.
//
// --traffic: the fixture traces TRG_ConstructTrafficObject 0x00412640 (build/m10e/traffic_trace.py; WorldKit::construct_traffic_object): the call record carries the storage (stream), the payload end
// (index), the node (mode) and the three pushed arguments as dwords (the S line); the storage (0x1e8 bytes), the car counter, the assertion words and the head of the baddy list are the entry state,
// the callees (the base baddy, the PSX region, the two sound calls, the list push, the animation cycle, the two payload readers, the link record, the position decode) are stubs that apply their
// k lines, and the Kit's answer must be retail's EAX.
//
// --trafficupdate: the fixture traces the traffic car's per-frame update 0x00412960 (build/m12/traffic_update_trace.py; WorldKit::update_traffic_object, which runs TRG_CheckTrafficProximity 0x004136e0
// inline): the call record carries the car (stream); the car (0x1e8 bytes), the voice time scale, the clock, the players and the assertion words are the entry state, the callees (the sound and
// replay calls, the car's collision with the skaters, the node queries, the fixed-point products, the angle and the horn's helpers) are stubs that apply their k lines. The vector subtraction, the
// normalisation and the angle difference are the Kit's own arithmetic and are not in the trace.
//
// --scriptobject: the fixture traces TRG_CreateScriptObject 0x00401060 (build/m10e/script_object_trace.py; WorldKit::construct_script_object): the call record carries the storage (stream), the key
// (index) and the parameter block (mode); the storage (0xcc bytes), the region name, the head of the script object list with its identifier counter and the object behind the head are the entry
// state, the four callees (Object_Construct, the PSX region, the model lookup, ObjectList_PushHead) are stubs that apply their k lines, and the Kit's answer must be retail's EAX.
//
// --enviro: the fixture traces Spool_FindEnviroItem 0x004b1ef0 (build/m10e/enviro_trace.py; WorldKit::spool_find_enviro_item): the key is the call record's mode, the list, its objects and the region
// tables are the entry state, and the Kit's answer must be retail's EAX.
//
// --tricks: the fixture traces TrickTable_BuildForPlayer 0x004bcf00 (build/m10e/trick_table_trace.py; CreateObjKit::trick_table_build): the call record carries the manager (stream), the player
// (mode) and the table argument (the S line, four bytes); the skater manager's queries are the stubs (the 0x35-byte block and the 0x104-byte career record behind their results are laid out
// where the stub returned them), the loaded TRICKS.BIN, the section pointers and the table are in the entry state, and the table the Kit writes must equal retail's.
//
// --lists: the fixture traces the object list functions (build/m10e/list_trace.py; ObjectList_PushHead / _Remove, Object_SuspendToList, CBody__UnSuspend, each top-level call with the object, its
// neighbours, the list heads and the object's vtable laid out); the Kit's function runs on the laid-out memory, only the assertion and the virtual slot are stubs.
//
// --create: the fixture traces TRG_CreateObject 0x004c5460 (WorldKit::trg_create_object) instead: the constructors, the power-up factory and Object_SuspendToList are stubs, the
// constructed object's bytes (the r lines) are laid out where the stub returned it, and the Kit's result must be retail's EAX.
//
// --mutate changes one byte the walk wrote in the first call that writes, after the run, and must make the replay fail (the comparison has teeth).
//
// Exit codes: 0 all match, 1 mismatch, 2 usage/format error.
#include "createobj_kit.hpp"
#include "world_kit.hpp"

#include "trg_interpreter_trace_fixture.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace opentony::runtime;
using namespace opentony::trg::trg_trace_fixture;

namespace {

constexpr std::uint32_t kAssert = 0x004011e0;
constexpr std::uint32_t kDecode = 0x004c8650;
constexpr std::uint32_t kRegisterCameraPoint = 0x00411f30;
constexpr std::uint32_t kSuspendVirtualCall = 0x00480384;   // the return address of Object_SuspendToList's virtual call
constexpr std::uint32_t kObAiVirtualCall = 0x00481074;      // the return address of Ob_AI's virtual call (slot 0, argument 1: the delete of a dead object)
constexpr std::uint32_t kLocals = 0x7ff0ea00;           // the Kit's stand-ins for the walk's stack locals (world_trig_nodes.cpp)
constexpr std::uint32_t kLocalsBytes = 0x100;

int failures = 0;
std::uint32_t current_call = 0;

void fail(const std::string& message) {
    ++failures;
    if (failures <= 40) {
        std::cerr << "call " << current_call << ": " << message << "\n";
    }
}

std::string hex(std::uint32_t value) {
    std::ostringstream os;
    os << "0x" << std::hex << value;
    return os.str();
}

// how a callee's arguments are compared (see world_trig_commands_trace_test.cpp)
struct Signature {
    bool thiscall = false;
    std::vector<std::uint32_t> masks;
    std::vector<std::size_t> pointees;
};

const std::map<std::uint32_t, Signature>& signatures() {
    constexpr std::uint32_t all = 0xffffffff;
    constexpr std::uint32_t byte = 0xff;   // pushed from an 8-bit register: the upper bits are whatever the register held
    static const std::map<std::uint32_t, Signature> table{
        {0x00403d30, {false, {all}, {}}},                                                    // CBit_New(size)
        {0x00411f30, {false, {all}, {}}},                                                    // TRG_RegisterCameraPoint(index)
        {0x00415380, {false, {}, {}}},                                                       // Career_CountThings
        {0x0045c390, {true, {0, all, all, all, all, all, byte, byte, byte, byte, byte, byte}, {0}}},   // TRG_InitializeExtendedRecord(vector, index, 4 words, 6 bytes)
        {0x00466b70, {false, {all}, {}}},                                                    // CClass_OpNew(size)
        {0x00469900, {false, {}, {}}},                                                       // Game_PrepareVideoMasksWrapper
        {0x004bd760, {true, {all, all}, {}}},                                                // TrickObject_Construct(node, 0)
        {0x004c4b10, {false, {}, {}}},                                                       // Trig_ClearTrigMenu
        {0x004c5420, {false, {all, all}, {}}},                                               // TRG_FindObjectPreambleFlag(1, preamble)
        {0x004c5460, {false, {all}, {}}},                                                    // TRG_CreateObject(index)
        {0x004c7ea0, {false, {}, {}}},                                                       // Trig_ScanRestart
        {0x004c84d0, {false, {all, all, all}, {}}},                                          // TRG_CreateLinkRecord(key, index, stream)
        {0x004c8650, {false, {0, all}, {}}},                                                 // TRG_DecodeNodePosition(out vector, index)
        // TRG_CreateObject's callees
        {0x00401270, {true, {}, {}}},                                                        // Baddy_Construct(storage)
        {0x0047fe30, {true, {all}, {}}},                                                     // Object_SelectNamedPsxRegion(object; the region name)
        {0x004801d0, {true, {all}, {}}},                                                     // ObjectList_PushHead(object; the list head)
        {0x00480240, {true, {all}, {}}},                                                     // TRG_ReadFixedPosition(object; the record)
        {0x004802c0, {true, {all}, {}}},                                                     // TRG_ReadObjectParameters(object; the record)
        {0x00403000, {true, {all, all}, {}}},                                                // TRG_ConstructSubtype0x00cb(storage; payload end, node)
        {0x00412640, {true, {all, all, all}, {}}},                                           // TRG_ConstructTrafficObject(storage; payload end, node, object type)
        {0x0047fd30, {false, {all}, {}}},                                                    // TRG_AllocObjectStorage(size)
        {0x004802f0, {true, {all}, {}}},                                                     // Object_SuspendToList(object; the list head)
        {0x0049f250, {true, {all, all}, {}}},                                                // TRG_ConstructSubtype0x0192(storage; payload end, node)
        {0x004ad7d0, {false, {all, all, all}, {}}},                                          // Audio_PlayPositionalSound(sound, position, 0)
        {0x004ad950, {false, {all, all, all}, {}}},                                          // Audio_UpdatePositionalSound(voice, 0, 0)
        {0x00480890, {true, {all, all}, {}}},                                                // Animation_Cycle(object; 0, 1)
        {0x004a82c0, {true, {all}, {}}},                                                     // CPowerUp__SetNode(power-up; node)
        {0x004a8e50, {false, {all, 0, all, all, all}, {1}}},                                 // Powerup_CreateFromTrigger(type, position, flag, word, node)
        {0x004c5440, {false, {all}, {}}},                                                    // TRG_FindObjectPreambleEnd(preamble)
        {0x004c8550, {false, {all}, {}}},                                                    // TRG_GetNodeLinks(index)
        // Powerup_Construct's callees (and the factory's)
        {0x00480060, {true, {}, {}}},                                                        // Object_Construct(object)
        {0x004c9340, {false, {all}, {}}},                                                    // Rnd(limit)
        {0x004ca140, {false, {all, all, all}, {}}},                                          // Utils_GetGroundHeight(position, 0, 8000)
        {0x00480420, {true, {}, {}}},                                                        // Object_WantSprite(object)
        {0x004b1de0, {false, {all, all}, {}}},                                               // PSX_FindModelIndexByChecksum(checksum, region)
        {0x00414540, {false, {all}, {}}},                                                    // Career_GoalIndex(goal)
        {0x00415060, {false, {all}, {}}},                                                    // Career_GotMoney(node)
        {0x004a8250, {true, {}, {}}},                                                        // maybe_destroy_model(power-up)
        {0x004a7c50, {true, {all, 0, 0, all, all, all}, {1, 2}}},                            // Powerup_Construct(storage; type, position, velocity, flags, word, node)
        // the traffic car update's callees (and its horn's)
        {0x004ad8b0, {false, {all, all, all}, {}}},                                          // SFX_ModifyPos(voice, position, 0)
        {0x004cde70, {false, {}, {}}},                                                       // Video_Playing
        {0x00461bf0, {false, {all}, {}}},                                                    // M3d_BuildTransform(car)
        {0x004cdef0, {false, {all, all, all, all}, {}}},                                     // Replay_LoadCarPosition(players, number, position, angles)
        {0x004cde80, {false, {all, all, all, all, all, all}, {}}},                           // Replay_StoreCarPosition(players, number, position, angles dword; the angles word is the c line's edx)
        {0x00412ef0, {false, {all}, {}}},                                                    // Traffic_Check(car)
        {0x004f5f90, {false, {0, all}, {}}},                                                 // Fixed_DotProductQ12(direction (stack local), velocity)
        {0x004f5fc0, {false, {all, all}, {}}},                                               // Fixed_MulQ12(a, b)
        {0x004e8560, {false, {all, all}, {}}},                                               // orig_ratan2(y, x)
        {0x004f5f50, {false, {all, all, all, all}, {}}},                                     // Fixed_CrossSignQ12(a, b, c, d)
        {0x004c9620, {false, {all, all}, {}}},                                               // Utils_CrapDist(a, b)
        {0x004ad620, {false, {all, all, all}, {}}},                                          // SFX_PlayX(sound, 0x2000, 0)
        // Ob_AI's callees
        {0x0047ffc0, {true, {}, {}}},                                                        // CItem__FlashUpdate(object)
        {0x00480460, {true, {}, {}}},                                                        // Object_SpriteUpdate(object)
        {0x00480fa0, {true, {}, {}}},                                                        // Animation_InvokeUpdateHooks(object)
        // TrickTable_BuildForPlayer's callees (the skater manager's queries)
        {0x00416050, {false, {all}, {}}},                                                    // TrickConfig_FindByCharacter(character)
        {0x004b0c60, {true, {all}, {}}},                                                     // Skater_GetResourceSlot(player)
        {0x004b0cd0, {true, {all}, {}}},                                                     // SkaterManager_GetCostume(player)
        {0x004b1430, {true, {all}, {}}},                                                     // Skater_GetSlotRecord(slot)
        {0x004b1570, {true, {all}, {}}},                                                     // SkaterManager_LoadedBlock(slot)
        {0x004b1980, {false, {all}, {}}},                                                    // Skater_MapEnabledIndex(set)
    };
    return table;
}

// one retail callee call, with what it did
struct Expected {
    const Event* call = nullptr;
    std::optional<std::uint32_t> eax;
    std::vector<Event::Change> effects;          // k lines
    Bytes vector;                                // the v line: the out vector TRG_DecodeNodePosition filled
    std::vector<Event::Change> own_before;       // the walk's own writes since the previous call
};

struct Script {
    std::vector<Expected> calls;
    std::vector<Event::Change> own_after;
};

Script build_script(const Call& call) {
    Script script;
    std::vector<Event::Change> own;
    const auto& ev = call.events;
    for (std::size_t i = 0; i < ev.size(); ++i) {
        const Event& e = ev[i];
        if (e.kind == 'w') {
            own.insert(own.end(), e.changes.begin(), e.changes.end());
            continue;
        }
        if (e.kind != 'c') {
            continue;
        }
        Expected x;
        x.call = &e;
        x.own_before = std::move(own);
        own.clear();
        std::size_t j = i + 1;
        for (; j < ev.size(); ++j) {
            const Event& n = ev[j];
            if (n.kind == 'k') {
                x.effects.insert(x.effects.end(), n.changes.begin(), n.changes.end());
            } else if (n.kind == 'r' && !x.eax && n.callee == e.callee) {
                x.eax = n.eax;
            } else if (n.kind == 'v' && x.eax) {
                x.vector = n.data;
            } else {
                break;
            }
        }
        i = j - 1;
        script.calls.push_back(std::move(x));
    }
    script.own_after = std::move(own);
    return script;
}

// the bytes of the call's entry, by address (R first, then what the returned pointers pointed at, first sighting)
std::map<std::uint32_t, std::uint8_t> entry_bytes(const Call& call) {
    std::map<std::uint32_t, std::uint8_t> bytes;
    const auto lay = [&bytes](std::uint32_t base, const Bytes& data) {
        for (std::size_t i = 0; i < data.size(); ++i) {
            bytes.emplace(base + static_cast<std::uint32_t>(i), data[i]);
        }
    };
    for (const auto& [base, data] : call.regions) {
        lay(base, data);
    }
    for (const Event& e : call.events) {
        if (e.kind == 'r' && e.eax != 0 && !e.data.empty()) {
            lay(e.eax, e.data);
        }
    }
    return bytes;
}

RetailMemory build_memory(const std::map<std::uint32_t, std::uint8_t>& bytes) {
    RetailMemory memory;
    std::uint32_t base = 0;
    std::vector<std::uint8_t> run;
    for (const auto& [address, value] : bytes) {
        if (!run.empty() && address != base + run.size()) {
            memory.add_region(base, std::move(run));
            run = {};
        }
        if (run.empty()) {
            base = address;
        }
        run.push_back(value);
    }
    if (!run.empty()) {
        memory.add_region(base, std::move(run));
    }
    memory.add_region(kLocals, kLocalsBytes);
    return memory;
}

void apply(std::map<std::uint32_t, std::uint8_t>& bytes, const std::vector<Event::Change>& changes) {
    for (const auto& c : changes) {
        for (std::size_t i = 0; i < c.after.size(); ++i) {
            bytes[c.address + static_cast<std::uint32_t>(i)] = c.after[i];
        }
    }
}

void apply(RetailMemory& memory, const std::vector<Event::Change>& changes) {
    for (const auto& c : changes) {
        if (memory.contains(c.address, c.after.size())) {
            memory.write(c.address, c.after);
        } else {
            fail("a callee write outside the laid-out memory at " + hex(c.address));
        }
    }
}

// the laid-out memory must hold what retail held at this point
void compare(const RetailMemory& memory, const std::map<std::uint32_t, std::uint8_t>& expected, const std::string& where) {
    int shown = 0;
    for (const auto& [address, value] : expected) {
        if (!memory.contains(address)) {
            continue;
        }
        const std::uint8_t got = memory.u8(address);
        if (got != value && shown++ < 4) {
            fail(where + ": byte " + hex(address) + " is " + hex(got) + ", retail " + hex(value));
        }
    }
}

// the services TRG_RegisterCameraPoint calls: only the position decode, whose result it drops
class CameraPointHost final : public ServiceHost {
public:
    explicit CameraPointHost(RetailMemory& memory) : memory_(memory) {}

    std::uint32_t call(const std::string& name, const std::vector<std::uint32_t>&, const std::string&) override {
        if (name != "engine_004c8650") {
            fail("TRG_RegisterCameraPoint called " + name);
        }
        // what TRG_DecodeNodePosition leaves behind besides the vector: its assertion location (line 0xafe, trig.cpp)
        memory_.set32(0x00568638, 0xafe);
        const std::string file = "H:\\TonyHawk\\Pc2\\trig.cpp";
        memory_.write(0x0056881c, Bytes(file.begin(), file.end()));
        memory_.set8(0x0056881c + file.size(), 0);
        return 0;
    }

private:
    RetailMemory& memory_;
};

class Host final : public ServiceHost {
public:
    Host(const Script& script, RetailMemory& memory, std::map<std::uint32_t, std::uint8_t>& expected)
        : script_(script), memory_(memory), expected_(expected) {}

    std::uint32_t call(const std::string& name, const std::vector<std::uint32_t>& args, const std::string&) override {
        std::uint32_t address = 0;
        std::vector<std::uint32_t> kit_args = args;
        if (name == "assert") {
            address = kAssert;
            kit_args.insert(kit_args.begin(), 0);
        } else if (name.rfind("engine_", 0) == 0) {
            address = static_cast<std::uint32_t>(std::stoul(name.substr(7), nullptr, 16));
        } else if (name.rfind("vcall_", 0) == 0) {   // a virtual call: the target the vtable holds
            address = static_cast<std::uint32_t>(std::stoul(name.substr(6), nullptr, 16));
        } else {
            fail("unexpected service " + name);
            return 0;
        }
        skip_passing_assertions();
        if (next_ >= script_.calls.size()) {
            fail("the Kit calls " + name + " after retail's last call");
            return 0;
        }
        const Expected& x = script_.calls[next_++];
        apply(expected_, x.own_before);
        compare(memory_, expected_, "before " + name);
        if (x.call->callee != address) {
            fail("the Kit calls " + name + ", retail " + hex(x.call->callee) + " (site " + hex(x.call->site) + ")");
            return 0;
        }
        if (address == 0x004cde80 && kit_args.size() == 7) {   // Replay_StoreCarPosition's seventh argument, a word, travels in the c line's edx
            if ((kit_args[6] & 0xffff) != (x.call->edx & 0xffff)) {
                fail("Replay_StoreCarPosition: the angles word is " + hex(kit_args[6]) + ", retail " + hex(x.call->edx));
            }
            kit_args.pop_back();
        }
        compare_arguments(address, kit_args, *x.call);
        apply(expected_, x.effects);
        if (address == kRegisterCameraPoint) {
            // TRG_RegisterCameraPoint is ported as well: run it on the laid-out memory instead of applying retail's writes; the comparison before the next call (and at the end) holds it to them
            CameraPointHost nested(memory_);
            RetailServices services;
            services.attach(&nested);
            worldkit::WorldKit inner(memory_, services);
            inner.trg_register_camera_point(kit_args[0]);
            ++camera_points_;
            return x.eax.value_or(0);
        }
        apply(memory_, x.effects);
        if (address == kDecode && !x.vector.empty() && !kit_args.empty()) {
            memory_.write(kit_args[0], x.vector);   // the out vector lives on retail's stack; the Kit's is in the scratch window
        }
        return x.eax.value_or(0);
    }

    // the calls the Kit never made: retail's passing assertions are fine, anything else is a mismatch
    void finish() {
        for (; next_ < script_.calls.size(); ++next_) {
            const Expected& x = script_.calls[next_];
            apply(expected_, x.own_before);
            apply(expected_, x.effects);
            apply(memory_, x.effects);
            if (x.call->callee != kAssert || (x.call->arg[0] & 0xff) == 0) {
                fail("retail calls " + hex(x.call->callee) + " (site " + hex(x.call->site) + "), the Kit does not");
            }
        }
        apply(expected_, script_.own_after);
    }

    std::size_t compared_calls() const { return compared_; }
    std::size_t camera_points() const { return camera_points_; }

private:
    void skip_passing_assertions() {
        while (next_ < script_.calls.size()) {
            const Expected& x = script_.calls[next_];
            if (x.call->callee != kAssert || (x.call->arg[0] & 0xff) == 0) {
                break;
            }
            apply(expected_, x.own_before);
            apply(expected_, x.effects);
            apply(memory_, x.effects);
            ++next_;
        }
    }

    void compare_arguments(std::uint32_t address, const std::vector<std::uint32_t>& kit_args, const Event& retail) {
        ++compared_;
        if (address == kAssert) {
            if (kit_args.size() < 2 || kit_args[1] != retail.arg[1]) {
                fail("assertion " + hex(kit_args.size() > 1 ? kit_args[1] : 0) + ", retail " + hex(retail.arg[1]));
            }
            return;
        }
        if (retail.site == kSuspendVirtualCall) {   // Object_SuspendToList's virtual slot 1: the object in ECX, no arguments
            if (kit_args.size() != 1 || kit_args[0] != retail.ecx) {
                fail("the virtual call of Object_SuspendToList: object " + hex(kit_args.empty() ? 0 : kit_args[0]) + ", retail ECX " + hex(retail.ecx));
            }
            return;
        }
        if (retail.site == kObAiVirtualCall) {   // Ob_AI's virtual delete: the object in ECX and the argument 1
            if (kit_args.size() != 2 || kit_args[0] != retail.ecx || kit_args[1] != retail.arg[0]) {
                fail("the virtual call of Ob_AI: object " + hex(kit_args.empty() ? 0 : kit_args[0]) + ", retail ECX " + hex(retail.ecx));
            }
            return;
        }
        const auto it = signatures().find(address);
        if (it == signatures().end()) {
            fail("no signature for " + hex(address));
            return;
        }
        const Signature& sig = it->second;
        std::size_t first = 0;
        if (sig.thiscall) {
            if (kit_args.empty() || kit_args[0] != retail.ecx) {
                fail(hex(address) + ": object " + hex(kit_args.empty() ? 0 : kit_args[0]) + ", retail ECX " + hex(retail.ecx));
            }
            first = 1;
        }
        if (kit_args.size() != first + sig.masks.size()) {
            fail(hex(address) + ": " + std::to_string(kit_args.size() - first) + " arguments, expected " + std::to_string(sig.masks.size()));
            return;
        }
        for (std::size_t k = 0; k < sig.masks.size(); ++k) {
            if ((kit_args[first + k] & sig.masks[k]) != (retail.arg[k] & sig.masks[k])) {
                fail(hex(address) + ": argument " + std::to_string(k) + " is " + hex(kit_args[first + k]) + ", retail " + hex(retail.arg[k]));
            }
        }
        for (std::size_t p = 0; p < sig.pointees.size(); ++p) {
            if (p >= retail.pointees.size()) {
                fail(hex(address) + ": the trace has no bytes behind argument " + std::to_string(sig.pointees[p]));
                continue;
            }
            const Bytes& want = retail.pointees[p];
            const Bytes got = memory_.bytes(kit_args[first + sig.pointees[p]], want.size());
            if (got != want) {
                fail(hex(address) + ": the bytes behind argument " + std::to_string(sig.pointees[p]) + " differ");
            }
        }
    }

    const Script& script_;
    RetailMemory& memory_;
    std::map<std::uint32_t, std::uint8_t>& expected_;
    std::size_t next_ = 0;
    std::size_t compared_ = 0;
    std::size_t camera_points_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: opentony_world_trig_nodes_trace_test FIXTURE [--create] [--mutate]\n";
        return 2;
    }
    bool mutate = false;
    bool create = false;   // the fixture traces TRG_CreateObject (build/m10e/create_trace.py) instead of TRG_ParseNodes
    bool lists = false;    // the fixture traces the object list functions (build/m10e/list_trace.py)
    bool tricks = false;   // the fixture traces TrickTable_BuildForPlayer (build/m10e/trick_table_trace.py)
    bool enviro = false;   // the fixture traces Spool_FindEnviroItem (build/m10e/enviro_trace.py)
    bool construct = false;   // the fixture traces TRG_ConstructSubtype0x0192 (build/m10e/construct_trace.py)
    bool traffic = false;        // the fixture traces TRG_ConstructTrafficObject (build/m10e/traffic_trace.py)
    bool trafficupdate = false;  // the fixture traces the traffic car's update 0x00412960 (build/m12/traffic_update_trace.py)
    bool scriptobject = false;   // the fixture traces TRG_CreateScriptObject (build/m10e/script_object_trace.py)
    bool powerup = false;     // the fixture traces Powerup_CreateFromTrigger and Powerup_Construct (build/m10e/powerup_trace.py)
    bool obai = false;        // the fixture traces Ob_AI (build/m10e/ob_ai_trace.py)
    bool animstart = false;   // the fixture traces Animation_Start (build/m10e/anim_start_trace.py)
    for (int k = 2; k < argc; ++k) {
        mutate = mutate || std::string(argv[k]) == "--mutate";
        create = create || std::string(argv[k]) == "--create";
        lists = lists || std::string(argv[k]) == "--lists";
        tricks = tricks || std::string(argv[k]) == "--tricks";
        enviro = enviro || std::string(argv[k]) == "--enviro";
        construct = construct || std::string(argv[k]) == "--construct";
        traffic = traffic || std::string(argv[k]) == "--traffic";
        trafficupdate = trafficupdate || std::string(argv[k]) == "--trafficupdate";
        scriptobject = scriptobject || std::string(argv[k]) == "--scriptobject";
        powerup = powerup || std::string(argv[k]) == "--powerup";
        obai = obai || std::string(argv[k]) == "--obai";
        animstart = animstart || std::string(argv[k]) == "--animstart";
    }
    std::map<std::uint32_t, std::size_t> function_calls;
    std::vector<Call> calls;
    try {
        calls = parse_fixture(argv[1]);
    } catch (const std::exception& error) {
        std::cerr << "fixture: " << error.what() << "\n";
        return 2;
    }
    bool mutated = false;
    std::size_t matched = 0;
    std::size_t compared = 0;
    std::size_t camera_points = 0;
    for (const Call& call : calls) {
        current_call = call.n;
        const int before = failures;
        const Script script = build_script(call);
        std::map<std::uint32_t, std::uint8_t> expected = entry_bytes(call);
        RetailMemory memory = build_memory(expected);
        RetailServices services;
        Host host(script, memory, expected);
        services.attach(&host);
        worldkit::WorldKit kit(memory, services);
        createobj::CreateObjKit trick_kit(memory, services);
        try {
            if (enviro) {
                const std::uint32_t result = kit.spool_find_enviro_item(call.mode);
                for (const Event& e : call.events) {
                    // (--mutate: the first answer is changed, a lookup has no write to change)
                    const std::uint32_t expected_eax = mutate && !mutated ? e.eax ^ 4u : e.eax;
                    mutated = mutated || mutate;
                    if (e.kind == 'x' && expected_eax != result) {
                        fail("the Kit returns " + hex(result) + ", retail " + hex(e.eax));
                    }
                }
                ++function_calls[call.index];
            } else if (construct) {
                const std::uint32_t result = kit.construct_subtype_0x0192(call.stream, call.index, call.mode);
                for (const Event& e : call.events) {
                    if (e.kind == 'x' && e.eax != result) {
                        fail("the Kit returns " + hex(result) + ", retail " + hex(e.eax));
                    }
                }
                ++function_calls[0x0049f250];
            } else if (traffic) {
                const auto arg = [&call](std::size_t k) { return le(call.stream_bytes, 4 * k, 4); };
                const std::uint32_t result = kit.construct_traffic_object(call.stream, arg(0), arg(1), arg(2));
                for (const Event& e : call.events) {
                    if (e.kind == 'x' && e.eax != result) {
                        fail("the Kit returns " + hex(result) + ", retail " + hex(e.eax));
                    }
                }
                ++function_calls[0x00412640];
            } else if (trafficupdate) {
                kit.update_traffic_object(call.stream);
                ++function_calls[0x00412960];
            } else if (scriptobject) {
                const std::uint32_t result = kit.construct_script_object(call.stream, call.index, call.mode);
                for (const Event& e : call.events) {
                    if (e.kind == 'x' && e.eax != result) {
                        fail("the Kit returns " + hex(result) + ", retail " + hex(e.eax));
                    }
                }
                ++function_calls[0x00401060];
            } else if (animstart) {
                const auto arg = [&call](std::size_t k) { return le(call.stream_bytes, 4 * k, 4); };
                kit.animation_start(call.stream, arg(0), static_cast<std::int32_t>(arg(1)), static_cast<std::int32_t>(arg(2)), arg(3));
                ++function_calls[0x00480730];
            } else if (obai) {
                kit.object_ai_frame(call.mode);
                ++function_calls[0x00480ff0];
            } else if (powerup) {
                const auto arg = [&call](std::size_t k) { return le(call.stream_bytes, 4 * k, 4); };
                const std::uint32_t result = call.index == 0x004a7c50
                    ? kit.powerup_construct(call.stream, arg(0), arg(1), arg(2), arg(3), arg(4), arg(5))
                    : kit.powerup_create_from_trigger(arg(0), arg(1), arg(2), arg(3), arg(4));
                for (const Event& e : call.events) {
                    if (e.kind == 'x' && e.eax != result) {
                        fail("the Kit returns " + hex(result) + ", retail " + hex(e.eax));
                    }
                }
                ++function_calls[call.index];
            } else if (tricks) {
                // the call record carries the manager (stream), the player (mode) and, as the S line, the table argument
                trick_kit.trick_table_build(call.stream, call.mode, le(call.stream_bytes, 0, 4));
                ++function_calls[call.index];
            } else if (lists) {
                // the call record carries the function (index), the object (stream) and the list argument (mode)
                switch (call.index) {
                case 0x004801d0: kit.object_list_push_head(call.stream, call.mode); break;
                case 0x004801f0: kit.object_list_remove(call.stream, call.mode); break;
                case 0x004802f0: kit.object_suspend_to_list(call.stream, call.mode); break;
                case 0x004803b0: kit.object_unsuspend(call.stream); break;
                default: fail("a list trace of " + hex(call.index));
                }
                ++function_calls[call.index];
            } else if (create) {
                const std::uint32_t result = kit.trg_create_object(call.index);
                for (const Event& e : call.events) {
                    if (e.kind == 'x' && e.eax != result) {
                        fail("the Kit returns " + hex(result) + ", retail " + hex(e.eax));
                    }
                }
            } else {
                kit.trg_parse_nodes();
            }
        } catch (const std::exception& error) {
            fail(std::string("the Kit stopped: ") + error.what());
        }
        host.finish();
        if (mutate && !mutated) {
            for (const Event& e : call.events) {
                if (e.kind == 'w' && !e.changes.empty() && memory.contains(e.changes.front().address)) {
                    const std::uint32_t at = e.changes.front().address;
                    memory.set8(at, memory.u8(at) ^ 0x01);
                    mutated = true;
                    break;
                }
            }
        }
        compare(memory, expected, "at the end");
        compared += host.compared_calls();
        camera_points += host.camera_points();
        if (failures == before) {
            ++matched;
        }
    }
    std::cout << argv[1] << ": " << matched << "/" << calls.size() << " calls match, " << compared << " callee calls compared, " << camera_points
              << " of them TRG_RegisterCameraPoint run natively\n";
    for (const auto& [function, count] : function_calls) {
        std::cout << "  " << hex(function) << ": " << count << " calls\n";
    }
    if (mutate && !mutated) {
        std::cerr << "--mutate found no write to change\n";
        return 2;
    }
    return failures == 0 ? 0 : 1;
}
