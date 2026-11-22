// Replays the retail TRG_InterpretCommandStream traces (re/evidence/fixtures/trg-interpreter-*.txt, 214 calls; scripts/make_trg_interpreter_fixture.py documents the format) through
// the retail-memory form of the interpreter (WorldKit::trg_interpret_command_stream, world_trig_commands.cpp), word for word.
//
// Each call's entry state is laid out in a RetailMemory at the retail addresses: the tracked regions (R), the stream window (S) and the bytes behind the pointers the callees returned
// (the command point record, the link lists, the trick config record). Every callee is a stub (a ServiceHost): the Kit's calls must be retail's, in order, with retail's arguments
// (the boxes and the parameter block behind pointer arguments compared by their bytes), each answered with retail's EAX and followed by the memory the callee wrote (the k lines,
// and the writes between the reports of a blocking callee). Before every call and at the end the whole laid-out memory must equal retail's at that point: the entry state with the
// interpreter's own writes (w lines, the assertion line and file words included) and the callees' writes applied in order. A passing M3dUtils_InBetween is not a Kit call (a Kit
// reports a failed assertion only), so retail's passing ones are skipped. The probe tracked the skater's gap slots (+0x2f74) but of the 20 gap definition indexes after them only the
// first and the last (+0x2fc4, +0x3010): the rest are laid out as zeros the Kit may write and are not compared.
//
//   opentony_world_trig_commands_trace_test FIXTURE.txt [--mutate]
//
// --mutate changes one byte the interpreter wrote in the first call that writes, after the run, and must make the replay fail (the comparison has teeth).
//
// Exit codes: 0 all match, 1 mismatch, 2 usage/format error.
#include "world_kit.hpp"

#include "trg_interpreter_trace_fixture.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace opentony::runtime;
using namespace opentony::trg::trg_trace_fixture;

namespace {

constexpr std::uint32_t kAssert = 0x004011e0;
constexpr std::uint32_t kLocals = 0x7ff0ea00;            // the Kit's stand-ins for the interpreter's stack locals (world_trig_commands.cpp)
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

// how a callee's arguments are compared: the Kit passes a thiscall's object first (retail: ECX); `masks` per stack argument (0 = not compared: retail's stack address of a
// local, or a word pushed with whatever the register's upper half held), `pointees` the stack arguments whose bytes the trace recorded after the bar
struct Signature {
    bool thiscall = false;
    std::vector<std::uint32_t> masks;
    std::vector<std::size_t> pointees;
};

const std::map<std::uint32_t, Signature>& signatures() {
    constexpr std::uint32_t all = 0xffffffff;
    constexpr std::uint32_t word = 0xffff;
    constexpr std::uint32_t byte = 0xff;
    static const std::map<std::uint32_t, Signature> table{
        {0x00401000, {false, {word}, {}}},                 // ScriptObject_ClearFlag
        {0x00401030, {false, {word}, {}}},                 // ScriptObject_SetFlag
        {0x00401060, {true, {all, 0}, {1}}},               // TRG_CreateScriptObject(checksum, the parameter block)
        {0x0040bd00, {true, {all}, {}}},                   // Camera_SetOverrideA (a sign-extended word)
        {0x0040bd10, {true, {all}, {}}},
        {0x0040bd20, {true, {word}, {}}},
        {0x0040bd30, {true, {word}, {}}},
        {0x0040be60, {true, {word}, {}}},
        {0x0040f140, {true, {word, word}, {}}},
        {0x0040f150, {true, {word, word}, {}}},
        {0x0040f160, {true, {word, word}, {}}},
        {0x0040f170, {true, {word, word}, {}}},
        {0x0040f180, {true, {word, word}, {}}},
        {0x0040f190, {true, {word, word}, {}}},
        {0x0040f1a0, {true, {word, word}, {}}},
        {0x00410c40, {true, {}, {}}},                      // Camera_ResetAndSettle
        {0x00414c50, {false, {all}, {}}},                  // Gap_CompleteDefinition
        {0x00416050, {false, {all}, {}}},                  // TrickConfig_FindByCharacter
        {0x0042fc70, {false, {}, {}}},                     // Db_UpdateSky
        {0x00464710, {false, {all, all, all}, {}}},        // M3D_SetFogRange
        {0x00466c10, {false, {}, {}}},                     // LevelEvent_InitializeAndDispatch
        {0x0046c670, {true, {}, {}}},                      // Skater_ResetOrientationRows
        {0x00473650, {false, {}, {}}},                     // Mess_DeleteAll
        {0x0047fd30, {false, {all}, {}}},                  // TRG_AllocObjectStorage
        {0x00480730, {true, {all, all, all, all}, {}}},    // Animation_Start
        {0x0048e9c0, {true, {}, {}}},                      // CPanelData__OutOfBounds
        {0x0048f5f0, {true, {}, {}}},                      // Trick_TakeOff
        {0x0048f690, {true, {all, all, all, all}, {}}},   // Gap_ApplyDefinitionState(skater; definition + 8, -score, 0, slot definition)
        {0x0048f8f0, {true, {}, {}}},                      // Trick_Land
        {0x0048fb20, {true, {}, {}}},                      // CBruce__Trick_Bail
        {0x004904c0, {true, {}, {}}},                      // CBruce__ResetTrickBuffers
        {0x00496300, {true, {}, {}}},                      // Skater_ClearTrickFlags
        {0x004ad5e0, {false, {all}, {}}},                  // Audio_Nothing
        {0x004ad620, {false, {all, all, all}, {}}},        // SFX_PlayX
        {0x004ad9f0, {false, {all}, {}}},                  // SFX_StopVoice
        {0x004ada20, {false, {byte}, {}}},                 // SFX_SetReverb
        {0x004adc30, {false, {word}, {}}},
        {0x004adc50, {false, {word}, {}}},
        {0x004adc60, {false, {word}, {}}},
        {0x004b3230, {false, {all}, {}}},                  // PSX_FindLoadedRegionByName
        {0x004b3270, {false, {all}, {}}},                  // PSX_ClearNamedRegion
        {0x004b37a0, {false, {all, all, all, all}, {}}},   // PSX_QueueResource
        {0x004b3df0, {false, {}, {}}},                     // PSX_DrainSpooler
        {0x004b8500, {false, {}, {}}},                     // Tag_SwapRoles
        {0x004c4c50, {false, {all}, {}}},                  // Trig_FindRestartByName
        {0x004c4d10, {false, {all}, {}}},                  // FudgeAngles
        {0x004c5ac0, {false, {all}, {}}},                  // TRG_FindCommandPointLink
        {0x004c5b00, {false, {all}, {}}},                  // TRG_ExecuteCommandList
        {0x004c5b60, {false, {all}, {}}},                  // TRG_SendSignalToNodes
        {0x004c5c70, {false, {all, all}, {}}},             // TRG_ApplyActivationOrSuspendList
        {0x004c5d70, {false, {all}, {}}},                  // TRG_SkipAlignedCString
        {0x004c5d90, {false, {all}, {}}},                  // Trig_AllocateDualBuffer
        {0x004c5da0, {false, {}, {}}},                     // Trig_RemoveDualBuffer
        {0x004c77f0, {false, {all, all}, {}}},             // TRG_SendVisible
        {0x004c7a00, {false, {all, all}, {}}},             // TRG_SendKillFromNode
        {0x004c7c50, {false, {all}, {}}},                  // TRG_SkipCommandPayload
        {0x004c8550, {false, {all}, {}}},                  // TRG_GetNodeLinks
        {0x004c8650, {false, {0, all}, {}}},               // TRG_DecodeNodePosition(the out vector, index)
        {0x004c9340, {false, {all}, {}}},                  // Rnd
        {0x004ca270, {false, {0, 0}, {0, 1}}},             // RuntimeObject_ApplyBoundsToLists(the two corners)
        {0x004ca2d0, {false, {0, 0, byte, byte}, {0, 1}}}, // RuntimeObject_UpdateVisibilityBounds(the corners, two booleans)
        {0x004cde70, {false, {}, {}}},                     // Video_Playing
        {0x004de010, {false, {all}, {}}},                  // NetGame_IsRemotePlayer
    };
    return table;
}

// one retail callee call, with what it did
struct Expected {
    const Event* call = nullptr;
    std::optional<std::uint32_t> eax;            // none: the harness saw no return (a blocking callee)
    std::vector<Event::Change> effects;          // k lines, and the w lines between the reports of a blocking callee
    std::vector<Event::Change> own_before;       // the interpreter's own writes since the previous call
};

struct Script {
    std::vector<Expected> calls;
    std::vector<Event::Change> own_after;        // the interpreter's writes after its last call
    std::size_t unreturned = 0;
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
            continue;   // x, and k / r lines consumed below
        }
        Expected x;
        x.call = &e;
        x.own_before = std::move(own);
        own.clear();
        std::size_t j = i + 1;
        for (; j < ev.size(); ++j) {
            const Event& n = ev[j];
            if (n.kind == 'k' || (n.kind == 'w' && !x.eax)) {
                x.effects.insert(x.effects.end(), n.changes.begin(), n.changes.end());
            } else if (n.kind == 'c' && !x.eax && n.callee == e.callee && n.site == e.site) {
                // the debugger reports a blocking callee again at the same site
            } else if (n.kind == 'r' && !x.eax && n.callee == e.callee) {
                x.eax = n.eax;
            } else {
                break;
            }
        }
        if (!x.eax) {
            ++script.unreturned;
        }
        i = j - 1;
        script.calls.push_back(std::move(x));
    }
    script.own_after = std::move(own);
    return script;
}

// the bytes of the call's entry, by address (R first, then the stream, then what the callees' returned pointers pointed at, first sighting, then the static data)
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
    lay(call.stream, call.stream_bytes);
    for (const Event& e : call.events) {
        if (e.kind == 'r' && e.eax != 0 && !e.data.empty()) {
            lay(e.eax, e.data);
        }
    }
    for (const auto& [base, data] : call.statics) {
        lay(base, data);   // the gap definition table (D lines): the executable's data, where nothing traced covers it
    }
    return bytes;
}

// the skater's gap definition indexes the probe did not track (see the top): zeros, not compared
std::map<std::uint32_t, std::uint8_t> untracked_bytes(const Call& call, const std::map<std::uint32_t, std::uint8_t>& bytes) {
    std::map<std::uint32_t, std::uint8_t> extra;
    const std::uint32_t skater = call.entry(0x0056a954);
    if (skater == 0) {
        return extra;
    }
    for (std::uint32_t at = skater + 0x2fc4; at < skater + 0x3014; ++at) {
        if (bytes.count(at) == 0) {
            extra.emplace(at, 0);
        }
    }
    return extra;
}

RetailMemory build_memory(std::map<std::uint32_t, std::uint8_t> bytes, const std::map<std::uint32_t, std::uint8_t>& untracked) {
    bytes.insert(untracked.begin(), untracked.end());
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
        if (got != value) {
            if (shown++ < 4) {
                fail(where + ": byte " + hex(address) + " is " + hex(got) + ", retail " + hex(value));
            }
        }
    }
}

class Host final : public ServiceHost {
public:
    Host(const Call& call, const Script& script, RetailMemory& memory, std::map<std::uint32_t, std::uint8_t>& expected)
        : call_(call), script_(script), memory_(memory), expected_(expected) {}

    std::uint32_t call(const std::string& name, const std::vector<std::uint32_t>& args, const std::string&) override {
        std::uint32_t address = 0;
        std::vector<std::uint32_t> kit_args = args;
        if (name == "assert") {
            address = kAssert;
            kit_args.insert(kit_args.begin(), 0);
        } else if (name.rfind("engine_", 0) == 0) {
            address = static_cast<std::uint32_t>(std::stoul(name.substr(7), nullptr, 16));
        } else {
            fail("unexpected service " + name);
            return 0;
        }
        // skip retail's passing assertions (no Kit call), applying what they did
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
        compare_arguments(address, kit_args, *x.call);
        apply(expected_, x.effects);
        apply(memory_, x.effects);
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

private:
    void compare_arguments(std::uint32_t address, const std::vector<std::uint32_t>& kit_args, const Event& retail) {
        ++compared_;
        if (address == kAssert) {
            if (kit_args.size() < 2 || kit_args[1] != retail.arg[1]) {
                fail("assertion " + hex(kit_args.size() > 1 ? kit_args[1] : 0) + ", retail " + hex(retail.arg[1]));
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

    const Call& call_;
    const Script& script_;
    RetailMemory& memory_;
    std::map<std::uint32_t, std::uint8_t>& expected_;
    std::size_t next_ = 0;
    std::size_t compared_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: opentony_world_trig_commands_trace_test FIXTURE [--mutate]\n";
        return 2;
    }
    const bool mutate = argc > 2 && std::string(argv[2]) == "--mutate";
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
    std::size_t unreturned = 0;
    for (const Call& call : calls) {
        current_call = call.n;
        const int before = failures;
        const Script script = build_script(call);
        unreturned += script.unreturned;
        std::map<std::uint32_t, std::uint8_t> expected = entry_bytes(call);
        RetailMemory memory = build_memory(expected, untracked_bytes(call, expected));
        RetailServices services;
        Host host(call, script, memory, expected);
        services.attach(&host);
        worldkit::WorldKit kit(memory, services);
        try {
            kit.trg_interpret_command_stream(call.stream, call.index, call.mode);
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
        if (failures == before) {
            ++matched;
        }
    }
    std::cout << argv[1] << ": " << matched << "/" << calls.size() << " calls match, " << compared << " callee calls compared, " << unreturned
              << " blocking calls without a return\n";
    if (mutate && !mutated) {
        std::cerr << "--mutate found no write to change\n";
        return 2;
    }
    return failures == 0 ? 0 : 1;
}
