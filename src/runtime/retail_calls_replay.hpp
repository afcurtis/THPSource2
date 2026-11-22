#pragma once

// The replay half of the forced-call differential pattern (Phases 4.8 and 4.9): a fixture made by
// scripts/make_*_calls_fixture.py from a GDB probe (re/gdb/probes/*_calls.py) holds the retail
// memory regions the ported code touches (R lines) and, for every case (C line), the writes that
// made a random legal state, the arguments, what the retail function returned, which bytes it
// wrote, which outside services it reached (in order) and the values its stubbed services
// returned. Each case is replayed from that state through a caller-supplied adapter table; the
// return value, every written byte and every service call must match.
//
// A test supplies its context type (memory + services + the ported objects) and an adapter table
// keyed by retail address, and calls replay::main.
//
// Exit codes: 0 all match, 1 mismatch, 2 usage/format error.
#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace opentony::runtime::replay {

using Args = std::vector<std::uint32_t>;

inline int S(std::uint32_t v) { return static_cast<std::int32_t>(v); }
inline std::uint32_t U(bool v) { return v ? 1u : 0u; }
inline std::uint32_t U(int v) { return static_cast<std::uint32_t>(v); }
inline std::uint32_t U(std::uint32_t v) { return v; }

inline std::vector<std::string> split(const std::string& text, char sep) {
    std::vector<std::string> out;
    std::string item;
    std::istringstream stream(text);
    while (std::getline(stream, item, sep)) {
        out.push_back(item);
    }
    return out;
}

inline std::uint32_t parse_hex(const std::string& s) { return static_cast<std::uint32_t>(std::stoull(s, nullptr, 16)); }

inline std::vector<std::uint8_t> parse_bytes(const std::string& hex) {
    std::vector<std::uint8_t> out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

inline std::string to_hex(const std::vector<std::uint8_t>& bytes) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (std::uint8_t b : bytes) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 15]);
    }
    return out;
}

// Bytes the retail function copies from uninitialised stack (a record built in a local and copied
// whole). The native port leaves them as it finds them; the replay does not compare them.
using Unspecified = std::vector<std::pair<std::uint32_t, std::uint32_t>>;   // (address, length)

// One function the replay knows: how to call the native port with the retail arguments.
template <typename Ctx>
struct Adapter {
    const char* name;
    std::uint32_t ret_mask;        // 0: the function returns nothing the callers use
    bool assert_clobbers_return;   // the return is whatever the assert routine leaves when an assert fired
    std::function<std::uint32_t(Ctx&, const Args&, std::uint32_t)> run;   // (context, stack arguments, `this`)
    std::function<Unspecified(Ctx&)> unspecified = nullptr;   // asked after the native run
    std::vector<std::string> time_dependent = {};             // services whose arguments follow the live clock: compared by name only
};

struct Call {
    std::string name;
    std::vector<std::uint32_t> args;
    std::string text;
};

inline Call parse_call(const std::string& token) {
    Call call;
    std::string head = token;
    const auto at = token.find('@');
    if (at != std::string::npos) {
        head = token.substr(0, at);
        const auto bytes = parse_bytes(token.substr(at + 1));
        call.text.assign(bytes.begin(), bytes.end());
    }
    const auto parts = split(head, ',');
    call.name = parts.at(0);
    for (std::size_t i = 1; i < parts.size(); ++i) {
        call.args.push_back(parse_hex(parts[i]));
    }
    return call;
}

inline std::string describe(const Call& call) {
    std::ostringstream out;
    out << call.name << '(';
    for (std::size_t i = 0; i < call.args.size(); ++i) {
        out << (i ? "," : "") << std::hex << call.args[i];
    }
    out << ')';
    if (!call.text.empty()) {
        bool printable = true;
        for (const char c : call.text) {
            printable = printable && c >= 0x20 && c < 0x7f;
        }
        if (printable) {
            out << " \"" << call.text << '"';
        } else {  // binary payload (a palette): show it as hex
            out << " 0x" << to_hex(std::vector<std::uint8_t>(call.text.begin(), call.text.end()));
        }
    }
    return out.str();
}

using Runs = std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>>;

// `runs` without the bytes in `skip`.
inline Runs without(const Runs& runs, const Unspecified& skip) {
    if (skip.empty()) {
        return runs;
    }
    auto skipped = [&](std::uint32_t address) {
        for (const auto& [start, length] : skip) {
            if (address >= start && address - start < length) {
                return true;
            }
        }
        return false;
    };
    Runs out;
    for (const auto& [base, bytes] : runs) {
        std::size_t i = 0;
        while (i < bytes.size()) {
            if (skipped(base + static_cast<std::uint32_t>(i))) {
                ++i;
                continue;
            }
            std::size_t j = i;
            while (j < bytes.size() && !skipped(base + static_cast<std::uint32_t>(j))) {
                ++j;
            }
            out.emplace_back(base + static_cast<std::uint32_t>(i), std::vector<std::uint8_t>(bytes.begin() + i, bytes.begin() + j));
            i = j;
        }
    }
    return out;
}

inline Runs diff_runs(const RetailMemory& before, const RetailMemory& after) {
    Runs runs;
    for (const auto& [base, old] : before.regions()) {
        const auto& now = after.regions().at(base);
        std::size_t i = 0;
        while (i < old.size()) {
            if (old[i] != now[i]) {
                std::size_t j = i;
                while (j < old.size() && old[j] != now[j]) {
                    ++j;
                }
                runs.emplace_back(base + static_cast<std::uint32_t>(i), std::vector<std::uint8_t>(now.begin() + i, now.begin() + j));
                i = j;
            } else {
                ++i;
            }
        }
    }
    return runs;
}

template <typename Ctx>
class Replay {
public:
    using Table = std::map<std::uint32_t, Adapter<Ctx>>;

    // `make` builds the context over a memory image; `unpredictable_assert` is the message address
    // of an assertion after which the retail result cannot be compared (0: none).
    Replay(const Table& table, std::function<std::unique_ptr<Ctx>(RetailMemory)> make, std::uint32_t unpredictable_assert = 0)
        : table_(table), make_(std::move(make)), unpredictable_assert_(unpredictable_assert) {}

    // called with the path of each fixture before its cases replay (a test whose ported code has a mode that depends on how the fixture was captured sets it here)
    std::function<void(const std::string&)> on_file;

    int run_files(int argc, char** argv, const char* title) {
        if (argc < 2) {
            std::cerr << "usage: " << argv[0] << " FIXTURE.txt [FIXTURE.txt ...]\n";
            return 2;
        }
        for (int i = 1; i < argc; ++i) {
            if (const int rc = replay_file(argv[i]); rc != 0) {
                return rc;
            }
        }
        std::cout << title << ": " << cases_run_ << " cases replayed, " << failures_ << " mismatches\n";
        for (const auto& [name, count] : per_function_) {
            std::cout << "  " << name << ": " << count << '\n';
        }
        return failures_ == 0 ? 0 : 1;
    }

private:
    struct Fixture {
        Runs base;
    };

    void fail(const std::string& what) {
        ++failures_;
        static const int cap = std::getenv("REPLAY_MAX") != nullptr ? std::atoi(std::getenv("REPLAY_MAX")) : 25;     // (REPLAY_MAX=n prints the first n mismatches)
        if (failures_ <= cap) {
            std::cerr << "MISMATCH: " << what << '\n';
        }
    }

    int replay_file(const std::string& path) {
        std::ifstream file(path);
        if (!file) {
            std::cerr << "cannot open " << path << '\n';
            return 2;
        }
        if (on_file) {
            on_file(path);
        }
        Fixture fx;
        std::string line;
        std::size_t index = 0;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }
            if (line[0] == 'R') {
                std::istringstream in(line);
                std::string tag, base, hex;
                in >> tag >> base >> hex;
                fx.base.emplace_back(parse_hex(base), parse_bytes(hex));
            } else if (line[0] == 'C') {
                replay_case(fx, line, index++);
            }
        }
        return 0;
    }

    void replay_case(const Fixture& fx, const std::string& line, std::size_t index) {
        std::map<std::string, std::string> kv;
        {
            std::istringstream in(line);
            std::string token;
            in >> token;  // "C"
            while (in >> token) {
                const auto eq = token.find('=');
                kv[token.substr(0, eq)] = eq == std::string::npos ? std::string() : token.substr(eq + 1);
            }
        }
        const std::uint32_t fn = parse_hex(kv.at("fn"));
        const auto found = table_.find(fn);
        if (found == table_.end()) {
            return;  // not ported (yet): the fixture may carry more functions than the test knows
        }
        const Adapter<Ctx>& adapter = found->second;
        const std::string where = std::string(adapter.name) + " case " + std::to_string(index);

        RetailMemory memory;
        for (const auto& [base, bytes] : fx.base) {
            memory.add_region(base, bytes);
        }
        for (const std::string& poke : split(kv["pokes"], ';')) {
            if (poke.empty()) {
                continue;
            }
            const auto eq = poke.find('=');
            memory.write(parse_hex(poke.substr(0, eq)), parse_bytes(poke.substr(eq + 1)));
        }
        const RetailMemory before = memory;
        auto ctx = make_(std::move(memory));
        for (const std::string& entry : split(kv["values"], ';')) {
            if (entry.empty()) {
                continue;
            }
            const auto eq = entry.find('=');
            for (const std::string& v : split(entry.substr(eq + 1), ',')) {
                ctx->svc.script(entry.substr(0, eq), static_cast<std::int32_t>(parse_hex(v)));
            }
        }
        Args args;
        for (const std::string& a : split(kv["args"], ',')) {
            if (!a.empty()) {
                args.push_back(parse_hex(a));
            }
        }

        const std::uint32_t self = kv["this"].empty() || kv["this"] == "-" ? 0 : parse_hex(kv["this"]);
        std::vector<Call> retail_calls;
        for (const std::string& token : split(kv["calls"], ';')) {
            if (!token.empty()) {
                retail_calls.push_back(parse_call(token));
            }
        }
        std::uint32_t native_ret = 0;
        try {
            native_ret = adapter.run(*ctx, args, self);
        } catch (const std::exception& e) {
            // where the native's service calls part from the retail ones before it threw is the first thing to look at
            const auto& so_far = ctx->svc.calls();
            std::size_t same = 0;
            while (same < so_far.size() && same < retail_calls.size() && so_far[same].name == retail_calls[same].name && so_far[same].args == retail_calls[same].args &&
                   so_far[same].text == retail_calls[same].text) {
                ++same;
            }
            std::ostringstream out;
            out << where << ": native threw: " << e.what() << " (after " << so_far.size() << " service calls, the first " << same << " as the retail ones";
            if (same < so_far.size() && same < retail_calls.size()) {
                out << "; then native " << describe({so_far[same].name, so_far[same].args, so_far[same].text}) << " retail " << describe(retail_calls[same]);
            } else if (same < retail_calls.size()) {
                out << "; retail goes on with " << describe(retail_calls[same]);
            }
            out << ')';
            fail(out.str());
            return;
        }
        ++cases_run_;
        ++per_function_[adapter.name];
        bool asserted = false;
        // "Gap %s not found": the ordinal the caller goes on to use is whatever the assert routine
        // leaves in eax. The retail run stubs the routine (eax unchanged), so nothing after it can be
        // compared; only that the assert fired (the calls below) is.
        bool unpredictable = false;
        for (const Call& call : retail_calls) {
            asserted = asserted || call.name == "assert";
            unpredictable = unpredictable ||
                            (call.name == "assert" && !call.args.empty() && unpredictable_assert_ != 0 && call.args[0] == unpredictable_assert_);
        }

        if (kv["ret"] != "-" && adapter.ret_mask != 0 && !unpredictable && !(asserted && adapter.assert_clobbers_return)) {
            const std::uint32_t retail_ret = parse_hex(kv["ret"]);
            if ((retail_ret & adapter.ret_mask) != (native_ret & adapter.ret_mask)) {
                std::ostringstream out;
                out << where << ": return native 0x" << std::hex << (native_ret & adapter.ret_mask) << " retail 0x"
                    << (retail_ret & adapter.ret_mask);
                fail(out.str());
            }
        }

        const auto& native_calls = ctx->svc.calls();
        bool calls_ok = native_calls.size() == retail_calls.size();
        for (std::size_t i = 0; calls_ok && i < native_calls.size(); ++i) {
            const bool timed = std::find(adapter.time_dependent.begin(), adapter.time_dependent.end(), native_calls[i].name) != adapter.time_dependent.end();
            calls_ok = native_calls[i].name == retail_calls[i].name && (timed || native_calls[i].args == retail_calls[i].args) &&
                       native_calls[i].text == retail_calls[i].text;
        }
        if (!calls_ok) {
            std::ostringstream out;
            out << where << ": service calls differ. native:";
            for (const auto& c : native_calls) {
                out << ' ' << describe({c.name, c.args, c.text});
            }
            out << " | retail:";
            for (const auto& c : retail_calls) {
                out << ' ' << describe(c);
            }
            fail(out.str());
        }

        Runs retail_writes;
        for (const std::string& w : split(kv["writes"], ';')) {
            if (!w.empty()) {
                const auto eq = w.find('=');
                retail_writes.emplace_back(parse_hex(w.substr(0, eq)), parse_bytes(w.substr(eq + 1)));
            }
        }
        const Unspecified skip = adapter.unspecified ? adapter.unspecified(*ctx) : Unspecified{};
        const auto native_writes = without(diff_runs(before, ctx->mem), skip);
        retail_writes = without(retail_writes, skip);
        if (!unpredictable && native_writes != retail_writes) {
            std::ostringstream out;
            out << where << ": memory writes differ (" << std::dec << native_writes.size() << " native runs, "
                << retail_writes.size() << " retail runs).";
            if (native_writes.size() + retail_writes.size() <= 16) {
                out << " native:";
                for (const auto& [addr, bytes] : native_writes) {
                    out << " " << std::hex << addr << "=" << to_hex(bytes);
                }
                out << " | retail:";
                for (const auto& [addr, bytes] : retail_writes) {
                    out << " " << std::hex << addr << "=" << to_hex(bytes);
                }
            } else {
                // many runs: show only the first place they part ways
                std::size_t i = 0;
                while (i < native_writes.size() && i < retail_writes.size() && native_writes[i] == retail_writes[i]) {
                    ++i;
                }
                out << " first difference at run " << std::dec << i << ":";
                auto show = [&](const char* who, const Runs& runs) {
                    out << ' ' << who << ' ';
                    if (i < runs.size()) {
                        out << std::hex << runs[i].first << "=" << to_hex(runs[i].second).substr(0, 96);
                    } else {
                        out << "(none)";
                    }
                };
                show("native", native_writes);
                show("| retail", retail_writes);
            }
            if (std::getenv("REPLAY_DEBUG") != nullptr && std::getenv("REPLAY_RUNS") != nullptr) {
                // REPLAY_RUNS=n: the runs of both sides around the first place they part ways (n before it, up to 2n after)
                std::size_t first = 0;
                while (first < native_writes.size() && first < retail_writes.size() && native_writes[first] == retail_writes[first]) {
                    ++first;
                }
                const std::size_t span = static_cast<std::size_t>(std::atoi(std::getenv("REPLAY_RUNS")));
                const std::size_t from = first > span ? first - span : 0;
                auto dump = [&](const char* who, const Runs& runs) {
                    out << "\n  " << who << " runs " << std::dec << from << "..:";
                    for (std::size_t k = from; k < runs.size() && k < from + 3 * span; ++k) {
                        out << " " << std::hex << runs[k].first << "=" << to_hex(runs[k].second).substr(0, 24);
                    }
                };
                dump("native", native_writes);
                dump("retail", retail_writes);
            }
            if (std::getenv("REPLAY_DEBUG") != nullptr) {
                // the individual bytes the two sides disagree on (an absent byte is one the side left unchanged)
                std::map<std::uint32_t, int> native_bytes, retail_bytes;
                for (const auto& [base, bytes] : native_writes) {
                    for (std::size_t i = 0; i < bytes.size(); ++i) native_bytes[base + static_cast<std::uint32_t>(i)] = bytes[i];
                }
                for (const auto& [base, bytes] : retail_writes) {
                    for (std::size_t i = 0; i < bytes.size(); ++i) retail_bytes[base + static_cast<std::uint32_t>(i)] = bytes[i];
                }
                std::map<std::uint32_t, int> all = native_bytes;
                all.insert(retail_bytes.begin(), retail_bytes.end());
                int shown = 0;
                out << "\n  differing bytes:";
                for (const auto& entry : all) {
                    const auto n = native_bytes.find(entry.first);
                    const auto r = retail_bytes.find(entry.first);
                    const int nv = n == native_bytes.end() ? -1 : n->second;
                    const int rv = r == retail_bytes.end() ? -1 : r->second;
                    if (nv != rv && shown++ < 24) {
                        out << " " << std::hex << entry.first << ":native=" << (nv < 0 ? std::string("-") : std::to_string(nv))
                            << "/retail=" << (rv < 0 ? std::string("-") : std::to_string(rv))
                            << "/before=" << std::to_string(before.u8(entry.first));
                    }
                }
            }
            fail(out.str());
        }
    }

    const Table& table_;
    std::function<std::unique_ptr<Ctx>(RetailMemory)> make_;
    std::uint32_t unpredictable_assert_;
    int failures_ = 0;
    int cases_run_ = 0;
    std::map<std::string, int> per_function_;
};

}  // namespace opentony::runtime::replay
