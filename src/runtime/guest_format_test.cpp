// Milestone 11b s2: the retail runtime's printf over 32-bit argument words (guest_format.hpp).
#include "guest_format.hpp"
#include "tests/test_check.hpp"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using opentony::runtime::guest::FormatSource;
using opentony::runtime::guest::format;

// the words of a call, and a few "guest" strings keyed by address
struct Call {
    std::vector<std::uint32_t> words;
    std::map<std::uint32_t, std::string> strings;
    std::size_t next = 0;

    FormatSource source() {
        return {[this]() -> std::uint32_t {
                    if (next >= words.size()) {
                        throw std::out_of_range("no more words");
                    }
                    return words[next++];
                },
                [this](std::uint32_t address) -> const char* {
                    const auto found = strings.find(address);
                    return found == strings.end() ? nullptr : found->second.c_str();   // an address the host cannot read
                }};
    }
};

std::string run(const char* f, std::initializer_list<std::uint32_t> words, std::map<std::uint32_t, std::string> strings = {}) {
    Call call{std::vector<std::uint32_t>(words), std::move(strings)};
    const std::string text = format(f, call.source());
    CHECK(call.next == call.words.size());   // every word is taken, no more
    return text;
}

std::pair<std::uint32_t, std::uint32_t> halves(double value) {
    std::uint64_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return {static_cast<std::uint32_t>(bits), static_cast<std::uint32_t>(bits >> 32)};
}

template <typename F>
bool throws_invalid(F f) {
    try {
        f();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

void integers_and_text() {
    CHECK(run("plain", {}) == "plain");
    CHECK(run("100%% of %d", {7}) == "100% of 7");
    CHECK(run("%d %i %u", {0xfffffffeu, 5, 0xfffffffeu}) == "-2 5 4294967294");
    CHECK(run("%x %X %o %#x", {255, 255, 8, 255}) == "ff FF 10 0xff");
    CHECK(run("[%5d][%-5d][%05d][%+d]", {42, 42, 42, 42}) == "[   42][42   ][00042][+42]");
    CHECK(run("%hd %hu", {0x12345u, 0x12345u}) == "9029 9029");
    CHECK(run("%ld %lu %lx", {0xffffffffu, 0xffffffffu, 0xffffffffu}) == "-1 4294967295 ffffffff");
    CHECK(run("%c%c", {'o', 'k'}) == "ok");
    CHECK(run("%s and %s", {1, 2}, {{1, "one"}, {2, "two"}}) == "one and two");
    CHECK(run("%s", {0}) == "(null)");
    CHECK(run("[%s]", {0x370000}) == "[(unreadable)]");
    CHECK(run("[%8s][%-8s][%.2s]", {1, 1, 1}, {{1, "abc"}}) == "[     abc][abc     ][ab]");
    CHECK(run("%p", {0x00401000}) == "00401000");
    CHECK(run("%*d|%-*d|%.*s", {4, 7, 4, 7, 1, 1}, {{1, "xyz"}}) == "   7|7   |x");
}

void sixty_four_bit_integers_take_two_words() {
    CHECK(run("%I64d", {0xfffffffeu, 0xffffffffu}) == "-2");
    CHECK(run("%I64u", {0, 1}) == "4294967296");
    CHECK(run("%lld %llx", {5, 0, 0xdeadbeefu, 0x1}) == "5 1deadbeef");
    CHECK(run("%d %I64x %d", {1, 0xffffffffu, 0, 2}) == "1 ffffffff 2");
}

void doubles_take_two_words() {
    const auto [lo, hi] = halves(3.25);
    CHECK(run("%f", {lo, hi}) == "3.250000");
    CHECK(run("%.2f|%8.3f|%e", {lo, hi, lo, hi, lo, hi}) == "3.25|   3.250|3.250000e+00");
    const auto [lo2, hi2] = halves(-0.5);
    CHECK(run("%d %g %d", {9, lo2, hi2, 10}) == "9 -0.5 10");
    CHECK(run("%lf", {lo, hi}) == "3.250000");
}

void refuses_what_it_does_not_know() {
    const auto make = [](const char* f) {
        return [f] {
            Call call{{1, 2, 3}, {{1, "x"}}};
            (void)format(f, call.source());
        };
    };
    CHECK(throws_invalid(make("%n")));
    CHECK(throws_invalid(make("%ls")));
    CHECK(throws_invalid(make("%lc")));
    CHECK(throws_invalid(make("%Lf")));
    CHECK(throws_invalid(make("%q")));
    CHECK(throws_invalid(make("%")));
    CHECK(throws_invalid(make("%5")));
}

void a_missing_word_is_the_callers_fault() {
    Call call{{1}, {}};
    bool thrown = false;
    try {
        (void)format("%d %d", call.source());
    } catch (const std::out_of_range&) {
        thrown = true;
    }
    CHECK(thrown);
}

}  // namespace

int main() {
    integers_and_text();
    sixty_four_bit_integers_take_two_words();
    doubles_take_two_words();
    refuses_what_it_does_not_know();
    a_missing_word_is_the_callers_fault();
    return 0;
}
