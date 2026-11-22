// Replays the retail traces of TrickScript_CommandSize 0x004bf6c0 and TrickScript_ReadWord 0x004be3c0 (re/evidence/fixtures/trickscript-size-*.txt; scripts/make_trick_script_fixture.py documents the
// format, build/m10e/script_size_trace.py is the probe) through their retail-memory forms (SkaterKit::trick_script_command_size / _read_word, skater_tricks_load.cpp). Each distinct retail call is
// laid out in a RetailMemory at an address of its own: the bytes at the command (or at the stream pointer a local holds), and the Kit's answer must be retail's, the returned address minus the
// command for the size, the word and how far the pointer moved for the reader.
//
//   opentony_trick_script_size_trace_test FIXTURE.txt [--mutate]
//
// --mutate changes the first expected answer; the replay must fail (the comparison has teeth).
//
// Exit codes: 0 all match, 1 mismatch, 2 usage/format error.
#include "skater_kit.hpp"

#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace opentony::runtime;

namespace {

constexpr std::uint32_t kStream = 0x00600000;   // where a call's bytes are laid out
constexpr std::uint32_t kCursor = 0x00601000;   // the local that holds the stream pointer

std::vector<std::uint8_t> unhex(const std::string& text) {
    std::vector<std::uint8_t> bytes;
    for (std::size_t i = 0; i + 1 < text.size(); i += 2) {
        bytes.push_back(static_cast<std::uint8_t>(std::stoul(text.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: opentony_trick_script_size_trace_test FIXTURE.txt [--mutate]\n";
        return 2;
    }
    const bool mutate = argc > 2 && std::string(argv[2]) == "--mutate";
    std::ifstream in(argv[1]);
    if (!in) {
        std::cerr << "cannot open " << argv[1] << "\n";
        return 2;
    }
    int failures = 0;
    std::size_t sizes = 0;
    std::size_t words = 0;
    bool mutated = false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream tokens(line);
        std::string tag;
        std::string hex;
        tokens >> tag >> hex;
        const std::vector<std::uint8_t> window = unhex(hex);
        RetailMemory memory;
        memory.add_region(kStream, window);
        memory.add_region(kCursor, 0x10);
        RetailServices services;
        skaterkit::SkaterKit kit(memory, services);
        try {
            if (tag == "S") {
                std::string delta_text;
                tokens >> delta_text;
                std::uint32_t delta = static_cast<std::uint32_t>(std::stoul(delta_text, nullptr, 16));
                if (mutate && !mutated) {
                    ++delta;
                    mutated = true;
                }
                const std::uint32_t got = kit.trick_script_command_size(kStream) - kStream;
                ++sizes;
                if (got != delta) {
                    ++failures;
                    if (failures <= 20) {
                        std::cerr << "size of " << hex.substr(0, 24) << ": the Kit says " << got << ", retail " << delta << "\n";
                    }
                }
            } else if (tag == "W") {
                std::string word_text;
                std::string advance_text;
                tokens >> word_text >> advance_text;
                const std::uint32_t word = static_cast<std::uint32_t>(std::stoul(word_text, nullptr, 16));
                const std::uint32_t advance = static_cast<std::uint32_t>(std::stoul(advance_text, nullptr, 16));
                memory.set32(kCursor, kStream);
                const std::uint32_t got = kit.trick_script_read_word(kCursor);
                ++words;
                if ((got & 0xffff) != word || memory.u32(kCursor) - kStream != advance) {
                    ++failures;
                    if (failures <= 20) {
                        std::cerr << "word at " << hex << ": the Kit says " << got << " and moves " << memory.u32(kCursor) - kStream << ", retail " << word << " and " << advance << "\n";
                    }
                }
            } else {
                std::cerr << "unknown fixture line: " << line << "\n";
                return 2;
            }
        } catch (const std::exception& error) {
            ++failures;
            if (failures <= 20) {
                std::cerr << tag << " " << hex.substr(0, 24) << ": the Kit stopped: " << error.what() << "\n";
            }
        }
    }
    std::cout << argv[1] << ": " << sizes << " command sizes and " << words << " word reads, " << failures << " mismatches\n";
    if (mutate && !mutated) {
        std::cerr << "--mutate found no size to change\n";
        return 2;
    }
    return failures == 0 ? 0 : 1;
}
