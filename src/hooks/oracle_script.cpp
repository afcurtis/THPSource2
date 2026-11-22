#include "oracle_script.h"

#include <windows.h>

#include <string.h>

// The script interpreter of the function-level oracle: see oracle_script.h. Written without the C++ library, like the other code that runs inside the retail process.

namespace {

const uint32_t kOutputCapacity = 32u << 20;
const uint32_t kMaxNames = 1024;
const uint32_t kMaxTokens = 24;
const uint32_t kOperatorNew = 0x005002fe;

struct Name {
    char text[32];
    uint32_t value;
};

struct State {
    const OtOracleBackend *backend;
    Name names[kMaxNames];
    uint32_t name_count;
    char *out;
    uint32_t used;
    char error[160];
    unsigned char *pixels;   // the capacity of one surface
    const char *output_path;
};

const uint32_t kPixelCapacity = 16u << 20;

// A read through a bad pointer answers a marker instead of faulting, so a script that follows a pointer a function left null (or wild) says so in its output (the same on
// both sides) rather than ending the run.
const uint32_t kNullRead = 0xbad0add0u;
uint32_t read32(uint32_t address) { return IsBadReadPtr((const void *)(UINT_PTR)address, 4) ? kNullRead : *(volatile uint32_t *)(UINT_PTR)address; }
uint32_t read16(uint32_t address) { return IsBadReadPtr((const void *)(UINT_PTR)address, 2) ? kNullRead & 0xffffu : *(volatile uint16_t *)(UINT_PTR)address; }
uint32_t read8(uint32_t address) { return IsBadReadPtr((const void *)(UINT_PTR)address, 1) ? kNullRead & 0xffu : *(volatile uint8_t *)(UINT_PTR)address; }
void write32(uint32_t address, uint32_t value) { *(volatile uint32_t *)(UINT_PTR)address = value; }
void write16(uint32_t address, uint32_t value) { *(volatile uint16_t *)(UINT_PTR)address = (uint16_t)value; }
void write8(uint32_t address, uint32_t value) { *(volatile uint8_t *)(UINT_PTR)address = (uint8_t)value; }

void emit(State &s, const char *text) {
    const uint32_t length = (uint32_t)strlen(text);
    if (s.used + length + 1 > kOutputCapacity) {
        return;
    }
    memcpy(s.out + s.used, text, length);
    s.used += length;
}

void emit_hex(State &s, uint32_t value, int digits) {
    char text[9];
    for (int k = 0; k < digits; ++k) {
        text[k] = "0123456789abcdef"[(value >> (4 * (digits - 1 - k))) & 15u];
    }
    text[digits] = 0;
    emit(s, text);
}

void fail(State &s, const char *message, const char *detail) {
    if (s.error[0] == 0) {
        lstrcpynA(s.error, message, sizeof(s.error));
        if (detail != 0) {
            lstrcatA(s.error, " ");
            lstrcpynA(s.error + lstrlenA(s.error), detail, (int)(sizeof(s.error) - (uint32_t)lstrlenA(s.error)));
        }
    }
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_name_char(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parse_number(const char *&p, uint32_t &value) {
    value = 0;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        int digits = 0;
        for (; hex_value(*p) >= 0; ++p, ++digits) {
            value = value * 16u + (uint32_t)hex_value(*p);
        }
        return digits != 0;
    }
    int digits = 0;
    for (; is_digit(*p); ++p, ++digits) {
        value = value * 10u + (uint32_t)(*p - '0');
    }
    return digits != 0;
}

bool lookup(State &s, const char *name, uint32_t length, uint32_t &value) {
    for (uint32_t k = 0; k < s.name_count; ++k) {
        if ((uint32_t)lstrlenA(s.names[k].text) == length && memcmp(s.names[k].text, name, length) == 0) {
            value = s.names[k].value;
            return true;
        }
    }
    return false;
}

void define(State &s, const char *name, uint32_t length, uint32_t value) {
    for (uint32_t k = 0; k < s.name_count; ++k) {
        if ((uint32_t)lstrlenA(s.names[k].text) == length && memcmp(s.names[k].text, name, length) == 0) {
            s.names[k].value = value;
            return;
        }
    }
    if (s.name_count == kMaxNames || length >= sizeof(s.names[0].text)) {
        fail(s, "too many names or a name too long:", name);
        return;
    }
    memcpy(s.names[s.name_count].text, name, length);
    s.names[s.name_count].text[length] = 0;
    s.names[s.name_count].value = value;
    ++s.name_count;
}

bool evaluate(State &s, const char *&p, uint32_t &value) {
    if (*p == '[') {
        ++p;
        uint32_t inner = 0;
        if (!evaluate(s, p, inner) || *p != ']') {
            fail(s, "bad expression", p);
            return false;
        }
        ++p;
        value = read32(inner);
    } else if (is_digit(*p)) {
        if (!parse_number(p, value)) {
            fail(s, "bad number", p);
            return false;
        }
    } else if (is_name_char(*p)) {
        const char *start = p;
        while (is_name_char(*p)) ++p;
        if (!lookup(s, start, (uint32_t)(p - start), value)) {
            fail(s, "unknown name", start);
            return false;
        }
    } else {
        fail(s, "bad expression", p);
        return false;
    }
    while (*p == '+' || *p == '-') {
        const char sign = *p++;
        uint32_t amount = 0;
        if (!parse_number(p, amount)) {
            fail(s, "bad offset", p);
            return false;
        }
        value = sign == '+' ? value + amount : value - amount;
    }
    return true;
}

bool value_of(State &s, const char *token, uint32_t &value) {
    const char *p = token;
    if (!evaluate(s, p, value)) {
        return false;
    }
    if (*p != 0) {
        fail(s, "trailing text in", token);
        return false;
    }
    return true;
}

uint32_t call_retail(State &s, uint32_t address, int thiscall, const uint32_t *args, uint32_t count) {
    return s.backend->call(s.backend->context, address, thiscall, args, count);
}

void write_output(State &s) {
    HANDLE out = CreateFileA(s.output_path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (out != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(out, s.out, s.used, &written, 0);
        CloseHandle(out);
    }
}

void emit_label(State &s, const char *command, const char *label) {
    emit(s, command);
    emit(s, " ");
    emit(s, label);
}

bool run_line(State &s, char *tokens[], uint32_t count) {
    const char *command = tokens[0];
    uint32_t a = 0, b = 0, c = 0;
    if (lstrcmpA(command, "set32") == 0 || lstrcmpA(command, "set16") == 0 || lstrcmpA(command, "set8") == 0) {
        if (count != 3 || !value_of(s, tokens[1], a) || !value_of(s, tokens[2], b)) {
            fail(s, "usage: set32|set16|set8 ADDR VALUE", 0);
            return false;
        }
        command[3] == '3' ? write32(a, b) : command[3] == '1' ? write16(a, b) : write8(a, b);
        return true;
    }
    if (lstrcmpA(command, "or32") == 0) {
        if (count != 3 || !value_of(s, tokens[1], a) || !value_of(s, tokens[2], b)) {
            fail(s, "usage: or32 ADDR VALUE", 0);
            return false;
        }
        write32(a, read32(a) | b);
        return true;
    }
    if (lstrcmpA(command, "alloc") == 0) {
        if (count != 3 || !value_of(s, tokens[2], a)) {
            fail(s, "usage: alloc NAME SIZE", 0);
            return false;
        }
        const uint32_t block = call_retail(s, kOperatorNew, 0, &a, 1);
        if (block == 0) {
            fail(s, "the allocator answered 0", 0);
            return false;
        }
        memset((void *)(UINT_PTR)block, 0, a);
        define(s, tokens[1], (uint32_t)lstrlenA(tokens[1]), block);
        return s.error[0] == 0;
    }
    if (lstrcmpA(command, "bytes") == 0) {
        if (count != 3 || !value_of(s, tokens[1], a)) {
            fail(s, "usage: bytes ADDR HEX", 0);
            return false;
        }
        const char *hex = tokens[2];
        for (; hex[0] != 0 && hex[1] != 0; hex += 2, ++a) {
            if (hex_value(hex[0]) < 0 || hex_value(hex[1]) < 0) {
                fail(s, "bad hex digit in", tokens[2]);
                return false;
            }
            write8(a, (uint32_t)(hex_value(hex[0]) * 16 + hex_value(hex[1])));
        }
        return true;
    }
    if (lstrcmpA(command, "fill") == 0) {
        if (count != 4 || !value_of(s, tokens[1], a) || !value_of(s, tokens[2], b) || !value_of(s, tokens[3], c)) {
            fail(s, "usage: fill ADDR VALUE SIZE", 0);
            return false;
        }
        memset((void *)(UINT_PTR)a, (int)(b & 0xffu), c);
        return true;
    }
    if (lstrcmpA(command, "pattern") == 0) {
        if (count != 4 || !value_of(s, tokens[1], a) || !value_of(s, tokens[2], b) || !value_of(s, tokens[3], c)) {
            fail(s, "usage: pattern ADDR SIZE SEED", 0);
            return false;
        }
        uint32_t state = c;
        for (uint32_t k = 0; k < b; ++k) {
            state = state * 1103515245u + 12345u;
            write8(a + k, (state >> 16) & 0xffu);
        }
        return true;
    }
    if (lstrcmpA(command, "call") == 0) {
        if (count < 3 || count > 3 + 8) {
            fail(s, "usage: call [NAME=]ADDR c|t ARG...", 0);
            return false;
        }
        char *name = 0;
        char *spec = tokens[1];
        for (char *at = spec; *at != 0; ++at) {
            if (*at == '=') {
                *at = 0;
                name = spec;
                spec = at + 1;
                break;
            }
        }
        uint32_t address = 0;
        uint32_t args[8];
        uint32_t n = 0;
        if (!value_of(s, spec, address)) {
            return false;
        }
        const int thiscall = tokens[2][0] == 't' ? 1 : tokens[2][0] == 's' ? 2 : 0;
        if (tokens[2][0] != 't' && tokens[2][0] != 'c' && tokens[2][0] != 's') {
            fail(s, "the convention is c, t or s, not", tokens[2]);
            return false;
        }
        for (uint32_t k = 3; k < count; ++k) {
            if (!value_of(s, tokens[k], args[n++])) {
                return false;
            }
        }
        const uint32_t result = call_retail(s, address, thiscall, args, n);
        if (name != 0) {
            define(s, name, (uint32_t)lstrlenA(name), result);
        }
        return s.error[0] == 0;
    }
    if (lstrcmpA(command, "let") == 0) {
        if (count != 3 || !value_of(s, tokens[2], a)) {
            fail(s, "usage: let NAME EXPR", 0);
            return false;
        }
        define(s, tokens[1], (uint32_t)lstrlenA(tokens[1]), a);
        return s.error[0] == 0;
    }
    if (lstrcmpA(command, "val") == 0) {
        if (count != 3 || !value_of(s, tokens[2], a)) {
            fail(s, "usage: val LABEL EXPR", 0);
            return false;
        }
        emit_label(s, "val", tokens[1]);
        emit(s, " ");
        emit_hex(s, a, 8);
        emit(s, "\n");
        return true;
    }
    if (lstrcmpA(command, "maskval") == 0) {
        if (count != 4 || !value_of(s, tokens[2], a) || !value_of(s, tokens[3], b)) {
            fail(s, "usage: maskval LABEL EXPR MASK", 0);
            return false;
        }
        emit_label(s, "val", tokens[1]);
        emit(s, " ");
        emit_hex(s, a & b, 8);
        emit(s, "\n");
        return true;
    }
    if (lstrcmpA(command, "same") == 0) {
        if (count != 4 || !value_of(s, tokens[2], a) || !value_of(s, tokens[3], b)) {
            fail(s, "usage: same LABEL EXPR EXPR", 0);
            return false;
        }
        emit_label(s, "same", tokens[1]);
        emit(s, a == b ? " 1\n" : " 0\n");
        return true;
    }
    if (lstrcmpA(command, "rec") == 0) {
        if (count < 3 || count > 4 || !value_of(s, tokens[2], a)) {
            fail(s, "usage: rec LABEL ADDR [WORD,...]", 0);
            return false;
        }
        uint32_t masked = 0;   // a bit per word
        if (count == 4) {
            for (const char *p = tokens[3]; *p != 0;) {
                uint32_t word = 0;
                if (!parse_number(p, word) || word > 10) {
                    fail(s, "bad word list", tokens[3]);
                    return false;
                }
                masked |= 1u << word;
                if (*p == ',') ++p;
            }
        }
        emit_label(s, "rec", tokens[1]);
        for (uint32_t k = 0; k < 11; ++k) {
            const uint32_t word = read32(a + k * 4);
            emit(s, " ");
            if ((masked >> k) & 1u) {
                emit(s, word != 0 ? "P" : "0");
            } else {
                emit_hex(s, word, 8);
            }
        }
        emit(s, "\n");
        return true;
    }
    if (lstrcmpA(command, "mem") == 0) {
        if (count != 4 || !value_of(s, tokens[2], a) || !value_of(s, tokens[3], b)) {
            fail(s, "usage: mem LABEL ADDR SIZE", 0);
            return false;
        }
        emit_label(s, "mem", tokens[1]);
        emit(s, " ");
        for (uint32_t k = 0; k < b; ++k) {
            emit_hex(s, read8(a + k), 2);
        }
        emit(s, "\n");
        return true;
    }
    if (lstrcmpA(command, "surf") == 0) {
        if (count != 3 || !value_of(s, tokens[2], a)) {
            fail(s, "usage: surf LABEL SURFACE", 0);
            return false;
        }
        uint32_t width = 0, height = 0, bytes = 0;
        emit_label(s, "surf", tokens[1]);
        if (a == 0 || !s.backend->surface(s.backend->context, a, &width, &height, &bytes, s.pixels, kPixelCapacity)) {
            emit(s, a == 0 ? " none\n" : " unreadable\n");
            return true;
        }
        emit(s, " ");
        emit_hex(s, width, 4);
        emit(s, "x");
        emit_hex(s, height, 4);
        emit(s, "x");
        emit_hex(s, bytes, 2);
        emit(s, " ");
        for (uint32_t k = 0; k < width * height * bytes; ++k) {
            emit_hex(s, s.pixels[k], 2);
        }
        emit(s, "\n");
        return true;
    }
    if (lstrcmpA(command, "tick") == 0) {
        if (s.backend->tick == 0) {
            fail(s, "this backend has no timer tick", 0);
            return false;
        }
        s.backend->tick(s.backend->context);
        return true;
    }
    if (lstrcmpA(command, "flush") == 0) {
        // what has been printed so far reaches the file now, so a run that crashes still says how far it got
        write_output(s);
        return true;
    }
    if (lstrcmpA(command, "echo") == 0) {
        for (uint32_t k = 1; k < count; ++k) {
            emit(s, k > 1 ? " " : "");
            emit(s, tokens[k]);
        }
        emit(s, "\n");
        return true;
    }
    fail(s, "unknown command", command);
    return false;
}

}  // namespace

int ot_oracle_run(const char *script_path, const char *output_path, const OtOracleBackend *backend) {
    State *s = (State *)VirtualAlloc(0, sizeof(State), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    char *source = 0;
    int failed_line = 0;
    if (s == 0) {
        return -1;
    }
    s->backend = backend;
    s->output_path = output_path;
    s->out = (char *)VirtualAlloc(0, kOutputCapacity, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    s->pixels = (unsigned char *)VirtualAlloc(0, kPixelCapacity, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    HANDLE file = CreateFileA(script_path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    DWORD size = 0;
    DWORD got = 0;
    if (s->out == 0 || s->pixels == 0 || file == INVALID_HANDLE_VALUE) {
        return -1;
    }
    size = GetFileSize(file, 0);
    source = (char *)VirtualAlloc(0, size + 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (source == 0 || !ReadFile(file, source, size, &got, 0) || got != size) {
        CloseHandle(file);
        return -1;
    }
    CloseHandle(file);
    source[size] = '\n';
    source[size + 1] = 0;

    int line_number = 0;
    for (char *line = source; *line != 0 && s->error[0] == 0;) {
        char *end = line;
        while (*end != '\n' && *end != 0) ++end;
        const bool last = *end == 0;
        *end = 0;
        ++line_number;
        // the tokens of the line (a `#` ends it)
        char *tokens[kMaxTokens];
        uint32_t count = 0;
        for (char *at = line; *at != 0 && *at != '#';) {
            while (*at == ' ' || *at == '\t' || *at == '\r') ++at;
            if (*at == 0 || *at == '#') break;
            if (count == kMaxTokens) {
                fail(*s, "too many arguments", 0);
                break;
            }
            tokens[count++] = at;
            while (*at != 0 && *at != ' ' && *at != '\t' && *at != '\r' && *at != '#') ++at;
            if (*at == '#') {
                *at = 0;
                break;
            }
            if (*at != 0) *at++ = 0;
        }
        if (s->error[0] == 0 && count != 0 && !run_line(*s, tokens, count)) {
            if (s->error[0] == 0) fail(*s, "the command failed", 0);
        }
        if (s->error[0] != 0) {
            failed_line = line_number;
            break;
        }
        if (last) break;
        line = end + 1;
    }
    if (failed_line != 0) {
        emit(*s, "error line ");
        emit_hex(*s, (uint32_t)failed_line, 4);
        emit(*s, ": ");
        emit(*s, s->error);
        emit(*s, "\n");
    } else {
        emit(*s, "end\n");
    }
    write_output(*s);
    return failed_line;
}
