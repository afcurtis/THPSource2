// Milestone 11b s2: the retail runtime's printf over 32-bit argument words (guest_format.hpp).
#include "guest_format.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace opentony::runtime::guest {
namespace {

// one conversion through the host's snprintf: `spec` is a complete `%...` specification for `value`'s C type
template <typename T>
void emit(std::string& out, const std::string& spec, T value) {
    const int length = std::snprintf(nullptr, 0, spec.c_str(), value);
    if (length < 0) {
        throw std::invalid_argument("guest printf: the host cannot format '" + spec + "'");
    }
    std::vector<char> piece(static_cast<std::size_t>(length) + 1);
    std::snprintf(piece.data(), piece.size(), spec.c_str(), value);
    out.append(piece.data(), static_cast<std::size_t>(length));   // not strlen: `%c` of 0 is one NUL byte
}

std::uint64_t two_words(const FormatSource& source) {
    const std::uint64_t low = source.next_word();
    const std::uint64_t high = source.next_word();
    return low | (high << 32);
}

}  // namespace

std::string format(const char* f, const FormatSource& source) {
    std::string out;
    while (*f != '\0') {
        if (*f != '%') {
            out += *f++;
            continue;
        }
        ++f;
        if (*f == '%') {
            out += '%';
            ++f;
            continue;
        }
        std::string spec = "%";
        while (*f != '\0' && std::strchr("-+ #0", *f) != nullptr) {
            spec += *f++;
        }
        if (*f == '*') {
            spec += std::to_string(static_cast<std::int32_t>(source.next_word()));
            ++f;
        } else {
            while (std::isdigit(static_cast<unsigned char>(*f)) != 0) {
                spec += *f++;
            }
        }
        if (*f == '.') {
            ++f;
            if (*f == '*') {
                const auto precision = static_cast<std::int32_t>(source.next_word());
                if (precision >= 0) {   // a negative precision is no precision
                    spec += "." + std::to_string(precision);
                }
                ++f;
            } else {
                spec += '.';
                while (std::isdigit(static_cast<unsigned char>(*f)) != 0) {
                    spec += *f++;
                }
            }
        }
        // the size prefix: `l` is 32 bits here (as in the 32-bit runtime and on Windows), `ll` and `I64` are two words
        bool wide_integer = false, long_prefix = false, wide_text = false;
        std::string size;
        if (std::strncmp(f, "I64", 3) == 0) {
            wide_integer = true;
            f += 3;
        } else if (std::strncmp(f, "I32", 3) == 0) {
            f += 3;
        } else if (std::strncmp(f, "ll", 2) == 0) {
            wide_integer = true;
            f += 2;
        } else if (*f == 'l') {
            long_prefix = true;
            ++f;
        } else if (std::strncmp(f, "hh", 2) == 0) {
            size = "hh";
            f += 2;
        } else if (*f == 'h') {
            size = "h";
            ++f;
        } else if (*f == 'w') {
            wide_text = true;
            ++f;
        } else if (*f == 'L' || *f == 'j' || *f == 'z' || *f == 't') {
            throw std::invalid_argument(std::string("guest printf: unsupported size prefix '") + *f + "'");
        }
        const char type = *f;
        if (type == '\0') {
            throw std::invalid_argument("guest printf: the format ends inside a conversion");
        }
        ++f;
        switch (type) {
        case 'd':
        case 'i':
            if (wide_integer) {
                emit(out, spec + "lld", static_cast<long long>(static_cast<std::int64_t>(two_words(source))));
            } else {
                emit(out, spec + size + "d", static_cast<int>(source.next_word()));
            }
            break;
        case 'u':
        case 'o':
        case 'x':
        case 'X':
            if (wide_integer) {
                emit(out, spec + "ll" + type, static_cast<unsigned long long>(two_words(source)));
            } else {
                emit(out, spec + size + type, static_cast<unsigned>(source.next_word()));
            }
            break;
        case 'c':
            if (long_prefix || wide_text) {
                throw std::invalid_argument("guest printf: wide characters are not supported");
            }
            emit(out, spec + "c", static_cast<int>(source.next_word()));
            break;
        case 's': {
            if (long_prefix || wide_text) {
                throw std::invalid_argument("guest printf: wide strings are not supported");
            }
            const std::uint32_t address = source.next_word();
            const char* text = address == 0 ? "(null)" : source.string_at(address);
            emit(out, spec + "s", text != nullptr ? text : "(unreadable)");
            break;
        }
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G': {
            const std::uint64_t bits = two_words(source);
            double value;
            static_assert(sizeof value == sizeof bits);
            std::memcpy(&value, &bits, sizeof value);
            emit(out, spec + type, value);
            break;
        }
        case 'p':
            emit(out, "%08X", static_cast<unsigned>(source.next_word()));
            break;
        case 'n':
            throw std::invalid_argument("guest printf: %n is refused");
        default:
            throw std::invalid_argument(std::string("guest printf: unknown conversion '") + type + "'");
        }
    }
    return out;
}

}  // namespace opentony::runtime::guest
