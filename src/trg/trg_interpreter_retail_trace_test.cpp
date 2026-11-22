// Replays the retail TRG_InterpretCommandStream trace (re/evidence/fixtures/trg-interpreter-*.txt,
// scripts/make_trg_interpreter_fixture.py documents the format) through the value model of
// src/trg/trg_runtime.cpp.
//
// Each retail call becomes a synthetic TRG file: node 0 is a command point holding the call's links
// (as dummy object nodes) and its stream at the file offset that keeps the retail address parity (the
// operands are aligned on absolute addresses), plus one restart node per name the call looked up. The
// model runs that stream through TriggerRuntime::dispatch_stream. Both sides are reduced to the same
// ordered list of effects, and the lists must be equal:
//   retail: the interpreter's calls to its callees and the writes it makes itself (the fixture's c and
//           w lines), read through the table in retail_effects();
//   model:  the TriggerServices callbacks, read through ModelRecorder.
// Callees that only answer a question (command point lookup, link list, string skip, Video_Playing,
// NetGame_IsRemotePlayer, TrickConfig_FindByCharacter) are inputs of the model run, not effects.
// A difference that is understood is listed in known_difference() with its cause; any other fails.

#include "trg_interpreter_trace_fixture.hpp"
#include "trg_runtime.hpp"

#include "tests/test_check.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace opentony::trg;

namespace {

using namespace opentony::trg::trg_trace_fixture;

// ---- the retail side ---------------------------------------------------------------------------

constexpr std::uint32_t kCameraList = 0x0055fa38;
constexpr std::uint32_t kSkater2 = 0x0056a960;
constexpr std::uint32_t kLevel = 0x0056a898;
constexpr std::uint32_t kModes = 0x00533f38;

struct Retail {
    std::vector<std::string> effects;
    std::set<std::string> unmapped;          // callees / writes the table does not know
    std::vector<std::uint16_t> links;        // TRG_GetNodeLinks(index)
    std::vector<std::string> restart_names;  // names looked up
    std::optional<Event> command_point;      // r of TRG_FindCommandPointLink (data = the record)
    std::optional<Event> trick_config;       // r of TrickConfig_FindByCharacter
    bool remote_player{false};
    bool gap{false};
    bool gap_slots{false};
    bool camera_present{false};
    bool skater_present{false};
};

std::string c_string_at(const Call& call, std::uint32_t pointer) {
    std::string out;
    if (pointer < call.stream || pointer - call.stream >= call.stream_bytes.size()) {
        return "<outside>";
    }
    for (std::size_t i = pointer - call.stream; i < call.stream_bytes.size() && call.stream_bytes[i] != 0; ++i) {
        out.push_back(static_cast<char>(call.stream_bytes[i]));
    }
    return out;
}

std::string join_targets(const std::vector<std::uint16_t>& targets) {
    std::string out;
    for (std::size_t i = 0; i < targets.size(); ++i) {
        out += (i ? "," : "") + hexs(targets[i]);
    }
    return out;
}

std::string box_text(const Bytes& a, const Bytes& b) {
    std::string out;
    for (const Bytes* box : {&a, &b}) {
        for (std::size_t i = 0; i < 3; ++i) {
            out += " " + hexs(le(*box, i * 4, 4));
        }
    }
    return out;
}

// the node list a pointer argument names: the data of the most recent TRG_GetNodeLinks that returned it
std::optional<std::vector<std::uint16_t>> list_at(const std::map<std::uint32_t, Bytes>& lists, std::uint32_t pointer) {
    const auto it = lists.find(pointer);
    if (it == lists.end() || it->second.size() < 2) {
        return std::nullopt;
    }
    const std::uint32_t count = le(it->second, 0, 2);
    if (2 + count * 2 > it->second.size()) {
        return std::nullopt;
    }
    std::vector<std::uint16_t> out;
    for (std::uint32_t i = 0; i < count; ++i) {
        out.push_back(static_cast<std::uint16_t>(le(it->second, 2 + i * 2, 2)));
    }
    return out;
}

const char* legacy_for(std::uint32_t callee) {
    switch (callee) {
    case 0x40be60: return "a1";
    case 0x40f140: return "82";
    case 0x40f150: return "87";
    case 0x40f160: return "8f";
    case 0x40f170: return "90";
    case 0x40f180: return "91";
    case 0x40f190: return "92";
    case 0x4ad5e0: return "9f";
    case 0x4adc30: return "9c";
    case 0x4adc50: return "9b";
    case 0x4adc60: return "96";
    default: return nullptr;
    }
}

Retail retail_effects(const Call& call) {
    Retail out;
    out.camera_present = call.entry(kCameraList) != 0;
    out.skater_present = call.entry(kSkater2) != 0;
    std::map<std::uint32_t, Bytes> lists;  // returned pointer -> data
    const std::uint32_t camera = call.entry(kCameraList);
    const std::uint32_t skater = call.entry(kSkater2);
    std::uint32_t command_record = 0;
    std::uint32_t trick_record = 0;
    // The debugger reports some blocking callees (PSX_DrainSpooler) again at the same call site before
    // they return: one logical call, whose return is the only r line.
    std::optional<std::pair<std::uint32_t, std::uint32_t>> open_call;
    for (const Event& ev : call.events) {
        if (ev.kind == 'c' && open_call && open_call->first == ev.callee && open_call->second == ev.site) {
            continue;
        }
        if (ev.kind == 'c') {
            open_call = std::make_pair(ev.callee, ev.site);
        } else if (ev.kind == 'r') {
            open_call.reset();
        }
        if (ev.kind == 'r') {
            if (ev.callee == 0x4c8550 && ev.eax != 0) {
                lists[ev.eax] = ev.data;
                if (out.links.empty() && ev.data.size() >= 2) {
                    const std::uint32_t count = le(ev.data, 0, 2);
                    for (std::uint32_t i = 0; i < count && 2 + (i + 1) * 2 <= ev.data.size(); ++i) {
                        out.links.push_back(static_cast<std::uint16_t>(le(ev.data, 2 + i * 2, 2)));
                    }
                }
            } else if (ev.callee == 0x4c5ac0 && ev.eax != 0) {
                out.command_point = ev;
                command_record = ev.eax;
            } else if (ev.callee == 0x416050 && ev.eax != 0) {
                out.trick_config = ev;
                trick_record = ev.eax;
            } else if (ev.callee == 0x4de010 && (ev.eax & 0xff) != 0) {
                out.remote_player = true;
            }
            continue;
        }
        if (ev.kind == 'c') {
            const auto& a = ev.arg;
            const std::uint32_t v16 = a[0] & 0xffff;
            switch (ev.callee) {
            case 0x401000: out.effects.push_back("flag clear " + hexs(v16)); break;
            case 0x401030: out.effects.push_back("flag set " + hexs(v16)); break;
            case 0x401060: {
                const Bytes& p = ev.pointees.empty() ? Bytes{} : ev.pointees[0];
                out.effects.push_back("script_object " + hexs(a[0]) + " " + hexs(le(p, 0, 2)) + " "
                                      + hexs(le(p, 2, 2)) + " " + hexs(le(p, 4, 2)));
                break;
            }
            case 0x4011e0:
                if ((a[0] & 0xff) == 0) {
                    out.effects.push_back("diag");
                }
                break;
            case 0x40bd00: out.effects.push_back("cobj 99 " + hexs(v16)); break;
            case 0x40bd10: out.effects.push_back("cobj 9a " + hexs(v16)); break;
            case 0x40bd20: out.effects.push_back("cobj a4 " + hexs(v16)); break;
            case 0x40bd30: out.effects.push_back("cobj a5 " + hexs(v16)); break;
            case 0x40f1a0: out.effects.push_back("cobj_pair a7 " + hexs(v16) + " " + hexs(a[1] & 0xffff)); break;
            case 0x40be60: case 0x40f140: case 0x40f150: case 0x40f160: case 0x40f170: case 0x40f180:
            case 0x40f190: case 0x4ad5e0: case 0x4adc30: case 0x4adc50: case 0x4adc60:
                out.effects.push_back(std::string("legacy ") + legacy_for(ev.callee));
                break;
            case 0x4ada20: out.effects.push_back("reverb " + hexs(a[0] & 0xff)); break;
            case 0x4ad620: out.effects.push_back("sfx_play " + hexs(v16)); break;
            case 0x4ad9f0: out.effects.push_back("sfx_stop " + hexs(v16)); break;
            case 0x4b37a0: out.effects.push_back("resource " + hexs(a[1]) + " " + c_string_at(call, a[0])); break;
            case 0x4b3270: out.effects.push_back("resource 2 " + c_string_at(call, a[0])); break;
            case 0x4b3df0: out.effects.push_back("flush"); break;
            case 0x4c4c50:
                out.restart_names.push_back(c_string_at(call, a[0]));
                out.effects.push_back("restart " + out.restart_names.back());
                break;
            case 0x473650: out.effects.push_back("mess_delete_all"); break;
            case 0x4c5b00: {
                const auto list = list_at(lists, a[0]);
                if (!list) {
                    out.unmapped.insert("ExecuteCommandList with an unknown list");
                    break;
                }
                for (std::uint16_t target : *list) {
                    out.effects.push_back("pulse " + hexs(target));
                }
                break;
            }
            case 0x4c5b60: {
                const auto list = list_at(lists, a[0]);
                if (!list) {
                    out.unmapped.insert("SendSignalToNodes with an unknown list");
                    break;
                }
                out.effects.push_back("signal " + join_targets(*list));
                break;
            }
            case 0x4c5c70: {
                const auto list = list_at(lists, a[0]);
                if (!list) {
                    out.unmapped.insert("ApplyActivationOrSuspendList with an unknown list");
                    break;
                }
                out.effects.push_back("suspend_activate " + hexs(a[1] & 0xffff) + " " + join_targets(*list));
                break;
            }
            case 0x4c77f0: out.effects.push_back("visible " + hexs(a[1] & 0xffff)); break;
            case 0x4c7a00: out.effects.push_back("kill " + hexs(a[1] & 0xff)); break;
            case 0x464710:
                out.effects.push_back("fog " + hexs(a[0] & 0xffff) + " " + hexs(a[1] & 0xffff) + " " + hexs(a[2] & 0xffff));
                break;
            case 0x466c10: out.effects.push_back("level_event"); break;
            case 0x4ca270:
                if (ev.pointees.size() != 2) {
                    out.unmapped.insert("bounds boxes missing from the fixture");
                    break;
                }
                out.effects.push_back("bounds_list" + box_text(ev.pointees[0], ev.pointees[1]));
                break;
            case 0x4ca2d0:
                if (ev.pointees.size() != 2) {
                    out.unmapped.insert("bounds boxes missing from the fixture");
                    break;
                }
                out.effects.push_back("bounds_vis" + box_text(ev.pointees[0], ev.pointees[1]) + " "
                                      + hexs(a[2] & 0xff) + " " + hexs(a[3] & 0xff));
                break;
            case 0x4c5d90: out.effects.push_back("timer " + hexs(a[0])); break;
            case 0x4c5da0: break;  // the old dual buffer goes away; 0x97's effect is the allocation
            case 0x42fc70: break;  // Db_UpdateSky: the write to the sky word is the effect
            case 0x47fd30:  // TRG_AllocObjectStorage: the storage for the script object, not an effect
            case 0x4c5ac0: case 0x4c5d70: case 0x4c7c50: case 0x4c8550: case 0x4cde70: case 0x4de010:
            case 0x4c9340: case 0x4b3230: case 0x416050: case 0x4c8650:
                break;  // questions the interpreter asks, answered from the entry state
            case 0x414c50: case 0x48f690: case 0x48f5f0: case 0x48f8f0:
                out.gap = true;
                break;
            default:
                out.unmapped.insert("callee " + hexs(ev.callee));
                break;
            }
            continue;
        }
        if (ev.kind != 'w') {
            continue;  // k: a callee's writes; x: the exit
        }
        for (const auto& ch : ev.changes) {
            const std::uint32_t at = ch.address;
            const std::uint32_t now = le(ch.after, 0, ch.after.size());
            const std::uint32_t was = le(ch.before, 0, ch.before.size());
            if (at == 0x568638 || (at >= 0x56881c && at < 0x56885c)) {
                continue;  // the assert location stamp (line, file)
            }
            if (at == 0x535af8) {
                out.effects.push_back("global aa " + hexs(now & 0xffff));
            } else if (at == 0x564368) {
                if ((now & 0xffff) != (was & 0xffff)) {
                    out.effects.push_back("global a6 " + hexs(now & 0xffff));
                }
                if ((now >> 16) != (was >> 16)) {
                    out.effects.push_back("global a9 " + hexs(now >> 16));
                }
            } else if (at == 0x5685ec) {
                out.effects.push_back("initial " + hexs(now & 0xffff));
            } else if (at == 0x563a60) {
                out.effects.push_back("script_value " + hexs(now));
            } else if (at == 0x56114c) {
                out.effects.push_back("level_value " + hexs(now));
            } else if (at == 0x56e1b4) {
                // the count grows by one per string; the model reports the finished list once
                if (now != 0) {
                    if (!out.effects.empty() && out.effects.back().rfind("cheat_strings", 0) == 0) {
                        out.effects.pop_back();
                    }
                    out.effects.push_back("cheat_strings " + hexs(now));
                }
            } else if (at >= 0x56e164 && at < 0x56e1b4) {
                // the pointer table of the cheat restart names
            } else if (at == 0x56e218) {
                out.effects.push_back("competition " + c_string_at(call, now));
            } else if (at == 0x56dcb4 || (at >= 0x55ff94 && at < 0x560094)) {
                // the competition name's loaded-region answer and the track spooling buffers
            } else if (camera != 0 && at >= camera + 0x3a0 && at < camera + 0x520) {
                const std::uint32_t off = at - camera;
                if (off == 0x504) {
                    out.effects.push_back("cobj a0 " + hexs(now));
                } else if (off == 0x434) {
                    if ((now & 0xffff) != (was & 0xffff)) {
                        out.effects.push_back("cobj a8 " + hexs(now & 0xffff));
                    }
                    if ((now >> 16) != (was >> 16)) {
                        out.effects.push_back("cobj ac " + hexs(now >> 16));
                    }
                } else if (off == 0x3dc) {
                    out.effects.push_back("cobj_copy ad");
                } else {
                    out.unmapped.insert("camera write +" + hexs(off));
                }
            } else if (skater != 0 && at >= skater + 0x3190 && at < skater + 0x31a0) {
                const std::uint32_t off = at - skater;
                if (off == 0x3198) {
                    out.effects.push_back("cskater a3 " + hexs(now));
                } else if (off == 0x319c) {
                    out.effects.push_back("cskater b1 " + hexs(now));
                } else {
                    out.unmapped.insert("skater write +" + hexs(off));
                }
            } else if (call.entry(0x0056a954) != 0 && at >= call.entry(0x0056a954) + 0x2f74
                       && at < call.entry(0x0056a954) + 0x3000) {
                out.gap_slots = true;  // opcode 0xc9 registers the gap in the skater's slot tables
            } else if (command_record != 0 && at >= command_record && at < command_record + 0x20) {
                // the command point's counters: compared after the run
            } else if (trick_record != 0 && at >= trick_record && at < trick_record + 0x100) {
                const std::uint32_t changed = (now ^ was) & 0xff;
                for (std::uint32_t bit = 0; bit < 8; ++bit) {
                    if (changed & (1u << bit)) {
                        out.effects.push_back("career_flag " + hexs(bit));
                    }
                }
            } else {
                out.unmapped.insert("write " + hexs(at));
            }
        }
    }
    return out;
}

// ---- the model side ----------------------------------------------------------------------------

struct ModelRecorder final : TriggerServices {
    std::vector<std::string> effects;
    std::vector<std::uint16_t> originals;  // synthetic node index -> retail node index
    const Call* call{};
    const Retail* retail{};
    std::uint32_t level{};

    std::string orig(std::size_t node) const {
        return node >= 1 && node - 1 < originals.size() ? hexs(originals[node - 1]) : "?" + hexs(static_cast<std::uint32_t>(node));
    }
    std::string orig_list(std::span<const std::uint16_t> nodes) const {
        std::string out;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            out += (i ? "," : "") + orig(nodes[i]);
        }
        return out;
    }

    void on_node_pulse(std::size_t node) override { effects.push_back("pulse " + orig(node)); }
    void on_suspend_activate(std::size_t, std::uint16_t op, std::span<const std::uint16_t> t) override {
        effects.push_back("suspend_activate " + hexs(op) + " " + orig_list(t));
    }
    void on_signal(std::size_t, std::span<const std::uint16_t> t) override { effects.push_back("signal " + orig_list(t)); }
    void on_kill(std::size_t, std::uint16_t op, std::span<const std::uint16_t>) override {
        effects.push_back(std::string("kill ") + (op == 0xc ? "1" : "0"));
    }
    void on_visible(std::size_t, std::uint16_t v, std::span<const std::uint16_t>) override {
        effects.push_back("visible " + hexs(v));
    }
    void on_object_flag_by_id(std::uint16_t id, bool set) override {
        effects.push_back(std::string("flag ") + (set ? "set " : "clear ") + hexs(id));
    }
    void on_global_word(std::uint16_t op, std::uint16_t v) override { effects.push_back("global " + hexs(op) + " " + hexs(v)); }
    void on_current_object_word(std::size_t, std::uint16_t op, std::uint16_t v) override {
        effects.push_back("cobj " + hexs(op) + " " + hexs(v));
    }
    void on_current_object_pair(std::size_t, std::uint16_t op, std::uint16_t a, std::uint16_t b) override {
        effects.push_back("cobj_pair " + hexs(op) + " " + hexs(a) + " " + hexs(b));
    }
    void on_current_object_copy(std::size_t, std::uint16_t op) override { effects.push_back("cobj_copy " + hexs(op)); }
    void on_current_skater_word(std::size_t, std::uint16_t op, std::uint16_t v) override {
        effects.push_back("cskater " + hexs(op) + " " + hexs(v));
    }
    void on_fog_range(std::uint16_t a, std::uint16_t b, std::uint16_t c) override {
        effects.push_back("fog " + hexs(a) + " " + hexs(b) + " " + hexs(c));
    }
    void on_music(std::int16_t v) override { effects.push_back("sfx_play " + hexs(static_cast<std::uint16_t>(v))); }
    void on_sound(std::int16_t v) override { effects.push_back("sfx_stop " + hexs(static_cast<std::uint16_t>(v))); }
    void on_resource(std::uint16_t mode, std::string_view name) override {
        effects.push_back("resource " + hexs(mode) + " " + std::string(name));
    }
    void on_flush_resources() override { effects.push_back("flush"); }
    void on_fixed_path_records(std::span<const FixedPathRecord> records) override {
        for (const auto& r : records) {
            effects.push_back("bounds_list" + record_text(r));
        }
    }
    void on_fixed_path(std::uint16_t first, std::uint16_t second, std::span<const FixedPathRecord> records) override {
        const bool fa = first < 2 ? first != 0 : first != 2;
        for (const auto& r : records) {
            effects.push_back("bounds_vis" + record_text(r) + " " + (fa ? "1" : "0") + " " + (second != 0 ? "1" : "0"));
        }
    }
    static std::string record_text(const FixedPathRecord& r) {
        const auto fixed = r.fixed12();
        std::string out;
        for (std::int32_t v : fixed) {
            out += " " + hexs(static_cast<std::uint32_t>(v));
        }
        return out;
    }
    void on_competition_name(std::string_view name) override { effects.push_back("competition " + std::string(name)); }
    void on_restart_selected(std::uint16_t, std::size_t, std::string_view name) override {
        effects.push_back("restart " + std::string(name));
    }
    void on_initial_state(std::uint16_t v) override { effects.push_back("initial " + hexs(v)); }
    void on_script_value(std::uint32_t v) override { effects.push_back("script_value " + hexs(v)); }
    void on_level_value(std::uint32_t v) override { effects.push_back("level_value " + hexs(v)); }
    void on_script_object(std::size_t, std::uint32_t key, std::array<std::uint16_t, 3> p) override {
        effects.push_back("script_object " + hexs(key) + " " + hexs(p[0]) + " " + hexs(p[1]) + " " + hexs(p[2]));
    }
    void on_timer(std::uint32_t ms) override { effects.push_back("timer " + hexs(ms)); }
    void on_reverb_type(std::uint8_t v) override { effects.push_back("reverb " + hexs(v)); }
    void on_level_event_state() override { effects.push_back("level_event"); }
    void on_cheat_restart_strings(std::span<const std::string> values) override {
        effects.push_back("cheat_strings " + hexs(static_cast<std::uint32_t>(values.size())));
    }
    void on_legacy_command(std::uint16_t op, std::span<const std::byte>, std::size_t) override {
        static const std::set<std::uint16_t> reported{0x82, 0x87, 0x8f, 0x90, 0x91, 0x92, 0x96, 0x9b, 0x9c, 0x9f, 0xa1};
        if (reported.count(op)) {
            effects.push_back("legacy " + hexs(op));
        }
    }
    void on_kill_bruce_restart(std::size_t) override { effects.push_back("kill_bruce"); }
    void on_gap(std::size_t, std::uint32_t, std::uint16_t) override { effects.push_back("gap"); }
    void set_career_flag(std::uint16_t f) override { effects.push_back("career_flag " + hexs(f)); }
    [[nodiscard]] bool career_flag(std::uint16_t f) const override {
        if (retail->trick_config) {
            const Bytes& d = retail->trick_config->data;
            const std::size_t at = 0x26 + level;
            return at < d.size() && (d[at] & (1u << f)) != 0;
        }
        return (call->entry(0x0056a838, 1) & (1u << f)) != 0;
    }
    [[nodiscard]] bool goal_complete(std::uint16_t g) const override {
        if (!retail->trick_config) {
            return false;
        }
        const Bytes& d = retail->trick_config->data;
        const std::size_t at = 0xc + level * 2;
        return at + 2 <= d.size() && (le(d, at, 2) & (1u << g)) != 0;
    }
    void on_diagnostic(std::string_view) override { effects.push_back("diag"); }
    void on_unknown_command(std::uint16_t, std::uint32_t, std::size_t, std::span<const std::byte>) override {}
};

// ---- the synthetic file ------------------------------------------------------------------------

void put16(Bytes& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>(v));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
}
void put32(Bytes& b, std::uint32_t v) {
    put16(b, v & 0xffff);
    put16(b, v >> 16);
}

struct Synthetic {
    Bytes bytes;
    std::uint32_t stream_offset{};
    std::vector<std::uint16_t> originals;
};

Synthetic build_file(const Call& call, const Retail& retail, std::uint32_t checksum) {
    Synthetic s;
    const std::size_t targets = retail.links.size();
    const std::size_t restarts = retail.restart_names.size();
    const std::size_t nodes = 1 + targets + restarts + 1;
    s.originals = retail.links;
    const std::size_t first = 12 + nodes * 4;

    Bytes node0;
    put16(node0, 6);
    put16(node0, static_cast<std::uint32_t>(targets));
    for (std::size_t i = 0; i < targets; ++i) {
        put16(node0, static_cast<std::uint32_t>(1 + i));
    }
    while ((first + node0.size()) & 3) {
        node0.push_back(0);
    }
    put32(node0, checksum);
    while (((first + node0.size()) & 3) != (call.stream & 3)) {
        node0.push_back(0);
    }
    const std::uint32_t stream_offset = static_cast<std::uint32_t>(first + node0.size());
    node0.insert(node0.end(), call.stream_bytes.begin(), call.stream_bytes.end());
    if (node0.size() & 1) {
        node0.push_back(0);
    }

    std::vector<Bytes> bodies;
    bodies.push_back(node0);
    for (std::size_t i = 0; i < targets; ++i) {
        Bytes t;
        put16(t, 1);
        put16(t, 0);
        put16(t, 0);
        put16(t, 0);
        t.insert(t.end(), 8, 0);
        bodies.push_back(t);
    }
    std::size_t offset = first;
    for (const Bytes& b : bodies) {
        offset += b.size();
    }
    for (const std::string& name : retail.restart_names) {
        Bytes r;
        put16(r, 8);
        put16(r, 0);
        const std::size_t position = (offset + 7) & ~static_cast<std::size_t>(3);
        while (offset + r.size() < position + 18) {
            r.push_back(0);
        }
        r.insert(r.end(), name.begin(), name.end());
        r.push_back(0);
        r.push_back(0);
        if (r.size() & 1) {
            r.push_back(0);
        }
        offset += r.size();
        bodies.push_back(std::move(r));
    }
    Bytes end;
    put16(end, 0xff);
    put16(end, 0);
    bodies.push_back(end);

    s.bytes.insert(s.bytes.end(), {'_', 'T', 'R', 'G'});
    put32(s.bytes, 2);
    put32(s.bytes, static_cast<std::uint32_t>(nodes));
    std::size_t at = first;
    for (const Bytes& b : bodies) {
        put32(s.bytes, static_cast<std::uint32_t>(at));
        at += b.size();
    }
    for (const Bytes& b : bodies) {
        s.bytes.insert(s.bytes.end(), b.begin(), b.end());
    }
    s.stream_offset = stream_offset;
    return s;
}

// ---- known differences -------------------------------------------------------------------------

// A difference between the model and retail that is understood. Returns its cause, or null.
const char* known_difference(
    const Call& call, const Retail&, const std::vector<std::string>& want, const std::vector<std::string>& got) {
    // Opcode 0xc9 (gap). Retail asserts that the command point's checksum differs from the gap's own
    // ("Gap command has own checksum"); the model asserts the opposite and drops the command, where
    // retail registers the gap in the skater's slot tables (+0x2f74 ids, +0x2fc4 definition indices) and
    // the model would report on_gap. Everything else in the call agrees once the model's diag is removed.
    if (call.stream_bytes.size() >= 2 && le(call.stream_bytes, 0, 2) == 0xc9) {
        std::vector<std::string> trimmed = got;
        const auto diag = std::find(trimmed.begin(), trimmed.end(), "diag");
        if (diag != trimmed.end()) {
            trimmed.erase(diag);
            if (trimmed == want) {
                return "0xc9 gap: the model inverts the own-checksum assertion and has no skater slot tables";
            }
        }
    }
    return nullptr;
}

// The retail stores that leave the stored word unchanged are not in the write diff. Drop the model's
// state-setting effects that would store the value the word already holds (at entry, or after an
// earlier effect of the same call).
std::vector<std::string> drop_idempotent(const Call& call, const std::vector<std::string>& in) {
    const std::uint32_t camera = call.entry(kCameraList);
    const std::uint32_t skater = call.entry(kSkater2);
    std::map<std::uint32_t, std::uint32_t> current;
    auto holds = [&](std::uint32_t address, std::size_t size, std::uint32_t value) {
        auto it = current.find(address);
        std::optional<std::uint32_t> old = it != current.end() ? std::optional<std::uint32_t>(it->second)
                                                               : call.word(address, size);
        current[address] = value;
        return old && *old == value;
    };
    std::vector<std::string> out;
    for (const std::string& e : in) {
        std::istringstream words(e);
        std::string kind, a, b;
        words >> kind >> a >> b;
        bool drop = false;
        if (kind == "script_value") {
            drop = holds(0x563a60, 4, hex32(a));
        } else if (kind == "initial") {
            drop = holds(0x5685ec, 4, hex32(a));
        } else if (kind == "level_value") {
            drop = holds(0x56114c, 4, hex32(a));
        } else if (kind == "global" && a == "aa") {
            drop = holds(0x535af8, 4, hex32(b));
        } else if (kind == "global" && a == "a6") {
            drop = holds(0x564368, 2, hex32(b));
        } else if (kind == "global" && a == "a9") {
            drop = holds(0x56436a, 2, hex32(b));
        } else if (kind == "cobj" && camera != 0 && a == "a0") {
            drop = holds(camera + 0x504, 4, hex32(b));
        } else if (kind == "cobj" && camera != 0 && a == "a8") {
            drop = holds(camera + 0x434, 2, hex32(b));
        } else if (kind == "cobj" && camera != 0 && a == "ac") {
            drop = holds(camera + 0x436, 2, hex32(b));
        } else if (kind == "cobj_copy" && camera != 0) {
            const auto src = call.word(camera + 0x3a4, 4);
            drop = src && holds(camera + 0x3dc, 4, *src);
        } else if (kind == "cskater" && skater != 0 && a == "a3") {
            drop = holds(skater + 0x3198, 4, hex32(b));
        } else if (kind == "cskater" && skater != 0 && a == "b1") {
            drop = holds(skater + 0x319c, 4, hex32(b));
        }
        if (!drop) {
            out.push_back(e);
        }
    }
    return out;
}

bool is_camera_effect(const std::string& e) {
    return e.rfind("cobj", 0) == 0 || e.rfind("legacy 82", 0) == 0 || e.rfind("legacy 87", 0) == 0
        || e.rfind("legacy 8f", 0) == 0 || e.rfind("legacy 90", 0) == 0 || e.rfind("legacy 91", 0) == 0
        || e.rfind("legacy 92", 0) == 0 || e.rfind("legacy a1", 0) == 0;
}
bool is_skater_effect(const std::string& e) { return e.rfind("cskater", 0) == 0; }

bool is_state_write(const std::string& e) {
    static const char* const kinds[] = {"script_value", "level_value", "initial", "global", "cobj", "cskater"};
    for (const char* kind : kinds) {
        if (e.rfind(kind, 0) == 0) {
            return true;
        }
    }
    return false;
}

// Stores that retail makes between two callee calls arrive in one write diff, in address order, not in
// program order: compare each run of consecutive state writes as a set.
std::vector<std::string> normalize(std::vector<std::string> list) {
    for (std::size_t i = 0; i < list.size();) {
        if (!is_state_write(list[i])) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < list.size() && is_state_write(list[j])) {
            ++j;
        }
        std::sort(list.begin() + static_cast<std::ptrdiff_t>(i), list.begin() + static_cast<std::ptrdiff_t>(j));
        i = j;
    }
    return list;
}

std::string show(const std::vector<std::string>& list) {
    std::string out;
    for (const auto& e : list) {
        out += "    " + e + "\n";
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: trg_interpreter_retail_trace_test FIXTURE [--mutate]\n";
        return 2;
    }
    const bool mutate = argc > 2 && std::string(argv[2]) == "--mutate";
    std::vector<Call> calls = parse_fixture(argv[1]);
    CHECK(!calls.empty());
    bool mutated = false;
    std::size_t passed = 0, failed = 0, known = 0, effects_total = 0;
    std::set<std::string> unmapped;
    std::map<std::string, std::size_t> by_cause;
    std::map<std::string, std::size_t> kinds;

    for (Call& call : calls) {
        Retail retail = retail_effects(call);
        for (const auto& u : retail.unmapped) {
            unmapped.insert(u);
        }
        if (retail.remote_player || retail.gap) {
            std::cout << "call " << call.n << ": skipped (" << (retail.gap ? "gap command" : "remote player") << ")\n";
            continue;
        }
        if (mutate && !mutated && !retail.effects.empty() && call.stream_bytes.size() > 4) {
            // flip the first operand bit of the first stream word that is not the terminator
            call.stream_bytes[2] ^= 0x01;
            mutated = true;
        }
        std::uint32_t checksum = 0;
        if (retail.command_point) {
            checksum = le(retail.command_point->data, 0xc, 4);
        }
        const Synthetic synth = build_file(call, retail, checksum);
        ModelRecorder rec;
        rec.originals = synth.originals;
        rec.call = &call;
        rec.retail = &retail;
        rec.level = call.entry(kLevel);

        CommandPointRuntime point{};
        CommandPointRuntime* point_arg = nullptr;
        if (retail.command_point) {
            const Bytes& d = retail.command_point->data;
            point.state0 = static_cast<std::uint8_t>(le(d, 4, 1));
            point.state1 = static_cast<std::uint8_t>(le(d, 5, 1));
            point.initialized = static_cast<std::uint8_t>(le(d, 6, 1));
            point.pulse_count = static_cast<std::uint8_t>(le(d, 7, 1));
            point.state = static_cast<std::uint16_t>(le(d, 8, 2));
            point.checksum = checksum;
            point_arg = &point;
        }
        try {
            TriggerRuntime runtime(
                TrgFile::parse(std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(synth.bytes.data()), synth.bytes.size())),
                rec);
            runtime.dispatch_stream(0, synth.stream_offset, point_arg, call.mode != 0,
                                    call.entry(kModes + 4) == 2);
        } catch (const FormatError& error) {
            rec.effects.push_back(std::string("format_error ") + error.what());
        }

        std::vector<std::string> got;
        for (const auto& e : rec.effects) {
            if (!retail.camera_present && is_camera_effect(e)) {
                continue;  // retail does nothing without a camera object; the model reports the command
            }
            if (!retail.skater_present && is_skater_effect(e)) {
                continue;
            }
            got.push_back(e);
        }
        got = drop_idempotent(call, got);
        // the command point's counters after the run
        if (retail.command_point) {
            Bytes record = retail.command_point->data;
            for (const Event& ev : call.events) {
                if (ev.kind != 'w') {
                    continue;
                }
                for (const auto& ch : ev.changes) {
                    if (ch.address >= retail.command_point->eax && ch.address < retail.command_point->eax + record.size()) {
                        for (std::size_t i = 0; i < ch.after.size(); ++i) {
                            record[ch.address - retail.command_point->eax + i] = ch.after[i];
                        }
                    }
                }
            }
            std::ostringstream state;
            state << "command_point init=" << int(le(record, 6, 1)) << " count=" << int(le(record, 7, 1))
                  << " state=" << le(record, 8, 2);
            std::ostringstream model;
            model << "command_point init=" << int(point.initialized) << " count=" << int(point.pulse_count)
                  << " state=" << point.state;
            retail.effects.push_back(state.str());
            got.push_back(model.str());
        }
        effects_total += retail.effects.size();
        for (const auto& e : retail.effects) {
            ++kinds[e.substr(0, e.find(' '))];
        }
        if (normalize(got) == normalize(retail.effects)) {
            ++passed;
            continue;
        }
        if (const char* cause = known_difference(call, retail, retail.effects, got)) {
            ++known;
            ++by_cause[cause];
            continue;
        }
        ++failed;
        std::cout << "call " << call.n << " (index " << std::hex << call.index << " mode " << call.mode << std::dec
                  << ", stream " << std::hex << call.stream << std::dec << "): effects differ\n"
                  << "  retail:\n" << show(retail.effects) << "  model:\n" << show(got);
    }

    std::cout << calls.size() << " calls: " << passed << " match, " << known << " known differences, " << failed
              << " fail; " << effects_total << " retail effects\n";
    for (const auto& [kind, count] : kinds) {
        std::cout << "  effect " << kind << ": " << count << "\n";
    }
    for (const auto& [cause, count] : by_cause) {
        std::cout << "  known difference (" << count << "): " << cause << "\n";
    }
    for (const auto& u : unmapped) {
        std::cout << "  UNMAPPED: " << u << "\n";
    }
    if (mutate && !mutated) {
        std::cout << "mutation had no call to change\n";
        return 0;  // the WILL_FAIL entry must not pass by accident
    }
    return failed == 0 && unmapped.empty() ? 0 : 1;
}
