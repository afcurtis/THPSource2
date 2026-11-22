#pragma once

// The retail TRG_InterpretCommandStream trace fixture (scripts/make_trg_interpreter_fixture.py
// documents the format): its parser, shared by the replay of the value-owned model
// (trg_interpreter_retail_trace_test.cpp) and of its retail-memory form
// (src/runtime/world_trig_commands_trace_test.cpp).

#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace opentony::trg::trg_trace_fixture {

using Bytes = std::vector<std::uint8_t>;

inline Bytes unhex(const std::string& text) {
    Bytes bytes;
    if (text == "-") {
        return bytes;
    }
    for (std::size_t i = 0; i + 1 < text.size(); i += 2) {
        bytes.push_back(static_cast<std::uint8_t>(std::stoul(text.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

inline std::uint32_t hex32(const std::string& text) { return static_cast<std::uint32_t>(std::stoul(text, nullptr, 16)); }

inline std::uint32_t le(const Bytes& b, std::size_t o, std::size_t n) {
    std::uint32_t v = 0;
    for (std::size_t i = 0; i < n && o + i < b.size(); ++i) {
        v |= static_cast<std::uint32_t>(b[o + i]) << (8 * i);
    }
    return v;
}

inline std::string hexs(std::uint32_t v) {
    std::ostringstream out;
    out << std::hex << v;
    return out.str();
}

struct Event {
    char kind{};                       // c r w k x v (v: the out-vector a TRG_ParseNodes callee wrote)
    std::uint32_t callee{};            // c, r
    std::uint32_t site{};              // c
    std::uint32_t ecx{}, edx{};        // c
    std::array<std::uint32_t, 6> arg{}; // c
    std::vector<Bytes> pointees;       // c
    std::uint32_t eax{};               // r, x
    Bytes data;                        // r
    struct Change { std::uint32_t address; Bytes before, after; };
    std::vector<Change> changes;       // w k
};

struct Call {
    std::uint32_t n{}, depth{}, index{}, mode{}, stream{}, caller{};
    Bytes stream_bytes;
    std::map<std::uint32_t, Bytes> regions;
    std::map<std::uint32_t, Bytes> statics;  // D lines: the executable's static data, not traced
    std::vector<Event> events;

    std::optional<std::uint32_t> word(std::uint32_t address, std::size_t size = 4) const {
        for (const auto& [base, data] : regions) {
            if (address >= base && address + size <= base + data.size()) {
                return le(data, address - base, size);
            }
        }
        return std::nullopt;
    }
    std::uint32_t entry(std::uint32_t address, std::size_t size = 4) const { return word(address, size).value_or(0); }
};

inline std::vector<Call> parse_fixture(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "cannot open " << path << "\n";
        std::exit(2);
    }
    std::vector<Call> calls;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream words(line);
        std::string tag;
        words >> tag;
        if (tag == "C") {
            Call call;
            std::string a, b, c, d, e, f;
            words >> a >> b >> c >> d >> e >> f;
            call.n = static_cast<std::uint32_t>(std::stoul(a));
            call.depth = static_cast<std::uint32_t>(std::stoul(b));
            call.index = hex32(c);
            call.mode = hex32(d);
            call.stream = hex32(e);
            call.caller = hex32(f);
            calls.push_back(std::move(call));
            continue;
        }
        if (calls.empty()) {
            throw std::runtime_error("fixture line before the first C line: " + line);
        }
        Call& call = calls.back();
        if (tag == "S") {
            std::string hex;
            words >> hex;
            call.stream_bytes = unhex(hex);
        } else if (tag == "D") {
            std::string address, hex;
            words >> address >> hex;
            call.statics[hex32(address)] = unhex(hex);
        } else if (tag == "R") {
            std::string address, hex;
            words >> address >> hex;
            call.regions[hex32(address)] = unhex(hex);
        } else if (tag == "c") {
            Event ev;
            ev.kind = 'c';
            std::string token;
            words >> token;
            ev.callee = hex32(token);
            words >> token;
            ev.site = hex32(token);
            words >> token;
            ev.ecx = hex32(token);
            words >> token;
            ev.edx = hex32(token);
            for (auto& arg : ev.arg) {
                words >> token;
                arg = hex32(token);
            }
            if (words >> token && token == "|") {
                while (words >> token) {
                    ev.pointees.push_back(unhex(token));
                }
            }
            call.events.push_back(std::move(ev));
        } else if (tag == "r") {
            Event ev;
            ev.kind = 'r';
            std::string token;
            words >> token;
            ev.callee = hex32(token);
            words >> token;
            ev.eax = hex32(token);
            words >> token;  // edx
            if (words >> token) {
                ev.data = unhex(token);
            }
            call.events.push_back(std::move(ev));
        } else if (tag == "w" || tag == "k") {
            Event ev;
            ev.kind = tag[0];
            std::string token;
            while (words >> token) {
                const auto first = token.find(':');
                const auto second = token.find(':', first + 1);
                ev.changes.push_back({hex32(token.substr(0, first)),
                                      unhex(token.substr(first + 1, second - first - 1)),
                                      unhex(token.substr(second + 1))});
            }
            call.events.push_back(std::move(ev));
        } else if (tag == "v") {
            Event ev;
            ev.kind = 'v';
            std::string token;
            words >> token;
            ev.data = unhex(token);
            call.events.push_back(std::move(ev));
        } else if (tag == "x") {
            Event ev;
            ev.kind = 'x';
            std::string token;
            words >> token;
            ev.eax = hex32(token);
            call.events.push_back(std::move(ev));
        } else if (tag == "Z") {
            // end of call
        } else {
            std::cerr << "unknown fixture line: " << line << "\n";
            std::exit(2);
        }
    }
    return calls;
}

}  // namespace opentony::trg::trg_trace_fixture
