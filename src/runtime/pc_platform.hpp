#pragma once

#include "retail_memory.hpp"
#include "guest_view.hpp"
#include "retail_services.hpp"
#include "seam_addresses_generated.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace opentony::runtime::pcplatform {

// Phase 4.13a: the PC platform layer of the game (pad.cpp, pc\pcinput.cpp, pc\winmain.cpp, pc\thstub.cpp, the PC file backends and the timer helpers). Like the other retail-memory ports the
// functions read and write the game's globals at their retail addresses; everything outside the 4.13a functions (the assertion routine, the DirectInput wrapper, the PSX library stubs,
// the CRT, the Win32 calls) is a named service, reported in order and answered from the scripted values of the replay: `engine_<address>` for a retail function, `win32_<name>` for an API.
inline std::string engine_name(std::uint32_t address) {
    char buffer[24];
    std::snprintf(buffer, sizeof buffer, "engine_%08x", address);
    return buffer;
}

// The scratch the stack locals of the retail functions (a formatted line, a loaded setting) live at in a replay: outside every region a replay compares. The probe records an argument
// that points into the caller's frame as these addresses (fcall `stack_to`).
constexpr std::uint32_t kScratchBase = 0x7ff00800;
constexpr std::uint32_t kScratchDefault = kScratchBase;           // the default text handed to the options file (0x100 bytes)
constexpr std::uint32_t kScratchOut = kScratchBase + 0x100;       // the buffer the options file fills in (0x100 bytes)
constexpr std::uint32_t kStackLocal = kScratchBase - 0x100;       // what an argument that points at a stack local is recorded as (when the text carries the content)
constexpr std::uint32_t kScratchBytes = 0x1000;

// The options file (WINMAIN_LoadSetting / SaveSetting wrap GetPrivateProfileString / WritePrivateProfileString) and the log.
constexpr std::uint32_t kLoadSetting = seam::hal::WINMAIN_LoadSetting;
constexpr std::uint32_t kSaveSetting = seam::hal::WINMAIN_SaveSetting;
constexpr std::uint32_t kPrintfLog = seam::hal::printf_log;

class PcKit {
public:
    PcKit(RetailMemory& memory, RetailServices& services) : m_(memory), s_(services) {}

protected:
    void check(bool condition, std::uint32_t message) { s_.check(condition, message); }
    void engine(std::uint32_t address, std::vector<std::uint32_t> args = {}, std::string text = {}) {
        s_.note(engine_name(address), std::move(args), std::move(text));
    }
    std::uint32_t engine_value(std::uint32_t address, std::vector<std::uint32_t> args = {}, std::string text = {}) {
        engine(address, std::move(args), std::move(text));
        return static_cast<std::uint32_t>(s_.value(engine_name(address)));
    }
    // a service whose effect on the caller's state the host reports as a value (a trick script that may change a field): the scripted answer, or `fallback` (the field as it was) when the host has none
    // (live, the answer is the field as the real call left it, not its EAX: not supported yet, Phase 5.1)
    std::uint32_t engine_answer(std::uint32_t address, std::vector<std::uint32_t> args, std::uint32_t fallback) {
        if (s_.live()) {
            throw ServiceFault(engine_name(address) + ": engine_answer has no live form yet");
        }
        engine(address, std::move(args));
        return static_cast<std::uint32_t>(s_.value(engine_name(address), static_cast<std::int32_t>(fallback)));
    }
    // a virtual call on an engine object: the slot of its class's vtable is the service (the object comes first; the text of a string argument is the call's text)
    std::uint32_t virtual_call(std::uint32_t object, std::uint32_t slot, std::vector<std::uint32_t> args = {}, std::string text = {}) {
        const std::uint32_t target = guest::read_scalar<std::uint32_t>(m_, guest::read_scalar<std::uint32_t>(m_, object) + 4 * slot);
        args.insert(args.begin(), object);
        if (s_.live()) {
            // any class's method can sit in the slot, so the live host calls the address the vtable holds (a thiscall with the object in ECX) instead of looking up a service
            char name[24];
            std::snprintf(name, sizeof name, "vcall_%08x", target);
            s_.note(name, std::move(args), std::move(text));
            return static_cast<std::uint32_t>(s_.value(name));
        }
        return engine_value(target, std::move(args), std::move(text));
    }
    // printf_log(text): the probe's stub is named "printf_log" and reports the address of the format
    void log_line(std::uint32_t format) { s_.note("printf_log", {format}); }

    // NUL-terminated text into memory
    void set_string(std::uint32_t address, const std::string& text) {
        std::vector<std::uint8_t> bytes(text.begin(), text.end());
        bytes.push_back(0);
        m_.write(address, bytes);
    }
    // WINMAIN_LoadSetting(section, key, default, out): the file answers with the text it holds (the probe scripts `size` bytes of it, as dwords), written to `out`
    void load_setting(std::uint32_t section, std::uint32_t key, std::uint32_t fallback, std::uint32_t out, std::uint32_t size) {
        s_.note(engine_name(kLoadSetting), {section, key, fallback, out}, m_.cstring(fallback, 0x100));
        if (s_.live()) {
            return;   // the call has written `out` itself (milestone 9: the cold boot's WINMAIN_LoadOptions)
        }
        std::vector<std::uint8_t> bytes;
        for (std::uint32_t k = 0; k < size / 4; ++k) {
            const auto word = static_cast<std::uint32_t>(s_.value(engine_name(kLoadSetting)));
            for (std::uint32_t b = 0; b < 4; ++b) {
                bytes.push_back(static_cast<std::uint8_t>(word >> (8 * b)));
            }
        }
        m_.write(out, bytes);
    }
    // WINMAIN_SaveSetting(section, key, value)
    void save_setting(std::uint32_t section, std::uint32_t key, std::uint32_t value) {
        s_.note(engine_name(kSaveSetting), {section, key, value}, m_.cstring(value, 0x100));
    }
    // the C runtime's file calls, named for the function (the probe stubs them; the text of a call is the path)
    // (`path_argument` is the address the retail call was handed: a stack local is recorded as kStackLocal, a global as itself)
    std::uint32_t crt_fopen(const std::string& path, std::uint32_t path_argument, std::uint32_t mode) {
        if (s_.live() && path_argument == kStackLocal) {
            set_string(kStackLocal, path);   // (retail's local held the name; a replay carries it as the call's text)
        }
        s_.note("crt_fopen", {path_argument, mode}, path);
        return static_cast<std::uint32_t>(s_.value("crt_fopen"));
    }
    void crt_fclose(std::uint32_t file) { s_.note("crt_fclose", {file}); }
    void crt_remove(const std::string& path, std::uint32_t path_argument) {
        if (s_.live() && path_argument == kStackLocal) {
            set_string(kStackLocal, path);
        }
        s_.note("crt_remove", {path_argument}, path);
    }
    std::uint32_t crt_fwrite(std::uint32_t data, std::uint32_t size, std::uint32_t count, std::uint32_t file) {
        s_.note("crt_fwrite", {data, size, count, file});
        return static_cast<std::uint32_t>(s_.value("crt_fwrite"));
    }

    RetailMemory& m_;
    RetailServices& s_;
};

// The C runtime the options code leans on, with the behaviour of the game's MSVC runtime (the part the game can observe): sprintf of %s / %d / %i, sscanf("%d") and strtok.
namespace crt {

// sprintf(format, ...): the conversions the game's formats use
inline std::string format(const std::string& format, const std::vector<std::string>& strings, const std::vector<std::int32_t>& numbers = {}) {
    std::string out;
    std::size_t next_string = 0;
    std::size_t next_number = 0;
    for (std::size_t i = 0; i < format.size(); ++i) {
        if (format[i] != '%' || i + 1 >= format.size()) {
            out.push_back(format[i]);
            continue;
        }
        const char kind = format[++i];
        if (kind == 's') {
            out += strings.at(next_string++);
        } else if (kind == 'd' || kind == 'i') {
            out += std::to_string(numbers.at(next_number++));
        } else {
            out.push_back(kind);
        }
    }
    return out;
}

// sscanf(text, "%d", &value): leading white space skipped, an optional sign, at least one digit; the digits wrap at 32 bits. False (and `value` untouched) when nothing was read.
inline bool scan_int(const std::string& text, std::int32_t& value) {
    std::size_t i = 0;
    while (i < text.size() && (text[i] == ' ' || (text[i] >= '\t' && text[i] <= '\r'))) {
        ++i;
    }
    bool negative = false;
    if (i < text.size() && (text[i] == '-' || text[i] == '+')) {
        negative = text[i] == '-';
        ++i;
    }
    if (i >= text.size() || text[i] < '0' || text[i] > '9') {
        return false;
    }
    std::uint32_t number = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        number = number * 10u + static_cast<std::uint32_t>(text[i] - '0');
        ++i;
    }
    value = static_cast<std::int32_t>(negative ? 0u - number : number);
    return true;
}

// strtok over one string: tokens separated by any of the delimiter characters; the next token or none
class Strtok {
public:
    Strtok(std::string text, std::string delimiters) : text_(std::move(text)), delimiters_(std::move(delimiters)) {}
    bool next(std::string& token) {
        while (at_ < text_.size() && delimiters_.find(text_[at_]) != std::string::npos) {
            ++at_;
        }
        if (at_ >= text_.size()) {
            return false;
        }
        const std::size_t start = at_;
        while (at_ < text_.size() && delimiters_.find(text_[at_]) == std::string::npos) {
            ++at_;
        }
        token = text_.substr(start, at_ - start);
        if (at_ < text_.size()) {
            ++at_;       // (the delimiter becomes a NUL: the next call starts after it)
        }
        return true;
    }

private:
    std::string text_;
    std::string delimiters_;
    std::size_t at_ = 0;
};

// the ASCII case-insensitive equality CRT_StrICmp's callers test
inline bool equal_nocase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        char x = a[i];
        char y = b[i];
        if (x >= 'A' && x <= 'Z') {
            x = static_cast<char>(x - 'A' + 'a');
        }
        if (y >= 'A' && y <= 'Z') {
            y = static_cast<char>(y - 'A' + 'a');
        }
        if (x != y) {
            return false;
        }
    }
    return true;
}

}  // namespace crt

}  // namespace opentony::runtime::pcplatform
