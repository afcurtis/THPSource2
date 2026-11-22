// The free look's read sites in the camera Kit (free_look.hpp), on retail camera states: the cases of the camera forced-call fixtures (camera_retail_calls_test.cpp) replayed
// natively, twice each, and compared byte for byte and call for call:
//
//   opentony_free_look_camera_test FIXTURE.txt [FIXTURE.txt ...]
//
//   yaw       Y in the look  ==  Y added to the camera's own spin half word (+0x5b4)      follow_target, mode_2, update (update steps the look itself: OT_FREELOOK_FIXED's path)
//   pitch     P in the look  ==  P added to the pitch half word 0x00524a94                 smooth_and_validate
//             (C7: not in the landing steering, which the game gives no pitch: there the look's pitch is still applied and only the calls and a changed picture are checked)
//   distance  D in the look  ==  D added to the distance word 0x00524a98                   smooth_and_validate, while the camera is not in its vert-ramp branch (that one reads
//                                                                                          the word again, without the look)
//   step      S in the look  ==  S as the steering argument (S above the game's)          smooth_and_validate
//   at rest   switched on with no look  ==  switched off                                    all four
//
// Each comparison also counts the cases in which the offset changed what the camera wrote, and fails when there is none.
#include "camera_kit.hpp"
#include "free_look.hpp"
#include "retail_calls_replay.hpp"

#include <cstdio>
#include <functional>
#include <memory>
#include <string>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;
using namespace opentony::runtime::camerakit;
using opentony::runtime::pcplatform::kScratchBase;
using opentony::runtime::pcplatform::kScratchBytes;
namespace fl = opentony::runtime::freelook;

namespace {

constexpr std::uint32_t kFollowTarget = 0x00410610;
constexpr std::uint32_t kMode2 = 0x004113f0;
constexpr std::uint32_t kSmoothAndValidate = 0x0040e090;
constexpr std::uint32_t kUpdate = 0x0040f850;
constexpr std::uint32_t kSpin = 0x5b4;
constexpr std::uint32_t kPlayer = 0xc8;
constexpr std::uint32_t kPreparationCounter = 0x60c;
constexpr std::uint32_t kPitchWord = 0x00524a94;
constexpr std::uint32_t kDistanceWord = 0x00524a98;
constexpr std::uint32_t kDebugMode = 0x0056a8e0;

struct Ctx {     // as camera_retail_calls_test.cpp's
    RetailMemory mem;
    RetailServices svc;
    CameraKit camera;
    explicit Ctx(RetailMemory memory) : mem(std::move(memory)), camera(mem, svc) {
        if (!mem.contains(kScratchBase, 0x10)) {
            mem.add_region(kScratchBase, kScratchBytes);
        }
        if (!mem.contains(0x7ff0f000, 0x10)) {
            mem.add_region(0x7ff0f000, 0x200);
        }
        if (!mem.contains(0x00568638, 4) && !mem.contains(0x0056881c, 0x40)) {
            mem.add_region(0x00568638, 4);
            mem.add_region(0x0056881c, 0x40);
        }
    }
};

struct Case {
    std::uint32_t fn = 0;
    std::uint32_t self = 0;
    Args args;
    Runs pokes;
    std::vector<std::pair<std::string, std::int32_t>> values;
};

struct Outcome {
    std::unique_ptr<Ctx> ctx;
    std::string threw;
};

int g_failures = 0;

void fail(const std::string& what) {
    if (++g_failures <= 20) {
        std::printf("MISMATCH: %s\n", what.c_str());
    }
}

// one native run of the case: `prepare` edits the memory first, `host` sets the free look
Outcome run(const Runs& base, const Case& c, const std::function<void(RetailMemory&, Args&)>& prepare, const std::function<void()>& host) {
    RetailMemory memory;
    for (const auto& [address, bytes] : base) {
        memory.add_region(address, bytes);
    }
    for (const auto& [address, bytes] : c.pokes) {
        memory.write(address, bytes);
    }
    Args args = c.args;
    prepare(memory, args);
    Outcome out;
    out.ctx = std::make_unique<Ctx>(std::move(memory));
    for (const auto& [name, value] : c.values) {
        out.ctx->svc.script(name, value);
    }
    fl::reset();
    host();
    try {
        CameraKit& g = out.ctx->camera;
        switch (c.fn) {
        case kFollowTarget: g.follow_target(c.self); break;
        case kMode2: g.mode_2(c.self); break;
        case kSmoothAndValidate: g.smooth_and_validate(c.self, args.at(0)); break;
        case kUpdate: g.update(c.self); break;
        default: break;
        }
    } catch (const std::exception& e) {
        out.threw = e.what();
    }
    fl::reset();
    return out;
}

bool same_calls(const Outcome& a, const Outcome& b) {
    const auto& x = a.ctx->svc.calls();
    const auto& y = b.ctx->svc.calls();
    if (x.size() != y.size()) {
        return false;
    }
    for (std::size_t i = 0; i < x.size(); ++i) {
        if (x[i].name != y[i].name || x[i].args != y[i].args || x[i].text != y[i].text) {
            return false;
        }
    }
    return true;
}

struct Tally {
    int compared = 0;
    int changed = 0;
};

// a (the game's own word holding the offset) against b (the look): the same bytes outside `skip`, the same calls. `plain` is the run without either, to count the cases the
// offset made a difference in
void compare(const std::string& where, const Outcome& a, const Outcome& b, const Outcome& plain, const Unspecified& skip, Tally& tally) {
    ++tally.compared;
    if (a.threw != b.threw) {
        fail(where + ": one side threw: '" + a.threw + "' / '" + b.threw + "'");
        return;
    }
    const Runs differ = without(diff_runs(a.ctx->mem, b.ctx->mem), skip);
    if (!differ.empty()) {
        char text[160];
        std::snprintf(text, sizeof text, ": %zu byte runs differ, the first at 0x%08x", differ.size(), differ.front().first);
        fail(where + text);
        if (std::getenv("FREE_LOOK_DEBUG") != nullptr) {
            for (std::size_t k = 0; k < differ.size() && k < 8; ++k) {
                const auto& [address, bytes] = differ[k];
                std::printf("    0x%08x: a %s b %s\n", address, to_hex(a.ctx->mem.bytes(address, bytes.size())).c_str(), to_hex(bytes).c_str());
            }
        }
    }
    if (!same_calls(a, b)) {
        fail(where + ": the service calls differ");
    }
    if (!without(diff_runs(plain.ctx->mem, b.ctx->mem), skip).empty()) {
        ++tally.changed;
    }
}

void report(const char* name, const Tally& tally, bool changes = true) {
    std::printf("  %-34s %4d cases, %4d changed by the look\n", name, tally.compared, tally.changed);
    if (changes && tally.compared > 0 && tally.changed == 0) {
        fail(std::string(name) + ": the look changed nothing in any case");
    }
    if (!changes && tally.changed != 0) {
        fail(std::string(name) + ": changed what the camera wrote");
    }
}

void hold_look(const fl::Look& look) {     // the look of both players, as advance would leave it (follow_target, mode_2 and smooth_and_validate do not step it)
    fl::Config on;
    on.enabled = true;
    fl::set_config(on);
    fl::state().look[0] = look;
    fl::state().look[1] = look;
    fl::refresh_moved();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s FIXTURE.txt [FIXTURE.txt ...]\n", argv[0]);
        return 2;
    }
    const auto nothing = [](RetailMemory&, Args&) {};
    const auto off = [] {};
    const auto on_at_rest = [] {
        fl::Config on;
        on.enabled = true;
        fl::set_config(on);
    };
    Tally yaw_tally, update_tally, pitch_tally, landing_tally, distance_tally, step_tally, rest_tally;
    int cases = 0;
    for (int file_index = 1; file_index < argc; ++file_index) {
        std::ifstream file(argv[file_index]);
        if (!file) {
            std::fprintf(stderr, "cannot open %s\n", argv[file_index]);
            return 2;
        }
        Runs base;
        std::string line;
        std::size_t index = 0;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }
            if (line[0] == 'R') {
                std::istringstream in(line);
                std::string tag, address, hex;
                in >> tag >> address >> hex;
                base.emplace_back(parse_hex(address), parse_bytes(hex));
                continue;
            }
            if (line[0] != 'C') {
                continue;
            }
            const std::size_t case_index = index++;
            std::map<std::string, std::string> kv;
            {
                std::istringstream in(line);
                std::string token;
                in >> token;
                while (in >> token) {
                    const auto eq = token.find('=');
                    kv[token.substr(0, eq)] = eq == std::string::npos ? std::string() : token.substr(eq + 1);
                }
            }
            Case c;
            c.fn = parse_hex(kv.at("fn"));
            if (c.fn != kFollowTarget && c.fn != kMode2 && c.fn != kSmoothAndValidate && c.fn != kUpdate) {
                continue;
            }
            c.self = kv["this"].empty() || kv["this"] == "-" ? 0 : parse_hex(kv["this"]);
            for (const std::string& poke : split(kv["pokes"], ';')) {
                if (!poke.empty()) {
                    const auto eq = poke.find('=');
                    c.pokes.emplace_back(parse_hex(poke.substr(0, eq)), parse_bytes(poke.substr(eq + 1)));
                }
            }
            for (const std::string& entry : split(kv["values"], ';')) {
                if (!entry.empty()) {
                    const auto eq = entry.find('=');
                    for (const std::string& v : split(entry.substr(eq + 1), ',')) {
                        c.values.emplace_back(entry.substr(0, eq), static_cast<std::int32_t>(parse_hex(v)));
                    }
                }
            }
            for (const std::string& a : split(kv["args"], ',')) {
                if (!a.empty()) {
                    c.args.push_back(parse_hex(a));
                }
            }
            ++cases;
            const std::string where = std::string(argv[file_index]) + " case " + std::to_string(case_index) + " fn 0x" + kv.at("fn");
            const Outcome plain = run(base, c, nothing, off);
            compare(where + " at rest", run(base, c, nothing, off), run(base, c, nothing, on_at_rest), plain, {}, rest_tally);

            if (c.fn == kFollowTarget || c.fn == kMode2 || c.fn == kUpdate) {
                // update steps the look after its early returns: with the debug camera on (the fixtures' states are random; never in play) its short path runs the follow and the
                // smoothing with the look as it was, here none. The debug word is the one update leaves (its key chord may switch it)
                const bool reaches_look = c.fn != kUpdate || plain.ctx->mem.u32(kDebugMode) == 0;
                for (const std::int32_t yaw : {0x400, -0x555, 0x800}) {
                    const auto turned_spin = [&](RetailMemory& m, Args&) {
                        if (reaches_look && m.u16(c.self + kPlayer) < fl::kPlayers) {   // (a camera of no player takes no look)
                            m.set16(c.self + kSpin, m.u16(c.self + kSpin) + static_cast<std::uint32_t>(yaw));
                        }
                    };
                    fl::Look look;
                    look.yaw = yaw;
                    const Unspecified spin_word = {{c.self + kSpin, 2}};
                    if (c.fn == kUpdate) {
                        compare(where + " yaw (update, fixed)", run(base, c, turned_spin, off), run(base, c, nothing, [&] { fl::set_fixed(look); }), plain, spin_word, update_tally);
                    } else {
                        compare(where + " yaw", run(base, c, turned_spin, off), run(base, c, nothing, [&] { hold_look(look); }), plain, spin_word, yaw_tally);
                    }
                }
            }
            if (c.fn == kSmoothAndValidate) {
                for (const std::int32_t pitch : {0x100, -0x100, 0x200}) {
                    fl::Look look;
                    look.pitch = pitch;
                    const auto raised = [&](RetailMemory& m, Args&) { m.set16(kPitchWord, m.u16(kPitchWord) + static_cast<std::uint32_t>(pitch)); };
                    const Outcome with_word = run(base, c, raised, off);
                    const Outcome with_look = run(base, c, nothing, [&] { hold_look(look); });
                    if (!without(diff_runs(plain.ctx->mem, with_word.ctx->mem), {{kPitchWord, 2}}).empty()) {
                        compare(where + " pitch", with_word, with_look, plain, {{kPitchWord, 2}}, pitch_tally);
                    } else {
                        // the game's pitch word is not read here: the landing steering turns the target by no pitch (C7: the look's stays, so a held look does not snap for the landing).
                        // What must hold: no service call that the plain run does not make, and the look changes the picture
                        ++landing_tally.compared;
                        if (!same_calls(plain, with_look)) {
                            fail(where + " pitch (landing steering): the service calls differ from the plain run");
                        }
                        if (!without(diff_runs(plain.ctx->mem, with_look.ctx->mem), {}).empty()) {
                            ++landing_tally.changed;
                        }
                    }
                }
                RetailMemory probe;
                for (const auto& [address, bytes] : base) {
                    probe.add_region(address, bytes);
                }
                for (const auto& [address, bytes] : c.pokes) {
                    probe.write(address, bytes);
                }
                if (probe.s32(c.self + kPreparationCounter) < 4) {
                    for (const std::int32_t distance : {-0x40, 0x30}) {
                        fl::Look look;
                        look.distance = distance;
                        const auto longer = [&](RetailMemory& m, Args&) { m.set32(kDistanceWord, m.u32(kDistanceWord) + static_cast<std::uint32_t>(distance)); };
                        compare(where + " distance", run(base, c, longer, off), run(base, c, nothing, [&] { hold_look(look); }), plain, {{kDistanceWord, 4}}, distance_tally);
                    }
                }
                fl::Look look;
                look.step = 0x500;
                const auto faster = [&](RetailMemory&, Args& args) { args.at(0) = std::max<std::uint32_t>(args.at(0), 0x500); };   // (the fixtures' states are random: some steps are larger)
                compare(where + " step", run(base, c, faster, off), run(base, c, nothing, [&] { hold_look(look); }), plain, {}, step_tally);
            }
        }
    }
    std::printf("free look in the camera Kit: %d retail cases\n", cases);
    report("at rest (on, nothing held)", rest_tally, false);
    report("yaw (follow_target, mode_2)", yaw_tally);
    report("yaw (update, the fixed look)", update_tally);
    report("pitch (smooth_and_validate)", pitch_tally);
    report("pitch (the landing steering)", landing_tally);
    report("distance (smooth_and_validate)", distance_tally);
    report("step (smooth_and_validate)", step_tally);
    std::printf("%d mismatches\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
