// Unit tests of the C runtime pieces and the small pure logic of the PC platform layer (Phase 4.13a); the retail behaviour is checked by platform_retail_calls_test.
#include "pad_input.hpp"
#include "pc_system.hpp"

#include <cstdio>
#include <cstdlib>

using namespace opentony::runtime;
using namespace opentony::runtime::pcplatform;

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

void test_scan_int() {
    std::int32_t value = 99;
    expect(crt::scan_int("42", value) && value == 42, "plain number");
    expect(crt::scan_int("  -7abc", value) && value == -7, "leading white space, sign, trailing text");
    expect(crt::scan_int("+5", value) && value == 5, "plus sign");
    value = 123;
    expect(!crt::scan_int("", value) && value == 123, "empty string leaves the value");
    expect(!crt::scan_int("abc", value) && value == 123, "text leaves the value");
    expect(!crt::scan_int("-", value) && value == 123, "a sign alone leaves the value");
    expect(crt::scan_int("4294967295", value) && value == -1, "digits wrap at 32 bits");
}

void test_strtok_and_compare() {
    crt::Strtok tokens("Left,Right,,Up", ",");
    std::string token;
    expect(tokens.next(token) && token == "Left", "first token");
    expect(tokens.next(token) && token == "Right", "second token");
    expect(tokens.next(token) && token == "Up", "empty tokens are skipped");
    expect(!tokens.next(token), "no more tokens");
    crt::Strtok none(",,,", ",");
    expect(!none.next(token), "only delimiters: no token");
    expect(crt::equal_nocase("LeftArrow", "leftarrow"), "case-insensitive equal");
    expect(!crt::equal_nocase("Left", "Lefty"), "different lengths");
}

void test_format() {
    expect(crt::format("%s\\%s", {"a", "b"}) == "a\\b", "two strings");
    expect(crt::format("%i,%d", {}, {-3, 7}) == "-3,7", "two numbers");
}

// the pure helpers of the system class on a tiny memory image
void test_system_helpers() {
    RetailMemory memory;
    RetailServices services;
    memory.add_region(0x006a05a0, 0x40);
    memory.add_region(0x029da370, 0x40);
    PadInput pad(memory, services);
    PcSystem system(memory, services, pad);
    expect(system.bit_count(0xF81F) == 10, "bit count");
    expect(system.lowest_bit(0x7E0) == 5, "lowest bit");
    expect(system.lowest_bit(0) == 0, "lowest bit of zero");
    expect(!system.timer_active(), "timer idle");
    memory.set32(0x006a05a0, 1);
    expect(system.timer_active(), "timer active");
}

}  // namespace

int main() {
    test_scan_int();
    test_strtok_and_compare();
    test_format();
    test_system_helpers();
    if (failures == 0) {
        std::puts("pc platform tests passed");
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
