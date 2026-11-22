#pragma once

// Milestone 11b s2 (docs/PORT64_AUDIT.md 5.4 item 4): the printf family of the retail C runtime, as the game calls it.
//
// The game's sprintf / _vsnprintf / error print take 32-bit argument words: `%d %u %x %c %s %p` one word, `%f %e %g` two (a double), `%I64d` and
// `%lld` two, `%s` a guest address. A host's own printf cannot be handed those words on x86-64 (a `%s` needs a host pointer, a `%f` a register), so
// this walks the format, takes the words one by one through `FormatSource` and lets the host's snprintf format each conversion with the right C
// type. Portable (no guest memory here: the callbacks read it) and used on both builds, so the 32-bit scenarios stay the oracle.
#include <cstdint>
#include <functional>
#include <string>

namespace opentony::runtime::guest {

struct FormatSource {
    std::function<std::uint32_t()> next_word;              // the next 32-bit argument word (the caller refuses when there are no more)
    std::function<const char*(std::uint32_t)> string_at;   // the NUL-terminated string at a guest address; nullptr when it cannot be read (printed "(unreadable)")
};

// The formatted text, without a terminator. std::invalid_argument for what the retail runtime would not do or this one does not know: `%n`, wide
// strings and characters (`%ls %lc %ws %S %C`), `%L` and other size prefixes, an unknown conversion, a format that ends inside a conversion.
// A `%s` of address 0 prints "(null)", of an address `string_at` cannot read "(unreadable)", `%p` prints eight upper-case hex digits (the 32-bit runtime's form, whatever the host's pointer size).
[[nodiscard]] std::string format(const char* format, const FormatSource& source);

}  // namespace opentony::runtime::guest
